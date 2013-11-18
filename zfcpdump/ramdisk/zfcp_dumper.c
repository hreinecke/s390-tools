/******************************************************************************/
/*  zfcp_dumper.c                                                             */
/*                                                                            */
/*    Copyright IBM Corp. 2003, 2006.                                     */
/*    Author(s): Michael Holzheu <holzheu@de.ibm.com>                         */
/*               Andreas Herrmann <aherrman@de.ibm.com>                       */
/******************************************************************************/

#include <asm/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/reboot.h>
#include <sys/mount.h>
#include <linux/reboot.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>

#include "zfcp_dumper.h"
#include "../kernel/dump.h"
#include "zt_common.h"

#ifdef __s390x__
#define FMT64 "l"
#else
#define FMT64 "ll"
#endif

#ifndef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif

/******************************************************************************/
/* defines for zfcpdumper                                                     */
/******************************************************************************/

#define PROC_CMDLINE     "/proc/cmdline"
#define DEV_ZCORE        "/dev/zcore"
#define DEV_ZCORE_MAJOR  200
#define DEV_ZCORE_MINOR  0
#define DEV_SCSI         "/dev/scsidev"
#define DEV_SCSI_MAJOR   8
#define DEV_NULL         "/dev/null"
#define DEV_NULL_MAJOR   1
#define DEV_NULL_MINOR   3 
#define DUMP_MOUNT_POINT "/mnt"

#define PARM_DUMP_DIR            "dump_dir"
#define PARM_DUMP_DIR_DFLT       "/"

#define PARM_DUMP_PART           "dump_part"
#define PARM_DUMP_PART_DFLT      "1"

#define PARM_DUMP_COMPRESS       "dump_compress"
#define PARM_DUMP_COMPRESS_GZIP  "gzip"
#define PARM_DUMP_COMPRESS_NONE  "none"
#define PARM_DUMP_COMPRESS_DFLT   PARM_DUMP_COMPRESS_NONE

#define PARM_DUMP_MEM            "dump_mem"
#ifdef __s390x__
#define PARM_DUMP_MEM_DFLT       0xffffffffffffffff
#else
#define PARM_DUMP_MEM_DFLT       0xffffffff
#endif

#define PARM_DUMP_DEBUG          "dump_debug"
#define PARM_DUMP_DEBUG_DFLT      2
#define PARM_DUMP_DEBUG_MIN       1
#define PARM_DUMP_DEBUG_MAX       6

#define PARM_DUMP_MODE               "dump_mode"
#define PARM_DUMP_MODE_INTERACT      "interactive"
#define PARM_DUMP_MODE_INTERACT_NUM  0
#define PARM_DUMP_MODE_AUTO          "auto"
#define PARM_DUMP_MODE_AUTO_NUM      1
#define PARM_DUMP_MODE_DFLT          PARM_DUMP_MODE_INTERACT
#define PARM_DUMP_MODE_NUM_DFLT      PARM_DUMP_MODE_INTERACT_NUM

#define DUMP_FIRST 0
#define DUMP_LAST  1

#define ONE_MB 1048576

#define SLEEP_TIME_ERASE 5 /* seconds */
#define SLEEP_TIME_END   3 /* seconds */

/******************************************************************************/
/* defines dump format                                                        */
/* datastructures and defines are from the lkcd (linux kernel crash dumps)    */
/* project                                                                    */
/******************************************************************************/

#define UTS_LEN 65     /* do not change ... */
 
/*
 * Size of the buffer that's used to hold:
 *
 *      1. the dump header (paded to fill the complete buffer)
 *      2. the possibly compressed page headers and data
 */
#define DUMP_BUFFER_SIZE     (64 * 1024)  /* size of dump buffer (0x10000) */
 
/* header definitions for dumps from s390 standalone dump tools */
#define DUMP_MAGIC_S390SA     0xa8190173618f23fdULL /* s390sa magic number */
#define DUMP_HEADER_SZ_S390SA 4096
 
/* standard header definitions */
#define DUMP_MAGIC_NUMBER  0xa8190173618f23edULL  /* dump magic number  */
#define DUMP_VERSION_NUMBER 0x8      /* dump version number             */
#define DUMP_PANIC_LEN      0x100    /* dump panic string length        */
 
/* dump compression options -- add as necessary */
#define DUMP_COMPRESS_NONE     0x0   /* don't compress this dump      */
#define DUMP_COMPRESS_RLE      0x1   /* use RLE compression           */
#define DUMP_COMPRESS_GZIP     0x2   /* use GZIP compression          */
 
/* dump flags - any dump-type specific flags -- add as necessary */
#define DUMP_FLAGS_NONE        0x0   /* no flags are set for this dump   */
#define DUMP_FLAGS_NONDISRUPT  0x1   /* try to keep running after dump   */
 
/* dump header flags -- add as necessary */
#define DUMP_DH_FLAGS_NONE     0x0   /* no flags set (error condition!)  */
#define DUMP_DH_RAW            0x1   /* raw page (no compression)        */
#define DUMP_DH_COMPRESSED     0x2   /* page is compressed               */
#define DUMP_DH_END            0x4   /* end marker on a full dump        */
#define DUMP_DH_TRUNCATED      0x8   /* dump is incomplete               */
#define DUMP_DH_TEST_PATTERN   0x10  /* dump page is a test pattern      */
#define DUMP_DH_NOT_USED       0x20  /* 1st bit not used in flags        */

#define PAGE_SIZE           4096
/* macros for page size, mask etc. used in dump format
 * (this is not the system page size stored in the dump header)
 */
#define DUMP_PAGE_SHIFT     12ULL
#define DUMP_PAGE_SIZE      (1ULL << DUMP_PAGE_SHIFT)
#define DUMP_PAGE_MASK      (~(DUMP_PAGE_SIZE-1))
#define DUMP_HEADER_OFFSET  DUMP_PAGE_SIZE


