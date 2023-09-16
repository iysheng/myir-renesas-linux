// SPDX-License-Identifier: GPL-2.0+
/*
 * serial.c -- USB gadget serial driver
 *
 * Copyright (C) 2003 Al Borchers (alborchers@steinerpoint.com)
 * Copyright (C) 2008 by David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 * Copyright (C) 2023 by Yang Yongsheng
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include "u_serial.h"


/* Defines */

#define GBMP_VERSION_STR			"v0.9"
#define GBMP_VERSION_NUM			0x0009

#define GBMP_LONG_NAME			"Black Magic Probe"
#define GBMP_VERSION_NAME			GBMP_LONG_NAME " " GBMP_VERSION_STR

/*-------------------------------------------------------------------------*/
USB_GADGET_COMPOSITE_OPTIONS();

/* Thanks to NetChip Technologies for donating this product ID.
*
* DO NOT REUSE THESE IDs with a protocol-incompatible driver!!  Ever!!
* Instead:  allocate your own, using normal USB-IF procedures.
*/
#define GBMP_VENDOR_ID			0x1d50	/* NetChip */
#define GBMP_PRODUCT_ID			0x6018	/* Linux-USB Serial Gadget */

/* string IDs are assigned dynamically */

#define STRING_DESCRIPTION_IDX		USB_GADGET_FIRST_AVAIL_IDX

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "Red Inc",
	[USB_GADGET_PRODUCT_IDX].s = GBMP_VERSION_NAME,
	[USB_GADGET_SERIAL_IDX].s = "1236547890",
	[STRING_DESCRIPTION_IDX].s = NULL /* updated; f(use_acm) */,
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

static struct usb_device_descriptor device_desc = {
	.bLength =		USB_DT_DEVICE_SIZE,
	.bDescriptorType =	USB_DT_DEVICE,
	/* .bcdUSB = DYNAMIC */
	/* .bDeviceClass = f(use_acm) */
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	/* .bMaxPacketSize0 = f(hardware) */
	.idVendor =		cpu_to_le16(GBMP_VENDOR_ID),
	/* .idProduct =	f(use_acm) */
	.bcdDevice = cpu_to_le16(GBMP_VERSION_NUM),
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	.bNumConfigurations =	1,
};

static const struct usb_descriptor_header *otg_desc[2];

/*-------------------------------------------------------------------------*/

/* Module */
MODULE_DESCRIPTION(GBMP_VERSION_NAME);
MODULE_AUTHOR("Yang Yongsheng");
MODULE_LICENSE("GPL");

static bool use_acm = true;
module_param(use_acm, bool, 0);
MODULE_PARM_DESC(use_acm, "Use CDC ACM, default=yes");

static unsigned n_ports = 1;
module_param(n_ports, uint, 0);
MODULE_PARM_DESC(n_ports, "number of ports to create, default=1");

/*-------------------------------------------------------------------------*/

static struct usb_configuration serial_config_driver = {
	/* .label = f(use_acm) */
	/* .bConfigurationValue = f(use_acm) */
	/* .iConfiguration = DYNAMIC */
	.bmAttributes	= USB_CONFIG_ATT_SELFPOWER,
};

static struct usb_function_instance *fi_serial[MAX_U_SERIAL_PORTS];
static struct usb_function *f_serial[MAX_U_SERIAL_PORTS];

static int serial_register_ports(struct usb_composite_dev *cdev,
		struct usb_configuration *c, const char *f_name)
{
	int i;
	int ret;

	ret = usb_add_config_only(cdev, c);
	if (ret)
		goto out;

	for (i = 0; i < n_ports; i++) {

		fi_serial[i] = usb_get_function_instance(f_name);
		if (IS_ERR(fi_serial[i])) {
			ret = PTR_ERR(fi_serial[i]);
			goto fail;
		}

		f_serial[i] = usb_get_function(fi_serial[i]);
		if (IS_ERR(f_serial[i])) {
			ret = PTR_ERR(f_serial[i]);
			goto err_get_func;
		}

		ret = usb_add_function(c, f_serial[i]);
		if (ret)
			goto err_add_func;
	}

	return 0;

err_add_func:
	usb_put_function(f_serial[i]);
err_get_func:
	usb_put_function_instance(fi_serial[i]);

fail:
	i--;
	while (i >= 0) {
		usb_remove_function(c, f_serial[i]);
		usb_put_function(f_serial[i]);
		usb_put_function_instance(fi_serial[i]);
		i--;
	}
out:
	return ret;
}

static int gs_bind(struct usb_composite_dev *cdev)
{
	int			status;

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		goto fail;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;
	status = strings_dev[STRING_DESCRIPTION_IDX].id;
	serial_config_driver.iConfiguration = status;

	if (gadget_is_otg(cdev->gadget)) {
		if (!otg_desc[0]) {
			struct usb_descriptor_header *usb_desc;

			usb_desc = usb_otg_descriptor_alloc(cdev->gadget);
			if (!usb_desc) {
				status = -ENOMEM;
				goto fail;
			}
			usb_otg_descriptor_init(cdev->gadget, usb_desc);
			otg_desc[0] = usb_desc;
			otg_desc[1] = NULL;
		}
		serial_config_driver.descriptors = otg_desc;
		serial_config_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	/* register our configuration */
	if (use_acm) {
		status  = serial_register_ports(cdev, &serial_config_driver,
				"bmp");
		usb_ep_autoconfig_reset(cdev->gadget);
	}
	if (status < 0)
		goto fail1;

	usb_composite_overwrite_options(cdev, &coverwrite);
	INFO(cdev, "%s\n", GBMP_VERSION_NAME);

	return 0;
fail1:
	kfree(otg_desc[0]);
	otg_desc[0] = NULL;
fail:
	return status;
}

static int gs_unbind(struct usb_composite_dev *cdev)
{
	int i;

	for (i = 0; i < n_ports; i++) {
		usb_put_function(f_serial[i]);
		usb_put_function_instance(fi_serial[i]);
	}

	kfree(otg_desc[0]);
	otg_desc[0] = NULL;

	return 0;
}

static struct usb_composite_driver gserial_driver = {
	.name		= "g_bmp",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_SUPER,
	.bind		= gs_bind,
	.unbind		= gs_unbind,
	.gadget_driver = {
		.udc_name = "dummy_udc.0",
	}
};

static int __init init(void)
{
	/* We *could* export two configs; that'd be much cleaner...
	 * but neither of these product IDs was defined that way.
	 */
	if (use_acm) {
		serial_config_driver.label = "CDC ACM config";
		serial_config_driver.bConfigurationValue = 2;
		device_desc.bDeviceClass = USB_CLASS_COMM;
		device_desc.idProduct =
				cpu_to_le16(GBMP_PRODUCT_ID);
	}
	strings_dev[STRING_DESCRIPTION_IDX].s = serial_config_driver.label;

	return usb_composite_probe(&gserial_driver);
}
module_init(init);

static void __exit cleanup(void)
{
	usb_composite_unregister(&gserial_driver);
}
module_exit(cleanup);
