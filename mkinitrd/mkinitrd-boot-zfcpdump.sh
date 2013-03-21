#!/bin/bash
#
#%stage: boot
#%programs: e2fsck modprobe /usr/lib/s390-tools/zfcpdump
#%modules: zfcp ext2 ext3 sd_mod
#%if "$use_zfcpdump"
#
##### zfcpdump configuration
##
## this script configures the zfcp standalone dumper
##


