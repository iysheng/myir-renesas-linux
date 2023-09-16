// SPDX-License-Identifier: GPL-2.0+
/*
 * f_bmp.c -- USB BlackMagicProbe (BMP) function driver
 *
 * Copyright (C) 2003 Al Borchers (alborchers@steinerpoint.com)
 * Copyright (C) 2008 by David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 * Copyright (C) 2009 by Samsung Electronics
 * Copyright (C) 2023 by Yang Yongsheng
 * Author: Michal Nazarewicz (mina86@mina86.com)
 */

/* #define VERBOSE_DEBUG */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>

#include "u_serial.h"


/*
 * This CDC ACM function support just wraps control functions and
 * notifications around the generic serial-over-usb code.
 *
 * Because CDC ACM is standardized by the USB-IF, many host operating
 * systems have drivers for it.  Accordingly, ACM is the preferred
 * interop solution for serial-port type connections.  The control
 * models are often not necessary, and in any case don't do much in
 * this bare-bones implementation.
 *
 * Note that even MS-Windows has some support for ACM.  However, that
 * support is somewhat broken because when you use ACM in a composite
 * device, having multiple interfaces confuses the poor OS.  It doesn't
 * seem to understand CDC Union descriptors.  The new "association"
 * descriptors (roughly equivalent to CDC Unions) may sometimes help.
 */

struct f_bmp {
	struct gserial			port;
	u8				ctrl_id, data_id;
	u8				port_num;

	u8				pending;

	/* lock is mostly for pending and notify_req ... they get accessed
	 * by callbacks both from tty (open/close/break) under its spinlock,
	 * and notify_req.complete() which can't use that lock.
	 */
	spinlock_t			lock;

	struct usb_ep			*notify;
	struct usb_request		*notify_req;

	struct usb_cdc_line_coding	port_line_coding;	/* 8-N-1 etc */

	/* SetControlLineState request -- CDC 1.1 section 6.2.14 (INPUT) */
	u16				port_handshake_bits;
#define ACM_CTRL_RTS	(1 << 1)	/* unused with full duplex */
#define ACM_CTRL_DTR	(1 << 0)	/* host is ready for data r/w */

	/* SerialState notification -- CDC 1.1 section 6.3.5 (OUTPUT) */
	u16				serial_state;
#define ACM_CTRL_OVERRUN	(1 << 6)
#define ACM_CTRL_PARITY		(1 << 5)
#define ACM_CTRL_FRAMING	(1 << 4)
#define ACM_CTRL_RI		(1 << 3)
#define ACM_CTRL_BRK		(1 << 2)
#define ACM_CTRL_DSR		(1 << 1)
#define ACM_CTRL_DCD		(1 << 0)
};

static inline struct f_bmp *func_to_bmp(struct usb_function *f)
{
	return container_of(f, struct f_bmp, port.func);
}

static inline struct f_bmp *port_to_bmp(struct gserial *p)
{
	return container_of(p, struct f_bmp, port);
}

/*-------------------------------------------------------------------------*/

/* notification endpoint uses smallish and infrequent fixed-size messages */

#define GS_NOTIFY_INTERVAL_MS		32
#define GS_NOTIFY_MAXPACKET		10	/* notification + 2 bytes */

/* interface and class descriptors: */

static struct usb_interface_assoc_descriptor
bmp_iad_descriptor = {
	.bLength =		sizeof bmp_iad_descriptor,
	.bDescriptorType =	USB_DT_INTERFACE_ASSOCIATION,

	/* .bFirstInterface =	DYNAMIC, */
	.bInterfaceCount = 	2,	// control + data
	.bFunctionClass =	USB_CLASS_COMM,
	.bFunctionSubClass =	USB_CDC_SUBCLASS_ACM,
	.bFunctionProtocol =	USB_CDC_ACM_PROTO_AT_V25TER,
	/* .iFunction =		DYNAMIC */
};


static struct usb_interface_descriptor bmp_control_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_COMM,
	.bInterfaceSubClass =	USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol =	USB_CDC_ACM_PROTO_AT_V25TER,
	/* .iInterface = DYNAMIC */
};

static struct usb_interface_descriptor bmp_data_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	/* .iInterface = DYNAMIC */
};

