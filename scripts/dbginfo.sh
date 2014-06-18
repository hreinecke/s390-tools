#!/bin/sh
#
# dbginfo.sh - Tool to collect runtime, configuration, and trace information
#
# Copyright IBM Corp. 2002, 2014
#

# Switching to neutral locale
LC_ALL=C
export LC_ALL

# The kernel release version as delivered from uname -r
readonly KERNEL_RELEASE_VERSION=$(uname -r 2>/dev/null)

########################################
# Global used variables
#

# The general name of this script
readonly SCRIPTNAME="${0##*/}"

# The terminal
readonly TERMINAL=$(tty 2>/dev/null)

# The hostname of the system
readonly SYSTEMHOSTNAME=$(hostname -s 2>/dev/null)

# The processor ID for the first processor
readonly PROCESSORID=$(grep -E ".*processor 0:.*" /proc/cpuinfo | sed 's/.*identification[[:space:]]*\=[[:space:]]*\([[:alnum:]]*\).*/\1/g')

# The processor version for the first processor
readonly PROCESSORVERSION=$(grep -E ".*processor 0:.*" /proc/cpuinfo | sed 's/.*version[[:space:]]*\=[[:space:]]*\([[:alnum:]]*\).*/\1/g')

# The current date
readonly DATETIME=$(date +%Y-%m-%d-%H-%M-%S 2>/dev/null)

# The base working directory
readonly WORKDIR_BASE="/tmp/"

# The current working directory for the actual script execution
if test -z "${PROCESSORID}"; then
    readonly WORKDIR_CURRENT="DBGINFO-${DATETIME}-${SYSTEMHOSTNAME:-localhost}"
else
    readonly WORKDIR_CURRENT="DBGINFO-${DATETIME}-${SYSTEMHOSTNAME:-localhost}-${PROCESSORID}"
fi

# The current path where the collected information is put together
readonly WORKPATH="${WORKDIR_BASE}${WORKDIR_CURRENT}/"

# The current TAR archive that finally includes all collected information
readonly WORKARCHIVE="${WORKDIR_BASE}${WORKDIR_CURRENT}.tgz"

# The log file of activities from this script execution
readonly LOGFILE="${WORKPATH}dbginfo.log"

# File that includes output of Linux commands
readonly OUTPUT_FILE_CMD="${WORKPATH}runtime.out"

# File that includes output of z/VM commands (if running in z/VM)
readonly OUTPUT_FILE_VMCMD="${WORKPATH}zvm_runtime.out"

# File that includes content of files from sysfs
readonly OUTPUT_FILE_SYSFS="${WORKPATH}sysfsfiles.out"

# File that includes content of OSA OAT
readonly OUTPUT_FILE_OSAOAT="${WORKPATH}osa_oat"

# File that includes the output of journalctl
readonly OUTPUT_FILE_JOURNALCTL="${WORKPATH}journalctl.out"

# Mount point of the debug file system
readonly MOUNT_POINT_DEBUGFS="/sys/kernel/debug"

# The amount of steps running the whole collections
readonly COLLECTION_COUNT=7

# The kernel version (e.g. '2' from 2.6.32 or '3' from 3.2.1)
readonly KERNEL_VERSION=$(uname -r 2>/dev/null | cut -d'.' -f1)

# The kernel major revision number (e.g. '6' from 2.6.32 or '2' from 3.2.1)
readonly KERNEL_MAJOR_REVISION=$(uname -r 2>/dev/null | cut -d'.' -f2)

# The kernel mainor revision number (e.g. '32' from 2.6.32 or '1' from 3.2.1)
readonly KERNEL_MINOR_REVISION=$(uname -r 2>/dev/null | cut -d'.' -f3 | sed 's/[^0-9].*//g')

# Is this kernel supporting sysfs - since 2.4 (0=yes, 1=no)
if test "${KERNEL_VERSION}" -lt 2 ||
    ( test  "${KERNEL_VERSION}" -eq 2 && test "${KERNEL_MAJOR_REVISION}" -le 4 ); then
    readonly LINUX_SUPPORT_SYSFS=1
