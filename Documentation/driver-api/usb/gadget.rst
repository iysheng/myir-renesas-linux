========================
USB Gadget API for Linux
========================

:Author: David Brownell
:Date:   20 August 2004

Introduction
============

这个文档描述了一个 Linux-USB “Gadget” 内核模块 API，为了在外设以及潜入到 Linux 的
其他 USB 设备中使用。它提供了这些 API 的一个概览或者概览，并且展示了如何将它应用到一个开发的系统工程中。
发布这个 API 的首要目的是强调一系列的重要问题，包括：

-支持 USB2.0，支持高速设备
-处理上百个端点的设备看起来就像是具有两个固定功能的点。重写 Gadget 部分的驱动以便更方便地移植到新的硬件
-足够灵活来处理更加复杂的 USB 设备能力，比如多个配置，多个接口，组合设备以及**备选接口设置**。
-支持USB OTG,同步更新 host 端的升级
-和 host 端共享数据结构和API 模型，这有助于对 OTG 的支持，并且框架看起来更加对称（host端和device端使用相同的框架模型）
-往小了说，可以降低新的设备控制器的开发难度，I/O 处理不需要花费大量的内存和CPU资源。

This document presents a Linux-USB "Gadget" kernel mode API, for use
within peripherals and other USB devices that embed Linux. It provides
an overview of the API structure, and shows how that fits into a system
development project. This is the first such API released on Linux to
address a number of important problems, including:

-  Supports USB 2.0, for high speed devices which can stream data at
   several dozen megabytes per second.

-  Handles devices with dozens of endpoints just as well as ones with
   just two fixed-function ones. Gadget drivers can be written so
   they're easy to port to new hardware.

-  Flexible enough to expose more complex USB device capabilities such
   as multiple configurations, multiple interfaces, composite devices,
   and alternate interface settings.

-  USB "On-The-Go" (OTG) support, in conjunction with updates to the
   Linux-USB host side.

-  Sharing data structures and API models with the Linux-USB host side
   API. This helps the OTG support, and looks forward to more-symmetric
   frameworks (where the same I/O model is used by both host and device
   side drivers).

-  Minimalist, so it's easier to support new device controller hardware.
   I/O processing doesn't imply large demands for memory or CPU
   resources.

很多Linux 开发者将不能使用这个 API，因为他们的PC机，工作站或者服务器使用的是USB host控制器。
使用嵌入式 Linux 的用户可能会更容易碰到使用 USB device 硬件，即作为从机的 USB 设备。
Most Linux developers will not be able to use this API, since they have
USB ``host`` hardware in a PC, workstation, or server. Linux users with
embedded systems are more likely to have USB peripheral hardware. 

为了和在传统 USB 主机端运行的 USB device drivers 区别开来，我们在这里使用不同的术语：
在设备端运行的驱动是 USB gadget drivers，在 USB 交互协议中，host 端的 device driver 被称为"client driver",
gadget 驱动是 slave,或者称之为 "function driver".

To distinguish drivers running inside such hardware from the more familiar
Linux "USB device drivers", which are host side proxies for the real USB
devices, a different term is used: the drivers inside the peripherals
are "USB gadget drivers". In USB protocol interactions, the device
driver is the master (or "client driver") and the gadget driver is the
slave (or "function driver").

gadget API 效仿主机端的 Linux-USB API 实现缓存I/O相关的请求，以及那些可能被提交或者撤销的请求。
The gadget API resembles the host side Linux-USB API in that both use
queues of request objects to package I/O buffers, and those requests may
be submitted or canceled. 

他们共享标准 USB 的第 9 章通用的定义，包括，消息，结构体和常量。同时也包括
bind 和 unbind 驱动到设备的内容。host 的 peripheral 驱动 API 之间有差异，因为
host 端现有的 URB 框架暴露了一些细节的实现，并且假定这些它们不适用于 gadget API。
虽然控制传输和配置管理的模型完全不同（一边是硬件中性主设备，另一边是硬件感知从设备），
这里使用的端点I/0 API也可以被用到降低开销的主机册 API。
They share common definitions for the standard
USB *Chapter 9* messages, structures, and constants. Also, both APIs
bind and unbind drivers to devices. The APIs differ in detail, since the
host side's current URB framework exposes a number of implementation
details and assumptions that are inappropriate for a gadget API. While
the model for control transfers and configuration management is
necessarily different (one side is a hardware-neutral master, the other
is a hardware-aware slave), the endpoint I/0 API used here should also
be usable for an overhead-reduced host side API.

Gadget 驱动的结构体
Structure of Gadget Drivers
===========================

在USB外设端运行的系统内核一般具有至少3层结构来处理 USB 协议，同时可能在用户态有额外的层。
gadget API被用到中间层和最底层（直接和硬件打交道）交互。
A system running inside a USB peripheral normally has at least three
layers inside the kernel to handle USB protocol processing, and may have
additional layers in user space code. The ``gadget`` API is used by the
middle layer to interact with the lowest level (which directly handles
hardware).

