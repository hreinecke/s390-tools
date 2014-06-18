/*
 * s390-tools/zipl/src/install.c
 *   Functions handling the installation of the boot loader code onto disk.
 *
 * Copyright IBM Corp. 2001, 2009.
 *
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 *            Peter Oberparleiter <Peter.Oberparleiter@de.ibm.com>
 */

#include "install.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/mtio.h>
#include <syslog.h>

#include "boot.h"
#include "bootmap.h"
#include "disk.h"
#include "error.h"
#include "misc.h"

#define ECKD_CDL_DUMP_REC 3 /* Start record (0 based) for stage 2 and 1b */
/* CDL record 2 = 148 byte, independent from bs, bootinfo is last structure */
#define CDL_BOOTINFO_ADDR 112
/* ADDRESS for bootinfo structure to be BIOS compatible in MBR */
#define DEFAULT_BOOTINFO_ADDR 408

static inline unsigned long blk_cnt(int size, struct disk_info *info)
{
	return (size + info->phy_block_size - 1) / info->phy_block_size;
}

/* Types of SCSI disk layouts */
enum scsi_layout {
	scsi_layout_pcbios,
	scsi_layout_sun,
	scsi_layout_sgi,
	scsi_layout_unknown
};

/* From linux/fs.h */
#define BLKFLSBUF	_IO(0x12, 97)


/* Determine SCSI disk layout from the specified BOOTBLOCK. */
static enum scsi_layout
get_scsi_layout(unsigned char* bootblock)
{
	if ((bootblock[510] == 0x55) && (bootblock[511] == 0xaa))
		return scsi_layout_pcbios;
	else if ((bootblock[508] == 0xda) && (bootblock[509] == 0xbe))
		return scsi_layout_sun;
	else if ((bootblock[0] == 0x0b) && (bootblock[1] == 0xe5) &&
		 (bootblock[2] == 0xa9) && (bootblock[3] == 0x41))
		return scsi_layout_sgi;
	return scsi_layout_unknown;
}


#define DISK_LAYOUT_ID 0x00000001

static int
overwrite_partition_start(int fd, struct disk_info* info, int mv_dump_magic);

/* Create an IPL master boot record data structure for SCSI MBRs in memory
 * at location BUFFER. TABLE contains a pointer to the program table. INFO
 * provides information about the disk. */
static int
update_scsi_mbr(void* bootblock, disk_blockptr_t* table,
		struct disk_info* info, disk_blockptr_t* scsi_dump_sb_blockptr)
{
	struct scsi_mbr {
		uint8_t		magic[4];
		uint32_t	version_id;
		uint8_t		reserved[8];
		uint8_t		program_table_pointer[16];
		uint8_t		reserved2[0x50];
		struct boot_info boot_info;
	}  __attribute__ ((packed))* mbr;
	struct scsi_dump_param param;
	void* buffer;

	switch (get_scsi_layout(bootblock)) {
	case scsi_layout_pcbios:
		if (verbose)
			printf("Detected SCSI PCBIOS disk layout.\n");
		buffer = bootblock;
		break;
	case scsi_layout_sun:
	case scsi_layout_sgi:
		error_reason("Unsupported SCSI disk layout");
		return -1;
	default:
		if (info->partnum) {
			error_reason("Unsupported SCSI disk layout");
			return -1;
		} else {
			if (verbose)
				printf ("Detected plain SCSI partition.\n");
			buffer=bootblock;
		}
	}

	mbr = (struct scsi_mbr *) buffer;
	memset(buffer, 0, sizeof(struct scsi_mbr));
	memcpy(&mbr->magic, ZIPL_MAGIC, ZIPL_MAGIC_SIZE);
	mbr->version_id = DISK_LAYOUT_ID;
	bootmap_store_blockptr(&mbr->program_table_pointer, table, info);
	if (scsi_dump_sb_blockptr->linear.block != 0) {
		/* Write dump boot_info */
		param.block = scsi_dump_sb_blockptr->linear.block *
			      scsi_dump_sb_blockptr->linear.size;
		boot_get_dump_info(&mbr->boot_info, BOOT_INFO_DEV_TYPE_SCSI,
				   &param);
	}
	return 0;
}


/* Install bootloader for initial program load from a SCSI type disk. FD
 * specifies the file descriptor of the device file. PROGRAM_TABLE points
 * to the disk block containing the program table. INFO provides
 * information about the disk type. Return 0 on success, non-zero otherwise. */
