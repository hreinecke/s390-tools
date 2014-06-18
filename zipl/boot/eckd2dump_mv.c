/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Multi-volume ECKD DASD dump tool
 *
 * Copyright IBM Corp. 2013
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include "stage2dump.h"
#include "eckd2dump.h"
#include "error.h"

#define MVDUMP_SIZE		0x3000	/* Size of dump record */
#define MAX_DUMP_VOLUMES	32	/* Up to 32 dump volumes possible */

/*
 * Magic number at start of dump record
 */
uint64_t magic __attribute__((section(".stage2.head")))
	= 0x5a4d554c54363403ULL; /* ZMULT64, version 3 */

/*
 * Parameter format for ECKD MV dumper (13 bytes):
 *
 * DDSS SSEE EEBN O
 *
 * - DD  : Devno of dump target
 * - SSSS: Start block number of dump target
 * - EEEE: End block number of dump target
 * - B   : Blocksize of dump target (needs to be left-shifted by 8 bits)
 * - N   : End Record Number
 * - O   : Number of Heads of DASD
 *
 * We assume that the End Record Number is at track boundary.
 * This allows us to determine the number of Blocks Per Track.
 */
struct mvdump_param {
	uint16_t	devno;
	uint32_t	blk_start;
	uint32_t	blk_end;
	uint8_t		blk_size;
	uint8_t		bpt;
	uint8_t		num_heads;
} __packed;

/*
 * Provide storage for parameter table: 10 + 32*PTE_LENGTH = 426 bytes
 * We take 512 bytes to match with struct mvdump_parm_table in include/boot.h
 */
struct mvdump_parm_table {
	uint64_t	tod;
	uint16_t	num_param;
	struct mvdump_param param[MAX_DUMP_VOLUMES];
	unsigned char	reserved[512 - sizeof(uint64_t) - sizeof(uint16_t) -
			(MAX_DUMP_VOLUMES * sizeof(struct mvdump_param))];
} __packed;

static struct mvdump_parm_table mvdump_table
	__attribute__((section(".eckd2dump_mv.tail")));

static int volnr_current;

/*
 * Get device characteristics for current DASD device from zipl parameter block
 */
void dt_device_parm_setup(void)
{
	struct mvdump_param *param = &mvdump_table.param[volnr_current];

	device.devno = param->devno;
	device.blk_start = param->blk_start;
	device.blk_end = param->blk_end;
	device.blk_size = ((unsigned int) param->blk_size) << 8;
	device.bpt = param->bpt;
	device.num_heads = param->num_heads;
	stage2dump_eckd_init();
}

/*
 * Get subchannel ID for MV dump
 */
static int set_ssid_from_devno(uint16_t devno)
{
	struct subchannel_id ssid;
	struct schib schib;

	memset(&ssid, 0, sizeof(ssid));

	ssid.one = 1;
	ssid.sch_no = 0;
	do {
		if (store_subchannel(ssid, &schib) == 0) {
			if (schib.pmcw.dev == devno) {
				device.ssid = ssid;
				break;
			}
		}
		if (ssid.sch_no == 0xffff)
			return -1;
		ssid.sch_no++;
	} while (1);
	return 0;
}

/*
 * Enable current DASD device
 */
void dt_device_enable(void)
{
	struct mvdump_param *param = &mvdump_table.param[volnr_current];

	if (set_ssid_from_devno(param->devno) != 0)
		panic(ENODEVNO, "%04x is undefined", param->devno);
	io_irq_enable();
	set_device(device.ssid, ENABLED);
}

/*
 * Dump all memory to multiple DASD partitions
 */
void dt_dump_mem(void)
{
	unsigned long blk, addr, addr_end, blk_num, dev_mem_size, page;
	struct mvdump_parm_table *mvdump_table_new;
	struct df_s390_hdr *hdr_new;

	dump_hdr->mvdump_sign = DF_S390_MAGIC;
	dump_hdr->mvdump = 1;
	addr = 0;

	page = get_zeroed_page();
	do {
		printf("Dumping to: %04x", device.devno);

		/*
		 * Check whether parameter table on dump device has a valid
		 * time stamp. The parameter table is located right behind
		 * the dump tool, the corresponding block number is:
		 *   MAGIC_BLOCK_OFFSET + (MVDUMP_TOOL_SIZE / blocksize)
		 *   So dump tool starts on track 0, block 3
		 */
		mvdump_table_new = (void *) page;
		readblock(3 + MVDUMP_SIZE / 0x1000,
			  __pa(mvdump_table_new), m2b(0x1000));
		/*
		 * Check if time stamps match
		 */
		if (mvdump_table.tod != mvdump_table_new->tod)
			panic(ENOTIME, "Inconsistent time stamps");

		/*
		 * Check if dump partition has a valid dump signature.
		 * Bypass signature check if "--force" had been specified during
		 * zipl -M.
		 */
		hdr_new = (void *) page;
		if (!parm_tail.mvdump_force) {
			readblock(device.blk_start, __pa(hdr_new), 1);
			if (dump_hdr->magic != hdr_new->mvdump_sign)
				panic(ENOSIGN, "Wrong signature");
		}

		/*
		 * Write dump header
		 */
		blk = device.blk_start;
		writeblock(blk, __pa(dump_hdr), m2b(DF_S390_HDR_SIZE), 0);
		blk += m2b(DF_S390_HDR_SIZE);

		dev_mem_size = b2m(device.blk_end - blk + 1);
		addr_end = MIN(dump_hdr->mem_size, addr + dev_mem_size);

		/*
		 * Write memory
		 */
		memset((void *) page, 0, PAGE_SIZE);
		while (addr < addr_end) {
			blk_num = MIN(ECKD_BPWRT_MAX, device.blk_end - blk + 1);
			blk_num = MIN(m2b(addr_end - addr), blk_num);
			writeblock(blk, addr, blk_num, page);
			progress_print(addr);
			blk += blk_num;
			addr += b2m(blk_num);
		}
		if (addr == dump_hdr->mem_size)
			break;
		/*
		 * Switch to next volume if available
		 */
		dump_hdr->volnr += 1;
		volnr_current++;
		if (dump_hdr->volnr >= mvdump_table.num_param)
			panic(EMEM, "Device too small");
		dt_device_parm_setup();
		set_device(device.ssid, DISABLED);
		dt_device_enable();
	} while (1);
	progress_print(addr);
	/*
	 * Write end marker
	 */
	df_s390_em_page_init(page);
	writeblock(blk, page, 1, 0);
	free_page(page);
}