在 Linux 中，从底层到顶层，这些层分别是：
In Linux, from the bottom up, these layers are:

USB 控制器驱动
*USB Controller Driver*
这是最底层的软件层。这是唯一和硬件通信的层，通过寄存器，fifos,dma,irqs等等。
<linux/usb/gadget.h> 头文件中的 API 对外设控制器端点硬件进行抽象。这个硬件通过
endpoint 对象进行导出，endpoint 可以接收 IN/OUT 缓存的流，通过回调函数和 gadget 驱动
进行通信。因为一般的 USB 设备只有一个上行口，他们只能有一个对应的驱动。虽然控制器驱动
可以支持多种 gadget 驱动，但是同一时刻只能使用一种。
    This is the lowest software level. It is the only layer that talks
    to hardware, through registers, fifos, dma, irqs, and the like. The
    ``<linux/usb/gadget.h>`` API abstracts the peripheral controller
    endpoint hardware. That hardware is exposed through endpoint
    objects, which accept streams of IN/OUT buffers, and through
    callbacks that interact with gadget drivers. Since normal USB
    devices only have one upstream port, they only have one of these
    drivers. The controller driver can support any number of different
    gadget drivers, but only one of them can be used at a time.

    Examples of such controller hardware include the PCI-based NetChip
    2280 USB 2.0 high speed controller, the SA-11x0 or PXA-25x UDC
    (found within many PDAs), and a variety of other products.

Gadget 驱动
*Gadget Driver*
该驱动程序的下边界实现了和硬件无关的 USB 功能，通过调用控制器驱动的方式来实现。
因为硬件的容量和限制变化很大，并且在嵌入式设备中，容量很珍贵，单一时刻只会编译一种
gadget 驱动,最新的内核简化了这个过程支持新的硬件，通过 "autoconfiguring" 这个 endpoint。
Gadget 驱动响应包含如下：
    The lower boundary of this driver implements hardware-neutral USB
    functions, using calls to the controller driver. Because such
    hardware varies widely in capabilities and restrictions, and is used
    in embedded environments where space is at a premium, the gadget
    driver is often configured at compile time to work with endpoints
    supported by one particular controller. Gadget drivers may be
    portable to several different controllers, using conditional
    compilation. (Recent kernels substantially simplify the work
    involved in supporting new hardware, by *autoconfiguring* endpoints
    automatically for many bulk-oriented drivers.) Gadget driver
    responsibilities include:

    - 处理 setup 请求, 建立事务，通过 ep0 协议响应, 可能还包含有类专有的功能
    -  handling setup requests (ep0 protocol responses) possibly
       including class-specific functionality

    - 返回配置和字符串描述符
    -  returning configuration and string descriptors

    - （重新）设置配置和接口，包括使能和配置端点
    -  (re)setting configurations and interface altsettings, including
       enabling and configuring endpoints

    - 处理 life cycle (~~等时传输~~)事件，比如处理绑定大盘硬件，USB抑制/恢复，远程唤醒
    -  handling life cycle events, such as managing bindings to
       hardware, USB suspend/resume, remote wakeup, and disconnection
       from the USB host.

    - 在目前使能的 endpoints 上处理 IN 和 OUT 事务
    -  managing IN and OUT transfers on all currently enabled endpoints

    这些驱动可能使用了一些合适但是不被 Linux 社区鼓励的代码
    Such drivers may be modules of proprietary code, although that
    approach is discouraged in the Linux community.

上层
*Upper Level*
许多 gadget 驱动有一个上边界用来链接一些 Linux 驱动或者 Linux 的框架。通过 USB 根据协议传输的生产或者消费数据通过这个边界流通，
举例子：
    Most gadget drivers have an upper boundary that connects to some
    Linux driver or framework in Linux. Through that boundary flows the
    data which the gadget driver produces and/or consumes through
    protocol transfers over USB. Examples include:

    - 用户模式代码，使用通用（gadgetfs）或者 /dev 目录下的应用专有文件
    -  user mode code, using generic (gadgetfs) or application specific
       files in ``/dev``

    - 网络子系统（针对网络 gadget,像 CDC Ethernet Model gadget 驱动）
    -  networking subsystem (for network gadgets, like the CDC Ethernet
       Model gadget driver)

    - 数据捕获驱动，比如 video4Linux 或者扫描机驱动;或者测试以及测量硬件
    -  data capture drivers, perhaps video4Linux or a scanner driver; or
       test and measurement hardware.

    - 输入子系统（HID gadgets）
    -  input subsystem (for HID gadgets)

    - 音频子系统（audio gadgets）
    -  sound subsystem (for audio gadgets)

    - 文件子系统
    -  file system (for PTP gadgets)

    -  block i/o subsystem (for usb-storage gadgets)

    -  ... and more

