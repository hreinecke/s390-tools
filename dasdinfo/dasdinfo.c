/*
 * This file contains the dasdinfo tool which can be used
 * to get a unique DASD ID.
 *
 * Copyright IBM Corp. 2007
 *
 * Author(s): Volker Sameske (sameske@de.ibm.com)
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <ftw.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include "zt_common.h"

#define READCHUNK 80
#define BLKSSZGET    _IO(0x12,104)
#define DASD_IOCTL_LETTER 'D'
#define BIODASDINFO  _IOR(DASD_IOCTL_LETTER,1,struct dasd_information)
#define BIODASDINFO2 _IOR(DASD_IOCTL_LETTER,3,struct dasd_information2)
#define TEMP_DEV_MAX_RETRIES    1000

#define MAX(x,y) ((x)<(y)?(y):(x))

static const char tool_name[] = "dasdinfo: zSeries DASD information program";
static const char copyright_notice[] = "Copyright IBM Corp. 2007";

/* needed because ftw can not pass arbitrary arguments */
static char *searchbusid;
static char *busiddir;

struct volume_label {
	char volkey[4];
	char vollbl[4];
	char volid[6];
} __attribute__ ((packed));

struct dasd_information2 {
	unsigned int devno;
	unsigned int real_devno;
	unsigned int schid;
	unsigned int cu_type  : 16;
	unsigned int cu_model :  8;
	unsigned int dev_type : 16;
	unsigned int dev_model : 8;
	unsigned int open_count;
	unsigned int req_queue_len;
	unsigned int chanq_len;
	char type[4];
	unsigned int status;
	unsigned int label_block;
	unsigned int FBA_layout;
	unsigned int characteristics_size;
	unsigned int confdata_size;
	char characteristics[64];
	char configuration_data[256];
	unsigned int format;
	unsigned int features;
	unsigned int reserved0;
	unsigned int reserved1;
	unsigned int reserved2;
	unsigned int reserved3;
	unsigned int reserved4;
	unsigned int reserved5;
	unsigned int reserved6;
	unsigned int reserved7;
};

struct dasd_information {
	unsigned int devno;
	unsigned int real_devno;
	unsigned int schid;
	unsigned int cu_type  : 16;
	unsigned int cu_model :  8;
	unsigned int dev_type : 16;
	unsigned int dev_model : 8;
	unsigned int open_count;
	unsigned int req_queue_len;
	unsigned int chanq_len;
	char type[4];
	unsigned int status;
	unsigned int label_block;
	unsigned int FBA_layout;
	unsigned int characteristics_size;
	unsigned int confdata_size;
	char characteristics[64];
	char configuration_data[256];
};

struct dasd_data
{
	struct dasd_information2 dasd_info;
	int dasd_info_version;
	int blksize;
};

static void dinfo_print_version (void)
{
	printf ("%s version %s\n", tool_name, RELEASE_STRING);
	printf ("%s\n", copyright_notice);
}

static void dinfo_print_usage(char *cmd)
{
	printf(""
	       "Display specific information about a specified DASD device.\n"
	       "\n"
	       "Usage: %s [-a] [-u] [-x] [-l] [-e]\n"
	       "                {-i <busid> | -b <blockdev> | -d <devnode>}\n"
	       "       %s [-h] [-v]\n"
	       "\n"
	       "where:\n"
	       "    -a|--all\n"
	       "             same as -u -x -l\n"
	       "    -u|--uid\n"
	       "             print DASD uid (without z/VM minidisk token)\n"
	       "    -x|--extended-uid\n"
	       "             print DASD uid (including z/VM minidisk token)\n"
	       "    -l|--label\n"
	       "             print DASD volume label (volser)\n"
	       "    -i|--busid <busid>\n"
	       "             bus ID, e.g. 0.0.e910\n"
	       "    -b|--block <blockdev>\n"
	       "             block device name, e.g. dasdb\n"
	       "    -d|--devnode <devnode>\n"
	       "             device node, e.g. /dev/dasda\n"
	       "    -e|--export\n"
	       "             print all values (ID_BUS, ID_TYPE, ID_SERIAL)\n"
	       "    -h|--help\n"
	       "             prints this usage text\n"
	       "    -v|--version\n"
	       "             prints the version number\n"
	       "\n"
	       "Example: %s -u -b dasda\n",
	       cmd,cmd,cmd);
}

