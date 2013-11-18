/*
 * File...........: s390-tools/dasdfmt/dasdfmt.c
 * Author(s)......: Utz Bacher,      <utz.bacher@de.ibm.com>
 *                  Fritz Elfert,    <felfert@to.com>
 *                  Carsten Otte,    <cotte@de.ibm.com>
 *                  Volker Sameske,  <sameske@de.ibm.com>
 *                  Holger Smolinski,<smolinsk@de.ibm.com>
 *                  Gerhard Tonn     <ton@de.ibm.com>
 * Copyright IBM Corp. 1999,2007
 */

#include <sys/utsname.h>
#include <linux/version.h>

#include "zt_common.h"
#include "dasdfmt.h"
#include "vtoc.h" 

/* Full tool name */
static const char tool_name[] = "dasdfmt: zSeries DASD format program";

/* Copyright notice */
static const char copyright_notice[] = "Copyright IBM Corp. 1999, 2006";

static int filedes;
static int disk_disabled;
static format_data_t format_params;
char *prog_name;
volatile sig_atomic_t program_interrupt_in_progress;
int reqsize;

/*
 * Print version information.
 */
static void
print_version (void)
{
	printf ("%s version %s\n", tool_name, RELEASE_STRING);
	printf ("%s\n", copyright_notice);
}

/*
 * print the usage text and exit
 */
static void exit_usage(int exitcode)
{
	printf("Usage: %s [-htvypPLVFk]\n"
	       "	       [-l <volser>      | --label=<volser>]\n"
	       "               [-b <blocksize>   | --blocksize=<blocksize>]\n"
	       "               [-d <disk layout> | --disk_layout=<disk layout>]\n"
	       "               [-r <cylinder>   | --requestsize=<cylinder>]\n"
	       "               <diskspec>\n\n",prog_name);

	printf("       -t or --test     means testmode\n"
	       "       -V or --version  means print version\n"
	       "       -L or --no_label means don't write disk label\n"
	       "       -p or --progressbar means show a progress bar\n"
	       "       -P or --percentage means show a progress in percent\n"
	       "       -m x or --hashmarks=x means show a hashmark every x "
	       "cylinders\n"
	       "       -r x or --requestsize=x means use x cylinders in one "
	       "format step\n"
	       "       -v means verbose mode\n"
	       "       -F means don't check if the device is in use\n"
	       "       -k means keep volume serial\n"
               "       --norecordzero prevent storage server from modifying"
               " record 0\n\n"
	       "       <volser> is the volume identifier, which is converted\n"
	       "                to EBCDIC and written to disk. \n"
	       "                (6 characters, e.g. LNX001\n"
	       "       <blocksize> has to be power of 2 and at least 512\n"
	       "       <disk layout> is either \n"
	       "           'cdl' for compatible disk layout (default) or\n"
	       "           'ldl' for linux disk layout\n"
	       "       and <diskspec> is either\n"
	       "           -f /dev/dasdX or --device=/dev/dasdX\n"
	       "           if you do not use devfs\n"
	       "         or\n"
	       "           -f /dev/dasd/xxxx/device or "
	       "--device=/dev/dasd/xxxx/device\n"
	       "           and alternatively\n"
	       "           -n xxxx or --devno=xxxx\n"
	       "           in case you are using devfs.\n"
	       "           xxxx is your hexadecimal device number.\n");
	exit(exitcode);
}

static int reread_partition_table(void)
{
	int i = 0;
	int rc = -1;

	/* If the BLKRRPART ioctl fails, it is most likely due to
	   the device just beeing in use by udev. So it is worthwhile
	   to retry the ioctl after a second as it is likely to succeed.
	 */
	while (rc && (i < 5)) {
		++i;
		rc = ioctl(filedes, BLKRRPART, NULL);
		if (rc)
			sleep(1);
	}
	return rc;
}

/*
 * signal handler:
 * enables the disk again in case of SIGTERM, SIGINT and SIGQUIT
 */
static void program_interrupt_signal (int sig)
{
	int rc;

	if (program_interrupt_in_progress)
		raise (sig);
	program_interrupt_in_progress = 1;

	if (disk_disabled) {
		printf("Re-accessing the device... \n");
		rc = ioctl(filedes, BIODASDENABLE, &format_params);
		if (rc)
			ERRMSG_EXIT(EXIT_FAILURE,
				    "%s: (signal handler) IOCTL BIODASDENABLE "
				    "failed (%s)\n",prog_name,strerror(errno));
	}

	printf("Rereading the partition table... \n");
	rc = reread_partition_table();
	if (rc) {
		ERRMSG("%s: (signal handler) Re-reading partition table "
		       "failed. (%s)\n", prog_name, strerror(errno));
	} else
		printf("Exiting...\n");

	rc = close(filedes);
	if (rc)
		ERRMSG("%s: (signal handler) Unable to close device (%s)\n", 
		       prog_name, strerror(errno));

	signal (sig, SIG_DFL);
	raise (sig);
}