额外的层
*Additional Layers*
也可能存在其他的的层。可以包括内核层，比如网络协议栈，基于标准 POSIX 系统调用的
用户态接口应用 open(), close(), read() 以及 write()。在较新的系统上，POSIX 异步 I/O
调用也是一个侯选项。这些用户态的代码可以不是 GPL 的。
    Other layers may exist. These could include kernel layers, such as
    network protocol stacks, as well as user mode applications building
    on standard POSIX system call APIs such as ``open()``, ``close()``,
    ``read()`` and ``write()``. On newer systems, POSIX Async I/O calls may
    be an option. Such user mode code will not necessarily be subject to
    the GNU General Public License (GPL).

具有 OTG 能力的系统也将需要包含一个标准的 Linux-USB host 端的栈，usbcore,一个或者多个
Host Controller Drives(HCDs), USB Device Drives 来支持 OTG Controller Driver.
这里也会有一个 OTG Controller Driver,这个驱动对 gadget 和设备驱动开发人员是间接可见的。

OTG-capable systems will also need to include a standard Linux-USB host
side stack, with ``usbcore``, one or more *Host Controller Drivers*
(HCDs), *USB Device Drivers* to support the OTG "Targeted Peripheral
List", and so forth. There will also be an *OTG Controller Driver*,
which is visible to gadget and device driver developers only indirectly.
这有助于主机和设备端 USB 控制器实现这两个新的 OTG 协议（HNP 和 SRP）,在USB suspend 过程中，使用
HNP 实现角色切换(主机到外设，或者反向)，SRP 可以被视为电池友好的设备唤醒协议。
That helps the host and device side USB controllers implement the two
new OTG protocols (HNP and SRP). Roles switch (host to peripheral, or
vice versa) using HNP during USB suspend processing, and SRP can be
viewed as a more battery-friendly kind of device wakeup protocol.

随着时间的推移,开发了很多有助于简化一些 gadget 驱动任务的可复用的工具。举例来说，根据厂家
向量描述符构建配置接口和端点描述符现在是自动完成的，并且许多驱动现在使用自动配置来选择硬件
端点并初始化它们的描述符。实际有意义的一个潜在示例是编码实现一个标准 USB-IF 协议的 HID，网络，存储或者音频类。
一些开发者对 kdb 或者 kgdb hooks 有兴趣，可以远程对目标硬件进行远程调试。很多这样的 USB 协议代码都不需要专门的
硬件，就像 X11, HTTP或者 NFS 这些网络协议那样。这些 gadget 端的接口驱动也应该被合并实现混合/复合设备。
Over time, reusable utilities are evolving to help make some gadget
driver tasks simpler. For example, building configuration descriptors
from vectors of descriptors for the configurations interfaces and
endpoints is now automated, and many drivers now use autoconfiguration
to choose hardware endpoints and initialize their descriptors. A
potential example of particular interest is code implementing standard
USB-IF protocols for HID, networking, storage, or audio classes. Some
developers are interested in KDB or KGDB hooks, to let target hardware
be remotely debugged. Most such USB protocol code doesn't need to be
hardware-specific, any more than network protocols like X11, HTTP, or
NFS are. Such gadget-side interface drivers should eventually be
combined, to implement composite devices.

内核模式的 Gadget API
Kernel Mode Gadget API
======================

gadget 驱动声明它们自己为结构体 struct usb_gadget_driver，这个结构体负责枚举一个 struct usb_gadget 结构体中的大部分工作。
Gadget drivers declare themselves through a struct
:c:type:`usb_gadget_driver`, which is responsible for most parts of enumeration
for a struct usb_gadget. 
针对 set_configuration 的响应通常会涉及使能一个或者更多的这个 gadget 导出的 struct usb_ep 对象，
并且提交1个或者更多的 struct usb_request 缓冲来传输数据。理解了这4个数据类型以及它们的操作，你
将理解这些 API 是如何工作的。
The response to a set_configuration usually
involves enabling one or more of the struct usb_ep objects exposed by
the gadget, and submitting one or more struct usb_request buffers to
transfer data. Understand those four data types, and their operations,
and you will understand how this API works.

.. Note::

    除了第9章的数据类型，大部分重要的数据类型和函数都会在这里进行描述。
    Other than the "Chapter 9" data types, most of the significant data
    types and functions are described here.

    然而，在你阅读过程中，一些相关的信息很大程度上可能被忽略。一个例子就是端点是自动配置的。
    你将必须阅读这个头文件，并使用例程源码，来更全面地理解这些API。
    However, some relevant information is likely omitted from what you
    are reading. One example of such information is endpoint
    autoconfiguration. You'll have to read the header file, and use
    example source code (such as that for "Gadget Zero"), to fully
    understand the API.

    The part of the API implementing some basic driver capabilities is
    specific to the version of the Linux kernel that's in use. The 2.6
    and upper kernel versions include a *driver model* framework that has
    no analogue on earlier kernels; so those parts of the gadget API are
    not fully portable. (They are implemented on 2.4 kernels, but in a
    different way.) The driver model state is another part of this API that is
    ignored by the kerneldoc tools.