static char EBCtoASC[256] =
{
/* 0x00  NUL   SOH   STX   ETX  *SEL    HT  *RNL   DEL */
	0x00, 0x01, 0x02, 0x03, 0x07, 0x09, 0x07, 0x7F,
/* 0x08  -GE  -SPS  -RPT    VT    FF    CR    SO    SI */
	0x07, 0x07, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
/* 0x10  DLE   DC1   DC2   DC3  -RES   -NL    BS  -POC */
	0x10, 0x11, 0x12, 0x13, 0x07, 0x0A, 0x08, 0x07,
/* 0x18  CAN    EM  -UBS  -CU1  -IFS  -IGS  -IRS  -ITB */
	0x18, 0x19, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
/* 0x20  -DS  -SOS    FS  -WUS  -BYP    LF   ETB   ESC */
	0x07, 0x07, 0x1C, 0x07, 0x07, 0x0A, 0x17, 0x1B,
/* 0x28  -SA  -SFE   -SM  -CSP  -MFA   ENQ   ACK   BEL */
	0x07, 0x07, 0x07, 0x07, 0x07, 0x05, 0x06, 0x07,
/* 0x30 ----  ----   SYN   -IR   -PP  -TRN  -NBS   EOT */
	0x07, 0x07, 0x16, 0x07, 0x07, 0x07, 0x07, 0x04,
/* 0x38 -SBS   -IT  -RFF  -CU3   DC4   NAK  ----   SUB */
	0x07, 0x07, 0x07, 0x07, 0x14, 0x15, 0x07, 0x1A,
/* 0x40   SP   RSP           ?              ----       */
	0x20, 0xFF, 0x83, 0x84, 0x85, 0xA0, 0x07, 0x86,
/* 0x48                      .     <     (     +     | */
	0x87, 0xA4, 0x9B, 0x2E, 0x3C, 0x28, 0x2B, 0x7C,
/* 0x50    &                                      ---- */
	0x26, 0x82, 0x88, 0x89, 0x8A, 0xA1, 0x8C, 0x07,
/* 0x58          ?     !     $     *     )     ;       */
	0x8D, 0xE1, 0x21, 0x24, 0x2A, 0x29, 0x3B, 0xAA,
/* 0x60    -     /  ----     ?  ----  ----  ----       */
	0x2D, 0x2F, 0x07, 0x8E, 0x07, 0x07, 0x07, 0x8F,
/* 0x68             ----     ,     %     _     >     ? */
	0x80, 0xA5, 0x07, 0x2C, 0x25, 0x5F, 0x3E, 0x3F,
/* 0x70  ---        ----  ----  ----  ----  ----  ---- */
	0x07, 0x90, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
/* 0x78    *     `     :     #     @     '     =     " */
	0x70, 0x60, 0x3A, 0x23, 0x40, 0x27, 0x3D, 0x22,
/* 0x80    *     a     b     c     d     e     f     g */
	0x07, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
/* 0x88    h     i              ----  ----  ----       */
	0x68, 0x69, 0xAE, 0xAF, 0x07, 0x07, 0x07, 0xF1,
/* 0x90    ?     j     k     l     m     n     o     p */
	0xF8, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,
/* 0x98    q     r                    ----        ---- */
	0x71, 0x72, 0xA6, 0xA7, 0x91, 0x07, 0x92, 0x07,
/* 0xA0          ~     s     t     u     v     w     x */
	0xE6, 0x7E, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
/* 0xA8    y     z              ----  ----  ----  ---- */
	0x79, 0x7A, 0xAD, 0xAB, 0x07, 0x07, 0x07, 0x07,
/* 0xB0    ^                    ----     ?  ----       */
	0x5E, 0x9C, 0x9D, 0xFA, 0x07, 0x07, 0x07, 0xAC,
/* 0xB8       ----     [     ]  ----  ----  ----  ---- */
	0xAB, 0x07, 0x5B, 0x5D, 0x07, 0x07, 0x07, 0x07,
/* 0xC0    {     A     B     C     D     E     F     G */
	0x7B, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
/* 0xC8    H     I  ----           ?              ---- */
	0x48, 0x49, 0x07, 0x93, 0x94, 0x95, 0xA2, 0x07,
/* 0xD0    }     J     K     L     M     N     O     P */
	0x7D, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
/* 0xD8    Q     R  ----           ?                   */
	0x51, 0x52, 0x07, 0x96, 0x81, 0x97, 0xA3, 0x98,
/* 0xE0    \           S     T     U     V     W     X */
	0x5C, 0xF6, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
/* 0xE8    Y     Z        ----     ?  ----  ----  ---- */
	0x59, 0x5A, 0xFD, 0x07, 0x99, 0x07, 0x07, 0x07,
/* 0xF0    0     1     2     3     4     5     6     7 */
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
/* 0xF8    8     9  ----  ----     ?  ----  ----  ---- */
	0x38, 0x39, 0x07, 0x07, 0x9A, 0x07, 0x07, 0x07
};