/*
 * check given device name for blanks and some special characters
 * or create a devfs filename in case the devno was specified
 */
static void get_device_name(dasdfmt_info_t *info, char *name, int argc, char * argv[])
{
	struct stat dev_stat;

	if (((info->node_specified + info->devno_specified) > 1) ||
            ((info->node_specified + info->devno_specified) > 0 &&
              info->device_id < argc)) 
		ERRMSG_EXIT(EXIT_MISUSE,"%s: Device can only specified once! "
			    "(%#04x or %s)\n", prog_name, info->devno, name);

	if ((info->node_specified + info->devno_specified) == 0 &&
            info->device_id >= argc) 
		ERRMSG_EXIT(EXIT_MISUSE,"%s: No device specified!\n", 
			    prog_name);

	if ((info->devno_specified) && 
	    ((info->devno < 0x0000) || (info->devno > 0xffff)))
		ERRMSG_EXIT(EXIT_MISUSE,"%s: Devno '%#04x' is not in range "
			    "0x0000 - 0xFFFF!\n", prog_name, info->devno);

	if (info->devno_specified)
		sprintf(info->devname, "/dev/dasd/%04x/device", info->devno);
	else if (info->device_id < argc) {
		strcpy(info->devname, argv[info->device_id]);
        }
	else {
		if ((strchr(name, ' ') != NULL)||(strchr(name, '#') != NULL)||
		    (strchr(name, '[') != NULL)||(strchr(name, ']') != NULL)||
		    (strchr(name, '!') != NULL)||(strchr(name, '>') != NULL)||
		    (strchr(name, '(') != NULL)||(strchr(name, '<') != NULL)||
		    (strchr(name, ')') != NULL)||(strchr(name, ':') != NULL)||
		    (strchr(name, '&') != NULL)||(strchr(name, ';') != NULL))
			ERRMSG_EXIT(EXIT_MISUSE,"%s: Your filename contains "
				    "blanks or special characters!\n", 
				    prog_name);

		strncpy(info->devname, name, PATH_MAX - 1);
		info->devname[PATH_MAX - 1] = '\0';
	}

	if (stat(info->devname, &dev_stat) != 0)
		ERRMSG_EXIT(EXIT_MISUSE,
			    "%s: Could not get information for device node %s: %s\n",
			    prog_name, info->devname, strerror(errno));

	if (minor(dev_stat.st_rdev) & PARTN_MASK) {
		ERRMSG_EXIT(EXIT_MISUSE,
			    "%s: Unable to format partition %s. Please specify a device.\n",
			    prog_name, info->devname);
	}
}


/*
 * initialize the dasdfmt info structure
 */
static void init_info(dasdfmt_info_t *info)
{
	info->devno             = 0x0;
	info->usage_count       = 0;

	info->testmode          = 0;
	info->verbosity         = 0;
	info->withoutprompt     = 0;
	info->print_progressbar = 0;
	info->print_hashmarks   = 0;
	info->print_percentage  = 0;
	info->hashstep          = 0;
	info->force             = 0;
	info->writenolabel      = 0;
	info->labelspec         = 0;
        info->cdl_format        = 0;
	info->blksize_specified = 0;
	info->reqsize_specified = 0;
	info->node_specified    = 0;
	info->devno_specified   = 0;
	info->device_id         = 0;
	info->keep_volser	= 0;
}


/*
 * check for disk type and set some variables (e.g. usage count)
 */
static void check_disk(dasdfmt_info_t *info)
{
	dasd_information_t dasd_info;
	int ro, errno_save;

	if (ioctl(filedes, BLKROGET, &ro) != 0) {
		errno_save = errno;
		close(filedes);
		ERRMSG_EXIT(EXIT_FAILURE,
			    "%s: the ioctl call to retrieve read/write "
			    "status information failed (%s)\n",
			    prog_name, strerror(errno_save));
	}
	if (ro) {
		ERRMSG_EXIT(EXIT_FAILURE, "Disk is read only!\n");
	}

	if (ioctl(filedes, BIODASDINFO, &dasd_info) != 0) {
		errno_save = errno;
		close(filedes);
		ERRMSG_EXIT(EXIT_FAILURE,
			    "%s: the ioctl call to retrieve device "
			    "information failed (%s)\n",
			    prog_name, strerror(errno_save));
	}

	if (!info->force)
		if (dasd_info.open_count > 1)
			ERRMSG_EXIT(EXIT_BUSY, "Disk in use!\n");

	info->usage_count = dasd_info.open_count;
	info->devno       = dasd_info.devno;
        if (strncmp(dasd_info.type, "ECKD",4) != 0) {
		if (info->devno_specified) {
			ERRMSG_EXIT(EXIT_FAILURE, 
				    "%s: Unsupported disk type\n%x is not an "
				    "ECKD disk!\n", prog_name, info->devno);
		}
		else {
			ERRMSG_EXIT(EXIT_FAILURE, 
				    "%s: Unsupported disk type\n%s is not an "
				    "ECKD disk!\n", prog_name, info->devname);
		}
	}                                       
}                                    