核心的 API 不会导出每一个硬件的特征，只会导出来对大部分都有效的那部分。
这里有重要的硬件特征，比如 DMA 这类将会使用硬件专有的 API。
The core API does not expose every possible hardware feature, only the
most widely available ones. There are significant hardware features,
such as device-to-device DMA (without temporary storage in a memory
buffer) that would be added using hardware-specific APIs.

这个 API 运行驱动使用条件编译来处理不同硬件的端点能力，但是不需要这样做。
硬件可能有任意的限制，涉及到传输类型（事务类型），寻址，包大小，缓存以及其他可能。
通常情况下，这些区别仅仅影响端点0处理设备配置和管理的逻辑。API 支持有限的运行时检测能力，
通过端点的命名约定。许多驱动将至少部分可以自动完成自我配置。实际上，驱动初始化段通常具有自动
配置逻辑，通过扫描硬件的端点列表来查找匹配的驱动的需要（依赖约定）,消除一些条件编译中常见的原因.
This API allows drivers to use conditional compilation to handle
endpoint capabilities of different hardware, but doesn't require that.
Hardware tends to have arbitrary restrictions, relating to transfer
types, addressing, packet sizes, buffering, and availability. As a rule,
such differences only matter for "endpoint zero" logic that handles
device configuration and management. The API supports limited run-time
detection of capabilities, through naming conventions for endpoints.
Many drivers will be able to at least partially autoconfigure
themselves. In particular, driver init sections will often have endpoint
autoconfiguration logic that scans the hardware's list of endpoints to
find ones matching the driver requirements (relying on those
conventions), to eliminate some of the most common reasons for
conditional compilation.

和 Linux-USB 主机端的 API 那样，这个 API 暴露了 USB 消息 “矮胖” 的天性:
I/O 请求是一个或者多个包的术语，并且包边界对驱动来说是可见的。
Like the Linux-USB host side API, this API exposes the "chunky" nature
of USB messages: I/O requests are in terms of one or more "packets", and
packet boundaries are visible to drivers. 

和 RS-232 串口协议相比，USB 使用了同步协议像 HDLC（每一个帧N字节，多点寻址，host作为主站,devices作为从站）而不是异步协议（tty 风格：每一个帧8字节的数据位，没有奇偶校验，一个停止位），所以控制器驱动示例中不会缓存两个单字节到一个双字的USB IN包中，即使，gadget 驱动可能这样做了，当他们实现包边界不重要的协议时。
Compared to RS-232 serial
protocols, USB resembles synchronous protocols like HDLC (N bytes per
frame, multipoint addressing, host as the primary station and devices as
secondary stations) more than asynchronous ones (tty style: 8 data bits
per frame, no parity, one stop bit). So for example the controller
drivers won't buffer two single byte writes into a single two-byte USB
IN packet, although gadget drivers may do so when they implement
protocols where packet boundaries (and "short packets") are not
significant.

驱动生命周期
Driver Life Cycle
-----------------

gadget 驱动不需要知道很多硬件的细节就可以制作端点 I/O 请求给硬件,但是驱动建立/配置
代码需要处理差异。使用类似如下的 API。
Gadget drivers make endpoint I/O requests to hardware without needing to
know many details of the hardware, but driver setup/configuration code
needs to handle some differences. Use the API like this:

1. 注册一个实际的设备端的 usb 控制器驱动，比如 Linux PDAs 端的 sa11x0 或者 pxa25x 等等。
   这一点，设备逻辑上在 USB ch9 初始化状态“attached”，无电源且不可用（因为还没有完成枚举）。
   任何主机不应该看到这个设备，因为它还没有激活依靠上拉的数据线探测到设备接入，尽管
   VBUS 电源是有效的。这个一般是芯片厂家提供的。
1. Register a driver for the particular device side usb controller
   hardware, such as the net2280 on PCI (USB 2.0), sa11x0 or pxa25x as
   found in Linux PDAs, and so on. At this point the device is logically
   in the USB ch9 initial state (``attached``), drawing no power and not
   usable (since it does not yet support enumeration). Any host should
   not see the device, since it's not activated the data line pullup
   used by the host to detect a device, even if VBUS power is available.

2. 注册一个 gadget driver 实现更高层的设备功能。然后绑定到一个 usb_gadget ，这个
   结构在检测到 VBUS 之后，有时候会激活数据线上拉。
2. Register a gadget driver that implements some higher level device
   function. That will then bind() to a :c:type:`usb_gadget`, which activates
   the data line pullup sometime after detecting VBUS.

3. 硬件驱动现在可以开始枚举.处理的步骤是接收 USB power 和设置地址请求。其他步骤
   是被 gadget driver 驱动完成的。如果对应的 gadget 驱动在主机枚举之前没有加载，第
   7步骤之前的步骤就会被忽略。
3. The hardware driver can now start enumerating. The steps it handles
   are to accept USB ``power`` and ``set_address`` requests. Other steps are
   handled by the gadget driver. If the gadget driver module is unloaded
   before the host starts to enumerate, steps before step 7 are skipped.