static char *dinfo_ebcdic_dec (char *source, char *target, int l)
{
	int i;

	for (i = 0; i < l; i++)
		target[i]=EBCtoASC[(unsigned char)(source[i])];

	return target;
}

static int dinfo_read_dasd_uid (char *uidfile, char *readbuf, int readbuflen)
{
	FILE *dasduid;
	int offset = 0;

	if ((dasduid = fopen(uidfile,"r")) == NULL)
		return -1;

	while (fgets(readbuf + offset, READCHUNK, dasduid)  &&
	       readbuf[strlen(readbuf)-1] != '\n' ) {
		offset += READCHUNK-1;
		if ( offset+READCHUNK-1 >= readbuflen ) {
			readbuf = realloc(readbuf,
					  readbuflen + READCHUNK-1);
			readbuflen += READCHUNK-1;
		}
	}
	fclose(dasduid);

	if (strlen(readbuf) <= 1)
		return -1;

	return 0;
}

static int dinfo_read_dasd_vlabel (char *device, struct volume_label *vlabel,
				   char *readbuf)
{
	struct dasd_data data;
	struct volume_label tmp;
	int vlsize = sizeof(struct volume_label);
	unsigned long vlabel_start;
	char vollbl[5];
	int f;
	char *space;

	if ((f = open(device, O_RDONLY)) < 0) {
		printf("Could not open device node.\n");
		goto error;
	}

	if (ioctl(f, BLKSSZGET, &data.blksize) != 0) {
		printf("Unable to figure out block size.\n");
		goto error;
	}

	if (ioctl(f, BIODASDINFO2, &data.dasd_info) == 0)
		data.dasd_info_version = 2;
	else {
		if (ioctl(f, BIODASDINFO, &data.dasd_info) != 0) {
			printf("Unable to figure out DASD informations.\n");
			goto error;
		}
	}

	vlabel_start = data.dasd_info.label_block * data.blksize;
	if (lseek(f, vlabel_start, SEEK_SET) < 0)
		goto error;

	bzero(vlabel, vlsize);

	if (read(f, vlabel, vlsize) != vlsize) {
		printf("Could not read volume label.\n");
		goto error;
	}

	if (data.dasd_info.FBA_layout) {
		bzero(&tmp, vlsize);
		memcpy(&tmp, vlabel, vlsize);
		memcpy(vlabel->vollbl, &tmp, vlsize-4);
	}

	close(f);

	bzero(readbuf, 7);
	bzero(vollbl, 5);
	strncpy(vollbl, vlabel->vollbl, 4);
	dinfo_ebcdic_dec(vollbl, vollbl, 4);

	if ((strncmp(vollbl, "VOL1", 4) == 0) ||
	    (strncmp(vollbl, "LNX1", 4) == 0) ||
	    (strncmp(vollbl, "CMS1", 4) == 0)) {
		strncpy(readbuf, vlabel->volid, 6);
		dinfo_ebcdic_dec(readbuf, readbuf, 6);
		space = strchr(readbuf,' ');
		if (space)
			*space = 0;
	} else
		strcpy(readbuf, "");


	return 0;
error:
	close(f);
	return -1;
}