else
    readonly LINUX_SUPPORT_SYSFS=0
fi

# Is this kernel potentially using the /sys/kernel/debug feature - since 2.6.13 (0=yes, 1=no)
if test "${KERNEL_VERSION}" -lt 2 ||
    ( test "${KERNEL_VERSION}" -eq 2 &&
	( test "${KERNEL_MAJOR_REVISION}" -lt 6 ||
	    ( test "${KERNEL_MAJOR_REVISION}" -eq 6 && test "${KERNEL_MINOR_REVISION}" -lt 13 ))); then
    readonly LINUX_SUPPORT_SYSFSDBF=1
else
    readonly LINUX_SUPPORT_SYSFSDBF=0
fi

if test "x${PROCESSORVERSION}" = "xFF" || test "x${PROCESSORVERSION}" = "xff"; then
    readonly RUNTIME_ENVIRONMENT=$(grep -E "VM00.*Control Program.*" /proc/sysinfo| sed 's/.*:[[:space:]]*\([[:graph:]]*\).*/\1/g')
else
    readonly RUNTIME_ENVIRONMENT="LPAR"
fi


########################################

# Collection of proc fs entries
PROCFILES="\
  /proc/buddyinfo\
  /proc/cio_ignore\
  /proc/cmdline\
  /proc/cpuinfo\
  /proc/crypto\
  /proc/dasd/devices\
  /proc/dasd/statistics\
  /proc/devices\
  /proc/diskstats\
  /proc/driver/z90crypt\
  /proc/interrupts\
  /proc/iomem\
  /proc/mdstat\
  /proc/meminfo\
  /proc/misc\
  /proc/modules\
  /proc/mounts\
  /proc/net/vlan\
  /proc/net/bonding\
  /proc/partitions\
  /proc/qeth\
  /proc/qeth_perf\
  /proc/qeth_ipa_takeover\
  /proc/service_levels\
  /proc/slabinfo\
  /proc/stat\
  /proc/swaps\
  /proc/sys/kernel\
  /proc/sys/vm\
  /proc/sysinfo\
  /proc/version\
  /proc/zoneinfo\
  "

# Adding files to PROCFILES in case scsi devices are available
if test -e /proc/scsi; then
    PROCFILES="${PROCFILES}\
      $(find /proc/scsi -type f -perm /444 2>/dev/null)\
      "
fi

# Adding files to PROCFILES in case we run on Kernel 2.4 or older
if test "${LINUX_SUPPORT_SYSFS}" -eq 1; then
    PROCFILES="${PROCFILES}\
      /proc/chpids\
      /proc/chandev\
      /proc/ksyms\
      /proc/lvm/global\
      /proc/subchannels\
      "
fi

# Adding s390dbf files to PROCFILE in case we run on Kernel lower than 2.6.13
if test "${LINUX_SUPPORT_SYSFSDBF}" -eq 1; then
    if test -e /proc/s390dbf; then
	PROCFILES="${PROCFILES}\
          $(find /proc/s390dbf -type f -not -path "*/raw" -not -path "*/flush" 2>/dev/null)\
          "
    fi
fi

########################################

LOGFILES="\
  /var/log/anaconda.*\
  /var/log/audit\
  /var/log/boot*\
  /var/log/cron*\
  /var/log/dmesg*\
  /var/log/dracut.log*\
  /var/log/IBMtape.trace\
  /var/log/IBMtape.errorlog\
  /var/log/lin_tape.trace\
  /var/log/lin_tape.errorlog\
  /var/log/messages*\
  /var/log/sa\
  /var/log/yum.log\
  "

########################################