/*
 * check the volume serial for special 
 * characters  and move blanks to the end
 */
static int check_volser(char *s, int devno)
{
	int i,j;

	for (i=0; i<6; i++) {
		if ((s[i] < 0x20) || (s[i] > 0x7a) ||
		    ((s[i] >= 0x21)&&(s[i] <= 0x22)) ||  /*  !"        */
		    ((s[i] >= 0x26)&&(s[i] <= 0x2f)) ||  /* &'()*+,-./ */
		    ((s[i] >= 0x3a)&&(s[i] <= 0x3f)) ||  /*  :;<=>?    */
		    ((s[i] >= 0x5b)&&(s[i] <= 0x60)))    /*  \]^_´     */
			s[i] = ' ';
		s[i] = toupper(s[i]);
	}
	s[6] = 0x00;		

	for (i=0; i<6; i++) {
		if (s[i] == ' ')
			for (j=i; j<6; j++)
				if (s[j] != ' ') {
					s[i] = s[j];
					s[j] = ' ';
					break;
				}
	}

	if (s[0] == ' ') {
		printf("Usage error, switching to default.\n");
		sprintf(s, "0X%04x", devno);
		for (i=0; i<6; i++)
			s[i] = toupper(s[i]);
		return -1;
	}

	return 0;
}


/*
 * do some blocksize checks
 */
static int check_param(char *s, size_t buffsize, format_data_t *data)
{
	int tmp = data->blksize;

	if ((tmp < 512) || (tmp > 4096)) {
		strncpy(s,"Blocksize must be one of the following positive "
			"integers:\n512, 1024, 2048, 4096.", buffsize);
		if (buffsize > 0)
			s[buffsize - 1] = '\0';
		return -1;
	}

	while (tmp > 0) {
		if ((tmp % 2) && (tmp != 1)) {
			strncpy(s,"Blocksize must be a power of 2.", buffsize);
			if (buffsize > 0)
				s[buffsize - 1] = '\0';
			return -1;
		}
		tmp /= 2;
	}

	return 0;
}


/*
 * ask the user to specify a blocksize
 */
static format_data_t ask_user_for_blksize(format_data_t params)
{
	char c, str[ERR_LENGTH], buffer[20];
	int i, rc;

	i = params.blksize;

	do {
		params.blksize = i;

		printf("Please enter the blocksize of the formatting [%d]: ", i);
		if (fgets(buffer, sizeof(buffer), stdin) == NULL)
			break;

		rc = sscanf(buffer,"%d%c", &params.blksize, &c);
		if ((rc == 2) && (c == '\n')) 
			rc = 1;
		if (rc == -1) 
			rc = 1; /* this happens, if enter is pressed */
		if (rc != 1) 
			printf(" -- wrong input, try again.\n");

		if (check_param(str, ERR_LENGTH, &params) < 0) {
			printf(" -- %s\n",str); 
			rc = 0; 
		}
	} while (rc != 1);

	return params;
}


/*
 * print all information needed to format the device
 */
static void dasdfmt_print_info(dasdfmt_info_t *info, volume_label_t *vlabel,
			       unsigned int cylinders, unsigned int heads,
			       format_data_t *p)
{
	char volser[6], vollbl[4];

	printf("Drive Geometry: %d Cylinders * %d Heads =  %d Tracks\n",
	       cylinders, heads, (cylinders * heads));

	printf("\nI am going to format the device ");
	if (info->devno_specified)
		printf("%x in the following way:\n", info->devno);
	else
		printf("%s in the following way:\n", info->devname);

	printf("   Device number of device : 0x%x\n",info->devno);
	printf("   Labelling device        : %s\n",
	       (info->writenolabel)?"no":"yes");

	if (!info->writenolabel) {
		vtoc_volume_label_get_label(vlabel, vollbl);
		printf("   Disk label              : %.4s\n", vollbl);
		vtoc_volume_label_get_volser(vlabel, volser);
		printf("   Disk identifier         : %.6s\n", volser);
	}
	printf("   Extent start (trk no)   : %u\n", p->start_unit);
	printf("   Extent end (trk no)     : %u\n", p->stop_unit);
	printf("   Compatible Disk Layout  : %s\n",
	       (p->intensity & DASD_FMT_INT_COMPAT)?"yes":"no");
	printf("   Blocksize               : %d\n", p->blksize);

	if (info->testmode) 
		printf("Test mode active, omitting ioctl.\n");
}