static void *dinfo_malloc(size_t size)
{
	void *result;

	result = malloc(size);
	if (result == NULL) {
		printf("Could not allocate %lld bytes of memory",
			(unsigned long long) size);
	}
	return result;
}

static char *dinfo_make_path(char *dirname, char *filename)
{
	char *result;
	size_t len;

	len = strlen(dirname) + strlen(filename) + 2;
	result = (char *) dinfo_malloc(len);
	if (result == NULL)
		return NULL;
	sprintf(result, "%s/%s", dirname, filename);
	return result;
}

static int dinfo_create_devnode(dev_t dev, char **devno)
{
	char *result;
	char * pathname[] = { "/dev", getenv("TMPDIR"), "/tmp",
				getenv("HOME"), "." , "/"};
	char filename[] = "dasdinfo0000";
	mode_t mode;
	unsigned int path;
	int retry;
	int rc;
	int fd;

	mode = S_IFBLK | S_IRWXU;

	/* Try several locations for the temporary device node. */
	for (path=0; path < sizeof(pathname) / sizeof(pathname[0]); path++) {
		if (pathname[path] == NULL)
			continue;
		for (retry=0; retry < TEMP_DEV_MAX_RETRIES; retry++) {
			sprintf(filename, "dasdinfo%04d", retry);
			result = dinfo_make_path(pathname[path], filename);
			if (result == NULL)
				return -1;
			rc = mknod(result, mode, dev);
			if (rc == 0) {
				/* Need this test to cover
				 * 'nodev'-mounted
				 * filesystems. */
				fd = open(result, O_RDONLY);
				if (fd != -1) {
					close(fd);
					*devno = result;
					return 0;
				}
				remove(result);
				retry = TEMP_DEV_MAX_RETRIES;
			} else if (errno != EEXIST)
				retry = TEMP_DEV_MAX_RETRIES;
			free(result);
		}
	}
	printf("Error: Unable to create temporary device node");
	return -1;
}

static void dinfo_free_devnode(char *device)
{
	if (remove(device)) {
		printf("Warning: Could not remove "
			"temporary file %s", device);
	}
}

static int dinfo_extract_dev(dev_t *dev, char *str, int readbuflen)
{
	char tmp[readbuflen];
	char *p = NULL;
	int ma, mi;

	bzero(tmp, readbuflen);
	strncpy(tmp, str, readbuflen);
	if ((p = strchr(tmp, ':')) == NULL) {
		printf("Error: unable to extract major/minor\n");
		return -1;
	}

	*p = '\0';
	ma = atoi(tmp);
	mi = atoi(p + sizeof(char));

	*dev = makedev(ma, mi);

	return 0;
}