4. gadget 驱动的 setup() 函数会根据 usb 接口硬件提供的内容以及将要实现的功能返回 usb 描述符。
   这个过程可能涉及备用设置或者配置，除非硬件阻止这些操作。针对 OTG 设备，每一个配置描述符
   都包含一个 OTG 描述符。
4. The gadget driver's ``setup()`` call returns usb descriptors, based both
   on what the bus interface hardware provides and on the functionality
   being implemented. That can involve alternate settings or
   configurations, unless the hardware prevents such operation. For OTG
   devices, each configuration descriptor includes an OTG descriptor.

5. 当 USB 主机触发一个 set_configuration 调用后，gadget 驱动处理枚举的后面步骤.
   它会使能这个配置中所有使用的端点，以及默认设置中的所有接口。它涉及使用一个硬件端点链表，
   根据描述符使能每一个对应的端点。
   它也可能调用 usb_gadget_vbus_draw 函数来期望从 VBUS 获取配置中允许的更多电量。
   针对 OTG 设备，设置一个配置可能也会涉及到通过一个用户接口上报 HNP 能力。
5. The gadget driver handles the last step of enumeration, when the USB
   host issues a ``set_configuration`` call. It enables all endpoints used
   in that configuration, with all interfaces in their default settings.
   That involves using a list of the hardware's endpoints, enabling each
   endpoint according to its descriptor. It may also involve using
   ``usb_gadget_vbus_draw`` to let more power be drawn from VBUS, as
   allowed by that configuration. For OTG devices, setting a
   configuration may also involve reporting HNP capabilities through a
   user interface.

6. 执行实际的工作以及完成数据传输，可能涉及修改设置接口或者切换到一个新的配置，
   直到主机端断开这个链接。缓存任意数量的传输请求到每一个端点。在被断开之前，可能会抑制或者恢复
   很多次。一旦断开，驱动会返回到上述步骤3.
6. Do real work and perform data transfers, possibly involving changes
   to interface settings or switching to new configurations, until the
   device is disconnect()ed from the host. Queue any number of transfer
   requests to each endpoint. It may be suspended and resumed several
   times before being disconnected. On disconnect, the drivers go back
   to step 3 (above).

7. 当 gadget 驱动模块被卸载掉，会触发 unbind() 回调函数。这个会导致控制器驱动被卸载。
7. When the gadget driver module is being unloaded, the driver unbind()
   callback is issued. That lets the controller driver be unloaded.

驱动可以是动态加载或者是静态编译到内核的，可以让外设设备被枚举，但是一些驱动会
拒绝枚举直到一些更高级的组件使能它。注意在最低的层次，这里没有关于ep0配置逻辑如何实现的策略，
除了它应该遵守 USB 规范之外。这些问题涉及 gadget 驱动域中，包括了解一些USB控制器的实施约束或
者理解集成可服用组件到符合设备可能发生的情况。
Drivers will normally be arranged so that just loading the gadget driver
module (or statically linking it into a Linux kernel) allows the
peripheral device to be enumerated, but some drivers will defer
enumeration until some higher level component (like a user mode daemon)
enables it. Note that at this lowest level there are no policies about
how ep0 configuration logic is implemented, except that it should obey
USB specifications. Such issues are in the domain of gadget drivers,
including knowing about implementation constraints imposed by some USB
controllers or understanding that composite devices might happen to be
built by integrating reusable components.

注意上述生命周期可能和 OTG 设备有很大的不同。除了在每一个配置中提供额外的 OTG 描述符，
只有 HNP 相关的差异对驱动代码实际是可见的。他们在 set_configuration 请求阶段涉及上报需求，
以及在一些抑制回调过程涉及 HNP 的部分选项。同时，SRP 轻微地改变了 ``usb_gadget_wakeup`` 的语意。
Note that the lifecycle above can be slightly different for OTG devices.
Other than providing an additional OTG descriptor in each configuration,
only the HNP-related differences are particularly visible to driver
code. They involve reporting requirements during the ``SET_CONFIGURATION``
request, and the option to invoke HNP during some suspend callbacks.
Also, SRP changes the semantics of ``usb_gadget_wakeup`` slightly.

USB 2.0 第9章类型和常量
USB 2.0 Chapter 9 Types and Constants
-------------------------------------

gadget 驱动以来通用的 USB 结构体和常量，头文件定义在 linux/usb/ch9.h
这个头文件是 Linux 2.6+ 内核的标准。这部分内核和 host 端的一样。
Gadget drivers rely on common USB structures and constants defined in
the :ref:`linux/usb/ch9.h <usb_chapter9>` header file, which is standard in
Linux 2.6+ kernels. These are the same types and constants used by host side
drivers (and usbcore).

核心对象和方法
Core Objects and Methods
------------------------

这些核心对象和方法定义在 linux/usb/gadget.h ， gadget 驱动通过这些核心对象和方法和 USB 外设控制器驱动交互
These are declared in ``<linux/usb/gadget.h>``, and are used by gadget
drivers to interact with USB peripheral controller drivers.