static struct usb_cdc_header_desc bmp_header_desc = {
	.bLength =		sizeof(bmp_header_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_HEADER_TYPE,
	.bcdCDC =		cpu_to_le16(0x0110),
};

static struct usb_cdc_call_mgmt_descriptor
bmp_call_mgmt_descriptor = {
	.bLength =		sizeof(bmp_call_mgmt_descriptor),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_CALL_MANAGEMENT_TYPE,
	.bmCapabilities =	0,
	/* .bDataInterface = DYNAMIC */
};

static struct usb_cdc_acm_descriptor bmp_descriptor = {
	.bLength =		sizeof(bmp_descriptor),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_ACM_TYPE,
	.bmCapabilities =	USB_CDC_CAP_LINE,
};

static struct usb_cdc_union_desc bmp_union_desc = {
	.bLength =		sizeof(bmp_union_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_UNION_TYPE,
	/* .bMasterInterface0 =	DYNAMIC */
	/* .bSlaveInterface0 =	DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor bmp_fs_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(GS_NOTIFY_MAXPACKET),
	.bInterval =		GS_NOTIFY_INTERVAL_MS,
};

static struct usb_endpoint_descriptor bmp_fs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor bmp_fs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *bmp_fs_function[] = {
	(struct usb_descriptor_header *) &bmp_iad_descriptor,
	(struct usb_descriptor_header *) &bmp_control_interface_desc,
	(struct usb_descriptor_header *) &bmp_header_desc,
	(struct usb_descriptor_header *) &bmp_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &bmp_descriptor,
	(struct usb_descriptor_header *) &bmp_union_desc,
	(struct usb_descriptor_header *) &bmp_fs_notify_desc,
	(struct usb_descriptor_header *) &bmp_data_interface_desc,
	(struct usb_descriptor_header *) &bmp_fs_in_desc,
	(struct usb_descriptor_header *) &bmp_fs_out_desc,
	NULL,
};

/* high speed support: */
static struct usb_endpoint_descriptor bmp_hs_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(GS_NOTIFY_MAXPACKET),
	.bInterval =		USB_MS_TO_HS_INTERVAL(GS_NOTIFY_INTERVAL_MS),
};

static struct usb_endpoint_descriptor bmp_hs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor bmp_hs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *bmp_hs_function[] = {
	(struct usb_descriptor_header *) &bmp_iad_descriptor,
	(struct usb_descriptor_header *) &bmp_control_interface_desc,
	(struct usb_descriptor_header *) &bmp_header_desc,
	(struct usb_descriptor_header *) &bmp_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &bmp_descriptor,
	(struct usb_descriptor_header *) &bmp_union_desc,
	(struct usb_descriptor_header *) &bmp_hs_notify_desc,
	(struct usb_descriptor_header *) &bmp_data_interface_desc,
	(struct usb_descriptor_header *) &bmp_hs_in_desc,
	(struct usb_descriptor_header *) &bmp_hs_out_desc,
	NULL,
};

static struct usb_endpoint_descriptor bmp_ss_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_endpoint_descriptor bmp_ss_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor bmp_ss_bulk_comp_desc = {
	.bLength =              sizeof bmp_ss_bulk_comp_desc,
	.bDescriptorType =      USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_descriptor_header *bmp_ss_function[] = {
	(struct usb_descriptor_header *) &bmp_iad_descriptor,
	(struct usb_descriptor_header *) &bmp_control_interface_desc,
	(struct usb_descriptor_header *) &bmp_header_desc,
	(struct usb_descriptor_header *) &bmp_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &bmp_descriptor,
	(struct usb_descriptor_header *) &bmp_union_desc,
	(struct usb_descriptor_header *) &bmp_hs_notify_desc,
	(struct usb_descriptor_header *) &bmp_ss_bulk_comp_desc,
	(struct usb_descriptor_header *) &bmp_data_interface_desc,
	(struct usb_descriptor_header *) &bmp_ss_in_desc,
	(struct usb_descriptor_header *) &bmp_ss_bulk_comp_desc,
	(struct usb_descriptor_header *) &bmp_ss_out_desc,
	(struct usb_descriptor_header *) &bmp_ss_bulk_comp_desc,
	NULL,
};

/* string descriptors: */

#define ACM_CTRL_IDX	0
#define ACM_DATA_IDX	1
#define ACM_IAD_IDX	2

/* static strings, in UTF-8 */
static struct usb_string bmp_string_defs[] = {
	[ACM_CTRL_IDX].s = "CDC Abstract Control Model (ACM)",
	[ACM_DATA_IDX].s = "CDC ACM Data",
	[ACM_IAD_IDX ].s = "CDC Serial",
	{  } /* end of list */
};

static struct usb_gadget_strings bmp_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		bmp_string_defs,
};

static struct usb_gadget_strings *bmp_strings[] = {
	&bmp_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/

/* ACM control ... data handling is delegated to tty library code.
 * The main task of this function is to activate and deactivate
 * that code based on device state; track parameters like line
 * speed, handshake state, and so on; and issue notifications.
 */

static void bmp_complete_set_line_coding(struct usb_ep *ep,
		struct usb_request *req)
{
	struct f_bmp	*bmp = ep->driver_data;
	struct usb_composite_dev *cdev = bmp->port.func.config->cdev;

