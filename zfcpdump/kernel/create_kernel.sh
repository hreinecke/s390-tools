#!/bin/sh
#
#  Shell script to build and install the zfcpdump kernel
#
#  > create_kernel.sh (ARCH) : build kernel
#  > create_kernel.sh -r     : cleanup
#  > create_kernel.sh -i     : install files
#
#  Copyright IBM Corp. 2003, 2006.
#  Author(s): Michael Holzheu <holzheu@de.ibm.com>
#

. ../config

KERNEL="../extern/$KERNEL_TARBALL"
PATCHDIR=patches
PATCHES="linux-2.6.12-zfcpdump.diff \
         linux-2.6.12-gcc-4.1.diff \
         linux-2.6.12-con3215-fix.diff"

#
# check(): function to check error codes
#
function check ()
{
        if [ $? != 0 ]
        then
                echo "failed"
                exit 1
        else
                echo "ok"
        fi
}


#
# build(): function to build zfcpdump kernel
#
function build ()
{
	echo "build: `date`"
	if [ ! -f $KERNEL ] 
	then
		echo "please copy $KERNEL to `pwd`!"
		exit 1
	fi
	printf "%-30s: " "Removing old build"
	rm -rf linux-$KERNEL_VERSION
	check
	echo "============================================================"
	echo "Extracting Kernel"
	echo "============================================================"
	tar xfvj $KERNEL 
	check
	echo "============================================================"
	echo "Patching Kernel $KERNEL_VERSION:"
	echo "============================================================"
	cd "linux-$KERNEL_VERSION"
	for i in $PATCHES
	do
		printf "%-40s: " "  - $i"
		echo
		patch -p1 < ../$PATCHDIR/$i
		check
	done

	echo "============================================================"
	echo "Copying additional files"
	echo "============================================================"
	cp ../dump.c drivers/s390/char
	cp ../dump.h drivers/s390/char
	check
	echo "============================================================"
	echo "Make config"
	echo "============================================================"
	cp "../kernel_config_$T_ARCH" .config
	( yes | make oldconfig TOPDIR=`pwd` ARCH=s390 )
	check
	echo "============================================================"
	echo "Make image"
	echo "============================================================"
	( make image TOPDIR=`pwd` ARCH=s390 )
	check
	mv arch/s390/boot/image ../$ZFCPDUMP_IMAGE
	check
	echo "============================================================"
	echo "SUCCESS: built image '$ZFCPDUMP_IMAGE'"
	echo "============================================================"
}


#
# cleanup(): function to remove build files
#
function cleanup()
{
	rm -rf "linux-$KERNEL_VERSION"
	rm -f $ZFCPDUMP_IMAGE
}

#
# install(): function to install zfcpdump kernel
#
function install()
{
	cp $ZFCPDUMP_IMAGE "$INSTROOT/$ZFCPDUMP_DIR"; chmod 644 "$INSTROOT/$ZFCPDUMP_DIR/$ZFCPDUMP_IMAGE"
}

#
# main
#


if [ "$1" == "-r" ]
then
	cleanup
elif [ "$1" == "-i" ]
then 
	install
else
	T_ARCH="$1"
	build
fi