/******************************************************************************/
/* Data structures                                                            */
/******************************************************************************/

/* This is the header dumped at the top of every valid crash dump.
 */
typedef struct dump_header_s {
        uint64_t magic_number; /* dump magic number, unique to verify dump */
        uint32_t version;      /* version number of this dump */
        uint32_t header_size;  /* size of this header */
        uint32_t dump_level;   /* level of this dump */
        uint32_t page_size;    /* page size (e.g. 4K, 8K, 16K, etc.) */
        uint64_t memory_size;  /* size of entire physical memory */
        uint64_t memory_start; /* start of physical memory */
        uint64_t memory_end;   /* end of physical memory */
        /* the number of dump pages in this dump specifically */
        uint32_t num_dump_pages;
        char panic_string[DUMP_PANIC_LEN]; /* panic string, if available*/
 
        /* timeval depends on machine, two long values */
        struct {uint64_t tv_sec;
                uint64_t tv_usec;
        } time; /* the time of the system crash */

        /* the NEW utsname (uname) information -- in character form */
        /* we do this so we don't have to include utsname.h         */
        /* plus it helps us be more architecture independent        */
        char utsname_sysname[UTS_LEN];
        char utsname_nodename[UTS_LEN];
        char utsname_release[UTS_LEN];
        char utsname_version[UTS_LEN];
        char utsname_machine[UTS_LEN];
        char utsname_domainname[UTS_LEN];

        uint64_t current_task;  
        uint32_t dump_compress; /* compression type used in this dump */
        uint32_t dump_flags;    /* any additional flags */
        uint32_t dump_device;   /* any additional flags */
} __attribute__((packed)) dump_header_t;

/* This is the header used by zcore
 */
typedef struct dump_header_s390sa_s {
        uint64_t magic_number; /* magic number for this dump (unique)*/
        uint32_t version;      /* version number of this dump */
        uint32_t header_size;  /* size of this header */
        uint32_t dump_level;   /* the level of this dump (just a header?) */
        uint32_t page_size;    /* page size of dumped Linux (4K,8K,16K etc.) */
        uint64_t memory_size;  /* the size of all physical memory */
        uint64_t memory_start; /* the start of physical memory */
        uint64_t memory_end;   /* the end of physical memory */
        uint32_t num_pages;    /* number of pages in this dump */
        uint32_t pad;          /* ensure 8 byte alignment for tod and cpu_id */
        uint64_t tod;          /* the time of the dump generation */
        uint64_t cpu_id;       /* cpu id */
        uint32_t arch_id;
        uint32_t build_arch_id;
#define DH_ARCH_ID_S390X 2
#define DH_ARCH_ID_S390  1
} __attribute__((packed))  dump_header_s390sa_t;
 
/* Header associated to each physical page of memory saved in the system
 * crash dump.
 */
typedef struct dump_page_s {
        uint64_t address; /* the address of this dump page */
        uint32_t size;  /* the size of this dump page */
        uint32_t flags; /* flags (DUMP_COMPRESSED, DUMP_RAW or DUMP_END) */
} __attribute__((packed)) dump_page_t;


/* Compression function */
typedef int (*compress_fn_t)(const unsigned char *old, uint32_t old_size,
		unsigned char *new, uint32_t size);


/******************************************************************************/
/* Prototypes                                                                 */
/******************************************************************************/

static void release_hsa(void);
static int parse_parmline(void);
static int dump_init_sig(void);
static int dump_tune_vm(void);
static void dump_terminate(void);
static int dump_create_s390sa(char* sourcedev, char* dumpdir);
static void dump_display_progress(uint64_t mb_written, uint64_t mb_max);
  
/******************************************************************************/
/* Globals                                                                    */
/******************************************************************************/

global_t g;

/******************************************************************************/
/* Functions                                                                  */
/******************************************************************************/

/*
 * parse_parmline()
 *
 * Get dump parameters from /proc/cmdline
 * Return: 0       - ok
 *         (!= 0)  - error
 */