static int
install_scsi(int fd, disk_blockptr_t* program_table, struct disk_info* info,
	     disk_blockptr_t* scsi_dump_sb_blockptr)
{
	unsigned char* bootblock;
	int rc;

	bootblock = (unsigned char*) misc_malloc(info->phy_block_size);
	if (bootblock == NULL)
		return -1;
	/* Read bootblock */
	if (misc_seek(fd, 0)) {
		free(bootblock);
		return -1;
	}
	rc = misc_read(fd, bootblock, info->phy_block_size);
	if (rc) {
		error_text("Could not read master boot record");
		free(bootblock);
		return rc;
	}
	/* Put zIPL data into MBR */
	rc = update_scsi_mbr(bootblock, program_table, info,
			     scsi_dump_sb_blockptr);
	if (rc) {
		free(bootblock);
		return -1;
	}
	/* Write MBR back to disk */
	if (verbose)
		printf("Writing SCSI master boot record.\n");
	if (misc_seek(fd, 0)) {
		free(bootblock);
		return -1;
	}
	rc = DRY_RUN_FUNC(misc_write(fd, bootblock, info->phy_block_size));
	if (rc)
		error_text("Could not write master boot record");
	free(bootblock);
	return rc;
}


/* Install bootloader for initial program load from an FBA type disk. */
static int
install_fba(int fd, disk_blockptr_t *program_table,
	    disk_blockptr_t *stage1b_list, blocknum_t stage1b_count,
	    struct disk_info *info)
{
	struct boot_fba_stage0 stage0;

	/* Install stage 0 and store program table pointer */
	if (boot_init_fba_stage0(&stage0, stage1b_list, stage1b_count))
		return -1;

	boot_get_ipl_info(&stage0.boot_info,  BOOT_INFO_DEV_TYPE_FBA,
			  program_table, info);

	if (DRY_RUN_FUNC(misc_pwrite(fd, &stage0, sizeof(stage0), 0)))
		return -1;

	return 0;
}

/* Install stage1b bootloader for ECKD type disk */
int
install_eckd_stage1b(int fd, disk_blockptr_t **stage1b_list,
		     blocknum_t *stage1b_count, disk_blockptr_t *stage2_list,
		     blocknum_t stage2_count, struct disk_info *info)
{
	struct boot_eckd_stage1b *stage1b;
	int stage1b_size, rc = -1;

	*stage1b_list = NULL;
	*stage1b_count = 0;
	stage1b_size = ROUNDUP(sizeof(*stage1b), info->phy_block_size);
	stage1b = misc_malloc(stage1b_size);
	if (stage1b == NULL)
		goto out;
	memset(stage1b, 0, stage1b_size);
	if (boot_init_eckd_stage1b(stage1b, stage2_list, stage2_count))
		goto out_free_stage1b;
	*stage1b_count = disk_write_block_buffer(fd, 1, stage1b, stage1b_size,
						 stage1b_list, info);
	if (*stage1b_count == 0)
		goto out_free_stage1b;
	rc = 0;
out_free_stage1b:
	free(stage1b);
out:
	return rc;
}

/* Install bootloader for initial program load from an ECKD type disk with
 * Linux Disk Layout. */
static int
install_eckd_ldl(int fd, disk_blockptr_t *program_table,
		 disk_blockptr_t *stage1b_list, blocknum_t stage1b_count,
		 struct disk_info *info)
{
	struct boot_eckd_ldl_stage0 stage0;
	struct boot_eckd_stage1 stage1;

	/* Install stage 0 */
	boot_init_eckd_ldl_stage0(&stage0);
	if (DRY_RUN_FUNC(misc_pwrite(fd, &stage0, sizeof(stage0), 0)))
		return -1;
	/* Install stage 1 and store program table pointer */
	if (boot_init_eckd_stage1(&stage1, stage1b_list, stage1b_count))
		return -1;

	boot_get_ipl_info(&stage1.boot_info, BOOT_INFO_DEV_TYPE_ECKD,
			  program_table, info);

	if (DRY_RUN_FUNC(misc_pwrite(fd, &stage1, sizeof(stage1),
				     sizeof(stage0))))
		return -1;

	return 0;
}

/* Install bootloader for initial program load from an ECKD type disk with
 * OS/390 compatible disk layout. */
static int
install_eckd_cdl(int fd, disk_blockptr_t *program_table,
		 disk_blockptr_t *stage1b_list, blocknum_t stage1b_count,
		 struct disk_info *info)
{
	struct boot_eckd_cdl_stage0 stage0;
	struct boot_eckd_stage1 stage1;

	/* Install stage 0 */
	boot_init_eckd_cdl_stage0(&stage0);
	if (DRY_RUN_FUNC(misc_pwrite(fd, &stage0, sizeof(stage0), 4)))
		return -1;
	/* Install stage 1 and store program table pointer */
	if (boot_init_eckd_stage1(&stage1, stage1b_list, stage1b_count))
		return -1;

	boot_get_ipl_info(&stage1.boot_info, BOOT_INFO_DEV_TYPE_ECKD,
			  program_table, info);

	if (DRY_RUN_FUNC(misc_pwrite(fd, &stage1, sizeof(stage1),
				     4 + info->phy_block_size)))
		return -1;
	return 0;
}