/*
 * get volser
 */
static int dasdfmt_get_volser(char * devname, char * volser)
{
	dasd_information_t  dasd_info;
	int blksize;
	int f;
	volume_label_t vlabel;

	if ((f = open(devname, O_RDONLY)) == -1)
		ERRMSG_EXIT(EXIT_FAILURE,"%s: Unable to open device %s: %s\n", 
			    prog_name, devname, strerror(errno));

	if (ioctl(f, BIODASDINFO, &dasd_info) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: (label pos) IOCTL BIODASD"
			    "INFO failed (%s).\n",prog_name, strerror(errno));

	if (ioctl(f, BLKSSZGET, &blksize) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: (label pos) IOCTL BLKSSZGET "
			    "failed (%s).\n", prog_name, strerror(errno));

	if (close(f) != 0)
		ERRMSG("%s: error during close: %s\ncontinuing...\n", 
		       prog_name, strerror(errno));

	if ((strncmp(dasd_info.type, "ECKD", 4) == 0) &&
	    (!dasd_info.FBA_layout)) {
		/* OS/390 and zOS compatible disk layout */
		vtoc_read_volume_label(devname, dasd_info.label_block * blksize, &vlabel);
		vtoc_volume_label_get_volser(&vlabel, volser);
		return 0;
        }
	else {
		return -1;
	}
}
	
/*
 * do all the labeling (volume label and initial VTOC)
 */