#define PARM_MAX 200
static int parse_parmline(void){
	int fh = -1;
	int rc = 0;
	char* token;
	int i = 0, j;
	char* parms[PARM_MAX];
	int count;

	/* setting defaults */

	g.parm_dump_compress = PARM_DUMP_COMPRESS_DFLT;
	g.parm_dump_dir      = PARM_DUMP_DIR_DFLT;
	g.parm_dump_part     = PARM_DUMP_PART_DFLT;
	g.parm_dump_debug    = PARM_DUMP_DEBUG_DFLT;
	g.parm_dump_mode     = PARM_DUMP_MODE_NUM_DFLT;
	g.parm_dump_mem      = PARM_DUMP_MEM_DFLT;

	fh = open(PROC_CMDLINE,O_RDONLY);
	if(fh == -1){
		PRINT_PERR("open %s failed\n",PROC_CMDLINE);
		rc = -1;goto out;
	}
	if((count = read(fh,g.parmline,CMDLINE_MAX_LEN)) == -1){
		PRINT_PERR("read %s failed\n", PROC_CMDLINE);
		rc = -1;goto out;
	}
	g.parmline[count-1]='\0'; /* remove \n */

	token = strtok(g.parmline, " \t\n");
	while(token != NULL){
		parms[i] = token;
		token = strtok(NULL," \t\n");
		i++;
		if(i >= PARM_MAX){
			PRINT_WARN("THIS SHOULD NOT HAPPEN: More than %i "
				"kernel parmameters specified\n",PARM_MAX);
			PRINT_WARN("                        The rest of the "
				"parameters will not be considered!\n");
			break;
		}
	}
	for(j = 0; j < i; j++){
		token = strtok(parms[j],"=");
		if(token == NULL){
			/* No '=' parameter, just skip it */
			continue;

		/* Dump Dir */

		} else if(strcmp(token, PARM_DUMP_DIR) == 0){
			g.parm_dump_dir = strtok(NULL,"=");
			if(g.parm_dump_dir == NULL){
				PRINT_WARN("No value for '%s' parameter "
					"specified\n", PARM_DUMP_DIR);
				PRINT_WARN("Using default: %s\n",
					PARM_DUMP_DIR_DFLT);
				g.parm_dump_dir = PARM_DUMP_DIR_DFLT;
			}

		/* Dump Partition */

		} else if(strcmp(token, PARM_DUMP_PART) == 0){
			g.parm_dump_part = strtok(NULL,"=");
			if(g.parm_dump_part == NULL){
				PRINT_ERR("No value for '%s' parameter "
					"specified\n", PARM_DUMP_PART);
				rc = -1;
				goto out;
			}

		/* Dump mem */

		} else if(strcmp(token, PARM_DUMP_MEM) == 0){
			char* mem_str = strtok(NULL,"=");
			if(mem_str == NULL){
				PRINT_ERR("No value for '%s' parameter "
					"specified\n", PARM_DUMP_MEM);
				rc = -1;
				goto out;
			}
			g.parm_dump_mem = strtoll(mem_str,NULL,0);

		/* Dump Compression */

		} else if(strcmp(token, PARM_DUMP_COMPRESS) == 0){
			g.parm_dump_compress = strtok(NULL,"=");
			if(g.parm_dump_compress == NULL){
				PRINT_WARN("No value for '%s' parameter "
					"specified\n", PARM_DUMP_COMPRESS);
				PRINT_WARN("Using default: %s\n",
					PARM_DUMP_COMPRESS_DFLT);
				g.parm_dump_compress = PARM_DUMP_COMPRESS_DFLT;
			} else if((strcmp(g.parm_dump_compress,
					PARM_DUMP_COMPRESS_GZIP) != 0) &&
					(strcmp(g.parm_dump_compress,
					PARM_DUMP_COMPRESS_NONE) != 0)){
				PRINT_WARN("Unknown dump compression '%s' "
					"specified!\n",g.parm_dump_compress);
				PRINT_WARN("Using default: %s\n",
					PARM_DUMP_COMPRESS_DFLT);
				g.parm_dump_compress = PARM_DUMP_COMPRESS_DFLT;
			}

		/* Dump Debug */

		} else if(strcmp(token, PARM_DUMP_DEBUG) == 0){
			char *s = strtok(NULL,"=");
			if(s == NULL){
				PRINT_WARN("No value for '%s' parameter "
					"specified\n", PARM_DUMP_DEBUG);
				PRINT_WARN("Using default: %d\n",
					PARM_DUMP_DEBUG_DFLT);

			} else {
				g.parm_dump_debug = atoi(s);
				if((g.parm_dump_debug < PARM_DUMP_DEBUG_MIN)
				|| (g.parm_dump_debug > PARM_DUMP_DEBUG_MAX)){
					PRINT_WARN("Invalid value (%i) for %s "
					"parameter specified (allowed range is "
					"%i - %i)\n",g.parm_dump_debug,
					PARM_DUMP_DEBUG, PARM_DUMP_DEBUG_MIN,
					PARM_DUMP_DEBUG_MAX);
					PRINT_WARN("Using default: %i\n",
					PARM_DUMP_DEBUG_DFLT);
					g.parm_dump_debug=PARM_DUMP_DEBUG_DFLT;
				}
			}

		/* Dump Mode */

		} else if(strcmp(token, PARM_DUMP_MODE) == 0){
			char *s = strtok(NULL,"=");
			if(s == NULL){
				PRINT_WARN("No value for '%s' parameter "
					"specified\n", PARM_DUMP_MODE);
				PRINT_WARN("Using default: %s\n",
					PARM_DUMP_MODE_DFLT);
			} else if(strcmp(s,PARM_DUMP_MODE_INTERACT) == 0){
				g.parm_dump_mode = PARM_DUMP_MODE_INTERACT_NUM;
			} else if(strcmp(s,PARM_DUMP_MODE_AUTO) == 0){
				g.parm_dump_mode = PARM_DUMP_MODE_AUTO_NUM;
			} else {
				PRINT_WARN("Unknown dump mode: %s\n",s);
				PRINT_WARN("Using default: %s\n",
					PARM_DUMP_MODE_DFLT);
			}
		}
        }
	PRINT_TRACE("dump dir  : %s\n",g.parm_dump_dir);
	PRINT_TRACE("dump part : %s\n",g.parm_dump_part);
	PRINT_TRACE("dump comp : %s\n",g.parm_dump_compress);
	PRINT_TRACE("dump debug: %d\n",g.parm_dump_debug); 
	PRINT_TRACE("dump mem:   %llx\n", (unsigned long long) g.parm_dump_mem);
		
	if(g.parm_dump_mode == PARM_DUMP_MODE_AUTO_NUM){
		PRINT_TRACE("dump mode : %s\n",PARM_DUMP_MODE_AUTO);
	} else if(g.parm_dump_mode == PARM_DUMP_MODE_INTERACT_NUM) {
		PRINT_TRACE("dump mode : %s\n",PARM_DUMP_MODE_INTERACT);
	}

	sprintf(g.dump_dir, "%s/%s",DUMP_MOUNT_POINT, g.parm_dump_dir);
out:
	if(fh != -1)
		close(fh);
	return rc;
}

/*
 * dump_check_zcore()
 * 
 * Test if zcore is working properly
 * Return: 0  - zcore is ok 
 *         <0 - error 
 */