int
install_bootloader(const char *device, disk_blockptr_t *program_table,
		   disk_blockptr_t *scsi_dump_sb_blockptr,
		   disk_blockptr_t *stage1b_list, blocknum_t stage1b_count,
		   struct disk_info *info, struct job_data *job)
{
	int fd, rc;

	/* Inform user about what we're up to */
	printf("Preparing boot device: ");
	if (info->name) {
		printf("%s", info->name);
		if (info->devno >= 0)
			printf(" (%04x)", info->devno);
		printf(".\n");
	} else if (info->devno >= 0) {
		printf("%04x.\n", info->devno);
	} else {
		disk_print_devt(info->device);
		printf(".\n");
	}
	/* Open device file */
	fd = open(device, O_RDWR);
	if (fd == -1) {
		error_reason(strerror(errno));
		error_text("Could not open temporary device file '%s'",
			   device);
		return -1;
	}
	/* Ensure that potential cache inconsistencies between disk and
	 * partition are resolved by flushing the corresponding buffers. */
	if (!dry_run) {
		if (ioctl(fd, BLKFLSBUF)) {
			fprintf(stderr, "Warning: Could not flush disk "
				"caches.\n");
		}
	}
	/* Call disk specific install functions */
	rc = -1;
	switch (info->type) {
	case disk_type_scsi:
		rc = install_scsi(fd, program_table, info,
				  scsi_dump_sb_blockptr);
		if (rc == 0 && job->id == job_dump_partition)
			rc = overwrite_partition_start(fd, info, 0);
		break;
	case disk_type_fba:
		rc = install_fba(fd, program_table, stage1b_list,
				 stage1b_count, info);
		break;
	case disk_type_eckd_ldl:
		rc = install_eckd_ldl(fd, program_table, stage1b_list,
				      stage1b_count, info);
		break;
	case disk_type_eckd_cdl:
		rc = install_eckd_cdl(fd, program_table, stage1b_list,
				      stage1b_count, info);
		break;
	case disk_type_diag:
		/* Should not happen */
		break;
	}

	if (fsync(fd))
		error_text("Could not sync device file '%s'", device);
	if (close(fd))
		error_text("Could not close device file '%s'", device);

	if (!dry_run && rc == 0) {
		if (info->devno >= 0)
			syslog(LOG_INFO, "Boot loader written to %s (%04x) - "
			       "%02x:%02x",
			       (info->name ? info->name : "-"), info->devno,
			       major(info->device), minor(info->device));
		else
			syslog(LOG_INFO, "Boot loader written to %s - "
			       "%02x:%02x",
			       (info->name ? info->name : "-"),
			       major(info->device), minor(info->device));
	}
	return rc;
}


/* Rewind the tape device identified by FD. Return 0 on success, non-zero
 * otherwise. */
int
rewind_tape(int fd)
{
	struct mtop op;

	/* Magnetic tape rewind operation */
	op.mt_count = 1;
	op.mt_op = MTREW;
	if (ioctl(fd, MTIOCTOP, &op) == -1)
		return -1;
	else
		return 0;
}

static int
ask_for_confirmation(const char* fmt, ...)
{
	va_list args;
	char answer;

	/* Always assume positive answer in non-interactive mode */
	if (!interactive)
		return 0;
	/* Print question */
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	/* Process user reply */
	while (scanf("%c", &answer) != 1);
	if ((answer == 'y') || (answer == 'Y'))
		return 0;
	fprintf(stderr, "Operation canceled by user.\n");
	return -2;
}


/* Write data from file FILENAME to file descriptor FD. Data will be written
 * in blocks of BLOCKSIZE bytes. Return 0 on success, non-zero otherwise. */
static int
write_tapefile(int fd, const char* filename, size_t blocksize)
{
	struct stat stats;
	ssize_t written;
	off_t offset;
	size_t chunk;
	void* buffer;
	int read_fd;

	if (stat(filename, &stats)) {
		error_reason(strerror(errno));
		return -1;
	}
	if (!S_ISREG(stats.st_mode)) {
		error_reason("Not a regular file");
		return -1;
	}
	buffer = misc_malloc(blocksize);
	if (buffer == NULL)
		return -1;
	read_fd = open(filename, O_RDONLY);
	if (fd == -1) {
		error_reason(strerror(errno));
		free(buffer);
		return -1;
	}
	for (offset = 0; offset < stats.st_size; offset += chunk) {
		chunk = stats.st_size - offset;
		if (chunk > blocksize)
			chunk = blocksize;
		else
			memset(buffer, 0, blocksize);
		if (misc_read(read_fd, buffer, chunk)) {
			close(read_fd);
			free(buffer);
			return -1;
		}
		written = write(fd, buffer, chunk);
		if (written != (ssize_t) chunk) {
			if (written == -1)
				error_reason(strerror(errno));
			else
				error_reason("Write error");
			close(read_fd);
			free(buffer);
			return -1;
		}
	}
	close(read_fd);
	free(buffer);
	return 0;
}


