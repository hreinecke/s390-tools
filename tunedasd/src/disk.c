/*
 * s390-tools/tunedasd/src/disk.c
 *   Functions to handle DASD-IOCTLs.
 *
 * Copyright IBM Corp. 2004, 2006
 *
 * Author(s): Horst Hummel (horst.hummel@de.ibm.com)
 */

#include "disk.h"
#include "tunedasd.h"

#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>


#define BUS_ID_SIZE 30

/*
 * DASD DEFINITIONS (copied from dasd.h) 
 */

#define DASD_IOCTL_LETTER 'D'

/* 
 * struct profile_info_t
 * holds the profiling information 
 */
typedef struct dasd_profile_info_t {
        unsigned int dasd_io_reqs;	  /* # of requests processed at all */
        unsigned int dasd_io_sects;	  /* # of sectors processed at all */
        unsigned int dasd_io_secs[32];	  /* request's sizes */
        unsigned int dasd_io_times[32];	  /* requests's times */
        unsigned int dasd_io_timps[32];	  /* requests's times per sector */
        unsigned int dasd_io_time1[32];	  /* time from build to start */
        unsigned int dasd_io_time2[32];	  /* time from start to irq */
        unsigned int dasd_io_time2ps[32]; /*time from start to irq */
        unsigned int dasd_io_time3[32];	  /* time from irq to end */
        unsigned int dasd_io_nr_req[32];  /* # of requests in chanq */
} dasd_profile_info_t;


/* 
 * struct attrib_data_t
 * represents the operation (cache) bits for the device.
 * Used in DE to influence caching of the DASD.
 */
typedef struct attrib_data_t {
	unsigned char   operation:3;     /* cache operation mode */
	unsigned char   reserved:5;      
	unsigned short  nr_cyl;          /* no of cyliners for read ahaed */
	unsigned char   reserved2[29];   /* for future use */
} __attribute__ ((packed)) attrib_data_t;

/* definition of operation (cache) bits within attributes of DE */
#define DASD_NORMAL_CACHE  0x0
#define DASD_BYPASS_CACHE  0x1
#define DASD_INHIBIT_LOAD  0x2
#define DASD_SEQ_ACCESS    0x3
#define DASD_SEQ_PRESTAGE  0x4
#define DASD_REC_ACCESS    0x5

/*
 * Data returned by Sense Path Group ID (SNID)
 */
struct dasd_snid_data {
	struct {
		__u8 group:2;
		__u8 reserve:2;
		__u8 mode:1;
		__u8 res:3;
	} __attribute__ ((packed)) path_state;
	__u8 pgid[11];
} __attribute__ ((packed));

struct dasd_snid_ioctl_data {
	struct dasd_snid_data data;
	__u8 path_mask;
} __attribute__ ((packed));


/*
 * DASD-IOCTLs (copied from dasd.h) 
 */
/* Issue a reserve/release command, rsp. */
#define BIODASDRSRV    _IO (DASD_IOCTL_LETTER,2) /* reserve */
#define BIODASDRLSE    _IO (DASD_IOCTL_LETTER,3) /* release */
#define BIODASDSLCK    _IO (DASD_IOCTL_LETTER,4) /* steal lock */
/* reset profiling information of a device */
#define BIODASDPRRST   _IO (DASD_IOCTL_LETTER,5)

/* retrieve profiling information of a device */
#define BIODASDPRRD    _IOR (DASD_IOCTL_LETTER,2,dasd_profile_info_t)
/* Get Attributes (cache operations) */
#define BIODASDGATTR   _IOR(DASD_IOCTL_LETTER,5,attrib_data_t)

/* Set Attributes (cache operations) */
#define BIODASDSATTR   _IOW (DASD_IOCTL_LETTER,2,attrib_data_t) 

/* Get Sense Path Group ID (SNID) data */
#define BIODASDSNID    _IOWR(DASD_IOCTL_LETTER, 1, struct dasd_snid_ioctl_data)


/* id definition for profile items */
enum prof_id {
	prof_reqs       =  0, 
	prof_sects      =  1,
	prof_sizes      =  2,
	prof_total	=  3,
	prof_totsect	=  4,
	prof_start	=  5,
	prof_irq	=  6,
	prof_irqsect	=  7,
	prof_end        =  8,
	prof_queue      =  9
};

/* Mapping for caching modes */
static struct {
        char* mode; 
        int id;
} mode_list[] = {
        { "normal",     DASD_NORMAL_CACHE },
        { "bypass",     DASD_BYPASS_CACHE },
        { "inhibit",    DASD_INHIBIT_LOAD },
        { "sequential", DASD_SEQ_ACCESS   },
        { "prestage",   DASD_SEQ_PRESTAGE },
        { "record",     DASD_REC_ACCESS   }
};	
	

