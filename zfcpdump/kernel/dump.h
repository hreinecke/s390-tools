/*
 *  drivers/s390/char/dump.h
 *
 *    Copyright IBM Corp. 2003, 2006.
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#ifndef _DUMP_H_
#define _DUMP_H_

#define ZCORE_IOCTL_GET_PARMS	0x1
#define ZCORE_IOCTL_RELEASE_HSA	0x2
#define ZCORE_IOCTL_GET_ERROR	0x3
#define ZCORE_IOCTL_HSASIZE	0x4
#define ZCORE_IOCTL_REIPL	0x5

#define ZCORE_ERR_OK		0x0
#define ZCORE_ERR_HSA_FEATURE	0x1
#define ZCORE_ERR_CPU_INFO	0x2
#define ZCORE_ERR_OTHER		0x3
#define ZCORE_ERR_REIPL		0x4

#define BOOT_TYPE_IPL		0x10
#define BOOT_TYPE_DUMP		0x20

enum diag308_subcode  {
	DIAG308_REL_HSA	= 2,
	DIAG308_IPL	= 3,
	DIAG308_SET	= 5,
};

void setup_dump_base(void);
void setup_dump_devnos(char *cmdline, unsigned int init_devno,
		       unsigned int console_devno);
int  dump_init(void);

/* IPL parameter block */
struct ipl_list_header {
	__u32 length;
	__u8  reserved[3];
	__u8  version;
} __attribute__((packed));

struct ipl_block_fcp {
	__u32 length;
	__u8  pbt;
	__u8  reserved1[316-1];
	__u8  type;
	__u8  reserved2[5];
	__u16 devno;
	__u8  reserved3[4];
	__u64 wwpn;
	__u64 lun;
	__u32 bootprog;
	__u8  reserved4[12];
	__u64 br_lba;
	__u32 scp_data_len;
	__u8  reserved5[260];
	__u8  scp_data[];
} __attribute__((packed));

struct ipl_parameter_block {
	struct ipl_list_header header;
	struct ipl_block_fcp fcp;
} __attribute__((packed));

#endif /* _DUMP_H_ */