/* Write SIZE bytes of data from memory location DATA to file descriptor FD.
 * Data will be written in blocks of BLOCKSIZE bytes. Return 0 on success,
 * non-zero otherwise. */
static int
write_tapebuffer(int fd, const char* data, size_t size, size_t blocksize)
{
	ssize_t written;
	size_t offset;
	size_t chunk;
	void* buffer;

	buffer = misc_malloc(blocksize);
	if (buffer == NULL)
		return -1;
	for (offset = 0; offset < size; offset += chunk) {
		chunk = size - offset;
		if (chunk > blocksize)
			chunk = blocksize;
		else
			memset(buffer, 0, blocksize);
		memcpy(buffer, VOID_ADD(data, offset), chunk);
		written = write(fd, buffer, chunk);
		if (written != (ssize_t) chunk) {
			if (written == -1)
				error_reason(strerror(errno));
			else
				error_reason("Write error");
			free(buffer);
			return -1;
		}
	}
	free(buffer);
	return 0;
}


/* Write COUNT tapemarks to file handle FD. */
static int
write_tapemark(int fd, int count)
{
	struct mtop op;

	op.mt_count = count;
	op.mt_op = MTWEOF;
	if (ioctl(fd, MTIOCTOP, &op) == -1) {
		error_reason("Could not write tapemark");
		return -1;
	}
	return 0;
}


#define IPL_TAPE_BLOCKSIZE	1024

/* Install IPL record on tape device. */
int
install_tapeloader(const char* device, const char* image, const char* parmline,
		   const char* ramdisk, address_t image_addr,
		   address_t parm_addr, address_t initrd_addr)
{
	void* buffer;
	size_t size;
	int rc;
	int fd;

	printf("Preparing boot tape: %s\n", device);
	/* Prepare boot loader */
	rc = boot_get_tape_ipl(&buffer, &size, parm_addr, initrd_addr,
			       image_addr);
	if (rc)
		return rc;
	/* Open device */
	fd = open(device, O_RDWR);
	if (fd == -1) {
		error_reason(strerror(errno));
		error_text("Could not open tape device '%s'", device);
		free(buffer);
		return -1;
	}
	if (rewind_tape(fd) != 0) {
		error_text("Could not rewind tape device '%s'", device);
		free(buffer);
		close(fd);
		return -1;
	}
	/* Write boot loader */
	rc = DRY_RUN_FUNC(write_tapebuffer(fd, buffer, size,
		IPL_TAPE_BLOCKSIZE));
	free(buffer);
	if (rc) {
		error_text("Could not write boot loader to tape");
		close(fd);
		return rc;
	}
	rc = DRY_RUN_FUNC(write_tapemark(fd, 1));
	if (rc) {
		error_text("Could not write boot loader to tape");
		close(fd);
		return rc;
	}
	/* Write image file */
	if (verbose) {
		printf("  kernel image......: %s at 0x%llx\n", image,
		       (unsigned long long) image_addr);
	}
	rc = DRY_RUN_FUNC(write_tapefile(fd, image, IPL_TAPE_BLOCKSIZE));
	if (rc) {
		error_text("Could not write image file '%s' to tape", image);
		close(fd);
		return rc;
	}
	rc = DRY_RUN_FUNC(write_tapemark(fd, 1));
	if (rc) {
		error_text("Could not write boot loader to tape");
		close(fd);
		return rc;
	}
	if (parmline != NULL) {
		if (verbose) {
			printf("  kernel parmline...: '%s' at 0x%llx\n",
			       parmline, (unsigned long long) parm_addr);
		}
		/* Write parameter line */
		rc = DRY_RUN_FUNC(write_tapebuffer(fd, parmline,
			strlen(parmline), IPL_TAPE_BLOCKSIZE));
		if (rc) {
			error_text("Could not write parameter string to tape");
			close(fd);
			return rc;
		}
	}
	rc = DRY_RUN_FUNC(write_tapemark(fd, 1));
	if (rc) {
		error_text("Could not write boot loader to tape");
		close(fd);
		return rc;
	}
	if (ramdisk != NULL) {
		/* Write ramdisk */
		if (verbose) {
			printf("  initial ramdisk...: %s at 0x%llx\n",
			       ramdisk, (unsigned long long) initrd_addr);
		}
		rc = DRY_RUN_FUNC(write_tapefile(fd, ramdisk,
			IPL_TAPE_BLOCKSIZE));
		if (rc) {
			error_text("Could not write ramdisk file '%s' to tape",
				   ramdisk);
			close(fd);
			return rc;
		}
	}
	rc = DRY_RUN_FUNC(write_tapemark(fd, 1));
	if (rc) {
		error_text("Could not write boot loader to tape");
		close(fd);
		return rc;
	}
	if (rewind_tape(fd) != 0) {
		error_text("Could not rewind tape device '%s' to tape", device);
		rc = -1;
	}
	close(fd);
	return rc;
}

/* Write 64k null bytes with dump signature at offset 512 to
 * start of dump partition */