CONFIGFILES="\
  /etc/*.conf\
  /etc/anacrontab\
  /etc/auto.*\
  /etc/cron.*\
  /etc/crontab\
  /etc/crypttab\
  /etc/depmod.d\
  /etc/dracut.conf.d\
  /etc/exports\
  /etc/fstab\
  /etc/groups\
  /etc/hosts*\
  /etc/iscsi\
  /etc/inittab\
  /etc/logrotate.d\
  /etc/lvm\
  /etc/modprobe.conf*\
  /etc/modprobe.d\
  /etc/mtab\
  /etc/multipath\
  /etc/networks\
  /etc/pam.d\
  /etc/profile\
  /etc/profile.d\
  /etc/pki/tls/openssl.cnf\
  /etc/rc.d\
  /etc/rc.local\
  /etc/resolv.*\
  /etc/rsyslog.d\
  /etc/ssl/openssl.conf\
  /etc/sysconfig\
  /etc/sysctl.d\
  /etc/syslog*\
  /etc/udev*\
  /etc/xinet.d\
  /etc/*release\
  $(find /lib/modules -name modules.dep 2>/dev/null)\
  "

########################################

CMDS="uname -a\
  :uptime\
  :runlevel\
  :iptables -L\
  :ulimit -a\
  :ps -eo pid,tid,nlwp,policy,user,tname,ni,pri,psr,sgi_p,stat,wchan,start_time,time,pcpu,pmem,vsize,size,rss,share,command\
  :ps axX\
  :dmesg -s 1048576\
  :last\
  :lsshut\
  :ifconfig -a\
  :nm-tool\
  :route -n\
  :ip route list\
  :ip route list table all\
  :ip rule list\
  :ip neigh list\
  :ip link show\
  :ip ntable\
  :ipcs -a\
  :netstat -pantu\
  :netstat -s\
  :dmsetup ls\
  :dmsetup ls --tree\
  :dmsetup table\
  :dmsetup table --target multipath\
  :dmsetup status\
  :multipath -v6 -ll\
  :multipath -d\
  :multipath -t\
  :lsqeth\
  :lschp\
  :lscss\
  :lscpu -ae\
  :lsmem\
  :lsdasd\
  :ziorep_config -ADM\
  :lsmod\
  :lsdev\
  :lsscsi\
  :lstape\
  :lszfcp\
  :lszfcp -D\
  :lszfcp -V\
  :icainfo\
  :icastats\
  :lszcrypt -VV\
  :ivp.e\
  :pkcsconf -mlist\
  :cat /var/lib/opencryptoki/pk_config_data\
  :ls -al /usr/lib64/opencryptoki/stdll\
  :SPident\
  :rpm -qa | sort\
  :sysctl -a\
  :lsof\
  :mount\
  :df -h\
  :pvpath -qa\
  :find /boot -print0 | sort -z | xargs -0 -n 10 ls -ld\
  :find /dev -print0 | sort -z | xargs -0 -n 10 ls -ld\
  :java -version\
  :cat /root/.bash_history\
  :env\
  :journalctl --all --no-pager --since=$(date -d '5 day ago' +%Y-%m-%d) --until=now --lines=50000 > ${OUTPUT_FILE_JOURNALCTL}\
  "

########################################

VM_CMDS="q userid\
  :q users\
  :q privclass\
  :q cplevel\
  :q cpus\
  :q srm\
  :q vtod\
  :q timezone\
  :q loaddev\
  :q v osa\
  :q v dasd\
  :q v crypto\
  :q v fcp\
  :q v pav\
  :q v sw\
  :q v st\
  :q st\
  :q xstore\
  :q xstore user system\
  :q sxspages\
  :q vmlan\
  :q vswitch\
  :q vswitch details\
  :q vswitch access\
  :q vswitch active\
  :q vswitch accesslist\
  :q vswitch promiscuous\
  :q vswitch controller\
  :q port group all active details\
  :q set\
  :q comm\
  :q controller all\
  :q fcp\
  :q frames\
  :q lan\
  :q lan all details\
  :q lan all access\
  :q cache\
  :q nic\
  :q pav\
  :q proc\
  :q qioass\
  :q spaces\
  :q swch all\
  :q trace\
  :q mdcache\
  :q alloc page\
  :q alloc spool\
  :q dump\
  :q dumpdev\
  :q reorder VMUSERID\
  :q quickdsp VMUSERID\
  :ind load\
  :ind sp\
  :ind user\
  "