int dump_check_zcore(void)
{
	int rc = 0;
	int error_code;
	int fh;

	fh = open(DEV_ZCORE,O_RDWR); 
	if(fh == -1){
		PRINT_PERR("open %s failed\n", DEV_ZCORE);
		rc = -1; goto out;
	}
	if(ioctl(fh, ZCORE_IOCTL_GET_ERROR, &error_code) == -1){
		PRINT_PERR("ioctl 'get error code' failed\n");
		rc = -1; goto out;
	}
	switch(error_code){
		case ZCORE_ERR_OK:
			break;
		case ZCORE_ERR_HSA_FEATURE:
			PRINT_ERR("HSA - Feature not working\n");
			rc = -1; goto out;
		case ZCORE_ERR_CPU_INFO:
			PRINT_ERR("HSA - NO CPU INFO\n");
			rc = -1; goto out;
		case ZCORE_ERR_OTHER:
		default:
			PRINT_ERR("HSA - ERROR\n");
			rc = -1; goto out;
	}
out:
	if(fh != -1)
		close(fh);
	return rc;
}

/*
 * get_fcp_info()
 * 
 * Read fcp info from /dev/zcore
 * Return: 0  - ok
 *         -1 - error
 */
int get_fcp_info(struct ipl_block_fcp *fcp_info)
{
	int fh = -1, rc = 0;

	fh = open(DEV_ZCORE,O_RDWR); 
	if(fh == -1){
		PRINT_PERR("open %s failed", DEV_ZCORE);
		rc = -1;
		goto out;
	}
	if(ioctl(fh, ZCORE_IOCTL_GET_PARMS, fcp_info) == -1){
		PRINT_PERR("ioctl 'get fcp parms' failed");
		rc = -1;
		goto out;
	}
out:
	if(fh != -1)
		close(fh);
	return rc;
}

/*
 * get_hsa_size()
 * 
 * Return: size of HSA saved memory
 *         -1: error
 */
int get_hsa_size(void)
{
	int fh = -1;
	int rc = 0;

	fh = open(DEV_ZCORE,O_RDWR); 
	if(fh == -1){
		PRINT_PERR("open %s failed", DEV_ZCORE);
		rc = -1; goto out;
	}
	if(ioctl(fh, ZCORE_IOCTL_HSASIZE, &rc) == -1){
		PRINT_PERR("ioctl 'get hsa size' failed");
		rc = -1; goto out;
	}
	PRINT_TRACE("got hsa size: %i\n",rc);
out:
	if(fh != -1)
		close(fh);
	return rc;
}

static int
write_to_file(const char* fcp_file, const char* command)
{
	int rc = 0, fh = 0;

	fh = open(fcp_file,O_WRONLY);
	if(fh == -1){
		PRINT_PERR("Could not open %s",fcp_file);
		rc = -1; goto out;
	}
	if(write(fh, command, strlen(command)) != strlen(command)){
		PRINT_PERR("Write to %s failed",fcp_file);
		rc = -1; goto out;
	};
out:
	if(fh)
		close(fh);
	return rc;
}

/*
 * enable_zfcp_device()
 *
 * Enable the scsi disk for dumping
 * Parameter: fcp_info - Specifies dump device
 * Return:    0 - ok
 *         != 0 - error
 */
int enable_zfcp_device(struct ipl_block_fcp *fcp_info)
{
	char command_string[1024] = {0};
	char fcp_file[1024] = {0};

	sprintf(command_string, "1\n");
	sprintf(fcp_file,"/sys/bus/ccw/drivers/zfcp/0.0.%04x/online",
		fcp_info->devno);
	PRINT_TRACE("online: %s %s\n",command_string,fcp_file);
	if(write_to_file(fcp_file, command_string))
		return -1;
	sprintf(command_string, "0x%016llx\n",
		(unsigned long long) fcp_info->wwpn);
	sprintf(fcp_file,"/sys/bus/ccw/drivers/zfcp/0.0.%04x/port_add",
		fcp_info->devno);
	if(write_to_file(fcp_file, command_string))
		return -1;
	sprintf(command_string, "0x%016llx\n",
		(unsigned long long) fcp_info->lun);
	sprintf(fcp_file,"/sys/bus/ccw/drivers/zfcp/0.0.%04x/0x%016llx/"
		"unit_add", fcp_info->devno,
		(unsigned long long) fcp_info->wwpn);
	if(write_to_file(fcp_file, command_string))
		return -1;
	return 0;
}

/*
 * mount_dump_device()
 *
 * Mount the dump device
 * Return:    0 - ok
 *         != 0 - error
 */
int mount_dump_device(void)
{
	int rc = 0, pid;

	/* e2fsck */
	PRINT_TRACE("e2fsck\n");
	pid = fork();
	if(pid < 0){
		PRINT_PERR("fork failed\n");
		rc = -1; goto out;
	} else if(pid == 0){
		execl("/sbin/e2fsck", "e2fsck", DEV_SCSI, "-y", NULL);
	} else {
		waitpid (pid, NULL, 0);
	}

	/* mount dump device */	
	PRINT_TRACE("mount\n");
	if(mount(DEV_SCSI, DUMP_MOUNT_POINT, "ext3", 0, NULL) == 0){
		rc = 0;
		goto out;
	}
	if(mount(DEV_SCSI, DUMP_MOUNT_POINT, "ext2", 0, NULL) != 0){
		PRINT_PERR("mount failed\n");
		rc = -1; goto out;
	}
out:
	return rc;
}

/*
 * umount_dump_device()
 *
 * unmount the dump device
 * Return:    0 - ok
 *         != 0 - error
 */
int umount_dump_device(void)
{
	int rc = 0;
	if(umount(DUMP_MOUNT_POINT) != 0){
		PRINT_PERR("umount failed");
		rc = -1; goto out;
	}
out:
	return rc;
}


/*
 * release_hsa()
 *
 * Release the 32 MB stored in the HSA
 */
static void release_hsa(void)
{
	int fh = -1;

	if(g.hsa_released){
		goto out;
	}
	PRINT_TRACE("release hsa\n");
	fflush(stdout);
	fflush(stderr);
	fh = open(DEV_ZCORE,O_RDWR);
	if(fh == -1){
		PRINT_PERR("open %s failed", DEV_ZCORE);
		goto out;
	}
	if(ioctl(fh, ZCORE_IOCTL_RELEASE_HSA,NULL) == -1){
		PRINT_PERR("ioctl 'release hsa' failed");
		goto out;
	}
	g.hsa_released=1;
out:
	if(fh != -1)
		close(fh);
	return;
}