.. kernel-doc:: include/linux/usb/gadget.h
   :internal:

可选的工具
Optional Utilities
------------------

核心 API 足够写一个 USB gadget 驱动，但是一些可选的工具可以简化通用的任务。这些
工具包括端点的自动配置。
The core API is sufficient for writing a USB Gadget Driver, but some
optional utilities are provided to simplify common tasks. These
utilities include endpoint autoconfiguration.

.. kernel-doc:: drivers/usb/gadget/usbstring.c
   :export:

.. kernel-doc:: drivers/usb/gadget/config.c
   :export:

复合设备（给定配置包含不止一个功能，即不止一个接口,这种设备就是复合设备，redmagic 就是一个复合设备）框架
Composite Device Framework
--------------------------

核心 API 足够写一个复合USB设备（在给定的配置包含有不止一个功能）的驱动，以及
多配置设备（包含不止一个配置,但不一定共享给定的配置）。这里也有一个可选的框架
可以简化复用以及合并功能。
The core API is sufficient for writing drivers for composite USB devices
(with more than one function in a given configuration), and also
multi-configuration devices (also more than one function, but not
necessarily sharing a given configuration). There is however an optional
framework which makes it easier to reuse and combine functions.

使用这个框架的设备需要提供一个 strcuct usb_composite_driver 实例，在这里面提供
一个或者多个 usb_configuration 实例。每一个配置包含至少一个 struct usb_function 结构体，
这个结构体打包一个用户可见的角色比如"网络链"或者“大容量存储器”。管理功能也可能存在，比如
设备固件升级。
Devices using this framework provide a struct usb_composite_driver,
which in turn provides one or more struct usb_configuration
instances. Each such configuration includes at least one struct
:c:type:`usb_function`, which packages a user visible role such as "network
link" or "mass storage device". Management functions may also exist,
such as "Device Firmware Upgrade".

.. kernel-doc:: include/linux/usb/composite.h
   :internal:

.. kernel-doc:: drivers/usb/gadget/composite.c
   :export:

复合设备功能
Composite Device Functions
--------------------------

在写这篇文章时，当前很少一部分 gadget 驱动已经转换到这个框架。最近的计划是将除 gadgetfs 之外所有
的驱动都转换到这个框架中。
At this writing, a few of the current gadget drivers have been converted
to this framework. Near-term plans include converting all of them,
except for ``gadgetfs``.

外设控制器驱动
Peripheral Controller Drivers
=============================
最早支持这个 API 的硬件是 NetChip 2280 控制器，基于 PCI 支持 USB 2.0 告诉传输。
The first hardware supporting this API was the NetChip 2280 controller,
which supports USB 2.0 high speed and is based on PCI. 
对应的是 net2280 驱动模块。
This is the
``net2280`` driver module. The driver supports Linux kernel versions 2.4
and 2.6; contact NetChip Technologies for development boards and product
information.

其他工作在 gedget 框架的硬件包括：xxxx 等等。他们中的大部分是全速控制器。
Other hardware working in the ``gadget`` framework includes: Intel's PXA
25x and IXP42x series processors (``pxa2xx_udc``), Toshiba TC86c001
"Goku-S" (``goku_udc``), Renesas SH7705/7727 (``sh_udc``), MediaQ 11xx
(``mq11xx_udc``), Hynix HMS30C7202 (``h7202_udc``), National 9303/4
(``n9604_udc``), Texas Instruments OMAP (``omap_udc``), Sharp LH7A40x
(``lh7a40x_udc``), and more. Most of those are full speed controllers.

在写这篇文章的时候，这里有一些人正在修改一些其他的 USB 设备控制器驱动到这个框架，
计划使他们中的大部分都变得可用。
At this writing, there are people at work on drivers in this framework
for several other USB device controllers, with plans to make many of
them be widely available.

一个部分 USB 仿真器， dummy_hcd 驱动。它可以表现的像 net2280, pxa25x 或者 sa11x0 那样的端点和设备速度，并且它可以模拟控制，批量以及一些扩展的中断传输。
这允许你在一台PC上进行一个 gadget 驱动的部分工作，不需要任何专门的硬件，并且可以和用户态的调试工具
gdb 配合使用。
A partial USB simulator, the ``dummy_hcd`` driver, is available. It can
act like a net2280, a pxa25x, or an sa11x0 in terms of available
endpoints and device speeds; and it simulates control, bulk, and to some
extent interrupt transfers. That lets you develop some parts of a gadget
driver on a normal PC, without any special hardware, and perhaps with
the assistance of tools such as GDB running with User Mode Linux. 

至少有一个人表现出了改写这种方法的兴趣，将它链接到一个微控制器的模拟器。这类模拟器
有助于调试子系统，针对那些运行时硬件对软件开发不友好或者还没有准备好的场景。
At least one person has expressed interest in adapting that approach,
hooking it up to a simulator for a microcontroller. Such simulators can
help debug subsystems where the runtime hardware is unfriendly to
software development, or is not yet available.