	if (req->status != 0) {
		dev_dbg(&cdev->gadget->dev, "bmp ttyGS%d completion, err %d\n",
			bmp->port_num, req->status);
		return;
	}

	/* normal completion */
	if (req->actual != sizeof(bmp->port_line_coding)) {
		dev_dbg(&cdev->gadget->dev, "bmp ttyGS%d short resp, len %d\n",
			bmp->port_num, req->actual);
		usb_ep_set_halt(ep);
	} else {
		struct usb_cdc_line_coding	*value = req->buf;

		/* REVISIT:  we currently just remember this data.
		 * If we change that, (a) validate it first, then
		 * (b) update whatever hardware needs updating,
		 * (c) worry about locking.  This is information on
		 * the order of 9600-8-N-1 ... most of which means
		 * nothing unless we control a real RS232 line.
		 */
		/* 串口配置参数 */
		bmp->port_line_coding = *value;
	}
}

static int bmp_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct f_bmp		*bmp = func_to_bmp(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	printk("%s %d red come here to emua usb gadget\n", __func__, __LINE__);
	/* composite driver infrastructure handles everything except
	 * CDC class messages; interface activation uses set_alt().
	 *
	 * Note CDC spec table 4 lists the ACM request profile.  It requires
	 * encapsulated command support ... we don't handle any, and respond
	 * to them by stalling.  Options include get/set/clear comm features
	 * (not that useful) and SEND_BREAK.
	 */
	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {

	/* SET_LINE_CODING ... just read and save what the host sends */
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_SET_LINE_CODING:
		if (w_length != sizeof(struct usb_cdc_line_coding)
				|| w_index != bmp->ctrl_id)
			goto invalid;

		value = w_length;
		cdev->gadget->ep0->driver_data = bmp;
		req->complete = bmp_complete_set_line_coding;
		break;

	/* GET_LINE_CODING ... return what host sent, or initial value */
	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_GET_LINE_CODING:
		if (w_index != bmp->ctrl_id)
			goto invalid;

		value = min_t(unsigned, w_length,
				sizeof(struct usb_cdc_line_coding));
		memcpy(req->buf, &bmp->port_line_coding, value);
		break;

	/* SET_CONTROL_LINE_STATE ... save what the host sent */
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		if (w_index != bmp->ctrl_id)
			goto invalid;

		value = 0;

		/* FIXME we should not allow data to flow until the
		 * host sets the ACM_CTRL_DTR bit; and when it clears
		 * that bit, we should return to that no-flow state.
		 */
		bmp->port_handshake_bits = w_value;
		break;

	default:
invalid:
		dev_vdbg(&cdev->gadget->dev,
			 "invalid control req%02x.%02x v%04x i%04x l%d\n",
			 ctrl->bRequestType, ctrl->bRequest,
			 w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		dev_dbg(&cdev->gadget->dev,
			"bmp ttyGS%d req%02x.%02x v%04x i%04x l%d\n",
			bmp->port_num, ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			ERROR(cdev, "bmp response on ttyGS%d, err %d\n",
					bmp->port_num, value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static int bmp_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_bmp		*bmp = func_to_bmp(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	/* we know alt == 0, so this is an activation or a reset */

	printk("%s %d red come here to set_alt with usb gadget\n", __func__, __LINE__);
	if (intf == bmp->ctrl_id) {
		dev_vdbg(&cdev->gadget->dev,
				"reset bmp control interface %d\n", intf);
		usb_ep_disable(bmp->notify);

		if (!bmp->notify->desc)
			if (config_ep_by_speed(cdev->gadget, f, bmp->notify))
				return -EINVAL;

		usb_ep_enable(bmp->notify);

	} else if (intf == bmp->data_id) {
		if (bmp->notify->enabled) {
			dev_dbg(&cdev->gadget->dev,
				"reset bmp ttyGS%d\n", bmp->port_num);
			gserial_disconnect(&bmp->port);
		}
		if (!bmp->port.in->desc || !bmp->port.out->desc) {
			dev_dbg(&cdev->gadget->dev,
				"activate bmp ttyGS%d\n", bmp->port_num);
			if (config_ep_by_speed(cdev->gadget, f,
					       bmp->port.in) ||
			    config_ep_by_speed(cdev->gadget, f,
					       bmp->port.out)) {
				bmp->port.in->desc = NULL;
				bmp->port.out->desc = NULL;
				return -EINVAL;
			}
		}
		/* 将接口信息关联到 ep 的相关内容，同步绑定 usb 和 tty 设备之间的内容 */
		gserial_connect(&bmp->port, bmp->port_num);

	} else
		return -EINVAL;

	return 0;
}

static void bmp_disable(struct usb_function *f)
{
	struct f_bmp	*bmp = func_to_bmp(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	dev_dbg(&cdev->gadget->dev, "bmp ttyGS%d deactivated\n", bmp->port_num);
	gserial_disconnect(&bmp->port);
	usb_ep_disable(bmp->notify);
}

/*-------------------------------------------------------------------------*/

/**
 * bmp_cdc_notify - issue CDC notification to host
 * @bmp: wraps host to be notified
 * @type: notification type
 * @value: Refer to cdc specs, wValue field.
 * @data: data to be sent
 * @length: size of data
 * Context: irqs blocked, bmp->lock held, bmp_notify_req non-null
 *
 * Returns zero on success or a negative errno.
 *
 * See section 6.3.5 of the CDC 1.1 specification for information
 * about the only notification we issue:  SerialState change.
 */
static int bmp_cdc_notify(struct f_bmp *bmp, u8 type, u16 value,
		void *data, unsigned length)
{
	struct usb_ep			*ep = bmp->notify;
	struct usb_request		*req;
	struct usb_cdc_notification	*notify;
	const unsigned			len = sizeof(*notify) + length;
	void				*buf;
	int				status;

	req = bmp->notify_req;
	bmp->notify_req = NULL;
	bmp->pending = false;

	req->length = len;
	notify = req->buf;
	buf = notify + 1;

	notify->bmRequestType = USB_DIR_IN | USB_TYPE_CLASS
			| USB_RECIP_INTERFACE;
	notify->bNotificationType = type;
	notify->wValue = cpu_to_le16(value);
	notify->wIndex = cpu_to_le16(bmp->ctrl_id);
	notify->wLength = cpu_to_le16(length);
	memcpy(buf, data, length);

	/* ep_queue() can complete immediately if it fills the fifo... */
	spin_unlock(&bmp->lock);
	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	spin_lock(&bmp->lock);

	if (status < 0) {
		ERROR(bmp->port.func.config->cdev,
				"bmp ttyGS%d can't notify serial state, %d\n",
				bmp->port_num, status);
		bmp->notify_req = req;
	}

	return status;
}

static int bmp_notify_serial_state(struct f_bmp *bmp)
{
	struct usb_composite_dev *cdev = bmp->port.func.config->cdev;
	int			status;
	__le16			serial_state;

	spin_lock(&bmp->lock);
	if (bmp->notify_req) {
		dev_dbg(&cdev->gadget->dev, "bmp ttyGS%d serial state %04x\n",
			bmp->port_num, bmp->serial_state);
		serial_state = cpu_to_le16(bmp->serial_state);
		status = bmp_cdc_notify(bmp, USB_CDC_NOTIFY_SERIAL_STATE,
				0, &serial_state, sizeof(bmp->serial_state));
	} else {
		bmp->pending = true;
		status = 0;
	}
	spin_unlock(&bmp->lock);
	return status;
}

static void bmp_cdc_notify_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_bmp		*bmp = req->context;
	u8			doit = false;

	/* on this call path we do NOT hold the port spinlock,
	 * which is why ACM needs its own spinlock
	 */
	spin_lock(&bmp->lock);
	if (req->status != -ESHUTDOWN)
		doit = bmp->pending;
	bmp->notify_req = req;
	spin_unlock(&bmp->lock);

	if (doit)
		bmp_notify_serial_state(bmp);
}

/* connect == the TTY link is open */

static void bmp_connect(struct gserial *port)
{
	struct f_bmp		*bmp = port_to_bmp(port);

	bmp->serial_state |= ACM_CTRL_DSR | ACM_CTRL_DCD;
	bmp_notify_serial_state(bmp);
}

static void bmp_disconnect(struct gserial *port)
{
	struct f_bmp		*bmp = port_to_bmp(port);

	bmp->serial_state &= ~(ACM_CTRL_DSR | ACM_CTRL_DCD);
	bmp_notify_serial_state(bmp);
}

static int bmp_send_break(struct gserial *port, int duration)
{
	struct f_bmp		*bmp = port_to_bmp(port);
	u16			state;

	state = bmp->serial_state;
	state &= ~ACM_CTRL_BRK;
	if (duration)
		state |= ACM_CTRL_BRK;

	bmp->serial_state = state;
	return bmp_notify_serial_state(bmp);
}

/*-------------------------------------------------------------------------*/

/* ACM function driver setup/binding */
static int
bmp_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_bmp		*bmp = func_to_bmp(f);
	struct usb_string	*us;
	int			status;
	struct usb_ep		*ep;

	printk("%s %d red come here to bind with usb gadget\n", __func__, __LINE__);
	/* REVISIT might want instance-specific strings to help
	 * distinguish instances ...
	 */

	/* maybe allocate device-global string IDs, and patch descriptors */
	us = usb_gstrings_attach(cdev, bmp_strings,
			ARRAY_SIZE(bmp_string_defs));
	if (IS_ERR(us))
		return PTR_ERR(us);
	bmp_control_interface_desc.iInterface = us[ACM_CTRL_IDX].id;
	bmp_data_interface_desc.iInterface = us[ACM_DATA_IDX].id;
	bmp_iad_descriptor.iFunction = us[ACM_IAD_IDX].id;

	/* allocate instance-specific interface IDs, and patch descriptors */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	bmp->ctrl_id = status;
	bmp_iad_descriptor.bFirstInterface = status;

	bmp_control_interface_desc.bInterfaceNumber = status;
	bmp_union_desc .bMasterInterface0 = status;

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	bmp->data_id = status;

	bmp_data_interface_desc.bInterfaceNumber = status;
	bmp_union_desc.bSlaveInterface0 = status;
	bmp_call_mgmt_descriptor.bDataInterface = status;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &bmp_fs_in_desc);
	if (!ep)
		goto fail;
	bmp->port.in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &bmp_fs_out_desc);
	if (!ep)
		goto fail;
	bmp->port.out = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &bmp_fs_notify_desc);
	if (!ep)
		goto fail;
	bmp->notify = ep;

	/* allocate notification */
	bmp->notify_req = gs_alloc_req(ep,
			sizeof(struct usb_cdc_notification) + 2,
			GFP_KERNEL);
	if (!bmp->notify_req)
		goto fail;

	bmp->notify_req->complete = bmp_cdc_notify_complete;
	bmp->notify_req->context = bmp;

	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	bmp_hs_in_desc.bEndpointAddress = bmp_fs_in_desc.bEndpointAddress;
	bmp_hs_out_desc.bEndpointAddress = bmp_fs_out_desc.bEndpointAddress;
	bmp_hs_notify_desc.bEndpointAddress =
		bmp_fs_notify_desc.bEndpointAddress;

	bmp_ss_in_desc.bEndpointAddress = bmp_fs_in_desc.bEndpointAddress;
	bmp_ss_out_desc.bEndpointAddress = bmp_fs_out_desc.bEndpointAddress;

	status = usb_assign_descriptors(f, bmp_fs_function, bmp_hs_function,
			bmp_ss_function, NULL);
	if (status)
		goto fail;

	dev_dbg(&cdev->gadget->dev,
		"bmp ttyGS%d: %s speed IN/%s OUT/%s NOTIFY/%s\n",
		bmp->port_num,
		gadget_is_superspeed(c->cdev->gadget) ? "super" :
		gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
		bmp->port.in->name, bmp->port.out->name,
		bmp->notify->name);
	return 0;

fail:
	if (bmp->notify_req)
		gs_free_req(bmp->notify, bmp->notify_req);

	ERROR(cdev, "%s/%p: can't bind, err %d\n", f->name, f, status);

	return status;
}

static void bmp_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_bmp		*bmp = func_to_bmp(f);

	bmp_string_defs[0].id = 0;
	usb_free_all_descriptors(f);
	if (bmp->notify_req)
		gs_free_req(bmp->notify, bmp->notify_req);
}

static void bmp_free_func(struct usb_function *f)
{
	struct f_bmp		*bmp = func_to_bmp(f);

	kfree(bmp);
}

static struct usb_function *bmp_alloc_func(struct usb_function_instance *fi)
{
	struct f_serial_opts *opts;
	struct f_bmp *bmp;