###############################################################################

########################################
collect_cmdsout() {
    local cmd
    local ifs_orig="${IFS}"

    pr_syslog_stdout "1 of ${COLLECTION_COUNT}: Collecting command output"

    IFS=:
    for cmd in ${CMDS}; do
	IFS=${ifs_orig}	call_run_command "${cmd}" "${OUTPUT_FILE_CMD}"
    done
    IFS="${ifs_orig}"

    pr_log_stdout " "
}


########################################
collect_vmcmdsout() {
    local vm_command
    local cp_command
    local vm_cmds
    local vm_userid
    local module_loaded=1
    local ifs_orig="${IFS}"

    if echo "${RUNTIME_ENVIRONMENT}" | grep -qi "z/VM" >/dev/null 2>&1; then
	pr_syslog_stdout "2 of ${COLLECTION_COUNT}: Collecting z/VM command output"

	if which vmcp >/dev/null 2>&1; then
	    cp_command="vmcp"
	    if ! lsmod 2>/dev/null | grep -q vmcp; then
		modprobe vmcp && module_loaded=0 && sleep 2
	    fi
	elif which hcp >/dev/null 2>&1; then
	    cp_command="hcp"
	    if ! lsmod 2>/dev/null | grep -q cpint; then
		modprobe cpint && module_loaded=0 && sleep 2
	    fi
	else
	    pr_log_stdout " "
	    pr_log_stdout "${SCRIPTNAME}: Warning: No program found to communicate to z/VM CP"
	    pr_log_stdout "       Skipping collection of z/VM command output"
	    pr_log_stdout " "
	    return 1
	fi
	VMUSERID=$(${cp_command} q userid 2>/dev/null | sed -ne 's/^\([^[:space:]]*\).*$/\1/p')

	vm_cmds=$(echo "${VM_CMDS}" | sed "s/VMUSERID/${VMUSERID}/g")

	IFS=:
	for vm_command in ${vm_cmds}; do
	    IFS="${ifs_orig}"
	    local cp_buffer_size=2
	    local rc_buffer_size=2
	    while test ${rc_buffer_size} -eq 2 && test ${cp_buffer_size} -lt 1024; do
		cp_buffer_size=$(( cp_buffer_size * 2 ))

		eval ${cp_command} -b ${cp_buffer_size}k "${vm_command}" >/dev/null 2>&1
		rc_buffer_size=$?
	    done
	    call_run_command "${cp_command} -b ${cp_buffer_size}k ${vm_command}" "${OUTPUT_FILE_VMCMD}"
	    IFS=:
	done
	IFS=${ifs_orig}

	if test ${module_loaded} -eq 0 && test "x${cp_command}" = "xhcp"; then
	    rmmod cpint
	elif test ${module_loaded} -eq 0 && test "x${cp_command}" = "xvmcp"; then
	    rmmod vmcp
	fi
    else
	pr_syslog_stdout "2 of ${COLLECTION_COUNT}: Collecting z/VM command output skipped - no z/VM environment"
    fi

    pr_log_stdout " "
}


########################################
collect_procfs() {
    local file_name

    pr_syslog_stdout "3 of ${COLLECTION_COUNT}: Collecting procfs"

    for file_name in ${PROCFILES}; do
	call_collect_file "${file_name}"
    done

    pr_log_stdout " "
}


