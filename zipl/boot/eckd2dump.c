/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Common ECKD dump I/O functions
 *
 * Copyright IBM Corp. 2013
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include "eckd2dump.h"
#include "stage2dump.h"
#include "s390.h"
#include "cio.h"
#include "error.h"

#define CMD_LOCATE_RECORD	0x47
#define CMD_DEFINE_EXTEND	0x63
#define CMD_READ		0x86
#define CMD_WRITE		0x8d

struct eckd_device device;

/*
 * Data for Locate Record CCW
 */
struct lodata {
	uint8_t		op;
	uint8_t		xx;
	uint16_t	blk_count;
	uint32_t	cchh1;
	uint32_t	cchh2;
	uint8_t		bot;
	uint8_t		res2;
	uint16_t	blk_size;
	uint8_t		res3[0x18-0x10];
} __packed;

static struct lodata lodata = {
	.op		= 0x01,
	.xx		= 0x80,
	.blk_count	= 0x0001,
	.bot		= 0x03,
};

/*
 * Data for Define Extend CCW
 */
struct dedata {
	uint8_t		op;
	uint8_t		xx;
	uint8_t		res1[0x08-0x02];
	uint32_t	cchh1;
	uint32_t	cchh2;
} __packed;

static struct dedata dedata = {
	.op		= 0x80,
	.xx		= 0xc0,
};

/*
 * ORB /IRB
 */
static struct orb orb = {
	.intparm	= 0x0049504c,	/* Interruption Parameter */
	.fmt		= 0x1,		/* Use format 1 CCWs */
	.c64		= 0x1,		/* Use IDAs */
	.lpm		= 0xff,		/* Logical path mask */
};

static struct irb irb;

/*
 * CCW program and IDA list
 */
struct ccw_program {
	struct ccw1 deccw;
	struct ccw1 loccw;
	struct ccw1 wrccw[ECKD_BPWRT_MAX];
	unsigned long ida_list[ECKD_BPWRT_MAX];
} __packed __aligned(8);

static struct ccw_program ccw_program;

/*
 * Setup IDA list for CCW program
 */
static void ccw_setup(uint8_t cmd, unsigned long addr, unsigned long blk_count,
		      unsigned long zero_page)
{
	unsigned long i, *ida_list = ccw_program.ida_list;
	struct ccw1 *ccw_list = ccw_program.wrccw;

	for (i = 0; i < blk_count; i++) {
		if (zero_page)
			ida_list[i] = page_is_valid(addr) ? addr : zero_page;
		else
			ida_list[i] = addr;
		ccw_list[i].cmd_code = cmd;
		ccw_list[i].count = device.blk_size;
		ccw_list[i].flags = CCW_FLAG_CC | CCW_FLAG_IDA;
		ccw_list[i].cda = __pa32(&ida_list[i]);
		addr += device.blk_size;
	}
	/* No command chaining for last CCW */
	ccw_list[i - 1].flags &= ~CCW_FLAG_CC;
}

/*
 * Read or write ECKD blocks
 */
static void io_block(uint8_t cmd, unsigned long blk, unsigned long addr,
		     unsigned long blk_count, unsigned long zero_page)
{
	uint32_t trk, trk_end, cyl, cyl_end, bot, hd, hd_end, c0c1c2h0, down;
	unsigned long blk_end;

	if (blk >= device.blk_end)
		panic(EMEM, "%s", "Device too small");

	ccw_setup(cmd, addr, blk_count, zero_page);
	lodata.blk_count = blk_count;
	lodata.blk_size = device.blk_size;
	blk_end = blk + blk_count - 1;

	/* Compute start track and end block on track */
	trk = blk / device.bpt;
	bot = (blk % device.bpt) + 1;
	/* Compute start cylinder and head */
	cyl = trk / device.num_heads;
	hd = trk % device.num_heads;

	lodata.bot = (uint8_t) bot;

	/* Upper 12 bits of cylinder are coded into heads: c0c1c2h0 (2 byte) */
	c0c1c2h0 = cyl >> 16;
	c0c1c2h0 <<= 4;

	/* Combine to CCHH */
	down = cyl << 16;

	dedata.cchh1 = down | hd | c0c1c2h0;
	lodata.cchh1 = down | hd | c0c1c2h0;
	lodata.cchh2 = down | hd | c0c1c2h0;

	/* Compute end track and end block on track */
	trk_end = blk_end / device.bpt;
	/* Compute end cylinder and head */
	cyl_end = trk_end / device.num_heads;
	hd_end = trk_end % device.num_heads;

	/* Upper 12 bits of cylinder are coded into heads: c0c1c2h0 (2 byte) */
	c0c1c2h0 = cyl_end >> 16;
	c0c1c2h0 <<= 4;

	/* Combine to CCHH */
	down = cyl_end << 16;

	dedata.cchh2 = down | hd_end | c0c1c2h0;
	start_io(device.ssid, &irb, &orb);
}

/*
 * Write data to given block address
 */
void writeblock(unsigned long blk, unsigned long addr, unsigned long blk_count,
		unsigned long zero_page)
{
	lodata.op = 0x01; /* Indicate WRITE DATA operation */
	dedata.op = 0x80; /* Permit update write operations */
	io_block(CMD_WRITE, blk, addr, blk_count, zero_page);
}

/*
 * Read data from given block address
 */
void readblock(unsigned long blk, unsigned long addr, unsigned long blk_count)
{
	lodata.op = 0x06; /* Indicate READ DATA operation */
	dedata.op = 0x40; /* Inhibit all write operations */
	io_block(CMD_READ, blk, addr, blk_count, 0);
}

/*
 * Init ECKD common
 */
void stage2dump_eckd_init(void)
{
	ccw_program.deccw.cmd_code = CMD_DEFINE_EXTEND;
	ccw_program.deccw.flags = CCW_FLAG_CC;
	ccw_program.deccw.count = 0x0010;
	ccw_program.deccw.cda = __pa32(&dedata);

	ccw_program.loccw.cmd_code = CMD_LOCATE_RECORD;
	ccw_program.loccw.flags = CCW_FLAG_CC;
	ccw_program.loccw.count = 0x0010;
	ccw_program.loccw.cda = __pa32(&lodata);

	orb.cpa = __pa32(&ccw_program);
}