	bmp = kzalloc(sizeof(*bmp), GFP_KERNEL);
	if (!bmp)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&bmp->lock);

	/* 初始化 bmp 中的 struct gserial 结构体 */
	bmp->port.connect = bmp_connect;
	bmp->port.disconnect = bmp_disconnect;
	bmp->port.send_break = bmp_send_break;

	/* 初始化接口内容 */
	bmp->port.func.name = "bmp";
	bmp->port.func.strings = bmp_strings;
	/* descriptors are per-instance copies */
	/* 接口的 bind 函数
	 * 是在枚举阶段，配置这个接口的时候，执行这个接口的 bind 函数
	 * */
	bmp->port.func.bind = bmp_bind;
	/* 接口参数设置函数，默认首先使用 0 号设置 */
	bmp->port.func.set_alt = bmp_set_alt;
	bmp->port.func.setup = bmp_setup;
	bmp->port.func.disable = bmp_disable;

	opts = container_of(fi, struct f_serial_opts, func_inst);
	/* 主要是获取这个端口号，因为这个数据不在标准的 usb_function_instance 结构体中 */
	bmp->port_num = opts->port_num;
	bmp->port.func.unbind = bmp_unbind;
	bmp->port.func.free_func = bmp_free_func;

	return &bmp->port.func;
}

static inline struct f_serial_opts *to_f_serial_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_serial_opts,
			func_inst.group);
}