static void dasdfmt_write_labels(dasdfmt_info_t *info, volume_label_t *vlabel,
				 unsigned int cylinders, unsigned int heads)
{
	int        label_position;
	dasd_information_t  dasd_info;
	struct hd_geometry  geo;
	format4_label_t     f4;
	format5_label_t     f5;
	format7_label_t     f7;
	int rc, blksize;
	void *ipl1_record, *ipl2_record;
	int ipl1_record_len, ipl2_record_len;


	if (info->verbosity > 0) printf("Retrieving dasd information... ");

	if (ioctl(filedes, BIODASDINFO, &dasd_info) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: (write labels) IOCTL BIODASD"
			    "INFO failed (%s).\n",prog_name, strerror(errno));

	if (ioctl(filedes, BLKSSZGET, &blksize) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: (write labels) IOCTL BLKSSZGET "
			    "failed (%s).\n", prog_name, strerror(errno));

	/*
	 * Don't rely on the cylinders returned by HDIO_GETGEO, they might be
	 * to small. geo is only used to get the number of sectors, which may
	 * vary depending on the format.
	 */
	if (ioctl(filedes, HDIO_GETGEO, &geo) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: (write labels) IOCTL HDIO_GET"
			    "GEO failed (%s).\n", prog_name, strerror(errno));

	if (info->verbosity > 0) printf("ok\n");

	/* write empty bootstrap (initial IPL records) */
	if (info->verbosity > 0) printf("Writing empty bootstrap...\n");

	/*
	 * Note: ldl labels do not contain the key field
	 */
	if (info->cdl_format) {
		/* Prepare copy with key (CDL) */
		ipl1_record	= &ipl1;
		ipl2_record	= &ipl2;
		ipl1_record_len	= sizeof(ipl1);
		ipl2_record_len	= sizeof(ipl2);
	} else {
		/* Prepare copy without key (LDL) */
		ipl1_record	= ipl1.data;
		ipl2_record	= ipl2.data;
		ipl1_record_len	= sizeof(ipl1.data);
		ipl2_record_len	= sizeof(ipl2.data);
	}

	if (lseek(filedes, 0, SEEK_SET) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: lseek command 0 failed "
			    "(%s)\n", prog_name, strerror(errno));

	rc = write(filedes, ipl1_record, ipl1_record_len);
	if (rc != ipl1_record_len)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: Writing the bootstrap IPL1 "
			    "failed, only wrote %d bytes.\n", prog_name, rc);

	label_position = blksize;
	rc = lseek(filedes, label_position, SEEK_SET);
	if (rc != label_position)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: lseek command to %i failed "
			    "(%s).\n", prog_name, label_position, 
			    strerror(errno));

	rc = write(filedes, ipl2_record, ipl2_record_len);
	if (rc != ipl2_record_len)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: Writing the bootstrap IPL2 "
			    "failed, only wrote %d bytes.\n", prog_name, rc);

	/* write VTOC */
	vtoc_init_format4_label(&f4, USABLE_PARTITIONS, geo.cylinders,
				cylinders, heads, geo.sectors, blksize,
				dasd_info.dev_type);

	vtoc_init_format5_label(&f5);
	vtoc_init_format7_label(&f7);
	vtoc_set_freespace(&f4, &f5, &f7, '+', 0, FIRST_USABLE_TRK,
			   (cylinders * heads - 1), cylinders, heads);

	label_position = dasd_info.label_block * blksize;

	if (info->verbosity > 0) printf("Writing label...\n");

	rc = lseek(filedes, label_position, SEEK_SET);
	if (rc != label_position)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: lseek command to %i failed "
			    "(%s).\n", prog_name, label_position,
			    strerror(errno));

	/*
	 * Note: cdl volume labels do not contain the 'formatted_blocks' part
	 * and ldl labels do not contain the key field
	 */
	if (info->cdl_format)
		rc = write(filedes, vlabel, (sizeof(*vlabel) -
					     sizeof(vlabel->formatted_blocks)));
	else {
		vlabel->ldl_version = 0xf2; /* EBCDIC '2' */
		vlabel->formatted_blocks = cylinders * heads * geo.sectors;
		rc = write(filedes, &vlabel->vollbl, (sizeof(*vlabel)
						     - sizeof(vlabel->volkey)));
	}

	if (((rc != sizeof(*vlabel) - sizeof(vlabel->formatted_blocks)) &&
	     info->cdl_format) ||
	    ((rc != (sizeof(*vlabel) - sizeof(vlabel->volkey))) &&
	     (!info->cdl_format)))
		ERRMSG_EXIT(EXIT_FAILURE, "%s: Error writing volume label "
			    "(%d).\n", prog_name, rc);

	if (info->verbosity > 0) printf("Writing VTOC... ");

	label_position = (VTOC_START_CC * heads + VTOC_START_HH) *
		geo.sectors * blksize;

	rc = lseek(filedes, label_position, SEEK_SET);
	if (rc != label_position)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: lseek command to %i failed "
			    "(%s).\n", prog_name, label_position, 
			    strerror(errno));
			
	/* write VTOC FMT4 DSCB */
	rc = write(filedes, &f4, sizeof(format4_label_t));
	if (rc != sizeof(format4_label_t))
		ERRMSG_EXIT(EXIT_FAILURE, "%s: Error writing FMT4 label "
			    "(%d).\n", prog_name, rc);

	label_position += blksize;

	rc = lseek(filedes, label_position, SEEK_SET);
	if (rc != label_position)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: lseek to %i failed (%s).\n",
			    prog_name, label_position, strerror(errno));

	/* write VTOC FMT5 DSCB */
	rc = write(filedes, &f5, sizeof(format5_label_t));
	if (rc != sizeof(format5_label_t))
		ERRMSG_EXIT(EXIT_FAILURE, "%s: Error writing FMT5 label "
			    "(%d).\n", prog_name, rc);

	if ((cylinders * heads) > BIG_DISK_SIZE) {
		label_position += blksize;

		rc = lseek(filedes, label_position, SEEK_SET);
		if (rc != label_position)
			ERRMSG_EXIT(EXIT_FAILURE, "%s: lseek to %i failed "
				    "(%s).\n", prog_name, label_position,
				    strerror(errno));

		/* write VTOC FMT 7 DSCB (only on big disks) */
		rc = write(filedes, &f7, sizeof(format7_label_t));
		if (rc != sizeof(format7_label_t))
			ERRMSG_EXIT(EXIT_FAILURE, "%s: Error writing FMT7 "
				    "label (rc=%d).\n", prog_name, rc);
	}

	fsync(filedes);

	if (info->verbosity > 0) printf("ok\n");
}


/*
 * formats the disk cylinderwise
 */
