/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Common functions for stand-alone dump tools
 *
 * Copyright IBM Corp. 2013
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <stdarg.h>
#include "stage2dump.h"
#include "error.h"
#include "sclp.h"

#define CPU_ADDRESS_MAX	1000

/*
 * Static globals
 */
static unsigned long progress_next_addr;
static unsigned long progress_inc = 0x20000000; /* Issue message each 512M */

/*
 * IPL info in lowcore
 */
struct ipib_info {
	unsigned long	ipib;
	uint32_t	ipib_csum;
};

/*
 * Tail parameters
 */
struct stage2dump_parm_tail parm_tail
	__attribute__ ((section(".stage2dump.tail"))) = {
	.mem_upper_limit = 0xffffffffffffffffULL,
};

/*
 * Globals
 */
struct df_s390_hdr *dump_hdr;

/*
 * Init dumper: Allocate standard pages, set timers
 */
static void init_early(void)
{
	/* Allocate dump header */
	dump_hdr = (void *) get_zeroed_page();

	/* Set clock comparator and CPU timer to future */
	set_clock_comparator(0xffffffffffffffffULL);
	set_cpu_timer(0x7fffffffffffffffULL);
}

/*

 * Init dump progress messages to sclp console
 *
 * 8 messages if the memory is 4G or less, otherwise one message for each
 * 512M chunk of dumped memory
 */
static void progress_init(void)
{
	if (dump_hdr->mem_size <= (4 * 1024 * 1024 * 1024ULL))
		progress_inc = dump_hdr->mem_size >> 3;
	progress_next_addr = progress_inc;
}

/*
 * Print progress message
 */
void progress_print(unsigned long addr)
{
	if (addr < progress_next_addr && addr != dump_hdr->mem_size)
		return;
	printf("%08lu / %08lu MB", addr >> 20, dump_hdr->mem_size >> 20);
	progress_next_addr += progress_inc;
}

/*
 * Create IDA list starting at "addr" with "len" bytes
 */
void create_ida_list(unsigned long *list, int len, unsigned long addr,
		     unsigned long zero_page)
{
	unsigned long ida_addr;

	while (len > 0) {
		if (zero_page)
			ida_addr = page_is_valid(addr) ? addr : zero_page;
		else
			ida_addr = addr;
		*list = ida_addr;
		list++;
		addr += PAGE_SIZE;
		len -= PAGE_SIZE;
	}
}

/*
 * Initialize s390 dump header
 */
static void df_s390_dump_init(void)
{
	struct df_s390_hdr *dh = dump_hdr;

	dh->magic = DF_S390_MAGIC;
	dh->hdr_size = DF_S390_HDR_SIZE;
	dh->page_size = PAGE_SIZE;
	dh->dump_level = 4;
	dh->version = 5;
	dh->mem_start = 0;
	dh->arch = DF_S390_ARCH_64;
	dh->build_arch = DF_S390_ARCH_64;
	get_cpu_id((struct cpuid *) &dh->cpu_id);
	dh->tod = get_tod_clock();
	dh->volnr = 0;
}

/*
 * Initialize page with end marker
 */
void df_s390_em_page_init(unsigned long addr)
{
	struct df_s390_em *em = (void *) addr;

	em->magic = DF_S390_EM_MAGIC;
	em->tod = get_tod_clock();
}

/*
 * Find out memory size
 */
static void count_mem(void)
{
	unsigned long mem_size_max, mem_size, addr, rnmax, rzm;
	struct read_info_sccb *sccb;

	sccb = (void *) get_zeroed_page();
	/* Get memory max */
	if (sclp_read_info(sccb))
		panic(EMEMCOUNT, "Could not evaluate memory layout");
	rnmax = sccb->rnmax ? sccb->rnmax : sccb->rnmax2;
	rzm = sccb->rnsize ? sccb->rnsize : sccb->rnsize2;
	rzm <<= 20;
	mem_size_max = rnmax * rzm;
	mem_size = 0;
	/* Find out real memory end without standby memory */
	for (addr = 0; addr < mem_size_max; addr += rzm) {
		if (!page_is_valid(addr))
			continue;
		mem_size = addr + rzm;
	}
	free_page((unsigned long) sccb);

	dump_hdr->mem_size_real = mem_size;
	/* Check if we have an upper limit */
	if (mem_size > parm_tail.mem_upper_limit) {
		printf("Using memory limit");
		mem_size = parm_tail.mem_upper_limit;
	}
	dump_hdr->mem_size = dump_hdr->mem_end = mem_size;
	dump_hdr->num_pages = dump_hdr->mem_size >> 12;
}

/*
 * Copy 64 bit lowcore after store status
 */