/*
 * Check for valid cache parameters.
 */
int
check_cache (char* cache)
{
	unsigned int i;

	/* Check for valid parameters */
	for (i = 0; i < sizeof(mode_list)/sizeof(mode_list[0]); i++) {
		if (!strcmp (cache, mode_list[i].mode)) {
			return mode_list[i].id;
		}
	}
	error_print ("Invalid caching mode '%s' given", cache);
	return -1;
}

/*
 * Retrieve name of cache mode identified by number.
 */
char *
get_cache_name (int id)
{
	unsigned int i;
	
	for (i = 0; i < sizeof (mode_list) / sizeof (mode_list[0]);
	     i++) {
		if (id == mode_list[i].id) {
			return mode_list[i].mode;
		}
	}
	return "<unknown>";
}


/*
 * Check for valid number of cylinders.
 */
int
check_no_cyl (char* no_cyl)
{
	int nr_cyl;
	char* err_ptr;
	if (!no_cyl) {
		/* set default = 2 Cylinders - if option is missing */
		return 2;
	} else {
		err_ptr = NULL;
		nr_cyl = strtoul (no_cyl, &err_ptr, 0);
		
		if ((errno) ||
		    (*err_ptr != '\0')) { 
			error_print ("Invalid number of cylinders given '%s'",
				     no_cyl);
			return -1;
		}
	}
	return nr_cyl;
}


/*
 * Check for valid profile item parameters.
 */
int
check_prof_item (char* prof_item)
{
        /* Mapping for profile items */
	static struct {
		char* item; 
		int id;
	} prof_list[] = {
		{ "reqs",    prof_reqs    },
		{ "sects",   prof_sects   },
		{ "sizes",   prof_sizes   },
		{ "total",   prof_total   },
		{ "totsect", prof_totsect },
		{ "start",   prof_start   },
		{ "irq",     prof_irq     },   
		{ "irqsect", prof_irqsect },   
		{ "end",     prof_end     },
		{ "queue",   prof_queue   }
	};
	
	unsigned int i;

	/* Check for valid parameters */
	for (i = 0; i < sizeof(prof_list)/sizeof(prof_list[0]); i++) {
		if (!strcmp (prof_item, prof_list[i].item)) {
			return prof_list[i].id;
		}
	}
	error_print ("Invalid profile item '%s' given", prof_item);
	return -1;
}


/*
 * Get the caching algorithm used for the channel programs of this device.
 * 'cache' is the caching mode (see ESS docu for more info) and 'no_cyl'
 * the number of cylinders to be cached.
 */
int
disk_get_cache (char* device)
{
	int fd;
	attrib_data_t attrib_data;
	
	/* Open device file */
	fd = open (device, O_RDONLY);
	if (fd == -1) {
		error_print ("<%s> - %s", device, strerror (errno));
		return -1;
	}

	/* Get the given caching attributes */
	if (ioctl (fd, BIODASDGATTR, &attrib_data)) {
		error_print ("Could not get cache attributes for device <%s>",
			     device);
		close (fd);
		return -1;
	}

	printf ("%s (%i cyl)\n",
		get_cache_name(attrib_data.operation),
		attrib_data.nr_cyl);

	close (fd);
	return 0;
}


/*
 * Set the caching algorithm used for the channel programs of this device.
 * 'cache' is the caching mode (see ESS docu for more info) and 'no_cyl'
 * the number of cylinders to be cached.
 */
int
disk_set_cache (char* device, char* cache, char* no_cyl)
{
	int fd;
	attrib_data_t attrib_data;
	
        /* get caching mode and # cylinders */
	attrib_data.operation = check_cache (cache);
	attrib_data.nr_cyl = check_no_cyl (no_cyl);

	if (attrib_data.nr_cyl > 40) {
		printf ("WARNING: This is a very large number of "
			"cylinders ;) %i\n", 
			attrib_data.nr_cyl);
	}

	/* Open device file */
	fd = open (device, O_RDONLY);
	if (fd == -1) {
		error_print ("<%s> - %s", device, strerror (errno));
		return -1;
	}

	/* Set the given caching attributes */
	printf ("Setting cache mode for device <%s>...\n", device);
	if (ioctl (fd, BIODASDSATTR, &attrib_data)) {
		error_print ("Could not set caching for device <%s>", device);
		close (fd);
		return -1;
	}

	printf ("Done.\n");
	close (fd);
	return 0;
}


