/*
 * File...........: s390-tools/dasdview/dasdview.c
 * Author(s)......: Volker Sameske <sameske@de.ibm.com>
 *                  Gerhard Tonn   <ton@de.ibm.com>
 * Copyright IBM Corp. 2001, 2006.
 */

#define _LARGEFILE64_SOURCE    /* needed for unistd.h */
#define _FILE_OFFSET_BITS 64   /* needed for unistd.h */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <stdarg.h>
#include <ctype.h>

#include <sys/stat.h>
#include <sys/ioctl.h>

#include <sys/utsname.h>
#include <linux/version.h>

#include "zt_common.h"
#include "vtoc.h"
#include "dasdview.h"
#include "u2s.h"

/* Characters per line */
#define DASDVIEW_CPL 16

/* Full tool name */
static const char tool_name[] = "dasdview: zSeries DASD view program";

/* Copyright notice */
static const char copyright_notice[] = "Copyright IBM Corp. 2001, 2006";

/* Error message string */
#define ERROR_STRING_SIZE       1024
static char error_string[ERROR_STRING_SIZE];

/*
 * Generate and print an error message based on the formatted
 * text string FMT and a variable amount of extra arguments.
 */
void
zt_error_print (const char* fmt, ...)
{
	va_list args;

	va_start (args, fmt);
	vsnprintf (error_string, ERROR_STRING_SIZE, fmt, args);
	va_end (args);

	fprintf (stderr, "Error: %s\n", error_string);
}

/*
 * Print version information.
 */
static void
print_version (void)
{
	printf ("%s version %s\n", tool_name, RELEASE_STRING);
	printf ("%s\n", copyright_notice);
}

static void
dasdview_usage(void)
{
	printf("\nprints DASD information:\n\n");
	printf("dasdview [-b begin] [-s size] [-1|-2] \n"
	       "         [-i] [-x] [-j] [-c]\n"
	       "         [-l] [-t {info|f1|f4|f5|f7|f8|f9|all}] \n"
	       "         [-h] [-v] \n"
	       "         {-n devno|-f node} device\n"
	       "\nwhere:\n"
	       "-b: prints a DASD dump from 'begin'\n"
	       "-s: prints a DASD dump with size 'size'\n"
	       "-1: use format 1 for the dump (default)\n"
	       "-2: use format 2 for the dump\n"
	       "    'begin' and 'size' can have the following format:\n"
	       "    x[k|m|b|t|c]\n"
	       "    for x byte, kilobyte, megabyte, blocks, tracks and\n"
	       "    cylinders\n"
	       "-i: prints general DASD information and geometry\n"
	       "-x: prints extended DASD information\n"
	       "-j: prints the volume serial number (volume identifier)\n"
	       "-l: prints information about the volume label\n"
	       "-t: prints the table of content (VTOC)\n"
	       "-c: prints the characteristics of a device\n"
	       "-h: prints this usage text\n"
	       "-v: prints the version number\n"
	       "-n: specifies the device by a device number (needs devfs)\n"
	       "-f: specifies a device by a device node\n");
}


static void
dasdview_error(enum dasdview_failure why)
{
        char    error[ERROR_STRING_SIZE];

	switch (why)
	{
        case open_error:
	        snprintf(error, ERROR_STRING_SIZE, "%s open error\n%s\n",
			DASDVIEW_ERROR, error_str);
		break;
        case seek_error:
	        snprintf(error, ERROR_STRING_SIZE, "%s seek error\n%s\n",
			DASDVIEW_ERROR, error_str);
		break;
        case read_error:
	        snprintf(error, ERROR_STRING_SIZE, "%s read error\n%s\n",
			DASDVIEW_ERROR, error_str);
		break;
        case ioctl_error:
	        snprintf(error, ERROR_STRING_SIZE, "%s ioctl error\n%s\n",
			DASDVIEW_ERROR, error_str);
		break;
        case usage_error:
	        snprintf(error, ERROR_STRING_SIZE, "%s usage error\n%s\n",
			DASDVIEW_ERROR, error_str);
		break;
        case disk_layout:
	        snprintf(error, ERROR_STRING_SIZE, "%s disk layout error\n%s\n",
			DASDVIEW_ERROR, error_str);
		break;
        case vtoc_error:
	        snprintf(error, ERROR_STRING_SIZE, "%s VTOC error\n%s\n",
			DASDVIEW_ERROR, error_str);
		break;
	default:
	        snprintf(error,ERROR_STRING_SIZE, "%s bug\n%s\n",
			DASDVIEW_ERROR, error_str);
	}

	fputc('\n', stderr);
	fputs(error, stderr);

	exit(-1);
}


/*
 * replace special characters with dots and question marks
 */
static void
dot (char label[]) {

        int i;
	char c;

  	for (i = 0; i < 16; i++) {

	        c = label[i];
		if (c <= 0x20) label[i] = '?';
		if (c == 0x00) label[i] = '.';
		if (c == 0x60) label[i] = '?';
		if (c >= 0x7f) label[i] = '?';
	}
}

static void
dasdview_get_info(dasdview_info_t *info)
{
	int fd;
	struct dasd_eckd_characteristics *characteristics;

	fd = open(info->device, O_RDONLY);
	if (fd == -1)
	{
		zt_error_print("dasdview: open error\n" \
			"Could not open device '%s'.\n"
			"Maybe you are using the -n option without devfs or \n"
			"you have specified an unknown device or \n"
			"you are not authorized to do that.\n",
			info->device);
		exit(-1);
	}

	/* get disk geometry */
	if (ioctl(fd, HDIO_GETGEO, &info->geo) != 0)
	{
	        close(fd);
		zt_error_print("dasdview: ioctl error\n" \
			"Could not retrieve disk geometry " \
			"information.");
		exit(-1);
	}

	if (ioctl(fd, BLKSSZGET, &info->blksize) != 0)
	{
	        close(fd);
		zt_error_print("dasdview: ioctl error\n" \
			"Could not retrieve blocksize information!\n");
		exit(-1);
	}

	/* get disk information */
	if (ioctl(fd, BIODASDINFO2, &info->dasd_info) == 0) {
		info->dasd_info_version = 2;
	} else {
		/* INFO2 failed - try INFO using the same (larger) buffer */
		if (ioctl(fd, BIODASDINFO, &info->dasd_info) != 0) {
			close(fd);
			zt_error_print("dasdview: ioctl error\n"	\
				       "Could not retrieve disk information.");
			exit(-1);
		}
	}

	characteristics = (struct dasd_eckd_characteristics *)
		&info->dasd_info.characteristics;
	if (characteristics->no_cyl == LV_COMPAT_CYL &&
	    characteristics->long_no_cyl)
		info->hw_cylinders = characteristics->long_no_cyl;
	else
		info->hw_cylinders = characteristics->no_cyl;


	close(fd);
}


static void
dasdview_parse_input(unsigned long long *p, dasdview_info_t *info, char *s)
{
	unsigned long long l;
	char *endp;
	char suffix;

	l = strtoull(s, &endp, 0);
	if ((endp == s) || ((l+1) == 0))
		goto error;

	if (*endp) {
		if (!strchr("kmtbcKMTBC", *endp) || *(endp+1))
			goto error;
		suffix = tolower(*endp);
	} else
		suffix = 0;
	switch (suffix) {
	case 'k':
		l *= 1024LL;
		break;
	case 'm':
		l *= 1024LL * 1024LL;
		break;
	case 't':
		l *= (unsigned long long) info->blksize *
		     (unsigned long long) info->geo.sectors;
		break;
	case 'b':
		l *= (unsigned long long) info->blksize;
		break;
	case 'c':
		l *= (unsigned long long) info->blksize *
		     (unsigned long long) info->geo.sectors *
		     (unsigned long long) info->geo.heads;
		break;
	default:
		break;
	}
	*p = l;

	return;
error:
	zt_error_print("dasdview: usage error\n"
		       "%s is not a valid begin/size value!", s);
	exit(-1);
}

/*
 * Print general DASD information.
 */
