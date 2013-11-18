#!/bin/sh
#
#  Shell script to get the required tarballs for zfcpdump from the internet
# 
#  > get_files   : get files from internet
#  > get_files -c: check if all files are already there
#
#  Copyright IBM Corp. 2003, 2006.
#  Author(s): Michael Holzheu <holzheu@de.ibm.com>
#

. ./config

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
# get_files(): get files from the internet
#
function get_files()
{
	if [ "${WGET_PROXY}" != "" ]
	then
		export http_proxy=$WGET_PROXY
	fi

	if [ ! -f "extern/$E2FS_PROGS_TARBALL" ]
	then
		wget -N "$E2FS_PROGS_HOST/$E2FS_PROGS_TARBALL"
		check
		mv $E2FS_PROGS_TARBALL extern
	fi

	if [ ! -f "extern/$KERNEL_TARBALL" ]
	then
		wget -N "$KERNEL_HOST/$KERNEL_TARBALL"
		check
		mv $KERNEL_TARBALL extern
	fi
}

#
# check_files(): check if  files are in the extern directory
#
function check_files()
{
	missing="no"

	if [ ! -f "extern/$KERNEL_TARBALL" ]
	then
		if [ "$missing" == "no" ]
		then
			echo "********************************************************************************"
			missing="y"
		fi
		echo "* ERROR: 'extern/$KERNEL_TARBALL' missing"
	fi

	if [ ! -f "extern/$E2FS_PROGS_TARBALL" ]
	then
		if [ "$missing" == "no" ]
		then
			echo "********************************************************************************"
			missing="y"
		fi
		echo "* ERROR: 'extern/$E2FS_PROGS_TARBALL' missing"
	fi

	if [ "$missing" == "y" ]
	then
		echo "* Call 'get_files.sh' to get the required tarballs from the internet!"
		echo "********************************************************************************"
		exit 1
	else
		exit 0
	fi
}

if [ "$1" == "-c" ]
then
	check_files
else
	get_files
fi