/*
 * main()
 *
 * main routine of the zfcp_dumper
 */
int main(int argc, char* argv[])
{
	int rc = 0;
	struct ipl_block_fcp fcp_info;

#ifdef __s390x__
	PRINT("Linux for zSeries System Dumper starting\n");
	PRINT("Version %s (64 bit)\n",RELEASE_STRING);
#else
	PRINT("Linux for zSeries System Dumper starting\n");
	PRINT("Version %s (32 bit)\n",RELEASE_STRING);
#endif
	if(dump_init_sig()){
		PRINT_ERR("Init Signals failed!\n");
		rc = 1; goto out;
	}
	if(mount("proc","/proc","proc", 0, NULL)){
		PRINT_PERR("Unable to mount proc\n");
		rc = 1; goto out;
	}
	if(dump_tune_vm()){
		PRINT_PERR("Unable to set VM settings\n");
		rc = 1; goto out;
	}
	if(mount("sysfs","/sys","sysfs", 0, NULL)){
		PRINT_PERR("Unable to mount sysfs\n");
		rc = 1; goto out;
	}
	if(parse_parmline()){
		PRINT_ERR("Could not parse parmline\n");
		rc = 1; goto out;
	}
	if(mknod(DEV_NULL, S_IFCHR | 0600,
		makedev(DEV_NULL_MAJOR, DEV_NULL_MINOR))){
		rc = 1; goto out;
	}
	if(mknod(DEV_ZCORE, S_IFCHR | 0600,
		makedev(DEV_ZCORE_MAJOR, DEV_ZCORE_MINOR))){
		rc = 1; goto out;
	}
	if(mknod(DEV_SCSI, S_IFBLK | 0600,
		makedev(DEV_SCSI_MAJOR, atoi(g.parm_dump_part)))){
		rc = 1; goto out;
	}
	if(dump_check_zcore()){
		PRINT_ERR("Check if your Microcode is up to date!\n");
		rc = 1; goto out;
	}
	if(get_fcp_info(&fcp_info)){
		PRINT_ERR("Could not get fcp data:\n");
		rc = 1; goto out;
	}

	PRINT(" \n"); /* leading blank is needed that sclp console prints */
                      /* the newline */
	PRINT("DUMP PARAMETERS:\n");
	PRINT("================\n");
	PRINT("devno    : %x\n",fcp_info.devno);
	PRINT("wwpn     : %016llx\n", (unsigned long long) fcp_info.wwpn);
	PRINT("lun      : %016llx\n", (unsigned long long) fcp_info.lun);
	PRINT("conf     : %i\n",fcp_info.bootprog);
	PRINT("partition: %s\n",g.parm_dump_part);
	PRINT("directory: %s\n",g.parm_dump_dir);
	PRINT("compress : %s\n",g.parm_dump_compress);

	if(!(fcp_info.type & BOOT_TYPE_DUMP)){
		PRINT_ERR("IPL type is not dump\n");
		rc = 1; goto out;
	}
	if((g.hsa_size = get_hsa_size()) < 0){
		PRINT_ERR("Could not determine HSA size!\n");
		rc = 1; goto out;
	}
	PRINT(" \n");
	PRINT("MOUNT DUMP PARTITION:\n");
	PRINT("=====================\n");
	if(enable_zfcp_device(&fcp_info)){
		PRINT_ERR("Could not enable dump device\n");
		rc = 1; goto out;
	}
	if(mount_dump_device()){
		PRINT_ERR("Could not mount dump device\n");
		rc = 1; goto out;
	}
	PRINT(" \n");
	PRINT("DUMP PROCESS STARTED:\n");
	PRINT("=====================\n");
	if(dump_create_s390sa(DEV_ZCORE, g.dump_dir)){
		rc = 1; /* no goto out -> unmount device first! */
	}
	if(umount_dump_device()){
		PRINT_ERR("Could not umount dump device\n");
		rc = 1; goto out;
	}
out:
	PRINT(" \n");
	if(rc==0){
		PRINT("DUMP 'dump.%i' COMPLETE\n",g.dump_nr);
	} else {
		PRINT("DUMP 'dump.%i' FAILED\n",g.dump_nr);
	}
	fflush(stdout);
	dump_terminate();
	return rc;
}


/*
 * trigger_reipl()
 *
 * Issue ioctl system call to trigger reipl. The zcore device driver
 * issues the necessary diag308 calls only if valid ipib address and checksum
 * were found in lowcore. Otherwise the ZCORE_IOCTL_REIPL ioctl won't do
 * anything at all.
 */
void trigger_reipl(void)
{
	int fh;

	fh = open(DEV_ZCORE, O_RDWR);
	if (fh == -1) {
		PRINT_PERR("open %s failed", DEV_ZCORE);
		return;
	}
	ioctl(fh, ZCORE_IOCTL_REIPL, 0);
	close(fh);
}

/*
 * dump_terminate()
 *
 * Terminate the system dumper
 */
static void
dump_terminate(void)
{
	release_hsa();
	sleep(SLEEP_TIME_END); /* give the messages time to be displayed */
	trigger_reipl();
	reboot(LINUX_REBOOT_CMD_POWER_OFF);
}

/*
 * dump_sig_handler()
 *
 * Signal handler for zfcp_dumper
 */
static __sighandler_t
dump_sig_handler(int sig, siginfo_t *sip, void*p)
{
	PRINT_ERR("Got signal: %i\n",sig);
	PRINT_ERR("Dump failed!\n");
	dump_terminate();
	return 0;
}

/*
 * dump_init_sig()
 *
 * Setup the Signal handler for zfcp_dumper
 * Return:   0 - ok
 *         !=0 - error
 */