static void
dasdview_print_general_info(dasdview_info_t *info)
{
	printf("\n--- general DASD information -----------------" \
	       "---------------------------------\n");
	printf("device node            : %s\n",info->device);
#ifdef SYSFS
	struct utsname buf;
	unsigned char a,b,c;
	char suffix[sizeof(buf.release)];
	int rc;
	char busid[U2S_BUS_ID_SIZE];

	rc = uname(&buf);
	if(!rc)
	{
		sscanf(buf.release, "%c.%c.%c-%s", &a, &b, &c, suffix);
		if(KERNEL_VERSION(2,5,0) <= KERNEL_VERSION(a, b, c))
		{
			if(u2s_getbusid(info->device, busid) == -1)
				printf("busid                  :"
				       " <not found>\n");
			else
				printf("busid                  : %s\n", busid);
		}
		else
		{
#endif
			printf("device number          : hex %x  \tdec %d\n",
			       info->dasd_info.devno,
			       info->dasd_info.devno);
#ifdef SYSFS
		}
	}
#endif
	printf("type                   : %4s\n", info->dasd_info.type);
	printf("device type            : hex %x  \tdec %d\n",
	       info->dasd_info.dev_type,
	       info->dasd_info.dev_type);
	printf("\n--- DASD geometry ----------------------------" \
	       "---------------------------------\n");
	printf("number of cylinders    : hex %x  \tdec %d\n",
	       info->hw_cylinders,
	       info->hw_cylinders);
	printf("tracks per cylinder    : hex %x  \tdec %d\n",
	       info->geo.heads,
	       info->geo.heads);
	printf("blocks per track       : hex %x  \tdec %d\n",
	       info->geo.sectors,
	       info->geo.sectors);
	printf("blocksize              : hex %x  \tdec %d\n",
	       info->blksize,
	       info->blksize);
}

/*
 * Loop over the given character array and HEXdump the content.
 */
static inline void
dasdview_dump_array(char *name, int size, unsigned char *addr)
{
	int i;

	for (i = 0; i < size; i++) {
		if (i % DASDVIEW_CPL == 0) {
			if (i == 0)
				printf("%-23.23s: ", name);
			else
				printf("\n%25s", "");
		} else {
			if (i % 8 == 0) printf(" ");
			if (i % 4 == 0) printf(" ");
		}
		printf("%02x", addr[i]);
	}
	printf("\n");
}

/*
 * Print extended DASD information.
 */
static void
dasdview_print_extended_info(dasdview_info_t *info)
{
	unsigned int i;
	struct dasd_information2_t *dasd_info;
	struct {
		unsigned int mask;
		char *name;
	} flist[2] = {{DASD_FEATURE_READONLY, "ro"  },
		      {DASD_FEATURE_USEDIAG,  "diag"}};

	dasd_info = &info->dasd_info;
	printf("\n--- extended DASD information ----------------" \
	       "---------------------------------\n");
        printf("real device number     : hex %x  \tdec %d\n",
	       dasd_info->real_devno, dasd_info->real_devno);
	printf("subchannel identifier  : hex %x  \tdec %d\n",
	       dasd_info->schid, dasd_info->schid);
	printf("CU type  (SenseID)     : hex %x  \tdec %d\n",
	       dasd_info->cu_type, dasd_info->cu_type);
	printf("CU model (SenseID)     : hex %x  \tdec %d\n",
	       dasd_info->cu_model, dasd_info->cu_model);
	printf("device type  (SenseID) : hex %x  \tdec %d\n",
	       dasd_info->dev_type, dasd_info->dev_type);
	printf("device model (SenseID) : hex %x  \tdec %d\n",
	       dasd_info->dev_model, dasd_info->dev_model);
	printf("open count             : hex %x  \tdec %d\n",
	       dasd_info->open_count, dasd_info->open_count);
        printf("req_queue_len          : hex %x  \tdec %d\n",
	       dasd_info->req_queue_len, dasd_info->req_queue_len);
	printf("chanq_len              : hex %x  \tdec %d\n",
	       dasd_info->chanq_len, dasd_info->chanq_len);
	printf("status                 : hex %x  \tdec %d\n",
	       dasd_info->status, dasd_info->status);
	printf("label_block            : hex %x  \tdec %d\n",
	       dasd_info->label_block, dasd_info->label_block);
	printf("FBA_layout             : hex %x  \tdec %d\n",
	       dasd_info->FBA_layout, dasd_info->FBA_layout);
        printf("characteristics_size   : hex %x  \tdec %d\n",
	       dasd_info->characteristics_size,
	       dasd_info->characteristics_size);
        printf("confdata_size          : hex %x  \tdec %d\n",
	       dasd_info->confdata_size, dasd_info->confdata_size);

	if (info->dasd_info_version >= 2) {
		printf("format                 : hex %x  \tdec %d      \t%s\n",
		       dasd_info->format, dasd_info->format,
		       dasd_info->format == DASD_FORMAT_NONE ?
		       "NOT formatted" :
		       dasd_info->format == DASD_FORMAT_LDL  ?
		       "LDL formatted" :
		       dasd_info->format == DASD_FORMAT_CDL  ?
		       "CDL formatted" : "unknown format");

		printf("features               : hex %x  \tdec %d      \t",
		       dasd_info->features, dasd_info->features);
		if (dasd_info->features == DASD_FEATURE_DEFAULT)
			printf("default\n");
		else {
			for (i = 0; i < (sizeof(flist)/sizeof(flist[0])); i++)
				if (dasd_info->features & flist[i].mask)
					printf("%s ",flist[i].name);
			printf("\n");
		}
	}
	printf("\n");
	dasdview_dump_array("characteristics",
			    dasd_info->characteristics_size,
			    dasd_info->characteristics);
	printf("\n");
	dasdview_dump_array("configuration_data",
			    dasd_info->confdata_size,
			    dasd_info->configuration_data);
}


static void
dasdview_read_vlabel(dasdview_info_t *info, volume_label_t *vlabel)
{
	volume_label_t tmp;
	unsigned long  pos;

	pos = info->dasd_info.label_block * info->blksize;

	bzero(vlabel, sizeof(volume_label_t));
	if ((strncmp(info->dasd_info.type, "ECKD", 4) == 0) &&
	    (!info->dasd_info.FBA_layout)) {
		/* OS/390 and zOS compatible disk layout */
		vtoc_read_volume_label(info->device, pos, vlabel);
	}
	else {
		/* standard LINUX disk layout */
		vtoc_read_volume_label(info->device, pos, &tmp);
		memcpy(vlabel->vollbl, &tmp, sizeof(tmp)-4);
	}
}


