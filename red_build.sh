#!/bin/sh

function do_build
{
    make ARCH=arm64 Image dtbs -j2
}

function do_menuconfig
{
    make ARCH=arm64 menuconfig
}

function do_dtb2dts
{
    dtc -I dtb -O dts arch/arm64/boot/dts/myir/myb-rzg2l-disp.dtb > myb-rzg2l-disp.dts
}

function do_genpatch
{
	git diff > ~/Codes_of_pro/blackmagic4RZ/misc/linux_kernel_Documents.diff;
	cp cscope.files ~/Codes_of_pro/blackmagic4RZ/misc/kernel_cscope.files;
}

function do_mergepatch
{
	patch -i ~/Codes_of_pro/blackmagic4RZ/misc/linux_kernel_Documents.diff -p 1;
}

function do_copy
{
	echo "copy Image and dtb files to ../build"
	fd --glob Image -IH -x cp -frv {} ../build
	# 屏蔽掉 rzg2ul 芯片，这个芯片是单核 A55 的
	fd -e dtb --exclude *rzg2ul* . arch/arm64/boot/dts/myir -IH -X cp -frv {} ../build
}

function do_help
{
    echo "===============help message================="
	echo "b: do build"
	echo "c: do menuconfig"
	echo "d: do dtb2dts"
	#echo "e: do menuconfig"
	echo "g: do genpatch"
	echo "m: do mergepatch"
	echo "u: do copy build targets"
}

if [ $# -eq 0 ];then
# 编译内核文件, 生成 Image 和 dtb 文件
echo "I thought you shouldn't ignore paramter, so do nothing for blank paramter"
else
case $1
	in
	b) do_build;;
	c) do_menuconfig;;
	d) do_dtb2dts;;
	#e) echo "init enviroment"; export PATH=/usr/bin:$PATH; unset LD_LIBRARY_PATH; source ../../03_Tools/Toolchains/sdk/core_red/environment-setup-aarch64-poky-linux;;
	g) do_genpatch;;
	#m) do_mergepatch;;
	u) do_copy;;
	*) echo "No support this param $1";do_help;;
esac
fi
