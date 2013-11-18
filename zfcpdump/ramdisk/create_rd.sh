#!/bin/sh
#
#  Shell script to build and install the zfcpdump ramdisk
#
#  > create_rd.sh (ARCH) : build ramdisk
#  > create_rd.sh -r     : cleanup
#  > create_rd.sh -i     : install files
#
#  Copyright IBM Corp. 2003, 2006.
#  Author(s): Michael Holzheu <holzheu@de.ibm.com>
#

. ../config

E2FS_PROGS="../extern/$E2FS_PROGS_TARBALL"
EXT2_RD_FREE_SIZE=200

RD_TMP=rd
RD_IMAGE=ramdisk.dump
DIR_LIST="sbin \
          dev \
          proc \
          etc \
          mnt \
          sys "

#
# check() function to check error codes
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
# build(): build the ramdisk
#

function build ()
{
	echo "build: `date`"

	echo "==============================================================="
	echo "Removing old installation"
	echo "==============================================================="
	rm -rf $RD_TMP
	echo "ok"

	echo "==============================================================="
	echo "Creating RD dir structure"
	echo "==============================================================="
	mkdir $RD_TMP 
	for i in $DIR_LIST
	do
		mkdir "$RD_TMP/$i"
	done
	check

	#
	# Build e2fsprogs
	#

	if [ ! -f e2fsprogs-$E2FS_PROGS_VERSION/e2fsck/e2fsck ]
	then
		echo "==============================================================="
		echo "Extracting E2FSPROGS"
		echo "==============================================================="
		tar xfzv $E2FS_PROGS
		check
		echo "==============================================================="
		echo "Compiling E2FSPROGS"
		echo "==============================================================="
		cd e2fsprogs-$E2FS_PROGS_VERSION
		./configure
		make
		check
		cd ..
	fi
	echo "==============================================================="
	echo "Installing e2fsck"
	echo "==============================================================="
	strip e2fsprogs-$E2FS_PROGS_VERSION/e2fsck/e2fsck
	cp e2fsprogs-$E2FS_PROGS_VERSION/e2fsck/e2fsck $RD_TMP/sbin
	check

	#
	# copy additional files
	#

	strip zfcp_dumper
	cp zfcp_dumper $RD_TMP/sbin/init
	ln -s /proc/mounts "$RD_TMP/etc/mtab"

	#
	# create ramdisk
	#

	echo "==============================================================="
	echo "Creating ramdisk ($RD_FS)"
	echo "==============================================================="

	if [ "$RD_FS" == "romfs" ]
	then
		printf "%-30s: " "Creating romfs"
		genromfs -f $ZFCPDUMP_RD -d $RD_TMP
		check
		printf "%-30s: " "zip romfs"
		gzip $ZFCPDUMP_RD
		check
		mv "$ZFCPDUMP_RD.gz" $ZFCPDUMP_RD
	elif [ "$RD_FS" == "ext2" ]
	then
		RD_MIN_SIZE=`du -k -s $RD_TMP | awk '{print $1}'`
		RD_SIZE=`expr $RD_MIN_SIZE + $EXT2_RD_FREE_SIZE`

		printf "creating rd with size $RD_SIZE KB (used $RD_MIN_SIZE):"
		dd if=/dev/zero of=$RD_IMAGE bs=1k count=$RD_SIZE 
		check

		printf "%-30s: " "Creating ext2"
		yes | mke2fs $RD_IMAGE
		check
		printf "%-30s: " "Mounting RD"
		if [ ! -d mnt ]
		then
			mkdir mnt
		fi
		mount $RD_IMAGE "`pwd`/mnt" -o loop
		check

		printf "%-30s: " "Copy RD"
		cp -R $RD_TMP/* mnt
		if [ $? != 0 ]
		then
			umount mnt
			exit
		fi
		echo "ok"

		printf "%-30s: " "Umount RD"
		umount mnt
		check

		printf "%-30s: " "packing RD"
		gzip $RD_IMAGE
		check
		mv "$RD_IMAGE.gz" $ZFCPDUMP_RD
		check
	else
		echo "ERROR: Invalid ramdisk filesystem '$RD_FS'"
		exit
	fi
	echo "=============================================================="
	echo "SUCCESS: built ramdisk '$ZFCPDUMP_RD'"
	echo "=============================================================="
}

#
# cleanup(): function to remove build files
#
function cleanup()
{
	rm -rf $RD_TMP
	rm -rf e2fs*
	rm -f zfcp_dumper
	rm -f $RD_IMAGE
	rm -f "$RD_IMAGE.gz"
        rm -f $ZFCPDUMP_RD
}
 
#
# install(): function to install zfcpdump kernel
#
function install()
{
        cp $ZFCPDUMP_RD "$INSTROOT/$ZFCPDUMP_DIR"; chmod 644 "$INSTROOT/$ZFCPDUMP_DIR/$ZFCPDUMP_RD"
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
        ARCH="$1"
        build
fi