static void
dasdview_print_vlabel(dasdview_info_t *info)
{
	volume_label_t vlabel;

	unsigned char s4[5], t4[5], s5[6], t5[6], s6[7], t6[7];
	char s14[15], t14[15], s29[30], t29[30];
	int i;

	dasdview_read_vlabel(info, &vlabel);

	printf("\n--- volume label -----------------------------" \
	       "---------------------------------\n");

	bzero(s4, 5); bzero(t4, 5); strncpy((char *)s4, vlabel.volkey, 4);
	printf("volume label key        : ascii  '%4s'\n", s4);
	vtoc_ebcdic_dec((char *)s4, (char *)t4, 4);
	printf("                        : ebcdic '%4s'\n", t4);
	printf("                        : hex    ");
	for (i=0; i<4; i++) printf("%02x", s4[i]);

	bzero(s4, 5); bzero(s4, 5); strncpy((char *)s4, vlabel.vollbl, 4);
	printf("\n\nvolume label identifier : ascii  '%4s'\n", s4);
	vtoc_ebcdic_dec((char *)s4, (char *)t4, 4);
	printf("                        : ebcdic '%4s'\n", t4);
	printf("                        : hex    ");
	for (i=0; i<4; i++) printf("%02x", s4[i]);

	bzero(s6, 7); bzero(t6, 7); strncpy((char *)s6, vlabel.volid, 6);
	printf("\n\nvolume identifier       : ascii  '%6s'\n", s6);
	vtoc_ebcdic_dec((char *)s6, (char *)t6, 6);
	printf("                        : ebcdic '%6s'\n", t6);
	printf("                        : hex    ");
	for (i=0; i<6; i++) printf("%02x", s6[i]);

	printf("\n\nsecurity byte           : hex    %02x\n", vlabel.security);

	printf("\n\nVTOC pointer            : hex    %04x%04x%02x ",
	       vlabel.vtoc.cc, vlabel.vtoc.hh, vlabel.vtoc.b);
	if ((vlabel.vtoc.cc == 0x4040) && (vlabel.vtoc.hh == 0x4040) &&
	    (vlabel.vtoc.b == 0x40))
		printf("\n");
	else
		printf("\n                                 " \
		       "(cyl %d, trk %d, blk %d)\n\n",
		       vtoc_get_cyl_from_cchhb(&vlabel.vtoc),
		       vtoc_get_head_from_cchhb(&vlabel.vtoc), vlabel.vtoc.b);

	bzero(s5, 6); bzero(t5, 6); strncpy((char *)s5, vlabel.res1, 5);
	printf("reserved                : ascii  '%5s'\n", s5);
	vtoc_ebcdic_dec((char *)s5, (char *)t5, 5);
	printf("                        : ebcdic '%5s'\n", t5);
	printf("                        : hex    ");
	for (i=0; i<5; i++) printf("%02x", s5[i]);

	bzero(s4, 5); bzero(t4, 5); strncpy((char *)s4, vlabel.cisize, 4);
	printf("\n\nCI size for FBA         : ascii  '%4s'\n", s4);
	vtoc_ebcdic_dec((char *)s4, (char *)t4, 4);
	printf("                        : ebcdic '%4s'\n", t4);
	printf("                        : hex    ");
	for (i=0; i<4; i++) printf("%02x", s4[i]);

	bzero(s4, 5); bzero(t4, 5); strncpy((char *)s4, vlabel.blkperci, 4);
	printf("\n\nblocks per CI (FBA)     : ascii  '%4s'\n", s4);
	vtoc_ebcdic_dec((char *)s4, (char *)t4, 4);
	printf("                        : ebcdic '%4s'\n", t4);
	printf("                        : hex    ");
	for (i=0; i<4; i++) printf("%02x", s4[i]);

	bzero(s4, 5); bzero(t4, 5); strncpy((char *)s4, vlabel.labperci, 4);
	printf("\n\nlabels per CI (FBA)     : ascii  '%4s'\n", s4);
	vtoc_ebcdic_dec((char *)s4, (char *)t4, 4);
	printf("                        : ebcdic '%4s'\n", t4);
	printf("                        : hex    ");
	for (i=0; i<4; i++) printf("%02x", s4[i]);

	bzero(s4, 5); bzero(t4, 5); strncpy((char *)s4, vlabel.res2, 4);
	printf("\n\nreserved                : ascii  '%4s'\n", s4);
	vtoc_ebcdic_dec((char *)s4, (char *)t4, 4);
	printf("                        : ebcdic '%4s'\n", t4);
	printf("                        : hex    ");
	for (i=0; i<4; i++) printf("%02x", s4[i]);

	bzero(s14, 15); bzero(t14, 15); strncpy(s14, vlabel.lvtoc, 14);
	printf("\n\nowner code for VTOC     : ascii  '%14s'\n", s14);
	vtoc_ebcdic_dec(s14, t14, 14);
	printf("                          ebcdic '%14s'\n", t14);
	printf("                          hex    ");
	for (i=0; i<14; i++)
	{
		printf("%02x", s14[i]);
		if ((i+1)%4 == 0) printf(" ");
		if ((i+1)%8 == 0) printf(" ");
	}

	bzero(s29, 30); strncpy(s29, vlabel.res3, 28);
	printf("\n\nreserved                : ascii  '%28s'\n", s29);
	bzero(t29, 30);
	vtoc_ebcdic_dec(s29, t29, 28);
	printf("                          ebcdic '%28s'\n", t29);
	printf("                          hex    ");
	for (i=0; i<28; i++)
	{
		printf("%02x", s29[i]);
		if ((i+1)%4 == 0) printf(" ");
		if ((i+1)%8 == 0) printf(" ");
		if ((i+1)%16 == 0) printf("\n                " \
					  "                 ");
	}

	bzero(s4, 5); bzero(t4, 5); s4[0] = vlabel.ldl_version;
	printf("\n\nldl_version             : ascii  '%1s'\n", s4);
	vtoc_ebcdic_dec((char *)s4, (char *)t4, 1);
	printf("                        : ebcdic '%1s'\n", t4);
	printf("                        : hex    %02x", s4[0]);

	printf("\n\nformatted_blocks        : dec %llu",
	       vlabel.formatted_blocks);
	printf("\n                        : hex %016llx",
	       vlabel.formatted_blocks);

	printf("\n");
}


static void
dasdview_print_volser(dasdview_info_t *info)
{
	volume_label_t vlabel;
	char           volser[7];
	char           vollbl[5];

	dasdview_read_vlabel(info, &vlabel);

	bzero(vollbl, 5);
	bzero(volser, 7);
	strncpy(vollbl, vlabel.vollbl, 4);
	vtoc_ebcdic_dec(vollbl, vollbl, 4);

	if ((strncmp(vollbl, "VOL1", 4) == 0)||(strncmp(vollbl, "LNX1", 4) == 0)) {
	        strncpy(volser, vlabel.volid, 6);
		vtoc_ebcdic_dec(volser, volser, 6);
	} else
	        strncpy(volser, "      ", 6);

	printf("%6.6s\n", volser);
}


static void
dasdview_read_vtoc(dasdview_info_t *info)
{
        volume_label_t vlabel;
	format1_label_t tmp;
	unsigned long maxblk, pos;
	u_int64_t vtocblk;
	int i;

	pos = info->dasd_info.label_block * info->blksize;

	bzero(&vlabel, sizeof(vlabel));
	if ((strncmp(info->dasd_info.type, "ECKD", 4) == 0) &&
	    (!info->dasd_info.FBA_layout))
	{
		/* OS/390 and zOS compatible disk layout */
		vtoc_read_volume_label(info->device, pos, &vlabel);
	}
	else
	{
		zt_error_print("dasdview: disk layout error\n" \
			"%s is not formatted with the z/OS " \
			"compatible disk layout!\n", info->device);
		exit(-1);
	}

	vtocblk = (u_int64_t) vtoc_get_cyl_from_cchhb(&vlabel.vtoc) *
		info->geo.heads * info->geo.sectors +
		vtoc_get_head_from_cchhb(&vlabel.vtoc) * info->geo.sectors +
		vlabel.vtoc.b;

	/*
	 * geo.cylinders is the minimum of hw_cylinders and LV_COMPAT_CYL
	 * Actually the vtoc should be located in in the first 65k-1 tracks
	 * so this check could be even more restrictive, but it doesn't
	 * hurt the way it is. Linux cdl format restricts the vtoc to
	 * the first two tracks anyway.
	 */
	maxblk = info->geo.cylinders * info->geo.heads * info->geo.sectors;

	if ((vtocblk <= 0) || (vtocblk > maxblk))
	{
		zt_error_print("dasdview: VTOC error\n" \
			"Volume label VTOC pointer is not valid!\n");
		exit(-1);
	}

	vtoc_read_label(info->device, (vtocblk - 1) * info->blksize,
			NULL, &info->f4, NULL, NULL);

	if ((info->f4.DS4KEYCD[0] != 0x04) ||
	    (info->f4.DS4KEYCD[43] != 0x04) ||
	    (info->f4.DS4IDFMT != 0xf4))
	{
		/* format4 DSCB is invalid */
		zt_error_print("dasdview: VTOC error\n" \
			"Format 4 DSCB is invalid!\n");
		exit(-1);
	}

	info->f4c++;
	pos = (vtocblk - 1) * info->blksize;

	for (i=1; i<info->geo.sectors; i++)
	{
	        pos += info->blksize;
	        vtoc_read_label(info->device, pos, &tmp, NULL, NULL, NULL);

		switch (tmp.DS1FMTID) {
		case 0xf1:
			memcpy(&info->f1[info->f1c], &tmp,
			       sizeof(format1_label_t));
			info->f1c++;
			break;
		case 0xf4:
			info->f4c++;
			break;
		case 0xf5:
			memcpy(&info->f5, &tmp, sizeof(format1_label_t));
			info->f5c++;
			break;
		case 0xf7:
			memcpy(&info->f7, &tmp, sizeof(format1_label_t));
			info->f7c++;
			break;
		case 0xf8:
			memcpy(&info->f8[info->f8c], &tmp,
			       sizeof(format1_label_t));
			info->f8c++;
			break;
		case 0xf9:
			memcpy(&info->f9[info->f9c], &tmp,
			       sizeof(format1_label_t));
			info->f9c++;
			break;
		case 0x00:
			break;
		default:
			printf("Unknown label in VTOC detected (id=%x)\n",
				 tmp.DS1FMTID);
		}
	}

	if (info->f4c > 1)
        {
		zt_error_print("dasdview: VTOC error\n" \
			"More than one FMT4 DSCB!\n");
		exit(-1);
	}

	if (info->f5c > 1)
        {
		zt_error_print("dasdview: VTOC error\n" \
			"More than one FMT5 DSCB!\n");
		exit(-1);
	}

	if (info->f7c > 1)
        {
		zt_error_print("dasdview: VTOC error\n" \
			"More than one FMT7 DSCB!\n");
		exit(-1);
	}
}