static void copy_lowcore_64(void)
{
	char *real_cpu_cnt_ptr = ((char *) &dump_hdr->mem_size_real) + 11;
	char *cpu_cnt_ptr = ((char *) &dump_hdr->mem_size_real) + 9;
	unsigned long prefix;
	uint16_t cpu_cnt = 0;

	prefix = S390_lowcore.prefixreg_save_area;

	/* Need memcpy because of aligment problem of members */
	memcpy(&cpu_cnt, real_cpu_cnt_ptr, sizeof(cpu_cnt));
	cpu_cnt++;
	memcpy(real_cpu_cnt_ptr, &cpu_cnt, sizeof(cpu_cnt));

	if (prefix < 0x10000) /* if < linux-start addr */
		return;
	if (prefix % 0x1000) /* check page alignment */
		return;

	/* Save lowcore pointer (32 bit) in dump header */
	memcpy(&cpu_cnt, cpu_cnt_ptr, sizeof(cpu_cnt));
	dump_hdr->lc_vec[cpu_cnt] = prefix;
	cpu_cnt++;
	memcpy(cpu_cnt_ptr, &cpu_cnt, sizeof(cpu_cnt));
	/*
	 *  |-----------------------------------------------------------|
	 *  | Decimal |  Length   | Data                                |
	 *  | Address |  in Bytes |                                     |
	 *  |_________|___________|_____________________________________|
	 *  | 163     | 1         | Architectural Mode ID               |
	 *  | 4608    | 128       | Fl-pt registers 0-15                |
	 *  | 4736    | 128       | General registers 0-15              |
	 *  | 4864    | 16        | Current PSW                         |
	 *  | 4888    | 4         | Prefix register                     |
	 *  | 4892    | 4         | Fl-pt control register              |
	 *  | 4900    | 4         | TOD programmable register           |
	 *  | 4904    | 8         | CPU timer                           |
	 *  | 4912    | 1         | Zeros                               |
	 *  | 4913    | 7         | Bits 0-55 of clock comparator       |
	 *  | 4928    | 64        | Access registers 0-15               |
	 *  | 4992    | 128       | Control registers 0-15              |
	 *  |_________|___________|_____________________________________|
	 */
	memcpy((void *) prefix + 4608, (void *) 4608, 272);
	memcpy((void *) prefix + 4888, (void *) 4888, 8);
	memcpy((void *) prefix + 4900, (void *) 4900, 20);
	memcpy((void *) prefix + 4928, (void *) 4928, 192);
}

/*
 * Do store status
 */
static void store_status(void)
{
	unsigned short current_cpu;
	unsigned long page;
	int addr, cc;

	/* Save absolute zero lowcore */
	page = get_zeroed_page();
	memcpy((void *) page, (void *) 0x1000, PAGE_SIZE);

	current_cpu = stap();

	copy_lowcore_64();

	for (addr = 0; addr < CPU_ADDRESS_MAX; addr++) {
		if (addr == current_cpu)
			continue;
		do {
			cc = sigp(addr, SIGP_STOP_AND_STORE_STATUS, 0, NULL);
		} while (cc == SIGP_CC_BUSY);

		if (cc != SIGP_CC_ORDER_CODE_ACCEPTED)
			continue;
		copy_lowcore_64();
	}
	/* Restore absolute zero lowcore */
	memcpy((void *) 0x1000, (void *) page, PAGE_SIZE);
	free_page(page);
}

/*
 * Perform reipl: check lowcore for the address of an IPL Information
 * Block followed by a valid checksum (as defined in lowcore.h and set
 * by ipl.c). In case of match use diag308 to IPL.
 */
static void dump_exit(unsigned long code)
{
	struct ipib_info *ipib_info = (struct ipib_info *)&S390_lowcore.ipib;
	uint32_t ipib_len, csum;

	if (!ipib_info->ipib)
		libc_stop(code);
	ipib_len = *((uint32_t *) ipib_info->ipib);
	csum = csum_partial((void *) ipib_info->ipib, ipib_len, 0);
	if (ipib_info->ipib_csum != csum)
		libc_stop(code);
	diag308(DIAG308_SET, (void *) ipib_info->ipib);
	diag308(DIAG308_IPL, NULL);
}

/*
 * Print message and exit dumper
 */
void panic_notify(unsigned long code)
{
	printf("Dump failed");
	dump_exit(code);
}

/*
 * Create stand-alone dump
 */
void start(void)
{
	init_early();
	store_status();
	dt_device_parm_setup();
	sclp_setup(SCLP_INIT);
	dt_device_enable();
	df_s390_dump_init();
	printf("zIPL v%s dump tool (64 bit)", RELEASE_STRING);
	printf("Dumping 64 bit OS");
	count_mem();
	progress_init();
	dt_dump_mem();
	printf("Dump successful");
	dump_exit(0);
}
