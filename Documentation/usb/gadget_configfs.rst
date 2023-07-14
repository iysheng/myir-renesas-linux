============================================
Linux USB gadget configured through configfs
============================================


25th April 2013




Overview
========

A USB Linux Gadget is a device which has a UDC (USB Device Controller) and can
be connected to a USB Host to extend it with additional functions like a serial
port or a mass storage capability.

从主机的视角来看，一个 gadget 是一些配置的集合，每一个配置都包含了一系列的接口。这些
接口从 gadget 的角度来看就被称为功能。每一个功能表示比如串口，或者 SCSI 磁盘。
A gadget is seen by its host as a set of configurations, each of which contains
a number of interfaces which, from the gadget's perspective, are known as
functions, each function representing e.g. a serial connection or a SCSI disk.

Linux 给 gadgets 提供了一系列的功能来用。
Linux provides a number of functions for gadgets to use.

创建一个 gadget 意味着决定了将会使用的配置，以及每一个配置将会提供的功能。
Creating a gadget means deciding what configurations there will be
and which functions each configuration will provide.

Configfs (请参看 Documentation/filesystems/configfs.rst) 使告诉内核有关上述参数配置
相关的内容变得更加友好。这个文档就是有关这部分内容的介绍。
Configfs (please see `Documentation/filesystems/configfs.rst`) lends itself nicely
for the purpose of telling the kernel about the above mentioned decision.
This document is about how to do it.

这里也描述了 configfs 集成到 gadget 这部分内容中是如何设计的。
It also describes how configfs integration into gadget is designed.




Requirements
============

为了这个目的，前提条件是 configfs 必须有效， 所以 CONFIGFS_FS 必须选择为 y 或者 m.
因为这个原因，选择 USB_LIBCOMPOSITE 会强制选择 CONFIGFS_FS
In order for this to work configfs must be available, so CONFIGFS_FS must be
'y' or 'm' in .config. As of this writing USB_LIBCOMPOSITE selects CONFIGFS_FS.




Usage
=====