########################################
collect_sysfs() {
    local debugfs_mounted=0
    local file_name
    local file_names
    local rc_mount

    # Requires kernel version newer then 2.4
    if test "${LINUX_SUPPORT_SYSFS}" -eq 0; then
	pr_syslog_stdout "4 of ${COLLECTION_COUNT}: Collecting sysfs"
	# Requires kernel version of 2.6.13 or newer
	if test "${LINUX_SUPPORT_SYSFSDBF}" -eq 0; then
	    if ! grep -qE "${MOUNT_POINT_DEBUGFS}.*debugfs" /proc/mounts 2>/dev/null; then
		if mount -t debugfs debugfs "${MOUNT_POINT_DEBUGFS}" >/dev/null 2>&1; then
		    sleep 2
		    debugfs_mounted=1
		else
		    pr_log_stdout "${SCRIPTNAME}: Warning: Unable to mount debugfs at \"${MOUNT_POINT_DEBUGFS}\""
		fi
	    fi
	fi

	call_run_command "find /sys -print0 | sort -z | xargs -0 -n 10 ls -ld" "${OUTPUT_FILE_SYSFS}"

	find /sys -noleaf -type d 2>/dev/null | while IFS= read -r dir_name; do
	    mkdir -p "${WORKPATH}${dir_name}"
	done

	find /sys -noleaf -type f -perm /444 2>/dev/null | while IFS= read -r file_name; do
	    echo " ${file_name}"
	    if ! dd if="${file_name}" status=noxfer iflag=nonblock of="${WORKPATH}${file_name}" >/dev/null 2>&1; then
		echo "${SCRIPTNAME}: Warning: failed to copy \"${file_name}\""
	    fi
	done

	if test ${debugfs_mounted} -eq 1; then
	    umount "${MOUNT_POINT_DEBUGFS}"
	fi
    else
	pr_syslog_stdout "4 of ${COLLECTION_COUNT}: Collecting sysfs skipped. Kernel $(uname -r) must be newer than 2.4"
    fi

    pr_log_stdout " "
}


########################################
collect_logfiles() {
    local file_name

    pr_syslog_stdout "5 of ${COLLECTION_COUNT}: Collecting log files"

    for file_name in ${LOGFILES}; do
	call_collect_file "${file_name}"
    done

    pr_log_stdout " "
}


########################################
collect_configfiles() {
    local file_name

    pr_syslog_stdout "6 of ${COLLECTION_COUNT}: Collecting config files"

    for file_name in ${CONFIGFILES}; do
	call_collect_file "${file_name}"
    done

    pr_log_stdout " "
}


########################################
collect_osaoat() {
    local network_devices=$(lsqeth 2>/dev/null | grep "Device name" | sed 's/.*:[[:space:]]\+\([^[:space:]]*\)[[:space:]]\+/\1/g')
    local network_device

    if which qethqoat >/dev/null 2>&1; then
	if test -n "${network_devices}"; then
	    pr_syslog_stdout "7 of ${COLLECTION_COUNT}: Collecting osa oat output"
	    for network_device in ${network_devices}; do
		call_run_command "qethqoat ${network_device}" "${OUTPUT_FILE_OSAOAT}.out" &&
		call_run_command "qethqoat -r ${network_device}" "${OUTPUT_FILE_OSAOAT}_${network_device}.raw"
	    done
	else
	    pr_syslog_stdout "7 of ${COLLECTION_COUNT}: Collecting osa oat output skipped - no devices"
	fi
    else
	pr_syslog_stdout "7 of ${COLLECTION_COUNT}: Collecting osa oat output skipped - not available"
    fi

    pr_log_stdout " "
}


########################################
# Be aware that this output must be
# redirected into a separate logfile
call_run_command() {
    local cmd="${1}"
    local logfile="${2}"
    local raw_cmd=$(echo "${cmd}" | sed -ne 's/^\([^[:space:]]*\).*$/\1/p')

    echo "#######################################################" >> "${logfile}"
    echo "${USER}@${SYSTEMHOSTNAME:-localhost}> ${cmd}" >> "${logfile}"

    # check if command exists
    if ! which "${raw_cmd}" >/dev/null 2>&1; then
        # check if command is a builtin
	if ! command -v "${raw_cmd}" >/dev/null 2>&1; then
	    echo "${SCRIPTNAME}: Warning: Command \"${raw_cmd}\" not available" >> "${logfile}"
	    echo >> "${logfile}"
	    return 1
	fi
    fi

    if ! eval "${cmd}" >> "${logfile}" 2>&1; then
	echo "${SCRIPTNAME}: Warning: Command \"${cmd}\" failed" >> "${logfile}"
	echo >> "${logfile}"
	return 1
    else
	echo >> "${logfile}"
	return 0
    fi
}