static void dasdview_print_format1_8_short_info(format1_label_t *f1,
						struct hd_geometry *geo)
{
	char s6[7], s13[14], s44[45];
	unsigned long track_low , track_up;

	bzero(s44, 45);
	strncpy(s44, f1->DS1DSNAM, 44);
	vtoc_ebcdic_dec(s44, s44, 44);
	bzero(s6, 7);
	strncpy(s6, (char *)f1->DS1DSSN, 6);
	vtoc_ebcdic_dec(s6, s6, 6);
	bzero(s13, 14);
	strncpy(s13, (char *)f1->DS1SYSCD, 13);
	vtoc_ebcdic_dec(s13, s13, 13);

	track_low = cchh2trk(&f1->DS1EXT1.llimit, geo);
	track_up = cchh2trk(&f1->DS1EXT1.ulimit, geo);

	printf(" | %44s |          trk |          trk |\n",
	       s44);
	printf(" | data set serial number :"	     \
	       " '%6s'            |"					\
	       " %12ld | %12ld |\n", s6, track_low, track_up);
	printf(" | system code            :"   \
	       " '%13s'     |"						\
	       "      cyl/trk |      cyl/trk |\n", s13);
	printf(" | creation date          :"	       \
	       "  year %4d, day %3d |"				\
	       " %8d/%3d | %8d/%3d |\n",
	       f1->DS1CREDT.year + 1900,
	       f1->DS1CREDT.day,
	       vtoc_get_cyl_from_cchh(&f1->DS1EXT1.llimit),
	       vtoc_get_head_from_cchh(&f1->DS1EXT1.llimit),
	       vtoc_get_cyl_from_cchh(&f1->DS1EXT1.ulimit),
	       vtoc_get_head_from_cchh(&f1->DS1EXT1.ulimit));
	printf(" +-----------------------------------------"		\
	       "-----+--------------+--------------+\n");
}

static void dasdview_print_vtoc_info(dasdview_info_t *info)
{
        int i;

	printf("--- VTOC info --------------------------------" \
	       "---------------------------------\n");
	printf("The VTOC contains:\n");
	printf("  %d format 1 label(s)\n", info->f1c);
	printf("  %d format 4 label(s)\n", info->f4c);
	printf("  %d format 5 label(s)\n", info->f5c);
	printf("  %d format 7 label(s)\n", info->f7c);
	printf("  %d format 8 label(s)\n", info->f8c);
	printf("  %d format 9 label(s)\n", info->f9c);

	if ((info->f1c < 1) && (info->f8c < 1))	{
	        printf("There are no partitions defined.\n");
	} else {
	        printf("Other S/390 and zSeries operating systems would see " \
		       "the following data sets:\n");
		printf(" +----------------------------------------------+" \
		       "--------------+--------------+\n");
		printf(" | data set                                     |" \
		       " start        | end          |\n");
		printf(" +----------------------------------------------+" \
		       "--------------+--------------+\n");

		for (i=0; i<info->f1c; i++)
			dasdview_print_format1_8_short_info(&info->f1[i],
							    &info->geo);
		for (i=0; i<info->f8c; i++)
			dasdview_print_format1_8_short_info(&info->f8[i],
							    &info->geo);
	}
}

/*
 * Note: the explicit cylinder/head conversion for large volume
 * adresses should not be necessary for entries that point to
 * vtoc labels, as those must be located in the first 65K-1 tracks,
 * but we do it anyway to be on the safe side.
 */

