/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Single-volume FBA DASD dump tool
 *
 * Copyright IBM Corp. 2013
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include "stage2dump.h"
#include "error.h"
#include "fba.h"

#define BLK_PWRT	64	/* Blocks per write */
#define BLK_SIZE	0x200	/* FBA block size */

/*
 * Magic number at start of dump record
 */
uint64_t magic __attribute__((section(".stage2.head")))
	= 0x5a44464241363403ULL; /* ZDFBA64, version 3 */

/*
 * FBA dump device partition specification
 */
static struct {
	unsigned int blk_start;
	unsigned int blk_end;
} device;

/*
 * ORB / IRB
 */
static struct orb orb = {
	.intparm	= 0x0049504c,	/* Interruption Parameter */
	.fmt		= 0x1,		/* Use format 1 CCWs */
	.c64		= 0x1,		/* Use IDAs */
	.lpm		= 0xff,		/* Logical path mask */
};

static struct irb irb;

/*
 * Data for Locate Record CCW
 */
static struct LO_fba_data lodata = {
	.operation = {
		.cmd = 0x1,
	},
};

/*
 * Data for Define Extend CCW
 */
static struct DE_fba_data dedata = {
	.mask = {
		.perm	= 0x3,
	},
	.blk_size	= 0x200,
};

/*
 * CCW program and IDA list
 */
static struct {
	struct ccw1 deccw;
	struct ccw1 loccw;
	struct ccw1 wrccw;
	unsigned long ida_list[BLK_PWRT * BLK_SIZE / 4096];
} ccw_program __aligned(8);

/*
 * FBA parameter block passed by zipl
 */
struct fba_dump_param {
	uint32_t	res1;
	uint32_t	blk_start;
	uint32_t	res2;
	uint32_t	blk_end;
} __packed;

/*
 * Convert memory size to number of blocks
 */
static inline unsigned long m2b(unsigned long mem)
{
	return mem / BLK_SIZE;
}

/*
 * Convert number of blocks to memory size
 */
static inline unsigned long b2m(unsigned long blk)
{
	return blk * BLK_SIZE;
}

/*
 * Get device characteristics from zipl parameter block
 */
void dt_device_parm_setup(void)
{
	struct fba_dump_param *param = (void *) __stage2_desc;

	device.blk_start = param->blk_start;
	device.blk_end = param->blk_end;
}

/*
 * Enable DASD device
 */
void dt_device_enable(void)
{
	io_irq_enable();
	set_device(IPL_SC, ENABLED);
}

/*
 * Write memory with number of blocks to start block
 */
static void writeblock(unsigned long blk, unsigned long addr, unsigned long blk_count,
		       unsigned long zero_page)
{
	unsigned long blk_end;

	blk_end = blk + blk_count;
	if (blk_end >= device.blk_end)
		panic(EMEM, "Device too small");
	ccw_program.wrccw.count = b2m(blk_count);
	lodata.blk_ct = blk_count;
	lodata.blk_nr = blk;
	create_ida_list(ccw_program.ida_list, b2m(blk_count), addr, zero_page);
	start_io(IPL_SC, &irb, &orb);
}

/*
 * Initialize the CCW program
 */
static void ccw_program_init(void)
{
	ccw_program.deccw.cmd_code = DASD_FBA_CCW_DE;
	ccw_program.deccw.flags = CCW_FLAG_CC;
	ccw_program.deccw.count = 0x0010;
	ccw_program.deccw.cda = __pa32(&dedata);

	ccw_program.loccw.cmd_code = DASD_FBA_CCW_LOCATE;
	ccw_program.loccw.flags = CCW_FLAG_CC;
	ccw_program.loccw.count = 0x0008;
	ccw_program.loccw.cda = __pa32(&lodata);

	ccw_program.wrccw.cmd_code = DASD_FBA_CCW_WRITE;
	ccw_program.wrccw.flags = CCW_FLAG_IDA | CCW_FLAG_SLI;
	ccw_program.wrccw.cda = __pa32(ccw_program.ida_list);

	orb.cpa = __pa32(&ccw_program);
	dedata.ext_end = device.blk_end;
}

/*
 * Dump all memory to DASD partition
 */
void dt_dump_mem(void)
{
	unsigned long blk, addr, page;

	ccw_program_init();
	blk = device.blk_start;
	page = get_zeroed_page();

	/* Write dump header */
	writeblock(blk, __pa(dump_hdr), m2b(DF_S390_HDR_SIZE), 0);
	blk += DF_S390_HDR_SIZE / BLK_SIZE;

	/* Write memory */
	for (addr = 0; addr < dump_hdr->mem_size; addr += b2m(BLK_PWRT)) {
		writeblock(blk, addr, BLK_PWRT, page);
		progress_print(addr);
		blk += BLK_PWRT;
	}
	progress_print(addr);

	/* Write end marker */
	df_s390_em_page_init(page);
	writeblock(blk, page, 1, 0);
	free_page(page);
}