########################################
call_collect_file() {
    local directory_name
    local file_name="${1}"

    echo " ${file_name}"

    directory_name=$(dirname "${file_name}" 2>/dev/null)
    if test ! -e "${WORKPATH}${directory_name}"; then
	mkdir -p "${WORKPATH}${directory_name}" 2>&1
    fi
    if ! cp -r --preserve=mode,timestamps -d -L --parents "${file_name}" "${WORKPATH}" 2>&1; then
	return 1
    else
	return 0
    fi
}


###############################################################################


########################################
# print version info
print_version() {
    cat <<EOF
${SCRIPTNAME}: Debug information script version %S390_TOOLS_VERSION%
Copyright IBM Corp. 2002, 2014
EOF
}


########################################
# print how to use this script
print_usage()
{
    print_version

    cat <<EOF


Usage: ${SCRIPTNAME} [OPTIONS]

This script collects runtime, configuration and trace information about
your Linux on System z installation for debugging purposes.

It also traces information about z/VM if the Linux runs under z/VM.


The collected information is written to a TAR archive named

    /tmp/DBGINFO-[date]-[time]-[hostname]-[processorid].tgz

where [date] and [time] are the date and time when debug data is collected.
[hostname] indicates the hostname of the system the data was collected from.
The [processorid] is taken from the processor 0 and indicates the processor
identification.

Options:

        -h|--help          print this help
        -v|--version       print version information


Please report bugs to: linux390@de.ibm.com

EOF
}


########################################
# print that an instance is already running
print_alreadyrunning() {
    print_version

    cat <<EOF


Please check the system if another instance of '${SCRIPTNAME}' is already
running. If this is not the case, please remove the lock file
'${WORKDIR_BASE}${SCRIPTNAME}.lock'.
EOF
}