/*
 * Reserve the device.
 */
int 
disk_reserve (char* device)
{
	int fd;

	/* Open device file */
	fd = open (device, O_RDONLY);
	if (fd == -1) {
		error_print ("<%s> - %s", device, strerror (errno));
		return -1;
	}

	/* Reserve device */
	printf ("Reserving device <%s>...\n", device);
	if (ioctl (fd, BIODASDRSRV)) {
		error_print ("Could not reserve device <%s>", device);
		close (fd);
		return -1;
	}

	printf ("Done.\n");
	close (fd);
	return 0;
}


/*
 * Release the device.
 */
int 
disk_release (char* device)
{
	int fd;

	/* Open device file */
	fd = open (device, O_RDONLY);
	if (fd == -1) {
		error_print ("<%s> - %s", device, strerror (errno));
		return -1;
	}

	/* Release device */
	printf ("Releasing device <%s>...\n", device);
	if (ioctl (fd, BIODASDRLSE)) {
		error_print ("Could not release device <%s>", device);
		close (fd);
		return -1;
	}

	printf ("Done.\n");
	close (fd);
	return 0;
}


/*
 * Unconditional reserve the device.
 * This means to reserve the device even if it was already reserved.
 * The current reserve is broken (steal lock).
 */
int 
disk_slock (char* device)
{
	int fd;

	/* Open device file */
	fd = open (device, O_RDONLY);
	if (fd == -1) {
		error_print ("<%s> - %s", device, strerror (errno));
		return -1;
	}

	/* Unconditional reserve device */
	printf ("Unconditional reserving device <%s>...\n", device);
	if (ioctl (fd, BIODASDSLCK)) {
		error_print ("Could not unconditional reserve device <%s>", 
			     device);
		close (fd);
		return -1;
	}

	printf ("Done.\n");
	close (fd);
	return 0;
}


/*
 * Uses the Sense Path Group ID (SNID) ioctl to find out if
 * a device is reserved to it's path group.
 */
int
disk_query_reserve_status(char* device)
{
	int fd;
	struct dasd_snid_ioctl_data snid;

	/* Open device file */
	fd = open (device, O_RDONLY);
	if (fd == -1) {
		error_print ("<%s> - %s", device, strerror (errno));
		return -1;
	}
	snid.path_mask = 0;
	/* Release device */
	if (ioctl(fd, BIODASDSNID, &snid)) {
		error_print("Could not read reserve status"
			    " for device <%s>", device);
		close (fd);
		return -1;
	}
	switch (snid.data.path_state.reserve) {
	case 0:
		printf("none\n");
		break;
	case 1:
		printf("implicit\n");
		break;
	case 2:
		printf("other\n");
		break;
	case 3:
		printf("reserved\n");
		break;
	}
	close (fd);
	return 0;
}

int 
disk_profile_summary (dasd_profile_info_t dasd_profile_info)
{
	int factor, i;

        /* prevent counter 'overflow' on output */
	for (factor = 1; (dasd_profile_info.dasd_io_reqs / factor) > 9999999;
	     factor *= 10) ;
	/* print the profile info */
	printf("\n%d dasd I/O requests\n", dasd_profile_info.dasd_io_reqs);
	printf("with %u sectors(512B each)\n",
	       dasd_profile_info.dasd_io_sects);
	printf("Scale factor is %d \n\n", factor);

	printf("   __<4    ___8    __16    __32    __64 "
	       "   _128    _256    _512    __1k    __2k "
	       "   __4k    __8k    _16k    _32k    _64k "
	       "   128k\n");
	
	printf("   _256    _512    __1M    __2M    __4M "
	       "   __8M    _16M    _32M    _64M    128M "
	       "   256M    512M    __1G    __2G    __4G "
	       "   _>4G\n");

	printf("Histogram of sizes (512B secs)\n");
	for (i = 0; i < 16; i++) {
		printf("%7d ", dasd_profile_info.dasd_io_secs[i] / factor);
	}
	printf("\n");
	for (; i < 32; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_secs[i] / factor);
	}
	printf("\n");

	printf("Histogram of I/O times (microseconds)\n");
	for (i = 0; i < 16; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_times[i] / factor);
	}
	printf("\n");
	for (; i < 32; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_times[i] / factor);
	}
	printf("\n");

	printf("Histogram of I/O times per sector\n");
	for (i = 0; i < 16; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_timps[i] / factor);
	}
	printf("\n");
	for (; i < 32; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_timps[i] / factor);
	}
	printf("\n");

	printf("Histogram of I/O time till ssch\n");
	for (i = 0; i < 16; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_time1[i] / factor);
	}
	printf("\n");
	for (; i < 32; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_time1[i] / factor);
	}
	printf("\n");

	printf("Histogram of I/O time between ssch and irq\n");
	for (i = 0; i < 16; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_time2[i] / factor);
	}
	printf("\n");
	for (; i < 32; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_time2[i] / factor);
	}
	printf("\n");

	printf("Histogram of I/O time between ssch and irq per "
		"sector\n");
	for (i = 0; i < 16; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_time2ps[i] / factor);
	}
	printf("\n");
	for (; i < 32; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_time2ps[i] / factor);
	}
	printf("\n");

	printf("Histogram of I/O time between irq and end\n");
	for (i = 0; i < 16; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_time3[i] / factor);
	}
	printf("\n");
	for (; i < 32; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_time3[i] / factor);
	}
	printf("\n");

	printf("# of req in chanq at enqueuing (1..32) \n");
	for (i = 0; i < 16; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_nr_req[i] / factor);
	}
	printf("\n");
	for (; i < 32; i++) {
		printf("%7d ",	dasd_profile_info.dasd_io_nr_req[i] / factor);
	}
	printf("\n");

	return 0;
}