static int
dump_init_sig(void)
{
	int rc = 0;
	g.dump_sigact.sa_flags = (SA_NODEFER | SA_SIGINFO | SA_RESETHAND);
	g.dump_sigact.sa_handler = (__sighandler_t)dump_sig_handler;
	if (sigemptyset(&g.dump_sigact.sa_mask) < 0) {
		rc = -1; goto out;
	}
	if (sigaction(SIGINT, &g.dump_sigact, NULL) < 0) {
		rc = -1; goto out;
	}
	if (sigaction(SIGTERM, &g.dump_sigact, NULL) < 0) {
		rc = -1; goto out;
	}
	if (sigaction(SIGPIPE, &g.dump_sigact, NULL) < 0) {
		rc = -1; goto out;
	}
	if (sigaction(SIGABRT, &g.dump_sigact, NULL) < 0) {
		rc = -1; goto out;
	}
	if (sigaction(SIGSEGV, &g.dump_sigact, NULL) < 0) {
		rc = -1; goto out;
	}
	if (sigaction(SIGBUS, &g.dump_sigact, NULL) < 0) {
		rc = -1; goto out;
	}
out:
	return rc;
}

/*
 * dump_tune_vm()
 *
 * Set memory management parameters: Ensure that dirty pages are written
 * early enough! See "Documentation/filesystems/proc.txt"
 * Return:   0 - ok
 *         !=0 - error
 */
static int
dump_tune_vm(void)
{
	char* sysctl_names[] = {"/proc/sys/vm/dirty_ratio",
				"/proc/sys/vm/dirty_background_ratio",
				"/proc/sys/vm/dirty_writeback_centisecs",
				"/proc/sys/vm/dirty_expire_centisecs",
				"/proc/sys/vm/vfs_cache_pressure",
				"/proc/sys/vm/lowmem_reserve_ratio",
				NULL};
	char* sysctl_values[] = {"2","5","50","50","500","32",NULL};
	int fh = -1, i=0, rc=0;

	while(sysctl_names[i]){
		fh = open(sysctl_names[i],O_RDWR);
		if(fh < 0){
			rc = -1;
			goto out;
		}
		if(write(fh, sysctl_values[i], strlen(sysctl_values[i])) !=
			strlen(sysctl_values[i])){
			rc = -1;
			goto out;
		}
		i++;
	}
out:
	if(fh >= 0)
		close(fh);
	return rc;
}

/*
 * dump_get_num()
 *
 * Get dump number
 * Parameter: dumpdir - dump directory (absolute path)
 *            mode    - DUMP_FIRST: Find first dump number in directory
 *                    - DUMP_LAST:  Find last dump number in directory
 * Return: >= 0 - dump number
 *         -1   - no dump found in directory
 *         <-1  - error
 */
static int
dump_get_num(char *dumpdir, int mode)
{
	DIR* dir = NULL;
	struct dirent *dir_ent;
	int dump_found = 0;
	int rc = 0;

	dir=opendir(dumpdir);
	if(!dir){
		PRINT_PERR("Cannot evalute dump number\n");
		rc = -2; goto out;
	}

	while((dir_ent = readdir(dir))){
		int num;
		if(sscanf(dir_ent->d_name,"dump.%ui",&num) == 1){
			/* check if we have something like dump.001       */
			/* this is not treated as dump (leading zeros are */
			/* not allowed) */
			char suffix1[1024] = {0};
			char suffix2[1024] = {0};

			sscanf(dir_ent->d_name,"dump.%s",suffix1);
			sprintf(suffix2,"%i",num);
			if(strcmp(suffix1,suffix2) != 0){
				continue;
			}
			if(num < 0) {
				/* In the unlikely case of 'dump.-1',
				   'dump.-10' etc */
				continue;
			} if(!dump_found){
				dump_found = 1;
				rc = num;
			} else if(mode == DUMP_LAST) {
				rc = MAX(num,rc);
			} else if(mode == DUMP_FIRST) {
				rc = MIN(num,rc);
			} else {
				PRINT_ERR("BUG in %s:%d (unknown mode: %d)\n",
					__FILE__,__LINE__,mode);
				rc = -3;
				goto out;
			}
		}
	}
	if(!dump_found)
		rc = -1;
out:
	if(dir)
		closedir(dir);

	return rc;
}

/*
 * erase_oldest_dump()
 *
 * Erase oldest dump in dump directory
 * Return:    0 - ok
 *          !=0 - error
 */
static int
erase_oldest_dump(void)
{
	int rc = 0;
	int dump_nr;
	char dname[1024] = {0};
	char answer[1024] = {0};

	dump_nr = dump_get_num(g.dump_dir,DUMP_FIRST);
	if(dump_nr < 0){
		PRINT_ERR("BUG: cannot delete dump since number cannot be "
			"evaluated\n");
		rc = -1; goto out;
	}
	if(dump_nr == g.dump_nr){
		PRINT_ERR("Sorry, cannot delete any more dumps!\n");
		PRINT_ERR("No space left on device!\n");
		rc = -1; goto out;
	}
	sprintf(dname,"dump.%i",dump_nr);
	PRINT("No more space left on device!\n");

	if(g.parm_dump_mode==PARM_DUMP_MODE_AUTO_NUM){
		PRINT("Removing oldest dump: '%s'\n",dname);
	} else {
		while((strcmp(answer,"y") != 0) && (strcmp(answer,"n") != 0)){
			PRINT("Remove oldest dump: '%s' (y/n)? ",dname);
			scanf("%s",answer);
		}
		if(strcmp(answer,"n") == 0){
			rc = -1; goto out;
		}
	}
	sprintf(dname, "%s/dump.%i",g.dump_dir,dump_nr);
	if(unlink(dname) == -1){
		PRINT_PERR("Could not remove dump\n");
		rc = -1; goto out;
	}
	sync();
	/* Wait in order to give ext3 time to discover that file has been */
	/* removed.  */
	sleep(SLEEP_TIME_ERASE); 
	PRINT("Dump removed!\n");
out:
	return rc;
}