########################################
#
commandline_parse()
{
    local cmdline_arg1=${1}
    local cmdline_count=${#}

    if test "${cmdline_count}" -eq 1; then
	if test "${cmdline_arg1}" = '-h' || test "${cmdline_arg1}" = '--help'; then
	    print_usage
	elif test "${cmdline_arg1}" = '-v' || test "${cmdline_arg1}" = '--version'; then
	    print_version
	else
	    echo
	    echo "${SCRIPTNAME}: invalid option \"${cmdline_arg1}\""
	    echo "Try '${SCRIPTNAME} --help' for more information"
	    echo
	    exit 1
	fi
	exit 0
    elif test "${cmdline_count}" -ge 1; then
	echo
	echo "${SCRIPTNAME}: Error: Invalid number of arguments!"
	echo
	print_usage
	exit 1
    fi
}


########################################
# Setup the environment
environment_setup()
{
    if test ! -e "${WORKDIR_BASE}"; then
	mkdir -p "${WORKDIR_BASE}"
    elif test ! -d "${WORKDIR_BASE}"; then
	echo "${SCRIPTNAME}: Error: \"${WORKDIR_BASE}\" exists but this is a file!"
	echo "       Please make sure \"${WORKDIR_BASE}\" is a directory."
	exit 1
    fi

    if test -e "${WORKDIR_BASE}${SCRIPTNAME}".lock; then
	print_alreadyrunning
        exit 1
    else
	touch "${WORKDIR_BASE}${SCRIPTNAME}".lock
    fi

    if ! mkdir "${WORKPATH}" 2>/dev/null; then
	echo "${SCRIPTNAME}: Error: Target directory \"${WORKPATH}\" already exists or"
	echo "       \"${WORKDIR_BASE}\" does not exist!"
	exit 1
    fi
}


########################################
# create gzip-ped tar file
create_package()
{
    pr_stdout "Finalizing: Creating archive with collected data"
    cd "${WORKDIR_BASE}"

    if ! tar -czf "${WORKARCHIVE}" "${WORKDIR_CURRENT}"; then
	pr_stdout " "
	pr_stdout "${SCRIPTNAME}: Error: Collection of data failed!"
	pr_stdout "       The creation of \"${WORKARCHIVE}\" was not successful."
	pr_stdout "       Please check the directory \"${WORKDIR_BASE}\""
	pr_stdout "       to provide enough free available space."
    else
	pr_stdout " "
	pr_stdout "Collected data was saved to:"
	pr_stdout " >>  ${WORKARCHIVE}  <<"
    fi

    pr_stdout " "
}


########################################
# Cleaning up the prepared/collected information
environment_cleanup()
{
    if ! rm -rf "${WORKPATH}" 2>/dev/null; then
	pr_stdout " "
	pr_stdout "${SCRIPTNAME}: Warning: Deletion of \"${WORKPATH}\" failed"
	pr_stdout "       Please remove the directory manually"
	pr_stdout " "
    fi
    if ! rm -f "${WORKDIR_BASE}${SCRIPTNAME}".lock 2>/dev/null; then
	pr_stdout " "
	pr_stdout "${SCRIPTNAME}: Warning: Deletion of \"${WORKDIR_BASE}${SCRIPTNAME}\" failed"
	pr_stdout "       Please remove the file manually"
	pr_stdout " "
    fi
}


########################################
# Function to perform a cleanup in case of a received signal
emergency_exit()
{
    pr_stdout " "
    pr_stdout "${SCRIPTNAME}: Info: Data collection has been interrupted"
    pr_stdout "       Cleanup of temporary collected data"
    environment_cleanup
    pr_stdout "${SCRIPTNAME}: Info: Emergency exit processed"

    pr_stdout " "
    logger -t "${SCRIPTNAME}" "Data collection interrupted"
    exit
}


########################################
# Function to print to stdout when rediretion is active
pr_stdout()
{
    echo "${@}" >&8
}


########################################
# Function to print to stdout and into log file when rediretion is active
pr_log_stdout()
{
    echo "$@"
    echo "$@" >&8
}


########################################
# Function to print to stdout and into log file when rediretion is active
pr_syslog_stdout()
{
    echo "$@"
    echo "$@" >&8
    logger -t "${SCRIPTNAME}" "$@"
}


###############################################################################
# Running the script

commandline_parse "${@}"

# Verification to run as root
if test "$(/usr/bin/id -u 2>/dev/null)" -ne 0; then
    echo "${SCRIPTNAME}: Error: You must be user root to run \"${SCRIPTNAME}\"!"
    exit 1
fi

environment_setup
print_version

# saving stdout/stderr and redirecting stdout/stderr into log file
exec 8>&1 9>&2 >"${LOGFILE}" 2>&1

# trap on SIGHUP=1 SIGINT=2 SIGTERM=15
trap emergency_exit SIGHUP SIGINT SIGTERM

pr_log_stdout ""
pr_log_stdout "Hardware platform     = $(uname -i)"
pr_log_stdout "Kernel version        = ${KERNEL_VERSION}.${KERNEL_MAJOR_REVISION}.${KERNEL_MINOR_REVISION} ($(uname -r 2>/dev/null))"
pr_log_stdout "Runtime environment   = ${RUNTIME_ENVIRONMENT}"
pr_log_stdout ""

logger -t "${SCRIPTNAME}" "Starting data collection"

collect_cmdsout

collect_vmcmdsout

# Collecting the proc file system (content is specific based on kernel version)
collect_procfs

# Collecting sysfs in case we run on Kernel 2.4 or newer
collect_sysfs

collect_logfiles

collect_configfiles

collect_osaoat

create_package

environment_cleanup

logger -t "${SCRIPTNAME}" "Data collection completed"

exec 1>&8 2>&9 8>&- 9>&-

#EOF