static void dasdview_print_format1_8_full(format1_label_t *f1)
{
	char s6[7], s13[14], s44[45];
	int i;

	bzero(s6, 7);
	bzero(s13, 14);
	bzero(s44, 45);

	strncpy(s44, f1->DS1DSNAM, 44);
	printf("DS1DSNAM    : ascii  '%44s'\n", s44);
	vtoc_ebcdic_dec(s44, s44, 44);
	printf("              ebcdic '%44s'\n", s44);
	printf("DS1FMTID    : dec %d, hex %02x\n",
	       f1->DS1FMTID, f1->DS1FMTID);
	printf("DS1DSSN     : hex    ");
	for (i=0; i<6; i++) printf("%02x", f1->DS1DSSN[i]);
	strncpy(s6, (char *)f1->DS1DSSN, 6);
	printf("\n              ascii  '%6s'\n", s6);
	vtoc_ebcdic_dec(s6, s6, 6);
	printf("              ebcdic '%6s'\n", s6);
	printf("DS1VOLSQ    : dec %d, hex %04x\n",
	       f1->DS1VOLSQ, f1->DS1VOLSQ);
	printf("DS1CREDT    : hex %02x%04x "	\
	       "(year %d, day %d)\n",
	       f1->DS1CREDT.year, f1->DS1CREDT.day,
	       f1->DS1CREDT.year + 1900,
	       f1->DS1CREDT.day);
	printf("DS1EXPDT    : hex %02x%04x "	\
	       "(year %d, day %d)\n",
	       f1->DS1EXPDT.year, f1->DS1EXPDT.day,
	       f1->DS1EXPDT.year + 1900,
	       f1->DS1EXPDT.day);
	printf("DS1NOEPV    : dec %d, hex %02x\n",
	       f1->DS1NOEPV, f1->DS1NOEPV);
	printf("DS1NOBDB    : dec %d, hex %02x\n",
	       f1->DS1NOBDB, f1->DS1NOBDB);
	printf("DS1FLAG1    : dec %d, hex %02x\n",
	       f1->DS1FLAG1, f1->DS1FLAG1);
	printf("DS1SYSCD    : hex    ");
	for (i=0; i<13; i++) printf("%02x", f1->DS1SYSCD[i]);
	strncpy(s13, (char *)f1->DS1SYSCD, 13);
	printf("\n              ascii  '%13s'\n", s13);
	vtoc_ebcdic_dec(s13, s13, 13);
	printf("              ebcdic '%13s'\n", s13);
	printf("DS1REFD     : hex %02x%04x "	\
	       "(year %d, day %d)\n",
	       f1->DS1REFD.year, f1->DS1REFD.day,
	       f1->DS1REFD.year + 1900,
	       f1->DS1REFD.day);
	printf("DS1SMSFG    : dec %d, hex %02x\n",
	       f1->DS1SMSFG, f1->DS1SMSFG);
	printf("DS1SCXTF    : dec %d, hex %02x\n",
	       f1->DS1SCXTF, f1->DS1SCXTF);
	printf("DS1SCXTV    : dec %d, hex %04x\n",
	       f1->DS1SCXTV, f1->DS1SCXTV);
	printf("DS1DSRG1    : dec %d, hex %02x\n",
	       f1->DS1DSRG1, f1->DS1DSRG1);
	printf("DS1DSRG2    : dec %d, hex %02x\n",
	       f1->DS1DSRG2, f1->DS1DSRG2);
	printf("DS1RECFM    : dec %d, hex %02x\n",
	       f1->DS1RECFM, f1->DS1RECFM);
	printf("DS1OPTCD    : dec %d, hex %02x\n",
	       f1->DS1OPTCD, f1->DS1OPTCD);
	printf("DS1BLKL     : dec %d, hex %04x\n",
	       f1->DS1BLKL, f1->DS1BLKL);
	printf("DS1LRECL    : dec %d, hex %04x\n",
	       f1->DS1LRECL, f1->DS1LRECL);
	printf("DS1KEYL     : dec %d, hex %02x\n",
	       f1->DS1KEYL, f1->DS1KEYL);
	printf("DS1RKP      : dec %d, hex %04x\n",
	       f1->DS1RKP, f1->DS1RKP);
	printf("DS1DSIND    : dec %d, hex %02x\n",
	       f1->DS1DSIND, f1->DS1DSIND);
	printf("DS1SCAL1    : dec %d, hex %02x\n",
	       f1->DS1SCAL1, f1->DS1SCAL1);
	printf("DS1SCAL3    : hex ");
	for (i=0; i<3; i++) printf("%02x", f1->DS1SCAL3[i]);
	printf("\nDS1LSTAR    : hex %04x%02x "	\
	       "(trk %d, blk %d)\n",
	       f1->DS1LSTAR.tt, f1->DS1LSTAR.r,
	       f1->DS1LSTAR.tt, f1->DS1LSTAR.r);
	printf("DS1TRBAL    : dec %d, hex %04x\n",
	       f1->DS1TRBAL, f1->DS1TRBAL);
	printf("reserved    : dec %d, hex %04x\n",
	       f1->res1, f1->res1);
	printf("DS1EXT1     : hex %02x%02x%04x%04x%04x%04x\n",
	       f1->DS1EXT1.typeind,
	       f1->DS1EXT1.seqno,
	       f1->DS1EXT1.llimit.cc,
	       f1->DS1EXT1.llimit.hh,
	       f1->DS1EXT1.ulimit.cc,
	       f1->DS1EXT1.ulimit.hh);
	printf("              typeind    : dec %d, hex %02x\n",
	       f1->DS1EXT1.typeind,
	       f1->DS1EXT1.typeind);
	printf("              seqno      : dec %d, hex %02x\n",
	       f1->DS1EXT1.seqno, f1->DS1EXT1.seqno);
	printf("              llimit     : hex %04x%04x "	\
	       "(cyl %d, trk %d)\n",
	       f1->DS1EXT1.llimit.cc,
	       f1->DS1EXT1.llimit.hh,
	       vtoc_get_cyl_from_cchh(&f1->DS1EXT1.llimit),
	       vtoc_get_head_from_cchh(&f1->DS1EXT1.llimit));
	printf("              ulimit     : hex %04x%04x "	\
	       "(cyl %d, trk %d)\n",
	       f1->DS1EXT1.ulimit.cc,
	       f1->DS1EXT1.ulimit.hh,
	       vtoc_get_cyl_from_cchh(&f1->DS1EXT1.ulimit),
	       vtoc_get_head_from_cchh(&f1->DS1EXT1.ulimit));
	printf("DS1EXT2     : hex %02x%02x%04x%04x%04x%04x\n",
	       f1->DS1EXT2.typeind,
	       f1->DS1EXT2.seqno,
	       f1->DS1EXT2.llimit.cc,
	       f1->DS1EXT2.llimit.hh,
	       f1->DS1EXT2.ulimit.cc,
	       f1->DS1EXT2.ulimit.hh);
	printf("              typeind    : dec %d, hex %02x\n",
	       f1->DS1EXT2.typeind,
	       f1->DS1EXT2.typeind);
	printf("              seqno      : dec %d, hex %02x\n",
	       f1->DS1EXT2.seqno, f1->DS1EXT2.seqno);
	printf("              llimit     : hex %04x%04x "	\
	       "(cyl %d, trk %d)\n",
	       f1->DS1EXT2.llimit.cc,
	       f1->DS1EXT2.llimit.hh,
	       vtoc_get_cyl_from_cchh(&f1->DS1EXT2.llimit),
	       vtoc_get_head_from_cchh(&f1->DS1EXT2.llimit));
	printf("              ulimit     : hex %04x%04x "	\
	       "(cyl %d, trk %d)\n",
	       f1->DS1EXT2.ulimit.cc,
	       f1->DS1EXT2.ulimit.hh,
	       vtoc_get_cyl_from_cchh(&f1->DS1EXT2.ulimit),
	       vtoc_get_head_from_cchh(&f1->DS1EXT2.ulimit));
	printf("DS1EXT3     : hex %02x%02x%04x%04x%04x%04x\n",
	       f1->DS1EXT3.typeind,
	       f1->DS1EXT3.seqno,
	       f1->DS1EXT3.llimit.cc,
	       f1->DS1EXT3.llimit.hh,
	       f1->DS1EXT3.ulimit.cc,
	       f1->DS1EXT3.ulimit.hh);
	printf("              typeind    : dec %d, hex %02x\n",
	       f1->DS1EXT3.typeind,
	       f1->DS1EXT3.typeind);
	printf("              seqno      : dec %d, hex %02x\n",
	       f1->DS1EXT3.seqno, f1->DS1EXT3.seqno);
	printf("              llimit     : hex %04x%04x "	\
	       "(cyl %d, trk %d)\n",
	       f1->DS1EXT3.llimit.cc,
	       f1->DS1EXT3.llimit.hh,
	       vtoc_get_cyl_from_cchh(&f1->DS1EXT3.llimit),
	       vtoc_get_head_from_cchh(&f1->DS1EXT3.llimit));
	printf("              ulimit     : hex %04x%04x "	\
	       "(cyl %d, trk %d)\n",
	       f1->DS1EXT3.ulimit.cc,
	       f1->DS1EXT3.ulimit.hh,
	       vtoc_get_cyl_from_cchh(&f1->DS1EXT3.ulimit),
	       vtoc_get_head_from_cchh(&f1->DS1EXT3.ulimit));
	printf("DS1PTRDS    : %04x%04x%02x "		\
	       "(cyl %d, trk %d, blk %d)\n",
	       f1->DS1PTRDS.cc, f1->DS1PTRDS.hh,
	       f1->DS1PTRDS.b,
	       vtoc_get_cyl_from_cchhb(&f1->DS1PTRDS),
	       vtoc_get_head_from_cchhb(&f1->DS1PTRDS),
	       f1->DS1PTRDS.b);
}

static void dasdview_print_vtoc_f1(dasdview_info_t *info)
{
	int j;

	printf("--- VTOC format 1 labels ----------------------" \
	       "---------------------------------\n");

	if (info->f1c < 1)
	{
	        printf("This VTOC doesn't contain a format 1 label.\n");
		return;
	}

	for (j=0; j<info->f1c; j++) {
		printf("\n--- format 1 DSCB number %d ---\n", j+1);
		dasdview_print_format1_8_full(&info->f1[j]);
	}
}

static void dasdview_print_vtoc_f8(dasdview_info_t *info)
{
	int j;

	printf("--- VTOC format 8 labels ----------------------" \
	       "---------------------------------\n");

	if (info->f8c < 1)
	{
		printf("This VTOC doesn't contain a format 8 label.\n");
		return;
	}

	for (j=0; j<info->f8c; j++) {
		printf("\n--- format 8 DSCB number %d ---\n", j+1);
		dasdview_print_format1_8_full(&info->f8[j]);
	}
}