随着时间的推移，伴随这 USB 框架的演变也期望支持针对其他控制器的开发和贡献。
Support for other controllers is expected to be developed and
contributed over time, as this driver framework evolves.

gadget 驱动
Gadget Drivers
==============

除了 gadget zero （主要被用来测试和开发usb控制器硬件驱动），还有一些其他
的 gadget 驱动存在。
In addition to *Gadget Zero* (used primarily for testing and development
with drivers for usb controller hardware), other gadget drivers exist.

这里有一个网卡驱动，使用一种最流行的 CDC(communication device class)模型。
There's an ``ethernet`` gadget driver, which implements one of the most
useful *Communications Device Class* (CDC) models. 
电缆调制解调器互操作性的标准之一甚至专门指定这种使用以太网模型作为两个强制性选项之一。
One of the standards for cable modem interoperability even specifies the use of this ethernet
model as one of two mandatory options. 
gadget 使用这部分代码作为一个以太网调制器去链接到 USB 主机。它提供了以 gadget 的 cpu 作为主机访问网络的能力,可以方便地桥接，路由或者防火墙访问其他网络。
因为一些硬件不能完全满足 CDC 以太网的需要，这个驱动也实现了一个部分功能好用的 CDC 以太网。（这个
字迹不会宣传自己是 CDC 以太网，来避免创建时候出问题）
Gadgets using this code look to a USB host as if they're an Ethernet adapter. It provides access to a
network where the gadget's CPU is one host, which could easily be
bridging, routing, or firewalling access to other networks. Since some
hardware can't fully implement the CDC Ethernet requirements, this
driver also implements a "good parts only" subset of CDC Ethernet. (That
subset doesn't advertise itself as CDC Ethernet, to avoid creating
problems.)

支持微软的 RNDIS 协议, 和 windows 有关，暂时跳过。
Support for Microsoft's ``RNDIS`` protocol has been contributed by
Pengutronix and Auerswald GmbH. This is like CDC Ethernet, but it runs
on more slightly USB hardware (but less than the CDC subset). However,
its main claim to fame is being able to connect directly to recent
versions of Windows, using drivers that Microsoft bundles and supports,
making it much simpler to network with Windows.

这里也支持用户模式的 gadget 驱动，使用 gadgetfs.它将每一个端点作为句柄来提供了用户态的API接口。
使用常规的 read 和 write 系统调用。类似 gdb 和 pthread 可以被用到开发和调试用户态驱动
当中去，所以一旦有了一个鲁棒的控制器驱动，许多依赖它的应用程序将不需要心的内核态软件。
Linux 2.6 版本的异步IO(AIO)也是支持的，所以用户态软件可以仅仅使用比内核驱动稍微多一点的开销。
There is also support for user mode gadget drivers, using ``gadgetfs``.
This provides a *User Mode API* that presents each endpoint as a single
file descriptor. I/O is done using normal ``read()`` and ``read()`` calls.
Familiar tools like GDB and pthreads can be used to develop and debug
user mode drivers, so that once a robust controller driver is available
many applications for it won't require new kernel mode software. Linux
2.6 *Async I/O (AIO)* support is available, so that user mode software
can stream data with only slightly more overhead than a kernel driver.

这里还有一个 USB 大容量存储类驱动，提供了微软和苹果系统内部交互的新的解决方法。
There's a USB Mass Storage class driver, which provides a different
solution for interoperability with systems such as MS-Windows and MacOS.
That *Mass Storage* driver uses a file or block device as backing store
for a drive, like the ``loop`` driver. The USB host uses the BBB, CB, or
CBI versions of the mass storage class specification, using transparent
SCSI commands to access the data from the backing store.

这里还有一个串行驱动，通过 USB 实现 tty 风格的操作方法。最新的驱动版本支持 CDC ACM 风格的操作，
就像是一个 USB 调制器，所以在很多硬件上可以很容易和微软系统进行交互。一个有意思的用法是它可以用在
BIOS阶段，用到没有串口线的很小型的系统中。
There's a "serial line" driver, useful for TTY style operation over USB.
The latest version of that driver supports CDC ACM style operation, like
a USB modem, and so on most hardware it can interoperate easily with
MS-Windows. One interesting use of that driver is in boot firmware (like
a BIOS), which can sometimes use that model with very small systems
without real serial lines.

随着这个驱动框架的发展，欢迎开发支持其他种类的 gadget 驱动。
Support for other kinds of gadget is expected to be developed and
contributed over time, as this driver framework evolves.

USB OTG
USB On-The-GO (OTG)
===================

USB OTG 最开始是 TI 在 linux 2.6 版本中引入的，针对 16xx 和 17xx 系列的处理器。
其他 OTG 系统也应该使用类似的方式工作，但是硬件层差异可能较大。
USB OTG support on Linux 2.6 was initially developed by Texas
Instruments for `OMAP <http://www.omap.com>`__ 16xx and 17xx series
processors. Other OTG systems should work in similar ways, but the
hardware level details could be very different.

