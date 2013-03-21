#!/bin/bash
#
#%stage: device
#%depends: network
#

if [ "$interface" ]  ; then
    devpath=$(cd -P "/sys/class/net/$interface/device"; echo $PWD)
    ccwid=${devpath##*/}
    devtype=${devpath%/*}
    devtype=${devtype##*/}
    if [ -f /etc/udev/rules.d/51-${devtype}-${ccwid}.rules ] ; then
	cp /etc/udev/rules.d/51-${devtype}-${ccwid}.rules $tmp_mnt/etc/udev/rules.d
	load_qeth=1
    fi
fi

	