static void dasdfmt_format(dasdfmt_info_t *info, unsigned int cylinders,
			   unsigned int heads, format_data_t *format_params)
{
	format_data_t format_step;
	int j, cyl, tmp, p1, p2, hashcount = 0;
	unsigned int k;

	if (info->print_hashmarks) {
		if (info->hashstep < reqsize)
			info->hashstep = reqsize;
		if ((info->hashstep < 1) || (info->hashstep > 1000)) {
			printf("Hashmark increment is not in range <1,1000>, "
			       "using the default.\n");
			info->hashstep = 10;
		}
		
		printf("Printing hashmark every %d cylinders.\n", 
		       info->hashstep);
	}

	format_step.blksize   = format_params->blksize;
	format_step.intensity = format_params->intensity;
		
	k = 0;
	cyl = 1;
	if (info->print_progressbar || info->print_hashmarks)
		printf("\n");

	while (1) {
		p1 = -1; 
		p2 =  0;
		if (k + heads * reqsize >= format_params->stop_unit)
			reqsize = 1;
		format_step.start_unit = k;
		format_step.stop_unit  = k + reqsize * heads - 1;

		if (cyl == 1)
			format_step.start_unit += 1;

		if (ioctl(filedes, BIODASDFMT, &format_step) != 0)
			ERRMSG_EXIT(EXIT_FAILURE,"%s: (format cylinder) IOCTL "
				    "BIODASDFMT failed. (%s)\n",
				    prog_name, strerror(errno));

		if (info->print_progressbar) {
			printf("cyl %7d of %7d |", cyl, cylinders);
			p2 = p1;
			p1 = cyl*100/cylinders;
			if (p1 != p2)
			{
				/* percent value has changed */
				tmp = cyl*50/cylinders;
				for (j=1; j<=tmp; j++)
					printf("#");
				for (j=tmp+1; j<=50; j++) 
					printf("-");
				printf("|%3d%%", p1);
			}
			printf("\r");
			fflush(stdout);
		}

		if (info->print_hashmarks)
			if ((cyl / info->hashstep - hashcount) != 0) {
				printf("#");
				fflush(stdout);
				hashcount++;
			}
		if (info->print_percentage) {
			printf("cyl %7d of %7d |%3d%%\n", cyl, cylinders,
			       cyl*100/cylinders);
			fflush(stdout);
		}

		if (k % heads == 0) {
			k += reqsize * heads;
			cyl += reqsize;
		}
		else 
			k += format_params->stop_unit % heads;

		if (k > format_params->stop_unit) 
			break;
	}

	if (info->print_progressbar || info->print_hashmarks)
		printf("\n\n");	
}


/*
 *
 */
static void dasdfmt_prepare_and_format (dasdfmt_info_t *info,
					unsigned int cylinders,
					unsigned int heads,
					format_data_t *p)
{
	format_data_t temp = {
		start_unit: 0,
		stop_unit:  0,
		blksize:    p->blksize,
		intensity: ((p->intensity & ~DASD_FMT_INT_FMT_NOR0)
			    | DASD_FMT_INT_INVAL)
	};

	if (info->verbosity > 0) printf("Detaching the device...\n");

	if (ioctl(filedes, BIODASDDISABLE, p) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: (prepare device) IOCTL "
			    "BIODASDDISABLE failed. (%s)\n", prog_name, 
			    strerror(errno));
	disk_disabled = 1;

	if (info->verbosity > 0) printf("Invalidate first track...\n");

	if (ioctl(filedes, BIODASDFMT, &temp) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: (invalidate first track) IOCTL "
			    "BIODASDFMT failed. (%s)\n", prog_name, 
			    strerror(errno));

	/* except track 0 from standard formatting procss */
	p->start_unit = 1;

	dasdfmt_format(info, cylinders, heads, p);

	if (info->verbosity > 0) printf("formatting tracks complete...\n");

	temp.intensity = p->intensity;

	if (info->verbosity > 0) printf("Revalidate first track...\n");

	if (ioctl(filedes, BIODASDFMT, &temp) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: (re-validate first track) IOCTL"
			    " BIODASDFMT failed (%s)\n", prog_name, 
			    strerror(errno));

	if (info->verbosity > 0) printf("Re-accessing the device...\n");

	if (ioctl(filedes, BIODASDENABLE, p) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: (prepare device) IOCTL "
			    "BIODASDENABLE failed. (%s)\n", prog_name, 
			    strerror(errno));
	disk_disabled = 0;
}


/*
 *
 */