/*
 * dump_write()
 *
 * write buffer to dump. In case of ENOSPC try to remove oldest dump
 * Parameter: fd    - filedescriptor of dump file
 *            buf   - buffer to write
 *            count - nr of bytes to write
 *
 * Return:    size  - written bytes
 *            <0    - error
 */
static ssize_t 
dump_write(int fd, const void *buf, size_t count)
{
	ssize_t written;

	written=0;
	while(written != count){
		ssize_t rc;
		rc = write(fd,buf+written,count-written);
		if((rc == -1) && (errno == ENOSPC)){
			if(erase_oldest_dump() != 0){
				written = -1;
				goto out;
			}
		} else if (rc == -1) {
			written = -1;
			goto out;
		} else {
			written+=rc;
		}
	}
out:
	return written;
}

/*
 * compress_gzip()
 *
 * Wrapper to gzip compress routine
 * Parameter: old      - buffer to compress (in)
 *            old_size - size of old buffer in bytes (in)
 *            new      - buffer for compressed data (out)
 *            new_size - size of 'new' buffer in bytes (in)
 * Return:    >=0 - Size of compressed buffer
 *            < 0 - error
 */
 
int
compress_gzip(const unsigned char *old, uint32_t old_size, unsigned char *new,
		uint32_t new_size)
{
	int rc;
	unsigned long len = old_size;
	rc = compress(new, &len, old, new_size);
	switch(rc){
	case Z_OK:
		rc = len;
		break;
	case Z_MEM_ERROR:
		PRINT_ERR("Z_MEM_ERROR (not enough memory)!\n");
		rc = -1;
		break;
	case Z_BUF_ERROR:
		/* In this case the compressed output is bigger than
		   the uncompressed */
		rc = -1;
		break;
	case Z_DATA_ERROR:
		PRINT_ERR("Z_DATA_ERROR (input data corrupted)!\n");
		rc = -1;
		break;
	default:
		PRINT_ERR("Z_UNKNOWN_ERROR (rc 0x%x unknown)!\n",rc);
		rc = -1;
		break;
	}
	return rc;
}

/*
 * compress_none()
 *
 * Do nothing! - No compression
 */
int
compress_none(const unsigned char *old, uint32_t old_size, unsigned char *new,
	uint32_t new_size)
{
	return -1;
}

/*
 * s390sa_to_reg_header()
 *
 * Copy info from s390sa header to reg lkcd header
 * Parameter: dh_s390sa - s390 dump header (in)
 *            dh        - lkcd dump header (out)
 */
void
s390sa_to_reg_header(dump_header_s390sa_t* dh_s390sa, dump_header_t* dh)
{
	struct timeval     h_time;

	/* adjust todclock to 1970 */
	uint64_t tod = dh_s390sa->tod;
	tod -= 0x8126d60e46000000LL - (0x3c26700LL * 1000000 * 4096);
	tod >>= 12;
	h_time.tv_sec  = tod / 1000000;
	h_time.tv_usec = tod % 1000000;

	dh->memory_size    = dh_s390sa->memory_size;
	dh->memory_start   = dh_s390sa->memory_start;
	dh->memory_end     = dh_s390sa->memory_end;
	dh->num_dump_pages = dh_s390sa->num_pages;
	dh->page_size      = dh_s390sa->page_size;
	dh->dump_level     = dh_s390sa->dump_level;

	sprintf(dh->panic_string,"zSeries-dump (CPUID = %16"FMT64"x)",
		dh_s390sa->cpu_id);

	if(dh_s390sa->arch_id == DH_ARCH_ID_S390){
		strcpy(dh->utsname_machine,"s390");
	} else if(dh_s390sa->arch_id == DH_ARCH_ID_S390X) {
		strcpy(dh->utsname_machine,"s390x");
	} else {
		strcpy(dh->utsname_machine,"unknown");
	}

	dh->magic_number   = DUMP_MAGIC_NUMBER;
	dh->version        = DUMP_VERSION_NUMBER;
	dh->header_size    = sizeof(dump_header_t);
	dh->time.tv_sec    = h_time.tv_sec;
	dh->time.tv_usec   = h_time.tv_usec;
}

/*
 * dump_display_progress()
 *
 * Write progress information to screen
 * Parameter: bytes_written - So many bytes have been written to the dump
 *            bytes_max     - This is the whole memory to be written
 */
static void
dump_display_progress(uint64_t bytes_written, uint64_t bytes_max)
{
	int    time;
	struct timeval t;
	double percent_written;

	gettimeofday(&t, NULL);
	time = t.tv_sec;
	if ((time < g.last_progress_time) && (bytes_written != bytes_max)
		&& (bytes_written != 0))
		return;
	g.last_progress_time = time + 10;
	percent_written = ((double) bytes_written / (double) bytes_max) * 100.0;
	PRINT(" %4i MB of %4i MB (%5.1f%% )\n", (int)(bytes_written/ONE_MB),
		(int)(bytes_max/ONE_MB), percent_written);
	fflush(stdout);
}

/*
 * dump_create_s390sa()
 * retrieve a s390 (standalone) dump
 *
 * Return:   0  - ok
 *         !=0  - error
 */