int
disk_profile_item (dasd_profile_info_t dasd_profile_info, char *prof_item)
{
	int i;

	/* Check for given profile item*/
	switch (check_prof_item (prof_item)) {
	case prof_reqs:
		printf ("%d", dasd_profile_info.dasd_io_reqs);
		break;
	case prof_sects:
		printf ("%d", dasd_profile_info.dasd_io_sects);
		break;
	case prof_sizes:
		for (i = 0; i < 32; i++) {
			printf ("%10d|", dasd_profile_info.dasd_io_secs[i]);
		}
		break;
	case prof_total:
		for (i = 0; i < 32; i++) {
			printf ("%10d|", dasd_profile_info.dasd_io_times[i]);
		}
		break;
	case prof_totsect:
		for (i = 0; i < 32; i++) {
			printf ("%10d|", dasd_profile_info.dasd_io_timps[i]);
		}
		break;
	case prof_start:
		for (i = 0; i < 32; i++) {
			printf ("%10d|", dasd_profile_info.dasd_io_time1[i]);
		}
		break;
	case prof_irq:
		for (i = 0; i < 32; i++) {
			printf ("%10d|", dasd_profile_info.dasd_io_time2[i]);
		}
		break;
	case prof_irqsect:
		for (i = 0; i < 32; i++) {
			printf ("%10d|", dasd_profile_info.dasd_io_time2ps[i]);
		}
		break;
	case prof_end:
		for (i = 0; i < 32; i++) {
			printf ("%10d|", dasd_profile_info.dasd_io_time3[i]);
		}
		break;
	case prof_queue:
		for (i = 0; i < 32; i++) {
			printf ("%10d|", dasd_profile_info.dasd_io_nr_req[i]);
		}
		break;
	}

	printf ("\n");
	return 0;
}

/*
 * Get and print the profiling info of the device.
 */
int 
disk_profile (char* device, char* prof_item)
{
	int fd, rc;
	dasd_profile_info_t dasd_profile_info;

	/* Open device file */
	fd = open (device, O_RDONLY);
	if (fd == -1) {
		error_print ("<%s> - %s", device, strerror (errno));
		return -1;
	}

	/* Get the profile info */
	if (ioctl (fd, BIODASDPRRD, &dasd_profile_info)) {
		switch (errno) {
		case EIO:		/* profiling is not active */
			error_print ("Profiling (on device <%s>) is not "
				     "active.", device);
			break;
		default:  		/* all other errors */
			error_print ("Could not get profile info for device "
				     "<%s>.", device);
		}
		close (fd);
		return -1;
	}
	/* Check for profile item or summary */
	if (!prof_item) {
		rc = disk_profile_summary (dasd_profile_info);
	} else {
		rc = disk_profile_item (dasd_profile_info, prof_item);
	}
	
	close (fd);
	return rc;
}


/*
 * Reset the profiling counters of the device.
 */
int 
disk_reset_prof (char* device)
{
	int fd;

	/* Open device file */
	fd = open (device, O_RDONLY);
	if (fd == -1) {
		error_print ("<%s> - %s", device, strerror (errno));
		return -1;
	}

	/* reset profile info */
	printf ("Resetting profile info for device <%s>...\n", device);
	if (ioctl (fd, BIODASDPRRST)) {
		error_print ("Could not reset profile info for device <%s>",
			     device);
		close (fd);
		return -1;
	}

	printf ("Done.\n");
	close (fd);
	return 0;
}