static void do_format_dasd(dasdfmt_info_t *info, format_data_t *p, 
			   volume_label_t *vlabel)
{
	char               inp_buffer[5];
	dasd_information_t  dasd_info;
	struct dasd_eckd_characteristics *characteristics;
	unsigned int cylinders, heads;

	if (info->verbosity > 0) printf("Retrieving disk geometry...\n");

	if (ioctl(filedes, BIODASDINFO, &dasd_info) != 0)
		ERRMSG_EXIT(EXIT_FAILURE, "%s: (retrieving disk information) "
			    "IOCTL BIODASDINFO failed (%s).\n",
			    prog_name, strerror(errno));

	characteristics =
		(struct dasd_eckd_characteristics *) &dasd_info.characteristics;
	if (characteristics->no_cyl == LV_COMPAT_CYL &&
	    characteristics->long_no_cyl)
		cylinders = characteristics->long_no_cyl;
	else
		cylinders = characteristics->no_cyl;
	heads = characteristics->trk_per_cyl;

	p->start_unit = 0;
	p->stop_unit  = (cylinders * heads) - 1;

	if (info->writenolabel) {
		if (cylinders > LV_COMPAT_CYL && !info->withoutprompt) {
			printf("\n--->> ATTENTION! <<---\n");
			printf("You specified to write no labels to a"
			       " volume with more then %u cylinders.\n"
			       "Cylinders above this limit will not be"
			       " accessable as a linux partition!\n"
			       "Type \"yes\" to continue, no will leave"
			       " the disk untouched: ", LV_COMPAT_CYL);
			if (fgets(inp_buffer, sizeof(inp_buffer), stdin) == NULL)
				return;
			if (strcasecmp(inp_buffer, "yes") &&
			    strcasecmp(inp_buffer, "yes\n")) {
				printf("Omitting ioctl call (disk will "
					"NOT be formatted).\n");
				return;
			}
		}
	} else {
        	if (!info->labelspec && !info->keep_volser) {
			char buf[7];

	               	sprintf(buf, "0X%04x", info->devno);
			check_volser(buf, info->devno);
			vtoc_volume_label_set_volser(vlabel, buf);
		}

                if (p->intensity & DASD_FMT_INT_COMPAT) {
			info->cdl_format = 1;
			vtoc_volume_label_set_label(vlabel, "VOL1");
			vtoc_volume_label_set_key(vlabel, "VOL1");
			vtoc_set_cchhb(&vlabel->vtoc, 0x0000, 0x0001, 0x01);
		} else
			vtoc_volume_label_set_label(vlabel, "LNX1");
       	}

	if ((info->verbosity > 0) || (!info->withoutprompt)) 
		dasdfmt_print_info(info, vlabel, cylinders, heads, p);

	if (!info->testmode) {
		if (!info->withoutprompt) {
			printf("\n--->> ATTENTION! <<---\n");
			printf("All data of that device will be lost.\nType "
			       "\"yes\" to continue, no will leave the disk "
			       "untouched: ");
			if (fgets(inp_buffer, sizeof(inp_buffer), stdin) == NULL)
				return;
			if (strcasecmp(inp_buffer,"yes") &&
			    strcasecmp(inp_buffer,"yes\n")) {
				printf("Omitting ioctl call (disk will "
					"NOT be formatted).\n");
				return;
			}
		}

		if (!((info->withoutprompt)&&(info->verbosity<1)))
			printf("Formatting the device. This may take a "
			       "while (get yourself a coffee).\n");

		dasdfmt_prepare_and_format(info, cylinders, heads, p);

		printf("Finished formatting the device.\n");

		if (!info->writenolabel) 
			dasdfmt_write_labels(info, vlabel, cylinders, heads);

		printf("Rereading the partition table... ");
		if (reread_partition_table()) {
			ERRMSG("%s: error during rereading the partition "
			       "table: %s.\n", prog_name, strerror(errno));
		} else
			printf("ok\n");
	}
}