static void bmp_attr_release(struct config_item *item)
{
	struct f_serial_opts *opts = to_f_serial_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations bmp_item_ops = {
	.release                = bmp_attr_release,
};

static ssize_t f_bmp_port_num_show(struct config_item *item, char *page)
{
	return sprintf(page, "%u\n", to_f_serial_opts(item)->port_num);
}

CONFIGFS_ATTR_RO(f_bmp_, port_num);

static struct configfs_attribute *bmp_attrs[] = {
	&f_bmp_attr_port_num,
	NULL,
};

static const struct config_item_type bmp_func_type = {
	.ct_item_ops    = &bmp_item_ops,
	.ct_attrs	= bmp_attrs,
	.ct_owner       = THIS_MODULE,
};

static void bmp_free_instance(struct usb_function_instance *fi)
{
	struct f_serial_opts *opts;

	opts = container_of(fi, struct f_serial_opts, func_inst);
	gserial_free_line(opts->port_num);
	kfree(opts);
}

static struct usb_function_instance *bmp_alloc_instance(void)
{
	struct f_serial_opts *opts;
	int ret;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);
	opts->func_inst.free_func_inst = bmp_free_instance;
	/* 在本地注册一个 tty 设备 */
	ret = gserial_alloc_line(&opts->port_num);
	if (ret) {
		kfree(opts);
		return ERR_PTR(ret);
	}
	config_group_init_type_name(&opts->func_inst.group, "",
			&bmp_func_type);
	return &opts->func_inst;
}
DECLARE_USB_FUNCTION_INIT(bmp, bmp_alloc_instance, bmp_alloc_func);
MODULE_LICENSE("GPL");
