#!/bin/sh

function do_copy
{
	echo "copy Image and dtb files to ../build"
	fd --glob Image -IH -x cp -frv {} ../build
	# 屏蔽掉 rzg2ul 芯片，这个芯片是单核 A55 的
	fd -e dtb --exclude *rzg2ul* . arch/arm64/boot/dts/myir -IH -X cp -frv {} ../build
}

if [ $# -eq 0 ];then
# 编译内核文件, 生成 Image 和 dtb 文件
make ARCH=arm64 Image dtbs -j16
else
case $1
	in
	c) echo "do menuconfig";make ARCH=arm64 menuconfig;;
	d) echo "dtb->dts"; dtc -I dtb -O dts arch/arm64/boot/dts/myir/myb-rzg2l-disp.dtb > myir_rzg2.dts;;
	e) echo "init enviroment"; export PATH=/usr/bin:$PATH; unset LD_LIBRARY_PATH; source ../../03_Tools/Toolchains/sdk/core_red/environment-setup-aarch64-poky-linux;;
	g)
	# gen patch
	echo "gen patch";
	git diff > ~/Codes_of_pro/blackmagic4RZ/misc/linux_kernel_Documents.diff;
	cp cscope.files ~/Codes_of_pro/blackmagic4RZ/misc/kernel_cscope.files;;
	m)
	# merge patch
	echo "merge patch";
	patch -i ~/Codes_of_pro/blackmagic4RZ/misc/linux_kernel_Documents.diff -p 1;;
	u) do_copy;;
	*) echo "No support this param $1 [g: gen patch] [m: merge patch]";;
esac
fi