static int dinfo_get_dev_from_blockdev(char *blockdev, dev_t *dev)
{
	FILE *dasddev;
	int offset = 0;
	char *devfile = NULL;
	char *readbuf = NULL;
	int readbuflen = READCHUNK;

	if ((devfile = dinfo_malloc(readbuflen)) == NULL)
		return -1;

	sprintf(devfile,"/sys/block/%s/dev", blockdev);

	if ((readbuf = dinfo_malloc(readbuflen)) == NULL) {
		printf("Error: Not enough memory to allocate readbuffer\n");
		return -1;
	}

	if ((dasddev = fopen(devfile,"r")) == NULL)
		return -1;

	while (fgets(readbuf + offset, READCHUNK, dasddev)  &&
	       readbuf[strlen(readbuf)-1] != '\n' ) {
		offset += READCHUNK-1;
		if (offset+READCHUNK-1 >= readbuflen) {
			readbuf = realloc(readbuf,
					  readbuflen + READCHUNK-1);
			readbuflen += READCHUNK-1;
		}
	}
	fclose(dasddev);

	if (dinfo_extract_dev(dev, readbuf, readbuflen) != 0)
		return -1;

	return 0;
}

static int
dinfo_is_busiddir(const char *fpath, const struct stat *UNUSED(sb),
		  int tflag, struct FTW *ftwbuf)
{
	char *tempdir;
	char linkdir[128];
	ssize_t i;
	if (tflag != FTW_D || (strncmp((fpath + ftwbuf->base), searchbusid,
				       strlen(searchbusid)) != 0))
		return FTW_CONTINUE;
	/*
	 * ensure that the found entry is a busid and not a
	 * subchannel ID
	 * for large systems subchannel IDs may look like busids
	 */
	if (asprintf(&tempdir, "%s/driver", fpath) < 0)
		return -1;
	i = readlink(tempdir, linkdir, 128);
	free(tempdir);
	if ((i < 0) || (i >= 128))
		return -1;
	/* append '\0' because readlink returns non zero terminated string */
	tempdir[i+1] = '\0';
	if (strstr(linkdir, "dasd") == NULL)
		return FTW_CONTINUE;
	free(busiddir);
	busiddir = strdup(fpath);
	if (busiddir == NULL)
		return -1;
	return FTW_STOP;
}

static int
dinfo_find_entry(const char *dir, const char *searchstring,
		 char type, char **result)
{
	DIR *directory = NULL;
	struct dirent *dir_entry = NULL;
	directory = opendir(dir);
	if (directory == NULL)
		return -1;
	while ((dir_entry = readdir(directory)) != NULL) {
		/* compare if the found entry has exactly the same name
		   and type as searched */
		if ((strncmp(dir_entry->d_name, searchstring,
			     strlen(searchstring)) == 0)
		    && (dir_entry->d_type & type)) {
			*result = strdup(dir_entry->d_name);
			if (*result == NULL)
				goto out;
			closedir(directory);
			return 0; /* found */
		}
	}
out:
	closedir(directory);
	return -1; /* nothing found or error */
}

static int
dinfo_get_blockdev_from_busid(char *busid, char **blkdev)
{
	int flags = FTW_PHYS; /* do not follow links */
	int rc = -1;

	char *tempdir = NULL;
	char *result = NULL;
	char *sysfsdir = "/sys/devices/";

	/* dinfo_is_devnode needs to know the busid */
	searchbusid = busid;
	if (nftw(sysfsdir, dinfo_is_busiddir, 200, flags) != FTW_STOP)
		goto out;

	/*
	 * new sysfs: busid directory  contains a directory 'block'
	 * which contains a directory 'dasdXXX'
	 */
	rc = dinfo_find_entry(busiddir, "block", DT_DIR, &result);
	if (rc == 0) {
		if (asprintf(&tempdir, "%s/%s/", busiddir, result) < 0) {
			rc = -1;
			goto out2;
		}
		rc = dinfo_find_entry(tempdir, "dasd", DT_DIR, blkdev);
	} else {
		/*
		 * old sysfs: entry for busiddir contain a link
		 * 'block:dasdXXX'
		 */
		rc = dinfo_find_entry(busiddir, "block:", DT_LNK, &result);
		if (rc != 0)
			goto out2;
		*blkdev = strdup(strchr(result, ':') + 1);
		if (*blkdev == NULL)
			rc = -1;
	}

out:
	free(tempdir);
out2:
	free(busiddir);
	free(result);
	return rc;
}