static void
dasdview_print_vtoc_f4(dasdview_info_t *info)
{
        int i;

	printf("\n--- VTOC format 4 label ----------------------" \
	       "---------------------------------\n");

	if (info->f4c < 1)
	{
	        printf("This VTOC doesn't contain a format 4 label.\n");
		return;
	}

	printf("DS4KEYCD    : ");
	for (i=0; i<44; i++) printf("%02x", info->f4.DS4KEYCD[i]);
	printf("\nDS4IDFMT    : dec %d, hex %02x\n",
	       info->f4.DS4IDFMT, info->f4.DS4IDFMT);
	printf("DS4HPCHR    : %04x%04x%02x " \
	       "(cyl %d, trk %d, blk %d)\n",
	       info->f4.DS4HPCHR.cc, info->f4.DS4HPCHR.hh,
	       info->f4.DS4HPCHR.b,
	       vtoc_get_cyl_from_cchhb(&info->f4.DS4HPCHR),
	       vtoc_get_head_from_cchhb(&info->f4.DS4HPCHR),
	       info->f4.DS4HPCHR.b);
	printf("DS4DSREC    : dec %d, hex %04x\n",
	       info->f4.DS4DSREC, info->f4.DS4DSREC);
	printf("DS4HCCHH    : %04x%04x (cyl %d, trk %d)\n",
	       info->f4.DS4HCCHH.cc, info->f4.DS4HCCHH.hh,
	       vtoc_get_cyl_from_cchh(&info->f4.DS4HCCHH),
	       vtoc_get_head_from_cchh(&info->f4.DS4HCCHH));
	printf("DS4NOATK    : dec %d, hex %04x\n",
	       info->f4.DS4NOATK, info->f4.DS4NOATK);
	printf("DS4VTOCI    : dec %d, hex %02x\n",
	       info->f4.DS4VTOCI, info->f4.DS4VTOCI);
	printf("DS4NOEXT    : dec %d, hex %02x\n",
	       info->f4.DS4NOEXT, info->f4.DS4NOEXT);
	printf("DS4SMSFG    : dec %d, hex %02x\n",
	       info->f4.DS4SMSFG, info->f4.DS4SMSFG);
	printf("DS4DEVAC    : dec %d, hex %02x\n",
	       info->f4.DS4DEVAC, info->f4.DS4DEVAC);
	printf("DS4DSCYL    : dec %d, hex %04x\n",
	       info->f4.DS4DEVCT.DS4DSCYL, info->f4.DS4DEVCT.DS4DSCYL);
	printf("DS4DSTRK    : dec %d, hex %04x\n",
	       info->f4.DS4DEVCT.DS4DSTRK, info->f4.DS4DEVCT.DS4DSTRK);
	printf("DS4DEVTK    : dec %d, hex %04x\n",
	       info->f4.DS4DEVCT.DS4DEVTK, info->f4.DS4DEVCT.DS4DEVTK);
	printf("DS4DEVI     : dec %d, hex %02x\n",
	       info->f4.DS4DEVCT.DS4DEVI, info->f4.DS4DEVCT.DS4DEVI);
	printf("DS4DEVL     : dec %d, hex %02x\n",
	       info->f4.DS4DEVCT.DS4DEVL, info->f4.DS4DEVCT.DS4DEVL);
	printf("DS4DEVK     : dec %d, hex %02x\n",
	       info->f4.DS4DEVCT.DS4DEVK, info->f4.DS4DEVCT.DS4DEVK);
	printf("DS4DEVFG    : dec %d, hex %02x\n",
	       info->f4.DS4DEVCT.DS4DEVFG, info->f4.DS4DEVCT.DS4DEVFG);
	printf("DS4DEVTL    : dec %d, hex %04x\n",
	       info->f4.DS4DEVCT.DS4DEVTL, info->f4.DS4DEVCT.DS4DEVTL);
	printf("DS4DEVDT    : dec %d, hex %02x\n",
	       info->f4.DS4DEVCT.DS4DEVDT, info->f4.DS4DEVCT.DS4DEVDT);
	printf("DS4DEVDB    : dec %d, hex %02x\n",
	       info->f4.DS4DEVCT.DS4DEVDB, info->f4.DS4DEVCT.DS4DEVDB);
	printf("DS4AMTIM    : hex ");
	for (i=0; i<8; i++) printf("%02x", info->f4.DS4AMTIM[i]);
	printf("\nDS4AMCAT    : hex ");
	for (i=0; i<3; i++) printf("%02x", info->f4.DS4AMCAT[i]);
	printf("\nDS4R2TIM    : hex ");
	for (i=0; i<8; i++) printf("%02x", info->f4.DS4R2TIM[i]);
	printf("\nres1        : hex ");
	for (i=0; i<5; i++) printf("%02x", info->f4.res1[i]);
	printf("\nDS4F6PTR    : hex ");
	for (i=0; i<5; i++) printf("%02x", info->f4.DS4F6PTR[i]);
	printf("\nDS4VTOCE    : hex %02x%02x%04x%04x%04x%04x\n",
	       info->f4.DS4VTOCE.typeind, info->f4.DS4VTOCE.seqno,
	       info->f4.DS4VTOCE.llimit.cc, info->f4.DS4VTOCE.llimit.hh,
	       info->f4.DS4VTOCE.ulimit.cc, info->f4.DS4VTOCE.ulimit.hh);
	printf("              typeind    : dec %d, hex %02x\n",
	       info->f4.DS4VTOCE.typeind, info->f4.DS4VTOCE.typeind);
	printf("              seqno      : dec %d, hex %02x\n",
	       info->f4.DS4VTOCE.seqno, info->f4.DS4VTOCE.seqno);
	printf("              llimit     : hex %04x%04x (cyl %d, trk %d)\n",
	       info->f4.DS4VTOCE.llimit.cc, info->f4.DS4VTOCE.llimit.hh,
	       vtoc_get_cyl_from_cchh(&info->f4.DS4VTOCE.llimit),
	       vtoc_get_head_from_cchh(&info->f4.DS4VTOCE.llimit));
	printf("              ulimit     : hex %04x%04x (cyl %d, trk %d)\n",
	       info->f4.DS4VTOCE.ulimit.cc, info->f4.DS4VTOCE.ulimit.hh,
	       vtoc_get_cyl_from_cchh(&info->f4.DS4VTOCE.ulimit),
	       vtoc_get_head_from_cchh(&info->f4.DS4VTOCE.ulimit));
	printf("res2        : hex ");
	for (i=0; i<10; i++) printf("%02x", info->f4.res2[i]);
	printf("\nDS4EFLVL    : dec %d, hex %02x\n",
	       info->f4.DS4EFLVL, info->f4.DS4EFLVL);
	printf("DS4EFPTR    : hex %04x%04x%02x " \
	       "(cyl %d, trk %d, blk %d)\n",
	       info->f4.DS4EFPTR.cc, info->f4.DS4EFPTR.hh,
	       info->f4.DS4EFPTR.b,
	       vtoc_get_cyl_from_cchhb(&info->f4.DS4EFPTR),
	       vtoc_get_head_from_cchhb(&info->f4.DS4EFPTR),
	       info->f4.DS4EFPTR.b);
	printf("res3        : hex %02x\n", info->f4.res3);
	printf("DS4DCYL     : dec %d, hex %08x\n",
	       info->f4.DS4DCYL, info->f4.DS4DCYL);
	printf("res4        : hex ");
	for (i=0; i<2; i++) printf("%02x", info->f4.res4[i]);
	printf("\nDS4DEVF2    : dec %d, hex %02x\n",
	       info->f4.DS4DEVF2, info->f4.DS4DEVF2);
	printf("res5        : hex %02x\n", info->f4.res5);
}

static void
dasdview_print_vtoc_f5(dasdview_info_t *info)
{
        int i;

	printf("\n--- VTOC format 5 label ----------------------" \
	       "---------------------------------\n");

	if (info->f5c < 1)
	{
	        printf("This VTOC doesn't contain a format 5 label.\n");
		return;
	}

	printf("key identifier\n        DS5KEYID    : ");
	for (i=0; i<4; i++) printf("%02x", info->f5.DS5KEYID[i]);
	printf("\nfirst extent description\n");
	printf("        DS5AVEXT    : %04x%04x%02x " \
	       "(start trk: %d, length: %d cyl, %d trk)\n",
	       info->f5.DS5AVEXT.t,  info->f5.DS5AVEXT.fc,
	       info->f5.DS5AVEXT.ft, info->f5.DS5AVEXT.t,
	       info->f5.DS5AVEXT.fc, info->f5.DS5AVEXT.ft);
	printf("next 7 extent descriptions\n");
	for (i=0; i<7; i++)
        {
	        printf("        DS5EXTAV[%d] : %04x%04x%02x " \
		       "(start trk: %d, length: %d cyl, %d trk)\n", i+2,
		       info->f5.DS5EXTAV[i].t,  info->f5.DS5EXTAV[i].fc,
		       info->f5.DS5EXTAV[i].ft, info->f5.DS5EXTAV[i].t,
		       info->f5.DS5EXTAV[i].fc, info->f5.DS5EXTAV[i].ft);
	}
	printf("format identifier\n" \
	       "        DS5FMTID    : dec %d, hex %02x\n",
	       info->f5.DS5FMTID, info->f5.DS5FMTID);
	printf("next 18 extent descriptions\n");
	for (i=0; i<18; i++)
        {
	        printf("        DS5MAVET[%d] : %04x%04x%02x " \
		       "(start trk: %d, length: %d cyl, %d trk)\n", i+9,
		       info->f5.DS5MAVET[i].t,  info->f5.DS5MAVET[i].fc,
		       info->f5.DS5MAVET[i].ft, info->f5.DS5MAVET[i].t,
		       info->f5.DS5MAVET[i].fc, info->f5.DS5MAVET[i].ft);
	}
	printf("pointer to next format 5 label\n" \
	       "        DS5PTRDS    : %04x%04x%02x " \
	       "(cyl %d, trk %d, blk %d)\n",
	       info->f5.DS5PTRDS.cc, info->f5.DS5PTRDS.hh,
	       info->f5.DS5PTRDS.b,
	       vtoc_get_cyl_from_cchhb(&info->f5.DS5PTRDS),
	       vtoc_get_head_from_cchhb(&info->f5.DS5PTRDS),
	       info->f5.DS5PTRDS.b);
}