int main(int argc,char *argv[]) 
{
	dasdfmt_info_t info;
	volume_label_t vlabel;
	char old_volser[7];

	char dev_filename[PATH_MAX];
	char str[ERR_LENGTH];
	char buf[7];

	char *devno_param_str   = NULL;
	char *blksize_param_str = NULL;
	char *reqsize_param_str = NULL;
	char *hashstep_str      = NULL;

	int rc, index;

	/* Establish a handler for interrupt signals. */
	signal (SIGTERM, program_interrupt_signal);
	signal (SIGINT,  program_interrupt_signal);
	signal (SIGQUIT, program_interrupt_signal);

	/******************* initialization ********************/
	prog_name = argv[0];

	/* set default values */
	init_info(&info);
	vtoc_volume_label_init(&vlabel);

	format_params.blksize   = DEFAULT_BLOCKSIZE;
	format_params.intensity = DASD_FMT_INT_COMPAT;

	/*************** parse parameters **********************/

	while (1)
	{
		rc=getopt_long(argc, argv, dasdfmt_getopt_string,
			       dasdfmt_getopt_long_options, &index);

		switch (rc) 
		{
		case 'F':
 			info.force=1;
			break;

		case 'C':
                	format_params.intensity |= DASD_FMT_INT_COMPAT;
			break;

                case 'd' :
                        if (strncmp(optarg,"cdl",3)==0)
			{
				format_params.intensity |= DASD_FMT_INT_COMPAT;
				if (info.writenolabel)
				{
					printf("WARNING: using the cdl " \
					       "format without writing a " \
					       "label doesn't make much " \
					       "sense!\n");
					exit(1);
				}
			}
			else if (strncmp(optarg,"ldl",3)==0)
				format_params.intensity &= ~DASD_FMT_INT_COMPAT;
			else
			{
				printf("%s is not a valid option!\n", optarg);
				exit(1);
			}
                        break;

		case 'y':
			info.withoutprompt=1;
			break;

		case 'z':
                	format_params.intensity |= DASD_FMT_INT_FMT_NOR0;
			break;

		case 't':
			info.testmode=1;
			break;

		case 'p':
			if (!(info.print_hashmarks || info.print_percentage))
				info.print_progressbar = 1;
			break;

		case 'm':
			if (!(info.print_progressbar || info.print_percentage))
			{
				hashstep_str=optarg;
				info.print_hashmarks=1;
			}
			break;

		case 'P':
			if (!(info.print_hashmarks || info.print_progressbar))
				info.print_percentage = 1;
			break;

		case 'v':
			info.verbosity=1;
			break;

		case 'h':
			exit_usage(0);

		case 'V':
			print_version();
			exit(0);

		case 'l':
	               	strncpy(buf, optarg, 6);
			if (check_volser(buf, 0) < 0)
				break;
			vtoc_volume_label_set_volser(&vlabel,buf);
			info.labelspec=1;
			break;

		case 'L':
			if (format_params.intensity & DASD_FMT_INT_COMPAT)
			{
				printf("WARNING: using the cdl format " \
				       "without writing a label doesn't " \
				       "make much sense!\n");
				exit(1);
			}
			info.writenolabel=1;
			break;

		case 'b' :
			blksize_param_str=optarg;
			info.blksize_specified=1;
			break;
		case 'r':
			reqsize_param_str = optarg;
			info.reqsize_specified = 1;
			break;
		case 'n' :
			devno_param_str=optarg;
			info.devno_specified=1;
			break;
		
		case 'f' :
			strncpy(dev_filename, optarg, PATH_MAX);
			info.node_specified=1;
			break;
		case 'k' :
			info.keep_volser=1;
			break;
		case -1:
			/* End of options string - start of devices list */
			info.device_id = optind;
			break;
		default:
			ERRMSG_EXIT(EXIT_MISUSE,
				"Try '%s --help' for more"
				" information.\n",prog_name);
		}
        	if (rc==-1) break; // exit loop if finished
	}

	CHECK_SPEC_MAX_ONCE(info.blksize_specified, "blocksize");
	CHECK_SPEC_MAX_ONCE(info.labelspec, "label");
	CHECK_SPEC_MAX_ONCE(info.writenolabel, "omit-label-writing flag");

	if (info.devno_specified)
		PARSE_PARAM_INTO(info.devno, devno_param_str, 16,
				 "device number");
	if (info.blksize_specified)
		PARSE_PARAM_INTO(format_params.blksize,blksize_param_str,10,
				 "blocksize");
	if (info.reqsize_specified) {
		PARSE_PARAM_INTO(reqsize, reqsize_param_str, 10, "requestsize");
		if (reqsize < 1 || reqsize > 255)
			ERRMSG_EXIT(EXIT_FAILURE,
				    "invalid requestsize %d specified\n",
				    reqsize);
	} else
		reqsize = DEFAULT_REQUESTSIZE;
	if (info.print_hashmarks)
		PARSE_PARAM_INTO(info.hashstep, hashstep_str,10,"hashstep");

	get_device_name(&info, dev_filename, argc, argv);

        if (!info.blksize_specified)
                format_params = ask_user_for_blksize(format_params);

	if (info.keep_volser) {
		if(info.labelspec) {
			ERRMSG_EXIT(EXIT_MISUSE,"%s: The -k and -l options are mutually exclusive\n",
			       prog_name);
		}
		if(!(format_params.intensity & DASD_FMT_INT_COMPAT)) {
			printf("WARNING: VOLSER cannot be kept " \
			       "when using the ldl format!\n");
			exit(1);
		}
			
		if(dasdfmt_get_volser(info.devname, old_volser) == 0)
			vtoc_volume_label_set_volser(&vlabel, old_volser);
		else
			ERRMSG_EXIT(EXIT_FAILURE,"%s: VOLSER not found on device %s\n", 
			       prog_name, info.devname);
			
	}

	if ((filedes = open(info.devname, O_RDWR)) == -1)
		ERRMSG_EXIT(EXIT_FAILURE,"%s: Unable to open device %s: %s\n", 
			    prog_name, info.devname, strerror(errno));

	check_disk(&info);

	if (check_param(str, ERR_LENGTH, &format_params) < 0)
		ERRMSG_EXIT(EXIT_MISUSE, "%s: %s\n", prog_name, str);

	do_format_dasd(&info, &format_params, &vlabel);

	if (close(filedes) != 0)
		ERRMSG("%s: error during close: %s\ncontinuing...\n", 
		       prog_name, strerror(errno));

	return 0;
}