static int dinfo_get_uid_from_devnode(char **uidfile, char *devnode)
{
	struct stat stat_buffer;
	char stat_dev[READCHUNK];
	char sys_dev_path[READCHUNK];
	char *readbuf;
	DIR *directory = NULL;
	struct dirent *dir_entry = NULL;
	FILE *block_dev;
	int readbuflen = READCHUNK;
	int offset;

	if (stat(devnode, &stat_buffer) != 0) {
		printf("Error: could not stat %s\n", devnode);
		return -1;
	}

	sprintf(stat_dev, "%d:%d", major(stat_buffer.st_rdev),
		minor(stat_buffer.st_rdev));

	if ((directory = opendir("/sys/block/")) == NULL) {
		printf("Error: could not open directory /sys/block\n");
		return -1;
	}

	if ((readbuf = dinfo_malloc(readbuflen)) == NULL) {
		printf("Error: Not enough memory to allocate readbuffer\n");
		return -1;
	}

	while ((dir_entry = readdir(directory)) != NULL) {
		sprintf(sys_dev_path, "/sys/block/%s/dev", dir_entry->d_name);

		if ((block_dev = fopen(sys_dev_path,"r")) == NULL)
			continue;

		offset = 0;
		while (fgets(readbuf + offset, READCHUNK, block_dev)  &&
		       readbuf[strlen(readbuf)-1] != '\n' ) {
			offset += READCHUNK-1;
			if ( offset+READCHUNK-1 >= readbuflen ) {
				readbuf = realloc(readbuf,
						  readbuflen + READCHUNK-1);
				readbuflen += READCHUNK-1;
			}
		}
		fclose(block_dev);

		if (strncmp(stat_dev, readbuf,
			    MAX(strlen(stat_dev), strlen(readbuf)-1)) == 0) {
			sprintf(*uidfile,"/sys/block/%s/device/uid",
				dir_entry->d_name);
			break;
		}
	}

	closedir(directory);
	return 0;
}