static void
dasdview_print_vtoc_f7(dasdview_info_t *info)
{
        int i;

	printf("\n--- VTOC format 7 label ----------------------" \
	       "---------------------------------\n");

	if (info->f7c < 1)
	{
	        printf("This VTOC doesn't contain a format 7 label.\n");
		return;
	}

	printf("key identifier\n        DS7KEYID    : ");
	for (i=0; i<4; i++) printf("%02x", info->f7.DS7KEYID[i]);
	printf("\nfirst 5 extent descriptions\n");
	for (i=0; i<5; i++)
	{
	        printf("        DS7EXTNT[%d] : %08x %08x " \
		       "(start trk %d, end trk %d)\n", i+1,
		       info->f7.DS7EXTNT[i].a, info->f7.DS7EXTNT[i].b,
		       info->f7.DS7EXTNT[i].a, info->f7.DS7EXTNT[i].b);
	}
	printf("format identifier\n" \
	       "        DS7FMTID    : dec %d, hex %02x\n",
	       info->f7.DS7FMTID, info->f7.DS7FMTID);
	printf("next 11 extent descriptions\n");
	for (i=0; i<11; i++)
	{
	        printf("        DS7ADEXT[%d] : %08x %08x " \
		       "(start trk %d, end trk %d)\n", i+6,
		       info->f7.DS7ADEXT[i].a, info->f7.DS7ADEXT[i].b,
		       info->f7.DS7ADEXT[i].a, info->f7.DS7ADEXT[i].b);
	}
	printf("reserved field\n        res1        : ");
	for (i=0; i<2; i++) printf("%02x", info->f7.res1[i]);
	printf("\npointer to next format 7 label\n" \
	       "        DS7PTRDS    : %04x%04x%02x " \
	       "(cyl %d, trk %d, blk %d)\n",
	       info->f7.DS7PTRDS.cc, info->f7.DS7PTRDS.hh,
	       info->f7.DS7PTRDS.b,
	       vtoc_get_cyl_from_cchhb(&info->f7.DS7PTRDS),
	       vtoc_get_head_from_cchhb(&info->f7.DS7PTRDS),
	       info->f7.DS7PTRDS.b);
}

static void
dasdview_print_vtoc_f9(dasdview_info_t *info)
{
	unsigned int i;
	int j;

	printf("\n--- VTOC format 9 label ----------------------" \
	       "---------------------------------\n");

	if (info->f9c < 1) {
	        printf("This VTOC doesn't contain a format 9 label.\n");
		return;
	}

	for (j=0; j<info->f9c; j++) {
		printf("\n--- format 9 DSCB number %d ---\n", j+1);
		printf("DS9KEYID    : dec %d, hex %02x\n",
		       info->f9[j].DS9KEYID, info->f9[j].DS9KEYID);
		printf("DS9SUBTY    : dec %d, hex %02x\n",
		       info->f9[j].DS9SUBTY, info->f9[j].DS9SUBTY);
		printf("DS9NUMF9    : dec %d, hex %02x\n",
		       info->f9[j].DS9NUMF9, info->f9[j].DS9NUMF9);

		printf("res1        : hex ");
		for (i=0; i < sizeof(info->f9[j].res1); i++) {
			if ((i > 0) && (i % 16 == 0))
				printf("\n                  ");
			printf("%02x", info->f9[j].res1[i]);
			if ((i+9) % 16 == 0)
				printf(" ");
		}
		printf("\n");

		printf("DS9FMTID    : dec %d, hex %02x\n",
		       info->f9[j].DS9FMTID, info->f9[j].DS9FMTID);

		printf("res2        : hex ");
		for (i=0; i < sizeof(info->f9[j].res2); i++) {
			if ((i > 0) && (i % 16 == 0))
				printf("\n                  ");
			printf("%02x", info->f9[j].res2[i]);
			if ((i+9) % 16 == 0)
				printf(" ");
		}
		printf("\n");
	}
}

static int
dasdview_print_format1(unsigned int size, unsigned char *dumpstr)
{
	unsigned int i;
	char asc[17], ebc[17];

	for (i = 0; i < size; i++)
	{
		if ((i / 16) * 16 == i) {
			printf("\n|  ");
			strncpy(asc, (char *)dumpstr + i, 16);
			strncpy(ebc, (char *)dumpstr + i, 16);
			asc[16] = '\0';
			ebc[16] = '\0';
		}
		printf("%02X", dumpstr[i]);
		if (((i + 1) / 4)  * 4  == i + 1) printf(" ");
		if (((i + 1) / 8)  * 8  == i + 1) printf(" ");
		if (((i + 1) / 16) * 16 == i + 1) {
			vtoc_ebcdic_dec(asc, asc, 16);
			dot(asc);
			dot(ebc);
			printf("| %16.16s | %16.16s |", asc, ebc);
		}
	}

	return 0;
}

static int
dasdview_print_format2(unsigned int size, unsigned char *dumpstr,
		       unsigned long long begin)
{
	unsigned int i;
	char asc[17], ebc[17];

	for (i = 0; i < size; i++)
	{
		if ((i / 8) * 8 == i) {
			printf("\n | %13llu | %13llX |  ",
			       begin + (unsigned long long) i,
			       begin + (unsigned long long) i);

			strncpy(asc, (char *)dumpstr + i, 8);
			strncpy(ebc, (char *)dumpstr + i, 8);
		}
		printf("%02X", dumpstr[i]);
		if (((i + 1) / 4) * 4 == i + 1) printf("  ");
		if (((i + 1) / 8) * 8 == i + 1) {
			vtoc_ebcdic_dec(asc, asc, 8);
			dot(asc);
			dot(ebc);
			printf("| %8.8s | %8.8s |", asc, ebc);
		}
	}

	return 0;
}

static int
dasdview_view(dasdview_info_t *info)
{
	unsigned char  dumpstr[DUMP_STRING_SIZE];
	unsigned long long i=0, j=0, k=0, count=0;
	int   fd, rc;

	unsigned long long a=0;
	int b=0;

	k = ((info->size) % 16LL);

	if (k != 0)
	{
		info->size += (16LL - k);
	}

	fd = open(info->device, O_RDONLY);
	if (fd == -1)
	{
		zt_error_print("dasdview: open error\n" \
			"Unable to open device %s in read-only" \
			"mode!\n", info->device);
		exit(-1);
	}

	j = (info->begin / SEEK_STEP);
	k = (info->begin % SEEK_STEP);

        /* seek in SEEK_STEP steps */
	for (i=1; i <= j; i++)
	{
		rc = lseek64(fd, SEEK_STEP, SEEK_CUR);
		if (rc == -1)
		{
			printf("*** rc: %d (%d) ***\n", rc, errno);
			printf("*** j: %llu ***\n", j);
			printf("*** k: %llu ***\n", k);
			printf("*** a: %llu ***\n", a);
			printf("*** b: %d ***\n", b);
			close(fd);
			zt_error_print("dasdview: seek error\n" \
				"Unable to seek in device %s!\n",
				info->device);
			exit(-1);
		}
		b++;
		a += SEEK_STEP;
	}

	if (k > 0)
	{
		rc = lseek(fd, k, SEEK_CUR);
		if (rc == -1)
		{
			close(fd);
			zt_error_print("dasdview: seek error\n" \
				"Unable to seek in device %s!\n",
				info->device);
			exit(-1);
		}
	}

	j = info->size / DUMP_STRING_SIZE;
	k = info->size % DUMP_STRING_SIZE;


	if (info->format1)
	{
		printf("+----------------------------------------+" \
		       "------------------+------------------+\n");
		printf("| HEXADECIMAL                            |" \
		       " EBCDIC           | ASCII            |\n");
		printf("|  01....04 05....08  09....12 13....16  |" \
		       " 1.............16 | 1.............16 |\n");
		printf("+----------------------------------------+" \
		       "------------------+------------------+");
	}
	else if (info->format2)
	{
		printf(" +---------------+---------------+----------------" \
		       "------+----------+----------+\n");
		printf(" |     BYTE      |     BYTE      |     HEXADECIMAL" \
		       "      |  EBCDIC  |  ASCII   |\n");
		printf(" |    DECIMAL    |  HEXADECIMAL  |  1 2 3 4   5 6 " \
		       "7 8   | 12345678 | 12345678 |\n");
		printf(" +---------------+---------------+----------------" \
		       "------+----------+----------+");
	}

	count = info->begin;
	for (i=1; i <= j; i++)
	{
		bzero(dumpstr, DUMP_STRING_SIZE);
		rc = read(fd, &dumpstr, DUMP_STRING_SIZE);
		if (rc != DUMP_STRING_SIZE)
		{
			close(fd);
			zt_error_print("dasdview: read error\n" \
				"Unable to read from device %s!\n",
				info->device);
			exit(-1);
		}

		if (info->format1)
			dasdview_print_format1(DUMP_STRING_SIZE, dumpstr);
		else if (info->format2)
			dasdview_print_format2(DUMP_STRING_SIZE, dumpstr,
					       count);
		count += DUMP_STRING_SIZE;
	}

	if (k > 0)
	{
		bzero(dumpstr, DUMP_STRING_SIZE);
		rc = read(fd, &dumpstr, k);
		if (rc != (int) k)
		{
			close(fd);
			zt_error_print("dasdview: read error\n" \
				"Unable to read from device %s!\n",
				info->device);
			exit(-1);
		}

		if (info->format1)
			dasdview_print_format1((unsigned int) k, dumpstr);
		else if (info->format2)
			dasdview_print_format2((unsigned int) k, dumpstr,
					       count);
	}

	close(fd);

	if (info->format1)
	{
		printf("\n+----------------------------------------+" \
		       "------------------+------------------+\n\n");
	}
	else if (info->format2)
	{
		printf("\n +---------------+---------------+----------------" \
		       "------+----------+----------+\n\n");
	}

	return 0;
}