static int
overwrite_partition_start(int fd, struct disk_info* info, int mv_dump_magic)
{
	int rc;
	unsigned int bytes = 65536;
	char* buffer;
	const char dump_magic[] = {0xa8, 0x19, 0x01, 0x73,
		0x61, 0x8f, 0x23, 0xfd};

	if (misc_seek(fd, info->geo.start * info->phy_block_size))
		return -1;
	if (info->phy_block_size * info->phy_blocks < bytes)
		bytes = info->phy_block_size * info->phy_blocks;
	buffer = calloc(1, bytes);
	if (buffer == NULL) {
		error_text("Could not allocate buffer");
		return -1;
	}
	if (mv_dump_magic)
		memcpy(VOID_ADD(buffer, 512), dump_magic, sizeof(dump_magic));
	rc = DRY_RUN_FUNC(misc_write(fd, buffer, bytes));
	free(buffer);
	if (rc) {
		error_text("Could not write dump signature");
		return rc;
	}
	return 0;
}

/*
 * Ensure that end block is within bounds.
 * Force block size of 4KiB because otherwise there is not enough space
 * to write the dump tool.
 */
static int check_eckd_dump_partition(struct disk_info* info)
{
	unsigned long long end_blk = info->geo.start + info->phy_blocks - 1;

	if (end_blk > UINT32_MAX) {
		error_reason("partition end exceeds bounds (offset "
			     "%lld MB, max %lld MB)",
			(end_blk * info->phy_block_size) >> 20,
			(((unsigned long long) UINT32_MAX) *
				info->phy_block_size) >> 20);
		return -1;
	}
	if (info->phy_block_size != 4096) {
		error_reason("unsupported DASD block size %d (should be 4096)",
			     info->phy_block_size);
		return -1;
	}
	return 0;
}

static void eckd_dump_store_param(struct eckd_dump_param *param,
				  struct disk_info *info, blocknum_t count)
{
	param->start_blk = info->geo.start;
	param->end_blk = info->geo.start + info->phy_blocks - 1 - count;
	param->num_heads = info->geo.heads;
	param->blocksize = info->phy_block_size;
	param->bpt = info->geo.sectors;
}

static int install_svdump_eckd_ldl(int fd, struct disk_info *info, uint64_t mem)
{
	disk_blockptr_t *stage2_list, *stage1b_list;
	blocknum_t stage2_count, stage1b_count;
	struct boot_eckd_ldl_stage0 stage0;
	struct boot_eckd_stage1 stage1;
	struct eckd_dump_param param;
	size_t stage2_size;
	void *stage2;
	int rc = -1;

	if (boot_get_eckd_dump_stage2(&stage2, &stage2_size, mem))
		goto out;
	if (blk_cnt(stage2_size, info) > STAGE2_BLK_CNT_MAX) {
		error_reason("ECKD dump record is too large");
		goto out_free_stage2;
	}
	if (overwrite_partition_start(fd, info, 0))
		goto out_free_stage2;
	/* Install stage 2 and stage 1b to beginning of partition */
	if (misc_seek(fd, info->geo.start * info->phy_block_size))
		goto out_free_stage2;
	stage2_count = disk_write_block_buffer(fd, 1, stage2, stage2_size,
					       &stage2_list, info);
	if (stage2_count == 0)
		goto out_free_stage2_list;
	if (install_eckd_stage1b(fd, &stage1b_list, &stage1b_count,
				 stage2_list, stage2_count, info))
		goto out_free_stage2_list;
	/* Install stage 0 - afterwards we are at stage 1 position*/
	boot_init_eckd_ldl_stage0(&stage0);
	if (DRY_RUN_FUNC(misc_pwrite(fd, &stage0, sizeof(stage0), 0)))
		goto out_free_stage1b_list;
	/* Install stage 1 and fill in dump partition parameter */
	if (boot_init_eckd_stage1(&stage1, stage1b_list, stage1b_count))
		goto out_free_stage1b_list;

	eckd_dump_store_param(&param, info, 0);
	boot_get_dump_info(&stage1.boot_info, BOOT_INFO_DEV_TYPE_ECKD, &param);

	if (DRY_RUN_FUNC(misc_pwrite(fd, &stage1, sizeof(stage1),
				     sizeof(stage0))))
		goto out_free_stage1b_list;
	rc = 0;
out_free_stage1b_list:
	free(stage1b_list);
out_free_stage2_list:
	free(stage2_list);
out_free_stage2:
	free(stage2);
out:
	return rc;
}

