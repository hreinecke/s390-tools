/*
 * zipl boot loader error codes
 *
 * Copyright IBM Corp. 2012
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#ifndef __ERROR_H__
#define __ERROR_H__

/************************************************************************
 * zipl boot loader error codes (use decimal range 4500-4549)
 *
 * The error numbers are ABI. Do not change them!
 ************************************************************************/

/* I/O error: Enabling the IPL device failed */
#define EENABLE_DEV		0x00004500

/* I/O error: Disabling the IPL device failed */
#define EDISABLE_DEV		0x00004501

/* I/O error: The start subchannel command failed */
#define ESSCH			0x00004502

/* Internal error: The IPL type is incorrect */
#define EWRONGTYPE		0x00004510

/* kdump: No operating system information was found */
#define EOS_INFO_MISSING	0x00004520

/* kdump: The checksum of the operating system information is incorrect */
#define EOS_INFO_CSUM_FAILED	0x00004521

/* kdump: The major version of the operating system information is too high */
#define EOS_INFO_VERSION	0x00004522

/* kdump: No crashkernel memory was defined */
#define EOS_INFO_NOCRASHKERNEL	0x00004523

/* kdump: HSA copy failed */
#define EHSA_COPY_FAILED	0x00004524

/************************************************************************
 * The following codes are not ABI
 ************************************************************************/

/*
 * Stand-alone dump
 */
#define OK		0x00000000  /* Dump completed successfully */
#define EMEM		0x00004600  /* Device too small for dump */
#define EDEV_INVAL	0x00004601  /* Device not supported */
#define EMEMCOUNT	0x00004602  /* Mem counting did not work */

/*
 * Multi-volume dump
 */
#define ENODEV		0x00004603  /* The devno does not exist */
#define ENOSIGN		0x00004604  /* No valid dump signature on device */
#define ENOTIME		0x00004605  /* The zipl time stamps do not match */

#endif /* __ERROR_H__ */