static void
dasdview_print_characteristic(dasdview_info_t *info)
{
	dasd_information2_t dasd_info;
	dasd_info = info->dasd_info;
	printf("encrypted disk         : %s\n",
	       (dasd_info.characteristics[46]&0x80)?"yes":"no");
	printf("solid state device     : %s\n",
	       (dasd_info.characteristics[46]&0x40)?"yes":"no");
}

int main(int argc, char * argv[]) {

	dasdview_info_t info;
	int oc, index;
	unsigned long long max=0LL;

	char *devno_param_str = NULL;
	char *begin_param_str = NULL;
	char *size_param_str  = NULL;
	char *endptr          = NULL;

	bzero (&info, sizeof(info));
	while (1)
	{
		oc = getopt_long(argc, argv, dasdview_getopt_string,
				 dasdview_getopt_long_options, &index);

		switch (oc)
		{
		case 'h':
			dasdview_usage();
			exit(0);
		case ':':
			dasdview_usage();
			exit(1);
		case 'v':
			print_version();
			exit(0);
		case 'b':
			begin_param_str = optarg;
			info.action_specified = 1;
			info.begin_specified = 1;
			break;
		case 's':
			size_param_str = optarg;
			info.action_specified = 1;
			info.size_specified = 1;
			break;
		case '1':
			info.format1 = 1;
			info.format2 = 0;
			break;
		case '2':
			info.format1 = 0;
			info.format2 = 1;
			break;
		case 'n':
			devno_param_str = optarg;
			info.devno_specified = 1;
			break;
		case 'f':
			strcpy(info.device, optarg);
			info.node_specified = 1;
			break;
		case 'i':  /* print general DASD information and geometry */
			info.action_specified = 1;
			info.general_info = 1;
			break;
		case 'x':  /* print extended DASD information */
			info.action_specified = 1;
			info.extended_info = 1;
			break;
		case 'j':
			info.action_specified = 1;
			info.volser = 1;
			break;
		case 't':
		        if (strncmp(optarg,"info",4)==0)
		                info.vtoc_info = 1;
		        else if (strncmp(optarg,"f1",2)==0)
		                info.vtoc_f1 = 1;
		        else if (strncmp(optarg,"f4",2)==0)
		                info.vtoc_f4 = 1;
		        else if (strncmp(optarg,"f5",2)==0)
		                info.vtoc_f5 = 1;
		        else if (strncmp(optarg,"f7",2)==0)
		                info.vtoc_f7 = 1;
		        else if (strncmp(optarg,"f8",2)==0)
				info.vtoc_f8 = 1;
			else if (strncmp(optarg,"f9",2)==0)
				info.vtoc_f9 = 1;
			else if (strncmp(optarg,"all",3)==0)
		                info.vtoc_all = 1;
		        else
		        {
				zt_error_print("dasdview: usage error\n" \
					"%s is no valid option!\n",
					optarg);
				exit(-1);
			}
		        info.vtoc = 1;
			info.action_specified = 1;
			break;
		case 'l':
			info.action_specified = 1;
			info.vlabel_info = 1;
			break;
		case 'c':
			info.action_specified = 1;
			info.characteristic_specified = 1;
			break;
		case -1:
			/* End of options string - start of devices list */
			info.device_id = optind;
			break;
		default:
			fprintf(stderr, "Try 'dasdview --help' for more"
					" information.\n");
			exit(1);
		}
        	if (oc==-1) break;
	}

	if (info.devno_specified)
		PARSE_PARAM_INTO(info.devno, devno_param_str, 16,
				 "device number");

	/* do some tests */
	if (!(info.node_specified+info.devno_specified) &&
	    info.device_id >= argc)
	{
		zt_error_print("dasdview: usage error\n" \
			"Device not specified!");
		exit(-1);
	}

	if (((info.node_specified + info.devno_specified) > 1) ||
	    ((info.node_specified + info.devno_specified) > 0 &&
              info.device_id < argc))
	{
		zt_error_print("dasdview: usage error\n" \
			"Device can only specified once!");
		exit(-1);
	}

	if (info.device_id < argc)
	{
		strcpy(info.device, argv[info.device_id]);
	}
	if (info.devno_specified)
	{
		sprintf(info.device, "/dev/dasd/%04x/device", info.devno);
	}

	if ((info.devno_specified) &&
	    ((info.devno < 0x0000)||(info.devno > 0xffff)))
	{
		zt_error_print("dasdview: usage error\n" \
			"Devno '%#04x' is not in range " \
			"0x0000 - 0xFFFF!", info.devno);
		exit(-1);
	}

	dasdview_get_info(&info);

	if (info.begin_specified)
	{

		dasdview_parse_input(&info.begin, &info, begin_param_str);
	}
	else
		info.begin = DEFAULT_BEGIN;

	max = (unsigned long long) info.hw_cylinders *
		(unsigned long long) info.geo.heads *
		(unsigned long long) info.geo.sectors *
		(unsigned long long) info.blksize;
	if (info.begin > max)
	{
		zt_error_print("dasdview: usage error\n" \
			"'begin' value is not within disk range!");
		exit(-1);
	}

	if (info.size_specified)
	{
		dasdview_parse_input(&info.size, &info, size_param_str);
	}
	else
		info.size = DEFAULT_SIZE;

	if ((info.begin_specified || info.size_specified) &&
	    ((info.begin + info.size) > max             )   )
	{
		zt_error_print("dasdview: usage error\n" \
			"'begin' + 'size' is not within " \
			"disk range!");
		exit(-1);
	}

	if ((info.begin_specified || info.size_specified) &&
	    (!info.format1 && !info.format2))
	{
		info.format1 = 1;
	}

	if ((info.format1 || info.format2) &&
	    (!info.size_specified && !info.begin_specified))
	{
		zt_error_print("dasdview: usage error\n" \
			"Options -1 or -2 make only sense with " \
			"options -b or -s!");
		exit(-1);
	}

	/* do the output */

	if (info.begin_specified || info.size_specified)
		dasdview_view(&info);

	if (info.general_info || info.extended_info)
		dasdview_print_general_info(&info);

	if (info.extended_info)
		dasdview_print_extended_info(&info);

	if (info.volser)
		dasdview_print_volser(&info);

	if (info.vlabel_info)
		dasdview_print_vlabel(&info);

	if (info.vtoc)
	{
		dasdview_read_vtoc(&info);
	}

	if (info.vtoc_info || info.vtoc_all)
	{
		dasdview_print_vtoc_info(&info);
	}

	if (info.vtoc_f4 || info.vtoc_all)
	{
	        dasdview_print_vtoc_f4(&info);
	}

	if (info.vtoc_f5 || info.vtoc_all)
	{
	        dasdview_print_vtoc_f5(&info);
	}

	if (info.vtoc_f7 || info.vtoc_all)
	{
	        dasdview_print_vtoc_f7(&info);
	}

	if (info.vtoc_f1 || info.vtoc_all)
	{
	        dasdview_print_vtoc_f1(&info);
	}

	if (info.vtoc_f8 || info.vtoc_all)
	{
		dasdview_print_vtoc_f8(&info);
	}

	if (info.vtoc_f9 || info.vtoc_all)
	{
		dasdview_print_vtoc_f9(&info);
	}

	if (!info.action_specified)
	{
		printf("No action specified.\n");
	}

	if (info.characteristic_specified)
		dasdview_print_characteristic(&info);

	return 0;
}