static int install_dump_eckd_cdl(int fd, struct disk_info *info, void *stage2,
				 size_t stage2_size, int mvdump, int force)
{
	blocknum_t count, stage2_count, stage1b_count;
	disk_blockptr_t *stage2_list, *stage1b_list;
	struct boot_eckd_cdl_stage0 stage0_cdl;
	struct boot_eckd_stage1 stage1;
	struct eckd_dump_param param;
	int rc = -1;

	count = blk_cnt(stage2_size, info);
	if (count > STAGE2_BLK_CNT_MAX) {
		error_reason("ECKD dump record is too large");
		goto out;
	}
	count += blk_cnt(sizeof(struct boot_eckd_stage1b), info);
	if (count > (blocknum_t) info->geo.sectors - ECKD_CDL_DUMP_REC) {
		error_reason("ECKD dump record is too large");
		goto out;
	}
	/* Install stage 2 */
	if (misc_seek(fd, ECKD_CDL_DUMP_REC * info->phy_block_size))
		goto out;
	stage2_count = disk_write_block_buffer(fd, 1, stage2, stage2_size,
					       &stage2_list, info);
	if (stage2_count == 0)
		goto out;
	/* Install stage 1b behind stage 2*/
	if (install_eckd_stage1b(fd, &stage1b_list, &stage1b_count,
				 stage2_list, stage2_count, info))
		goto out_free_stage2_list;
	/* Install stage 0 */
	boot_init_eckd_cdl_stage0(&stage0_cdl);
	if (DRY_RUN_FUNC(misc_pwrite(fd, &stage0_cdl, sizeof(stage0_cdl), 4)))
		goto out_free_stage1b_list;
	/* Install stage 1 and fill in dump partition parameter */
	if (boot_init_eckd_stage1(&stage1, stage1b_list, stage1b_count))
		goto out_free_stage1b_list;

	eckd_dump_store_param(&param, info, 0);
	boot_get_dump_info(&stage1.boot_info, BOOT_INFO_DEV_TYPE_ECKD, &param);
	if (DRY_RUN_FUNC(misc_pwrite(fd, &stage1, sizeof(stage1),
				     info->phy_block_size + 4)))
		goto out_free_stage1b_list;
	if (!force && overwrite_partition_start(fd, info, mvdump))
		goto out_free_stage1b_list;
	rc = 0;
out_free_stage1b_list:
	free(stage1b_list);
out_free_stage2_list:
	free(stage2_list);
out:
	return rc;
}

static int
install_svdump_eckd_cdl(int fd, struct disk_info *info, uint64_t mem)
{
	size_t stage2_size;
	void *stage2;
	int rc;

	if (boot_get_eckd_dump_stage2(&stage2, &stage2_size, mem))
		return -1;
	rc = install_dump_eckd_cdl(fd, info, stage2, stage2_size, 0, 0);
	free(stage2);
	return rc;
}

static int
install_mvdump_eckd_cdl(int fd, struct disk_info *info, uint64_t mem,
			uint8_t force, struct mvdump_parm_table parm)
{
	size_t stage2_size;
	void *stage2;
	int rc;

	/* Write stage 2 + parameter block */
	if (boot_get_eckd_mvdump_stage2(&stage2, &stage2_size, mem,
					force, parm))
		return -1;
	rc = install_dump_eckd_cdl(fd, info, stage2, stage2_size, 1, force);
	free(stage2);
	return rc;
}

int
install_fba_stage1b(int fd, disk_blockptr_t **stage1b_list,
		    blocknum_t *stage1b_count, disk_blockptr_t *stage2_list,
		    blocknum_t stage2_count, struct disk_info *info)
{
	struct boot_fba_stage1b *stage1b;
	int stage1b_size, rc = -1;

	*stage1b_list = NULL;
	*stage1b_count = 0;
	stage1b_size = ROUNDUP(sizeof(*stage1b), info->phy_block_size);
	stage1b = misc_malloc(stage1b_size);
	if (stage1b == NULL)
		goto out;
	memset(stage1b, 0, stage1b_size);
	if (boot_init_fba_stage1b(stage1b, stage2_list, stage2_count))
		goto out_free_stage1b;
	*stage1b_count = disk_write_block_buffer(fd, 1, stage1b, stage1b_size,
						 stage1b_list, info);
	if (*stage1b_count == 0)
		goto out_free_stage1b;
	rc = 0;
out_free_stage1b:
	free(stage1b);
out:
	return rc;
}