static int
dump_create_s390sa(char* sourcedev, char* dumpdir)
{
	struct stat stat_buf;
	dump_header_t dh;
	dump_header_s390sa_t s390_dh;
	compress_fn_t compress_fn;
	dump_page_t dp;
	char dump_page_buf[DUMP_BUFFER_SIZE];
	char buf[DUMP_PAGE_SIZE];
	char dpcpage[DUMP_PAGE_SIZE];
	char dump_file_name[1024];
	uint64_t mem_loc;
	uint32_t buf_loc = 0;
	int size, fp_src = 0, fp_dump = 0;
	uint32_t dp_size,dp_flags;
	int rc = 0;

	if(stat(dumpdir, &stat_buf) < 0){
		PRINT_ERR("Specified dump dir '%s' not found!\n",dumpdir);
		rc = -1; goto out;
	} else if(!S_ISDIR(stat_buf.st_mode)){
		PRINT_ERR("Specified dump dir '%s' is not a directory!\n",
			dumpdir);
		rc = -1; goto out;
	}

	/* initialize progress time */
	g.last_progress_time = 0;

	/* get dump number */
	g.dump_nr = dump_get_num(dumpdir,DUMP_LAST);
	if(g.dump_nr == -1)
		g.dump_nr = 0;
	else
		g.dump_nr += 1;

	/* try to open the source device */
	if ((fp_src = open(sourcedev, O_RDONLY, 0)) < 0) {
		PRINT_ERR("open() source device '%s' failed!\n",sourcedev);
		rc = -1; goto out;
	}

	/* make the new filename */
	sprintf(dump_file_name, "%s/dump.%d", dumpdir, g.dump_nr);
	if ((fp_dump = open(dump_file_name, O_CREAT|O_RDWR|O_TRUNC,
		(S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH))) < 0) {
			PRINT_ERR("open() of dump file \"%s\" failed!\n",
				dump_file_name);
			rc = -1; goto out;
	}

	PRINT("dump file: dump.%d\n",g.dump_nr);
	memset(&dh, 0, sizeof(dh));

	/* get the dump header */

	if (lseek(fp_src, 0, SEEK_SET) < 0) {
		PRINT_ERR("Cannot lseek() to get the dump header from the "
			"dump file!\n");
		rc = -1; goto out;
	}
	if (read(fp_src,(char *)&s390_dh, sizeof(s390_dh)) != sizeof(s390_dh)) {
		PRINT_ERR("Cannot read() dump header from dump file!\n");
		rc = -1; goto out;
	}

	s390sa_to_reg_header(&s390_dh,&dh);

	if(strcmp(g.parm_dump_compress,PARM_DUMP_COMPRESS_GZIP) == 0){
		dh.dump_compress = DUMP_COMPRESS_GZIP;
		compress_fn = compress_gzip;
	}
	else{
		dh.dump_compress = DUMP_COMPRESS_NONE;
		compress_fn = compress_none;
	}

	if(g.parm_dump_mem < dh.memory_size){
		/* dump_mem parameter specified: Adjust memory size */
		dh.memory_size = g.parm_dump_mem;
		dh.memory_end  = g.parm_dump_mem;
		dh.num_dump_pages = g.parm_dump_mem / dh.page_size;
	}

	memset(dump_page_buf, 0, DUMP_BUFFER_SIZE);
	memcpy((void *)dump_page_buf, (const void *)&dh, sizeof(dump_header_t));
	if(lseek(fp_dump, 0L, SEEK_SET) < 0) {
		PRINT_ERR("lseek() failed\n");
		rc = -1; goto out;
	}
	if (dump_write(fp_dump, (char *)dump_page_buf, DUMP_BUFFER_SIZE)
		!= DUMP_BUFFER_SIZE) {
		PRINT_ERR("Error: Write dump header failed\n");
		rc = -1; goto out;
	}

	/* write dump */

	mem_loc = 0;
	if(lseek(fp_src, DUMP_HEADER_SZ_S390SA, SEEK_SET) < 0){
		PRINT_ERR("lseek() failed\n");
		rc = -1; goto out;
	}
	while (mem_loc < dh.memory_size) {
		if((!g.hsa_released) && (mem_loc > g.hsa_size)){
			release_hsa();
		}
		if(read(fp_src, buf, DUMP_PAGE_SIZE) != DUMP_PAGE_SIZE){
			PRINT_ERR("read error\n");
			rc = -1; goto out;
		}
		memset(dpcpage, 0, DUMP_PAGE_SIZE);
		/* get the new compressed page size
                 */

		size = compress_fn((unsigned char *)buf, DUMP_PAGE_SIZE,
			(unsigned char *)dpcpage, DUMP_PAGE_SIZE);

		/* if compression failed or compressed was ineffective,
		 * we write an uncompressed page
		 */
		if (size < 0) {
			dp_flags = DUMP_DH_RAW;
			dp_size  = DUMP_PAGE_SIZE;
		} else {
			dp_flags = DUMP_DH_COMPRESSED;
			dp_size  = size;
		}
		dp.address = mem_loc;
		dp.size    = dp_size;
		dp.flags   = dp_flags;
		memcpy((void *)(dump_page_buf + buf_loc), (const void *)&dp,
			sizeof(dump_page_t));
		buf_loc += sizeof(dump_page_t);
		/* copy the page of memory */
		if (dp_flags & DUMP_DH_COMPRESSED) {
			/* copy the compressed page */
			memcpy((void *)(dump_page_buf + buf_loc),
				(const void *)dpcpage, dp_size);
		} else {
			/* copy directly from memory */
			memcpy((void *)(dump_page_buf + buf_loc),
				(const void *)buf, dp_size);
		}
		buf_loc += dp_size;
		if(dump_write(fp_dump, dump_page_buf, buf_loc) != buf_loc){
			PRINT_ERR("write error\n");
			rc = -1; goto out;
		}
		buf_loc = 0;
		mem_loc += DUMP_PAGE_SIZE;
		dump_display_progress(mem_loc, dh.memory_size);
	}

	/* write end marker */

	dp.address = 0x0;
	dp.size    = DUMP_DH_END;
	dp.flags   = 0x0;
	dump_write(fp_dump, dump_page_buf, sizeof(dump_page_t)); 
out:
	if(fp_src != -1)
		close(fp_src);
	if(fp_dump != -1)
		close(fp_dump);
	return rc;
}