系统需要专门的硬件支持 OTG，尤其是一个专门的 Mini-AB 接口以及一个收发器来
支持双角色操作：集合一作为一个 host，使用标准的 USB host 驱动，也可以作为一个
外设，使用这个 gadget 驱动框架。
为此，系统软件依赖对这些接口的少量添加，以及影响哪个驱动链接到 OTG 端口的栈（这部分工作由 OTG 控制器实现）。在每一个角色，系统可以服用已经存在的硬件无关的驱动，在控制器驱动接口的顶层(usb_bus 和 usb_gadget)。这类驱动最多需要进行一些小的更改，并且为支持 OTG 添加的接口很多也可以使非 OTG 产品收益。
Systems need specialized hardware support to implement OTG, notably
including a special *Mini-AB* jack and associated transceiver to support
*Dual-Role* operation: they can act either as a host, using the standard
Linux-USB host side driver stack, or as a peripheral, using this
``gadget`` framework. To do that, the system software relies on small
additions to those programming interfaces, and on a new internal
component (here called an "OTG Controller") affecting which driver stack
connects to the OTG port. In each role, the system can re-use the
existing pool of hardware-neutral drivers, layered on top of the
controller driver interfaces (:c:type:`usb_bus` or :c:type:`usb_gadget`).
Such drivers need at most minor changes, and most of the calls added to
support OTG can also benefit non-OTG products.

- gadget 驱动检测 is_otg 标志，并且据此决定是否需要在他们的配置中包含一个 OTG 描述符。
-  Gadget drivers test the ``is_otg`` flag, and use it to determine
   whether or not to include an OTG descriptor in each of their
   configurations.

- gadget 驱动可能需要一些修改来支持两个新的 otg 协议，暴露到新的 gadget 属性中如 b_hnp_enable 标志.
  HNP 支持应该通过用户接口报告给用户（两个 LED 灯足够了），在一些场景中当主机抑制这个外设的时候触发。SRP 可以被用户初始化，比如按下相同的按键。
-  Gadget drivers may need changes to support the two new OTG protocols,
   exposed in new gadget attributes such as ``b_hnp_enable`` flag. HNP
   support should be reported through a user interface (two LEDs could
   suffice), and is triggered in some cases when the host suspends the
   peripheral. SRP support can be user-initiated just like remote
   wakeup, probably by pressing the same button.

-  On the host side, USB device drivers need to be taught to trigger HNP
   at appropriate moments, using ``usb_suspend_device()``. That also
   conserves battery power, which is useful even for non-OTG
   configurations.

 - 同时在主机端，驱动必须支持 OTG（目标外设列表）。这是一个白名单，被用来拒绝给定的
   Linux OTG 主机不支持的外设模式。 这个白名单是产品转悠的，每一个产品必须修改 otg_whitelist.h文件
   来匹配它的交互属性。
-  Also on the host side, a driver must support the OTG "Targeted
   Peripheral List". That's just a whitelist, used to reject peripherals
   not supported with a given Linux OTG host. *This whitelist is
   product-specific; each product must modify* ``otg_whitelist.h`` *to
   match its interoperability specification.*

   Non-OTG Linux hosts, like PCs and workstations, normally have some
   solution for adding drivers, so that peripherals that aren't
   recognized can eventually be supported. That approach is unreasonable
   for consumer products that may never have their firmware upgraded,
   and where it's usually unrealistic to expect traditional
   PC/workstation/server kinds of support model to work. For example,
   it's often impractical to change device firmware once the product has
   been distributed, so driver bugs can't normally be fixed if they're
   found after shipment.

在这些硬件中性的驱动接口 usb_bus 和 usb_gadget 之下需要一些额外的修改;这些内容就不再这里深入讨论。
这些影响硬件相关的代码以及主机控制器驱动初始化（因为OTG只能在一个端口上激活）。
他们也会涉及被成为 OTG控制器驱动的部分，管理OTG手法和OTG状态机逻辑以及针对OTG端口根集线器表现相关的内容。
Additional changes are needed below those hardware-neutral :c:type:`usb_bus`
and :c:type:`usb_gadget` driver interfaces; those aren't discussed here in any
detail. Those affect the hardware-specific code for each USB Host or
Peripheral controller, and how the HCD initializes (since OTG can be
active only on a single port). They also involve what may be called an
*OTG Controller Driver*, managing the OTG transceiver and the OTG state
machine logic as well as much of the root hub behavior for the OTG port.
OTG控制器驱动需要根据相关的设备角色激活以及取消激活USB控制器。一些还涉及到usbcore内部的修改，可以让它识别 OTG 线缆设备以及对 HNP 或者 SRP 协议进行合适的回应。
The OTG controller driver needs to activate and deactivate USB
controllers depending on the relevant device role. Some related changes
were needed inside usbcore, so that it can identify OTG-capable devices
and respond appropriately to HNP or SRP protocols.