static int
install_svdump_fba(int fd, struct disk_info *info, uint64_t mem)
{
	blocknum_t stage1b_count, stage2_count, blk;
	disk_blockptr_t *stage1b_list, *stage2_list;
	struct boot_fba_stage0 stage0;
	struct fba_dump_param param;
	size_t stage2_size;
	void *stage2;
	int rc = -1;

	/* Overwrite first 64k of partition */
	if (overwrite_partition_start(fd, info, 0))
		goto out;
	/* Install stage 2 at end of partition */
	if (boot_get_fba_dump_stage2(&stage2, &stage2_size, mem))
		goto out;
	if (blk_cnt(stage2_size, info) > STAGE2_BLK_CNT_MAX) {
		error_reason("FBA dump record is too large");
		goto out_free_stage2;
	}
	blk = (info->geo.start + info->phy_blocks - blk_cnt(stage2_size, info));
	if (misc_seek(fd, blk * info->phy_block_size))
		goto out_free_stage2;
	stage2_count = disk_write_block_buffer(fd, 1, stage2, stage2_size,
					       &stage2_list, info);
	if (stage2_count == 0)
		goto out_free_stage2;
	/* Install stage 1b in front of stage 2 */
	blk -= blk_cnt(sizeof(struct boot_fba_stage1b), info);
	if (misc_seek(fd, blk * info->phy_block_size))
		goto out_free_stage2_list;
	if (install_fba_stage1b(fd, &stage1b_list, &stage1b_count,
				stage2_list, stage2_count, info))
		goto out_free_stage2_list;
	/* Install stage 0/1 fill in dump partition parameter */
	if (boot_init_fba_stage0(&stage0, stage1b_list, stage1b_count))
		goto out_free_stage1b_list;

	param.start_blk = (uint64_t) info->geo.start;
	param.blockct = (uint64_t) blk - 1;
	boot_get_dump_info(&stage0.boot_info, BOOT_INFO_DEV_TYPE_FBA, &param);

	if (DRY_RUN_FUNC(misc_pwrite(fd, &stage0, sizeof(stage0), 0)))
		goto out_free_stage1b_list;

	rc = 0;
out_free_stage1b_list:
	free(stage1b_list);
out_free_stage2_list:
	free(stage2_list);
out_free_stage2:
	free(stage2);
out:
	return rc;
}

static int
install_dump_tape(int fd, uint64_t mem)
{
	void* buffer;
	size_t size;
	int rc;

	rc = boot_get_tape_dump(&buffer, &size, mem);
	if (rc)
		return rc;
	rc = DRY_RUN_FUNC(misc_write(fd, buffer, size));
	if (rc)
		error_text("Could not write to tape device");
	free(buffer);
	return rc;
}


int
install_dump(const char* device, struct job_target_data* target, uint64_t mem)
{
	struct disk_info* info;
	char* tempdev;
	uint64_t part_size;
	int fd;
	int rc;

	fd = misc_open_exclusive(device);
	if (fd == -1) {
		error_text("Could not open dump device '%s'", device);
		return -1;
	}
	if (rewind_tape(fd) == 0) {
		/* Rewind worked - this is a tape */
		rc = ask_for_confirmation("Warning: All information on device "
					  "'%s' will be lost!\nDo you want to "
					  "continue creating a dump "
 					  "tape (y/n) ?", device);
		if (rc) {
			close(fd);
			return rc;
		}
		if (verbose)
			printf("Installing tape dump record\n");
		rc = install_dump_tape(fd, mem);
		if (rc) {
			error_text("Could not install dump record on tape "
				   "device '%s'", device);
		} else {
			if (verbose) {
				printf("Dump record successfully installed on "
				       "tape device '%s'.\n", device);
			}
		}
		close(fd);
		return rc;
	}
	close(fd);
	/* This is a disk device */
	rc = disk_get_info(device, target, &info);
	if (rc) {
		error_text("Could not get information for dump target "
			   "'%s'", device);
		return rc;
	}
	if (info->partnum == 0) {
		error_reason("Dump target '%s' is not a disk partition",
			     device);
		disk_free_info(info);
		return -1;
	}
	if (verbose) {
		printf("Target device information\n");
		disk_print_info(info);
	}
	rc = misc_temp_dev(info->device, 1, &tempdev);
	if (rc) {
		disk_free_info(info);
		return -1;
	}
	fd = open(tempdev, O_RDWR);
	if (fd == -1) {
		error_text("Could not open temporary device node '%s'",
			   tempdev);
		misc_free_temp_dev(tempdev);
		disk_free_info(info);
		return -1;
	}
	switch (info->type) {
	case disk_type_eckd_ldl:
	case disk_type_eckd_cdl:
		if (check_eckd_dump_partition(info)) {
			error_text("Dump target '%s'", device);
			rc = -1;
			break;
		}
		/* Fall through. */
	case disk_type_fba:
		part_size = info->phy_block_size * info->phy_blocks;
		printf("Dump target: partition '%s' with a size of %llu MB.\n",
		       device, (unsigned long long) part_size >> 20);
		rc = ask_for_confirmation("Warning: All information on "
					  "partition '%s' will be lost!\n"
					  "Do you want to continue creating "
					  "a dump partition (y/n)?", device);
		if (rc)
			break;
		if (verbose) {
			printf("Installing dump record on partition with %s\n",
			       disk_get_type_name(info->type));
		}
		if (info->type == disk_type_eckd_ldl)
			rc = install_svdump_eckd_ldl(fd, info, mem);
		else if (info->type == disk_type_eckd_cdl)
			rc = install_svdump_eckd_cdl(fd, info, mem);
		else
			rc = install_svdump_fba(fd, info, mem);
		break;
	case disk_type_scsi:
		error_reason("%s: Unsupported disk type '%s' (try --dumptofs)",
			     device, disk_get_type_name(info->type));
		rc = -1;
		break;
	case disk_type_diag:
		error_reason("%s: Unsupported disk type '%s'",
			     device, disk_get_type_name(info->type));
		rc = -1;
		break;
	}
	misc_free_temp_dev(tempdev);
	disk_free_info(info);
	if (fsync(fd))
		error_text("Could not sync device file '%s'", device);
	if (close(fd))
		error_text("Could not close device file '%s'", device);
	return rc;
}