(通过 configfs 描述第一个功能最原始的文章如下)
(The original post describing the first function
made available through configfs can be seen here:
http://www.spinics.net/lists/linux-usb/msg76388.html)

::

	$ modprobe libcomposite
	$ mount none $CONFIGFS_HOME -t configfs

CONFIGFS_HOME 是 configfs 的挂载点
where CONFIGFS_HOME is the mount point for configfs

1. 创建 gadgets
1. Creating the gadgets
-----------------------

每一个将要创建的 gadget 都要创建一个对应的目录
For each gadget to be created its corresponding directory must be created::

	$ mkdir $CONFIGFS_HOME/usb_gadget/<gadget name>

e.g.::

	$ mkdir $CONFIGFS_HOME/usb_gadget/g1

	...
	...
	...

	$ cd $CONFIGFS_HOME/usb_gadget/g1

每一个 gadget 都需要和它对应的 VID 和 PID
Each gadget needs to have its vendor id <VID> and product id <PID> specified::

	$ echo <VID> > idVendor
	$ echo <PID> > idProduct

一个 gadget 也需要它的序列号，制造商和产品字符串。
A gadget also needs its serial number, manufacturer and product strings.
为了有一个地方存储它们，需要为每一个语言创建一个字符串的子目录，比如：
In order to have a place to store them, a strings subdirectory must be created
for each language, e.g.::

0X409 表示美式英语，完整的文档请参看https://winprotocoldoc.blob.core.windows.net/productionwindowsarchives/MS-LCID/%5bMS-LCID%5d.pdf
	$ mkdir strings/0x409

然后就可以使用如下命令设置对应的字符串了
Then the strings can be specified::

	$ echo <serial number> > strings/0x409/serialnumber
	$ echo <manufacturer> > strings/0x409/manufacturer
	$ echo <product> > strings/0x409/product

2. 创建配置
2. Creating the configurations
------------------------------

每一个 gadget 都有一些配置组成，对应的都需要创建相关的目录:
Each gadget will consist of a number of configurations, their corresponding
directories must be created:

$ mkdir configs/<name>.<number>
其中，<name>是一个文件系统中有效的任意字符串，<number>对应的是配置的编号
where <name> can be any string which is legal in a filesystem and the
<number> is the configuration's number, e.g.::

	$ mkdir configs/c.1

	...
	...
	...

每一个配置也需要一些字符串信息，所以也需要为每一个语言ID创建一个目录
Each configuration also needs its strings, so a subdirectory must be created
for each language, e.g.::

	$ mkdir configs/c.1/strings/0x409

配置的字符串内容就可以通过如下命令存储起来了
Then the configuration string can be specified::

	$ echo <configuration> > configs/c.1/strings/0x409/configuration

也可以为这个配置设置一些属性信息,比如
Some attributes can also be set for a configuration, e.g.::

设置电流最大为 120 * 2ma, 电流最低刻度为 2ma
	$ echo 120 > configs/c.1/MaxPower

3. 创建功能
3. Creating the functions
-------------------------

gadget 将提供一些功能，每一个功能都需要创建对应的目录：
The gadget will provide some functions, for each function its corresponding
directory must be created::

	$ mkdir functions/<name>.<instance name>

其中 <name> 对应的是允许的功能的名字，并且示例名字是一个文件系统中允许的任意字符串,
where <name> corresponds to one of allowed function names and instance name
is an arbitrary string allowed in a filesystem, e.g.::

  $ mkdir functions/ncm.usb0 # usb_f_ncm.ko gets loaded with request_module()

  ...
  ...
  ...
每一个功能提供了它独有的一些属性集，可能是只读的或者是有读写权限的。通过对这些
属性进行重写可以实现专门的功能。
Each function provides its specific set of attributes, with either read-only
or read-write access. Where applicable they need to be written to as
appropriate.
请参看 Documentation/ABI/*/configfs-usb-gadget 来获得更多信息
Please refer to Documentation/ABI/*/configfs-usb-gadget* for more information.

4. 将功能和它们的配置关联起来
4. Associating the functions with their configurations
------------------------------------------------------

到这里的时候，已经创建了很多 gadget,每一个 gadget 都有一些专门的配置和功能。还存在
的问题是任意的配置包含有哪些功能。(相同的功能可以用在多个配置中)。这是通过创建软链接
的形式实现的。
At this moment a number of gadgets is created, each of which has a number of
configurations specified and a number of functions available. What remains
is specifying which function is available in which configuration (the same
function can be used in multiple configurations). This is achieved with
creating symbolic links::

	$ ln -s functions/<name>.<instance name> configs/<name>.<number>

e.g.::

	$ ln -s functions/ncm.usb0 configs/c.1

	...
	...
	...

5. 使能 gadget
5. Enabling the gadget
----------------------

所有的上述步骤实现了将配置和功能继承到 gagdet 的目的。
All the above steps serve the purpose of composing the gadget of
configurations and functions.

一个示意目录结构如下所示：
An example directory structure might look like this::

  .
  ./strings
  ./strings/0x409
  ./strings/0x409/serialnumber
  ./strings/0x409/product
  ./strings/0x409/manufacturer
  ./configs
  ./configs/c.1
  ./configs/c.1/ncm.usb0 -> ../../../../usb_gadget/g1/functions/ncm.usb0
  ./configs/c.1/strings
  ./configs/c.1/strings/0x409
  ./configs/c.1/strings/0x409/configuration
  ./configs/c.1/bmAttributes
  ./configs/c.1/MaxPower
  ./functions
  ./functions/ncm.usb0
  ./functions/ncm.usb0/ifname
  ./functions/ncm.usb0/qmult
  ./functions/ncm.usb0/host_addr
  ./functions/ncm.usb0/dev_addr
  ./UDC
  ./bcdUSB
  ./bcdDevice
  ./idProduct
  ./idVendor
  ./bMaxPacketSize0
  ./bDeviceProtocol
  ./bDeviceSubClass
  ./bDeviceClass

上述 gadget 必须使能后，才能被 USB host 枚举出来。
Such a gadget must be finally enabled so that the USB host can enumerate it.

使用如下命令使能这个 gadget
In order to enable the gadget it must be bound to a UDC (USB Device
Controller)::

	$ echo <udc name> > UDC

where <udc name> is one of those found in /sys/class/udc/*
e.g.::

	$ echo s3c-hsotg > UDC


6. 禁用这个 gadget
6. Disabling the gadget
-----------------------

::

	$ echo "" > UDC

7. Cleaning up
--------------

从配置中移除功能
Remove functions from configurations::

	$ rm configs/<config name>.<number>/<function>

where <config name>.<number> specify the configuration and <function> is
a symlink to a function being removed from the configuration, e.g.::

	$ rm configs/c.1/ncm.usb0

	...
	...
	...

从配置中移除字符串
Remove strings directories in configurations:

	$ rmdir configs/<config name>.<number>/strings/<lang>

e.g.::

	$ rmdir configs/c.1/strings/0x409

	...
	...
	...

移除配置
and remove the configurations::

	$ rmdir configs/<config name>.<number>

e.g.::

	rmdir configs/c.1

	...
	...
	...

移除功能（即使功能模块没有被卸载）
Remove functions (function modules are not unloaded, though):

	$ rmdir functions/<name>.<instance name>

e.g.::

	$ rmdir functions/ncm.usb0

	...
	...
	...

移除 gadget 的字符串目录
Remove strings directories in the gadget::

	$ rmdir strings/<lang>

e.g.::

	$ rmdir strings/0x409

最终移除这个 gadget
and finally remove the gadget::

	$ cd ..
	$ rmdir <gadget name>

e.g.::

	$ rmdir g1




设计实现
Implementation design
=====================
有关 configfs 是如何实现的如下所述。configfs 中分为 item 和 group,它们都是
以目录的形式表现的。它们之间的区别是 group 可以包含其他 groups.下面的图片中，
只展示了一个 item.items 和 groups 都可以包含属性，属性是以文件形式存储的。读者
可以创建或者删除目录，但是不能直接删除文件，这些文件的权限根据它们表示的内容不同，
权限可能是只读的或者读写的。
Below the idea of how configfs works is presented.
In configfs there are items and groups, both represented as directories.
The difference between an item and a group is that a group can contain
other groups. In the picture below only an item is shown.
Both items and groups can have attributes, which are represented as files.
The user can create and remove directories, but cannot remove files,
which can be read-only or read-write, depending on what they represent.

configfs 中的文件系统部分对 config_items/groups 和 configfs_attributes 进行操作，
它们是通用的并且对所有配置的元素具有相同的类型。
但是，它们嵌入到专用的较大的结构体中。下属图片中有一个 cs 包含了一个 config_item
和一个 sa 包含了一个 configfs_attribute
The filesystem part of configfs operates on config_items/groups and
configfs_attributes which are generic and of the same type for all
configured elements. However, they are embedded in usage-specific
larger structures. In the picture below there is a "cs" which contains
a config_item and an "sa" which contains a configfs_attribute.

文件系统的概览如下：
The filesystem view would be like this::

  ./
  ./cs        (directory)
     |
     +--sa    (file)
     |
     .
     .
     .

无论任何时候一个用户读写这个 sa 文件，就会调用一个接受 struct config_item 和
struct configfs_attribute 参数的函数。
Whenever a user reads/writes the "sa" file, a function is called
which accepts a struct config_item and a struct configfs_attribute.

在刚才提到的函数中，会通过 container_of 这个宏获取对应的 sa 和 cs 数据结构。
使用一个有关 sa 参数的函数来展示或者存储，还包含了cs参数以及一个字符串。
show函数用来展示文件的内容（从cs复制文件到缓存），而store函数用来修改文件的文件（
从缓存复制数据到cs）,但是还需要最终的实现者决定这两个函数实际用来做什么事情。
In the said function the "cs" and "sa" are retrieved using the well
known container_of technique and an appropriate sa's function (show or
store) is called and passed the "cs" and a character buffer. The "show"
is for displaying the file's contents (copy data from the cs to the
buffer), while the "store" is for modifying the file's contents (copy data
from the buffer to the cs), but it is up to the implementer of the
two functions to decide what they actually do.

::

  typedef struct configured_structure cs;
  typedef struct specific_attribute sa;

                                         sa
                         +----------------------------------+
          cs             |  (*show)(cs *, buffer);          |
  +-----------------+    |  (*store)(cs *, buffer, length); |
  |                 |    |                                  |
  | +-------------+ |    |       +------------------+       |
  | | struct      |-|----|------>|struct            |       |
  | | config_item | |    |       |configfs_attribute|       |
  | +-------------+ |    |       +------------------+       |
  |                 |    +----------------------------------+
  | data to be set  |                .
  |                 |                .
  +-----------------+                .

文件的名字由 config item/group 的设计者决定，但是目录的命名可以随意。
一个 group 可以自动创建一些它默认的 sub-groups
The file names are decided by the config item/group designer, while
the directories in general can be named at will. A group can have
a number of its default sub-groups created automatically.

有关 configfs 更多信息请参看文档
For more information on configfs please see
`Documentation/filesystems/configfs.rst`.

上述介绍的一些转换 USB gadget 概念如下：
The concepts described above translate to USB gadgets like this:

1. 一个 gadget 有自己的配置组，每一个配置组（设备描述符）有一些属性（idVendor, idProduct 等）
还有一些默认的 sub-groups (configs(配置描述符), functions(接口描述符还是端点描述符？感觉是接口描述符), strings).
写属性会将信息存储到合适的位置。在配置中，用户可以创建它们的functions 和字符串 sub-grouops.
1. A gadget has its config group, which has some attributes (idVendor,
idProduct etc) and default sub-groups (configs, functions, strings).
Writing to the attributes causes the information to be stored in
appropriate locations. In the configs, functions and strings sub-groups
a user can create their sub-groups to represent configurations, functions,
and groups of strings in a given language.

2. 用户创建配置和功能，在配置中创建符号链接到具体的功能。当往 gadget‘s UDC 属性
中写入的时候需要这些信息，表示绑定 gadget 到指定的 UDC。drivers/usb/gadget/configfs.c 文件
会迭代所有的配置，并且在每一个配置中会迭代绑定的所有功能。通过这种方式，所有的 gadget 就都绑定起来了。
2. The user creates configurations and functions, in the configurations
creates symbolic links to functions. This information is used when the
gadget's UDC attribute is written to, which means binding the gadget
to the UDC. The code in drivers/usb/gadget/configfs.c iterates over
all configurations, and in each configuration it iterates over all
functions and binds them. This way the whole gadget is bound.

3. 文件 drivers/usb/gadget/configfs.c 包含了如下功能的代码
   - gadget 的 config_group
   - gadget 的 default group (configs, functions, strings)
   - 关联功能到配置(符号链接)

3. The file drivers/usb/gadget/configfs.c contains code for

	- gadget's config_group
	- gadget's default groups (configs, functions, strings)
	- associating functions with configurations (symlinks)

4. 每一个 USB 的功能很自然地有它自己需要的配置，所以 config_groups 实际使用的函数
定义在文件 drivers/usb/gadget/f_*.c

4. Each USB function naturally has its own view of what it wants
configured, so config_groups for particular functions are defined
in the functions implementation files drivers/usb/gadget/f_*.c.

5. 功能代码在使用的时候使用下述方式进行编写
5. Function's code is written in such a way that it uses

usb_get_function_instance() 反过来调用请求的模块。所以，假定 modprobe 正常工作，
实际功能的模块会自动加载。请注意，反之，在一个 gadget 被禁用并且卸载掉之后，模块亦然是加载状态。
usb_get_function_instance(), which, in turn, calls request_module.
So, provided that modprobe works, modules for particular functions
are loaded automatically. Please note that the converse is not true:
after a gadget is disabled and torn down, the modules remain loaded.
