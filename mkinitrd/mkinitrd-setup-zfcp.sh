#!/bin/bash
#
#%stage: device
#
if [ "$(echo $block_modules | grep zfcp)" ]; then
	root_zfcp=1
fi

save_var root_zfcp

if [ "$root_zfcp" ]; then
    for dev in $blockdev; do
    	update_blockdev $dev
	if [ "$blockdriver" = "sd" ]; then
	    sysdev=$(majorminor2blockdev $blockmajor $blockminor)
	    dir=/sys/block/${sysdev##/dev/}
	    # dir should contain the correct directory now
	    if [ ! -d "$dir" ] || [ ! -d ${dir}/device ] ; then
	    	error 1 "zfcp device $dev not found in sysfs!"
	    else
		rule=
		dir=$(cd -P $dir/device; echo $PWD)
		scsinum=${dir##*/}
		# Configure the controller
		host=${scsinum%%:*}
		ccwdir=$(cd -P /sys/class/scsi_host/host$host/device; cd ..; echo $PWD)
		ccw=${ccwdir##*/}
		rule=$tmp_mnt/etc/udev/rules.d/51-zfcp-${ccw}.rules
		if [ ! -f "$rule" ] ; then
		    cat > $rule <<EOF
ACTION=="add", SUBSYSTEM=="ccw", KERNEL=="$ccw", IMPORT{program}="collect $ccw %k $ccw zfcp"
ACTION=="add", SUBSYSTEM=="drivers", KERNEL=="zfcp", IMPORT{program}="collect $ccw %k $ccw zfcp"
ACTION=="add", ENV{COLLECT_$ccw}=="0", ATTR{[ccw/$ccw]online}="1"
EOF
		fi
		# Configure the FC target
		tgtnum=${scsinum%:*}
		tgtdir=$(cd -P /sys/class/fc_transport/target$tgtnum; echo $PWD)
		read wwpn < $tgtdir/port_name
		read lun < $dir/fcp_lun
		cat >> $rule <<EOF
ACTION=="add", KERNEL=="rport-*", ATTR{port_name}=="$wwpn", SUBSYSTEMS=="ccw", KERNELS=="$ccw", ATTR{[ccw/$ccw]$wwpn/unit_add}="$lun"
EOF
	        verbose "[ZFCP] $sysdev -> ${ccw}:${wwpn}:${lun}"
	    fi
	fi
    done
fi