int
install_mvdump(char* const device[], struct job_target_data* target, int count,
	       uint64_t mem, uint8_t force)
{
	struct disk_info* info[MAX_DUMP_VOLUMES] = {0};
	struct mvdump_parm_table parm;
	char* tempdev;
	uint64_t total_size = 0;
	int rc = 0, i, j, fd;
	struct timeval time;

	memset(&parm, 0, sizeof(struct mvdump_parm_table));
	gettimeofday(&time, NULL);
	parm.num_param = count;
	parm.timestamp = (time.tv_sec << 20) + time.tv_usec;
	for (i = 0; i < count; i++) {
		fd = open(device[i], O_RDWR);
		if (fd == -1) {
			error_reason(strerror(errno));
			error_text("Could not open dump target '%s'",
				   device[i]);
			rc = -1;
			goto out;
		}
		if (rewind_tape(fd) == 0) {
			/* Rewind worked - this is a tape */
			error_text("Dump target '%s' is a tape device",
				   device[i]);
			close(fd);
			rc = -1;
			goto out;
		}
		close(fd);
		/* This is a disk device */
		rc = disk_get_info(device[i], target, &info[i]);
		if (rc) {
			error_text("Could not get information for dump target "
				   "'%s'", device[i]);
			goto out;
		}
		if (info[i]->partnum == 0) {
			error_reason("Dump target '%s' is not a disk partition",
				     device[i]);
			rc = -1;
			goto out;
		}
		if (info[i]->type != disk_type_eckd_cdl) {
			error_reason("Dump target '%s' has to be ECKD DASD "
				     "with cdl format.", device[i]);
			rc = -1;
			goto out;
		}
		for (j = 0; j < i; j++) {
			if (info[j]->partition == info[i]->partition) {
				error_text("Dump targets '%s' and '%s' are "
					   "identical devices.",
					   device[i], device[j]);
				rc = -1;
				goto out;
			}
		}
		/* Make sure target device belongs to subchannel set 0 */
		rc = disk_check_subchannel_set(info[i]->devno,
					       info[i]->device, device[i]);
		if (rc)
			goto out;
		if (check_eckd_dump_partition(info[i])) {
			error_text("Dump target '%s'", device[i]);
			rc = -1;
			goto out;
		}
		parm.param[i].start_blk = info[i]->geo.start;
		parm.param[i].end_blk = info[i]->geo.start +
					info[i]->phy_blocks - 1;
		parm.param[i].bpt = info[i]->geo.sectors;
		parm.param[i].num_heads = info[i]->geo.heads;
		parm.param[i].blocksize = info[i]->phy_block_size >> 8;
		parm.param[i].devno = info[i]->devno;
	}
	if (verbose) {
		for (i = 0; i < count; i++) {
			printf("Multi-volume dump target %d:\n", i + 1);
			disk_print_info(info[i]);
			printf("-------------------------------------------\n");
		}
	}
	for (i = 0; i < count; i++)
		total_size += info[i]->phy_block_size * info[i]->phy_blocks;
	printf("Dump target: %d partitions with a total size of %ld MB.\n",
	       count, (long) total_size >> 20);
	if (interactive) {
		printf("Warning: All information on the following "
		       "partitions will be lost!\n");
		for (i = 0; i < count; i++)
			printf("   %s\n", device[i]);
		rc = ask_for_confirmation("Do you want to continue creating "
					  "multi-volume dump partitions "
					  "(y/n)?");
		if (rc)
			goto out;
	}
	for (i = 0; i < count; i++) {
		rc = misc_temp_dev(info[i]->device, 1, &tempdev);
		if (rc) {
			rc = -1;
			goto out;
		}
		fd = open(tempdev, O_RDWR);
		if (fd == -1) {
			error_text("Could not open temporary device node '%s'",
				   tempdev);
			misc_free_temp_dev(tempdev);
			rc = -1;
			goto out;
		}
		if (verbose)
			printf("Installing dump record on target partition "
			       "'%s'\n", device[i]);
		rc = install_mvdump_eckd_cdl(fd, info[i], mem, force, parm);
		misc_free_temp_dev(tempdev);

		if (fsync(fd))
			error_text("Could not sync device file '%s'", device);
		if (close(fd))
			error_text("Could not close device file '%s'", device);

		if (rc)
			goto out;
	}
out:
	for (i = 0; i < count; i++)
		if (info[i] != NULL)
			disk_free_info(info[i]);
	return rc;
}