int main(int argc, char * argv[])
{
	struct utsname uname_buf;
	int version, release;
	char *uidfile = NULL;
	char *device = NULL;
	char *readbuf = NULL;
	int readbuflen = READCHUNK;
	dev_t dev;
	int export = 0;
	int c;
	int print_uid = 0;
	int print_extended_uid = 0;
	int print_vlabel = 0;
	char *blockdev = NULL;
	char *busid = NULL;
	char *devnode = NULL;
	struct volume_label vlabel;
	char *srchuid;
	int i, rc = 0;

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{"all",          0, 0, 'a'},
			{"uid",          0, 0, 'u'},
			{"extended-uid", 0, 0, 'x'},
			{"label",        0, 0, 'l'},
			{"busid",        1, 0, 'i'},
			{"block",        1, 0, 'b'},
			{"devnode",      1, 0, 'd'},
			{"export",       0, 0, 'e'},
			{"help",         0, 0, 'h'},
			{"version",      0, 0, 'v'},
			{0, 0, 0, 0}
		};

		c = getopt_long (argc, argv, "vhaeuxlb:i:d:",
				 long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'a':
			print_uid = 1;
			print_vlabel = 1;
			print_extended_uid = 1;
			break;
		case 'u':
			print_uid = 1;
			break;
		case 'x':
			print_extended_uid = 1;
			break;
		case 'l':
			print_vlabel = 1;
			break;
		case 'i':
			busid=strdup(optarg);
			break;
		case 'b':
			blockdev=strdup(optarg);
			break;
		case 'd':
			devnode=strdup(optarg);
			break;
		case 'e':
			export = 1;
			break;
		case 'h':
			dinfo_print_usage(argv[0]);
			exit(0);
		case 'v':
			dinfo_print_version();
			exit(0);
		default:
			fprintf(stderr, "Try 'dasdinfo --help' for more "
				"information.\n");
			exit(1);
		}
	}

	uname(&uname_buf);
	sscanf(uname_buf.release, "%d.%d", &version,&release);
	if (strcmp(uname_buf.sysname,"Linux") ||
	    version < 2 || (version == 2 && release < 6)) {
		printf("%s %d.%d is not supported\n", uname_buf.sysname,
			version,release);
		exit(1);
	}

	if (!busid && !blockdev && !devnode) {
		printf("Error: please specify a device using either -b, -i "
		       "or -d\n");
		exit(1);
	}

	if ((busid && blockdev) || (busid && devnode) || (blockdev && devnode)) {
		printf("Error: please specify device only once,  either -b, -i "
		       "or -d\n");
		exit(1);
	}

	if (!print_uid && !print_extended_uid && !print_vlabel) {
		printf("Error: no action specified (e.g. -u)\n");
		exit(1);
	}

	if (((readbuf = dinfo_malloc(readbuflen)) == NULL) ||
	    ((uidfile = dinfo_malloc(readbuflen)) == NULL))
		exit(1);

	/* try to read the uid attribute */
	if (busid) {
		sprintf(uidfile,"/sys/bus/ccw/devices/%s/uid", busid);
	} else if (blockdev) {
		sprintf(uidfile,"/sys/block/%s/device/uid", blockdev);
	} else if (devnode) {
		if (dinfo_get_uid_from_devnode(&uidfile, devnode) != 0)
			goto error;
	}

	if (export) {
		printf("ID_BUS=ccw\n");
		printf("ID_TYPE=disk\n");
	}

	if (print_uid) {
		if (dinfo_read_dasd_uid(uidfile, readbuf, readbuflen) == 0) {
			/* look for the 4th '.' and cut there */
			srchuid = readbuf - 1;
			for (i = 0; i < 4; ++i) {
				srchuid = index(srchuid + 1, '.');
				if (!srchuid)
					break;
			}
			if (srchuid) {
				srchuid[0] = '\n';
				srchuid[1] = 0;
			}
			if (export) {
				printf("ID_UID=%s",readbuf);
			} else
				printf("%s",readbuf);
			if (!print_vlabel && !print_extended_uid)
				goto out;
		}
	}

	if (print_extended_uid) {
		if (dinfo_read_dasd_uid(uidfile, readbuf, readbuflen) == 0) {
			if (export) {
				printf("ID_XUID=%s",readbuf);
			} else
				printf("%s",readbuf);
			if (!print_vlabel)
				goto out;
		}
	}

	/* there is no uid, try to read the volume serial */
	if (busid) {
		char *blockdev_name = NULL;

		if (dinfo_get_blockdev_from_busid(busid, &blockdev_name) != 0)
			goto error;

		if (dinfo_get_dev_from_blockdev(blockdev_name, &dev) != 0)
			goto error;

		if (dinfo_create_devnode(dev, &device) != 0)
			goto error;

		free(blockdev_name);

	} else if (blockdev) {
		if (dinfo_get_dev_from_blockdev(blockdev, &dev) != 0)
			goto error;

		if (dinfo_create_devnode(dev, &device) != 0)
			goto error;

	} else if (devnode) {
		if ((device = dinfo_malloc(readbuflen)) == NULL)
			exit(1);
		strcpy(device, devnode);
	}

	if (dinfo_read_dasd_vlabel(device, &vlabel, readbuf) == 0) {
		if (export) {
			printf("ID_SERIAL=%s\n",readbuf);
		} else
			printf("%s\n", readbuf);
		goto out;
	}

error:
	printf("Error: could not read unique DASD ID\n");
	rc = 1;

out:
	if (device && (busid || blockdev))
		dinfo_free_devnode(device);

	free(uidfile);
	free(device);
	free(readbuf);

	exit(rc);
}
