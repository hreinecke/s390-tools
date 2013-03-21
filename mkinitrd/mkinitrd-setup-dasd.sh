#!/bin/bash
#
#%stage: device
#
if [ "$(echo $blockdev | grep dasd)" ]; then
	root_dasd=1
fi

save_var root_dasd

if use_script dasd; then
    for dasd in $blockdev; do
    	update_blockdev $dasd
	if [ "$blockdriver" = "dasd" ]; then
	    sysdev=$(majorminor2blockdev $blockmajor $blockminor)
	    dir=/sys/block/${sysdev##/dev/}
	    # dir should contain the correct directory now
	    if [ ! -d "$dir" ] || [ ! -d ${dir}/device ] ; then
	    	error 1 "dasd device $dasd not found in sysfs!"
	    else
		dir=$(cd -P $dir/device; echo $PWD)
		ccw=${dir##*/}
		if [ -r "$dir/discipline" ]; then
		    read type < $dir/discipline
		    
		    case $type in
			ECKD)
			    drv="dasd-eckd"
			    discipline=0
			    ;;
			FBA)
			    drv="dasd-fba"
			    discipline=1
			    ;;
			DIAG)
			    dasd_modules="dasd_diag_mod"
			    drv="dasd-diag"
			    discipline=2
			    ;;
			*)
			    ;;
		    esac
		fi
		cat > $tmp_mnt/etc/udev/rules.d/51-dasd-${ccw}.rules <<EOF
ACTION=="add", SUBSYSTEM=="ccw", KERNEL=="$ccw", IMPORT{program}="collect $ccw %k ${ccw} $drv"
ACTION=="add", SUBSYSTEM=="drivers", KERNEL=="$drv", IMPORT{program}="collect $ccw %k ${ccw} $drv"
ACTION=="add", ENV{COLLECT_$ccw}=="0", ATTR{[ccw/$ccw]online}="1"
EOF
	        verbose "[DASD] $sysdev -> ${ccw} ($type)"
	    fi
	fi
    done
fi

save_var dasd_modules
