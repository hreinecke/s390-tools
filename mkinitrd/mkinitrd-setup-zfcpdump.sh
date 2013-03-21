#!/bin/bash
#
#%stage: setup
#
##### zfcpdump configuration
##
## this script configures the zfcp standalone dumper
##

if use_script zfcpdump; then
    mknod -m 0644 $tmp_mnt/dev/console c 5 1
    mknod -m 0644 $tmp_mnt/dev/null c 1 3
    for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 ; do
	mknod -m 0644 $tmp_mnt/dev/sda$n b 8 $n
    done

    mkdir  $tmp_mnt/mnt
    mkdir -p $tmp_mnt/usr/lib/s390-tools
    rm -f $tmp_mnt/init
    ln -s /usr/lib/s390-tools/zfcpdump $tmp_mnt/init

    use_zfcpdump=1
fi

save_var use_zfcpdump

