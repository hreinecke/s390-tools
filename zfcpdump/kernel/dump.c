/*
 *  drivers/s390/char/dump.c
 *
 *  Provides functionality to dump memory content and register sets
 *  to a dump device. The written dump can be analyzed afterwards
 *  with the dump analysis tool lcrash.
 *
 *    Copyright IBM Corp. 2003, 2006.
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */


#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <asm/uaccess.h>
#include <asm/sigp.h>
#include <asm/types.h>
#include <asm/sigp.h>
#include <asm/lowcore.h>
#include <asm/debug.h>
#include <asm/checksum.h>
#include "dump.h"

/*******************************************************************
 * defines 
 *******************************************************************/

#define DUMP_MEM_SIZE_MB 32UL

#define DUMP_SAVE_AREA_BASE_ESAME 4608
#define DUMP_SAVE_AREA_BASE_ESA   212

#define DUMP_LC_DUMP_REIPL      0xe00 /* ipib address and checksum */
#define DUMP_LC_ARCH_MODE_ADDR  0xa3  /* dec. 163: Address of arch id tag */
#define DUMP_LC_ARCH_MODE_ESAME 1     /* esame mode id tag */
#define DUMP_LC_ARCH_MODE_ESA   0     /* esa mode id tag */

#ifdef __s390x__
#define LOWCORE_SIZE 0x2000
#else
#define LOWCORE_SIZE 0x1000
#endif

#define DEBUG_LEVEL_DEFAULT 2

#define DUMP_MAX_CPUS 1024

#define MIN(a,b) ( (a)<(b) ? (a):(b) )
#define MAX(a,b) ( (a)>(b) ? (a):(b) )

#define STIDP(x) asm volatile ("STIDP 0(%0)" : : "a" (&(x)) : "memory" ,"cc")

#define DUMP_PRINT_TRACE(x...) if(dump.debug_level > 3) \
			{printk( KERN_ALERT "K_TRACE: " x );}
#define DUMP_PRINT_ERR(x...)  printk( KERN_ALERT "K_ERROR: " x )
#define DUMP_PRINT_WARN(x...) printk( KERN_ALERT "K_WARNING: " x )
#define DUMP_PRINT(x...)      printk( KERN_ALERT x )

/*******************************************************************
 * typedefs 
 *******************************************************************/

typedef enum {esa = 1,esame = 2} dump_arch_id_t;

/* lowcore data */

typedef union {
	struct {
		__u32 ext_save_area;
		__u64 timer;
		__u64 clock_comparator;
		__u8  pad1[24];
		__u8  psw[8];
		__u32 pref_reg;
		__u8  pad2[20];
		__u32 acc_regs[16];
		__u64 fp_regs[4];
		__u32 gp_regs[16];
		__u32 ctrl_regs[16];
	}  __attribute__((packed)) esa;

	struct {
		__u64 fp_regs[16];
		__u64 gp_regs[16];
		__u8  psw[16];
		__u8  pad1[8];
		__u32 pref_reg;
		__u32 fp_ctrl_reg;
		__u8  pad2[4];
		__u32 tod_reg;
		__u64 timer;
		__u64 clock_comparator;
		__u8  pad3[8];
		__u32 acc_regs[16];
		__u64 ctrl_regs[16];
	}  __attribute__((packed)) esame;
} dump_cpu_info_t;

/* dump arch info */

typedef struct _dump_arch {
	dump_arch_id_t	 arch;		 /* arch id */
	unsigned long	 save_area_base; /* save area base rel. to pref. page */
	__u32		 save_area_size; /* size fo save area */
	dump_cpu_info_t* cpu_info;	 /* cpu-info array with registers */
} dump_arch_t;

/*******************************************************************
 * prototypes
 *******************************************************************/
 
static void dump_cpu_stop_and_store_status(int cpu);
int dump_init(void);
void dump_exit(void);
static int zcore_init(void);
static void zcore_exit(void);
static int zcore_open(struct inode * inode, struct file * filp);
static loff_t zcore_lseek(struct file * file, loff_t offset, int orig);
static ssize_t zcore_read(struct file * file, char * buf, size_t count,
		loff_t *ppos);
static int zcore_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg) ;

static int sdias_init(void);
static int sdias_get_nr_blocks(void);
static int sdias_copy_blocks(void* target,int start_blk, int nr_blks);
static int sdias_memcpy(char* target, char* src, int count);

/*******************************************************************
 * externs 
 *******************************************************************/

extern unsigned long memory_size;   /* Mem size */
extern void          *ldipl_parm;   /* parameter block passed by ldipl */
extern volatile int __cpu_logical_map[];

/*******************************************************************
 * globals
 *******************************************************************/

unsigned int dump_prefix_array[DUMP_MAX_CPUS] 
             = {0xffffffff}; /* initialize to make sure that it is NOT put */
                             /* into bss section. Holds prefix registers for */
                             /* the following scenario: 64 bit system dumper */
                             /* and 32 bit Kernel which is dumped.           */
                             /* The array is filled in s390x/kernel/head.S   */
                             /* and is 0 terminated. The boot cpu is not     */
                             /* contained in the array. The prefix register  */
                             /* for a logical cpu x (according to            */
                             /* cpu_logical_map can be found with            */
                             /* dump_prefix_array[x-1]                       */

static struct {
	dump_arch_t		arch_info;	      /* ESA/EASME arch info */
	int			nr_cpus;	      /* Nr of cpus on system */
	unsigned long		mem_size;	      /* Memory size */
	unsigned long		sa_mem_size;	      /* HSA saved memsize */
	char			buffer[4096];	      /* i/o buffer */
	int			cpu_logical_map[500]; /* our internal mapping of logical cpu addresses (0,1,...n) to physical cpu addresses */
	int			debug_level;          /* the debug level (0 - 6). The bigger the more messages */
	debug_info_t            *dbf;                 /* Debug feature handle */
	int                     error_code;           /* error code of dumper. can be obtained from user space with ioctl */
} dump;

struct ipl_block_fcp *fcp_data;
static struct ipl_parameter_block *ipl_block;

struct ipib_info {
	unsigned long	ipib;
	u32		checksum;
} __attribute__((packed));

/*******************************************************************
 * functions
 *******************************************************************/

int diag308(unsigned long subcode, void *addr)
{
	register unsigned long _addr asm("0") = (unsigned long) addr;
	register unsigned long _rc asm("1") = 0;

	asm volatile(
		"	diag	%0,%2,0x308\n"
		"0:\n"
		: "+d" (_addr), "+d" (_rc)
		: "d" (subcode) : "cc", "memory");
	return _rc;
}

/*
 * Get the debug level
 */

static int __init dump_debug(char *str)
{
	get_option(&str, &console_loglevel);
	dump.debug_level=console_loglevel;
	return 1;
}
 
__setup("dump_debug=", dump_debug);

/*
 * copy memory not doing virtual address transation
 */
static __inline__ void*
memcpy_real(void *dest, const void *src, size_t count)
{
	unsigned long flags;
	void *ret;
 
	__asm__ __volatile__ ("stnsm %0, 0xF8\n"
			      : "=m" (flags) : : "memory");
	ret = memcpy(dest, src, count);
	__asm__ __volatile__ ("ssm %0 \n"
			      : "+m" (flags) : : "memory");
	return ret;
}

#define memcpy_absolute memcpy_real /* This is correct since we set the */
                                    /* prefix page to 0 */

/* 
 * Send a signal to a logical cpu number 
 */

#ifdef __s390x__

extern __inline__ sigp_ccode
dump_sigp(__u16 cpu_addr, sigp_order_code order_code)
{
	sigp_ccode ccode;
 
	__asm__ __volatile__(
		"    sgr    1,1\n"	  /* parameter=0 in gpr 1 */
		"    sigp   1,%1,0(%2)\n"
		"    ipm    %0\n"
		"    srl    %0,28"
		: "=d" (ccode)
		: "d" (dump.cpu_logical_map[cpu_addr]), "a" (order_code)
		: "cc" , "memory", "1" );
	return ccode;
}

#else /* __s390__ */

extern __inline__ sigp_ccode
dump_sigp(__u16 cpu_addr, sigp_order_code order_code)
{
	sigp_ccode ccode;

	__asm__ __volatile__(
		"    sr     1,1\n"        /* parameter=0 in gpr 1 */
		"    sigp   1,%1,0(%2)\n"
		"    ipm    %0\n"
		"    srl    %0,28"
		: "=d" (ccode)
		: "d" (dump.cpu_logical_map[cpu_addr]), "a" (order_code)
		: "cc" , "memory", "1" );
	return ccode;
}

#endif

/* 
 * Send a signal to a physical cpu number 
 */

#ifdef __s390x__

extern __inline__ sigp_ccode
dump_sigp_direct(__u16 cpu_addr, sigp_order_code order_code)
{
	sigp_ccode ccode;
 
	__asm__ __volatile__(
		"    sgr    1,1\n"	  /* parameter=0 in gpr 1 */
		"    sigp   1,%1,0(%2)\n"
		"    ipm    %0\n"
		"    srl    %0,28"
		: "=d" (ccode)
		: "a" (cpu_addr), "a" (order_code)
		: "cc" , "memory", "1" );
	return ccode;
}

#else /* __s390__ */

extern __inline__ sigp_ccode
dump_sigp_direct(__u16 cpu_addr, sigp_order_code order_code)
{
	sigp_ccode ccode;

	__asm__ __volatile__(
		"    sr     1,1\n"        /* parameter=0 in gpr 1 */
		"    sigp   1,%1,0(%2)\n"
		"    ipm    %0\n"
		"    srl    %0,28"
		: "=d" (ccode)
		: "a" (cpu_addr), "a" (order_code)
		: "cc" , "memory", "1" );
	return ccode;
}

#endif

/*
 * Count CPUs
 */
static void 
dump_smp_count_cpus(void)
{
	int curr_cpu;
	int num_cpus = 1;
	int rc;

	__u16 boot_cpu_addr = __cpu_logical_map[0]; /* physical number of */
                                                    /* cpu */
	dump.cpu_logical_map[0] = boot_cpu_addr;
	for (curr_cpu = 0; curr_cpu <= 65535; curr_cpu++) {
		if ((__u16) curr_cpu == boot_cpu_addr ){
			continue;
		}
		rc = dump_sigp_direct(curr_cpu, sigp_sense);
		if (rc == sigp_not_operational){
			continue;
		} else {
			dump.cpu_logical_map[num_cpus] = (__u16) curr_cpu;
			DUMP_PRINT_TRACE("detected cpu: %i (%x)\n",curr_cpu,rc);
			num_cpus++;
		}
	}
	DUMP_PRINT_TRACE("boot cpu	 : %i\n",boot_cpu_addr);
	dump.nr_cpus  = num_cpus;
}

/*
 * Print CPU info (Registers/PSWs) to console
 */
static void 
dump_print_cpu_info_esa(dump_cpu_info_t* cpu_info)
{
	int i;
	DUMP_PRINT_TRACE("psw   : %08x %08x\n",*((int*)cpu_info->esa.psw),
		*((int*)&(cpu_info->esa.psw[4])));
	DUMP_PRINT_TRACE("prefix: %08x\n",cpu_info->esa.pref_reg);
	DUMP_PRINT_TRACE("clk   : %016Lx\n",(long long)cpu_info->esa.timer);
	DUMP_PRINT_TRACE("clkcmp: %016Lx\n",
		(long long)cpu_info->esa.clock_comparator);
	DUMP_PRINT_TRACE("gpregs:\n");
	for(i = 0; i < 16; i+=4){
		DUMP_PRINT_TRACE("%08x %08x %08x %08x\n",
			cpu_info->esa.gp_regs[i],
			cpu_info->esa.gp_regs[i+1],
			cpu_info->esa.gp_regs[i+2],
			cpu_info->esa.gp_regs[i+3]);
	}
	DUMP_PRINT_TRACE("accregs:\n");
	for(i = 0; i < 16; i+=4){
		DUMP_PRINT_TRACE("%08x %08x %08x %08x\n",
			cpu_info->esa.acc_regs[i],
			cpu_info->esa.acc_regs[i+1],
			cpu_info->esa.acc_regs[i+2],
			cpu_info->esa.acc_regs[i+3]);
	}
	DUMP_PRINT_TRACE("ctrl_regs:\n");
	for(i = 0; i < 16; i+=4){
		DUMP_PRINT_TRACE("%08x %08x %08x %08x\n",
			cpu_info->esa.ctrl_regs[i],
			cpu_info->esa.ctrl_regs[i+1],
			cpu_info->esa.ctrl_regs[i+2],
			cpu_info->esa.ctrl_regs[i+3]);
	}
	DUMP_PRINT_TRACE("fp_regs:\n");
	for(i = 0; i < 4; i+=2){
		DUMP_PRINT_TRACE("%016Lx %016Lx\n",
			(long long)cpu_info->esa.fp_regs[i],
			(long long)cpu_info->esa.fp_regs[i+1]);
	}
}

/*
 * Print CPU info (Registers/PSWs) to console
 */

static void 
dump_print_cpu_info_esame(dump_cpu_info_t* cpu_info)
{
	int i;
	DUMP_PRINT_TRACE("psw: %016lx %016lx\n",
		*((unsigned long*)cpu_info->esame.psw),
		*((unsigned long*)&(cpu_info->esame.psw[8])));
	DUMP_PRINT_TRACE("prefix: %08x\n",cpu_info->esame.pref_reg);
	DUMP_PRINT_TRACE("clk   : %016Lx\n",(long long)cpu_info->esame.timer);
	DUMP_PRINT_TRACE("clkcmp: %016Lx\n",
		(long long)cpu_info->esame.clock_comparator);
	DUMP_PRINT_TRACE("fpctrl: %04x\n",cpu_info->esame.fp_ctrl_reg);
	DUMP_PRINT_TRACE("todreg: %04x\n",cpu_info->esame.tod_reg);
	DUMP_PRINT_TRACE("gpregs:\n");
	for(i = 0; i < 16; i+=2){
		DUMP_PRINT_TRACE("%016Lx %016Lx\n",
			(long long)cpu_info->esame.gp_regs[i],
			(long long)cpu_info->esame.gp_regs[i+1]);
	}
	DUMP_PRINT_TRACE("accregs:\n");
	for(i = 0; i < 16; i+=4){
		DUMP_PRINT_TRACE("%08x %08x %08x %08x\n",
			cpu_info->esame.acc_regs[i],
			cpu_info->esame.acc_regs[i+1],
			cpu_info->esame.acc_regs[i+2],
			cpu_info->esame.acc_regs[i+3]);
	}
	DUMP_PRINT_TRACE("ctrl_regs:\n");
	for(i = 0; i < 16; i+=2){
		DUMP_PRINT_TRACE("%016Lx %016Lx\n",
			(long long)cpu_info->esame.ctrl_regs[i],
			(long long)cpu_info->esame.ctrl_regs[i+1]);
	}
	DUMP_PRINT_TRACE("fp_regs:\n");
	for(i = 0; i < 16; i+=2){
		DUMP_PRINT_TRACE("%016Lx %016Lx\n",
			(long long)cpu_info->esame.fp_regs[i],
			(long long)cpu_info->esame.fp_regs[i+1]);
	}
}

/*
 * Print global data
 */
static void 
dump_print_glob_info(void)
{
	DUMP_PRINT_TRACE("Architecture    : %s\n",
		((dump.arch_info.arch == esa) ? "esa" : "esame"));
	DUMP_PRINT_TRACE("Nummber of cpus : %i\n", dump.nr_cpus);
	DUMP_PRINT_TRACE("Memory size     : %li\n",dump.mem_size);
	DUMP_PRINT_TRACE("HSA Memory size : %li\n",dump.sa_mem_size);
}

/*
 * Convert ESAME (64 bit) cpu info to ESA (32 bit) cpu info
 */
static void 
dump_copy_esame_to_esa(dump_cpu_info_t* esa_info, dump_cpu_info_t* esame_info,int cpu)
{
	int i;

	DUMP_PRINT_TRACE("dump_copy_esame_to_esa:");
	for(i = 0; i < 16; i++){
		esa_info->esa.gp_regs[i]   = esame_info->esame.gp_regs[i] & 
						0x00000000ffffffff;
		esa_info->esa.acc_regs[i]  = esame_info->esame.acc_regs[i];
		esa_info->esa.ctrl_regs[i] = esame_info->esame.ctrl_regs[i] & 
						0x00000000ffffffff;
	}
	/* locore for 31 bit has only space for fpregs 0,2,4,6 */
	esa_info->esa.fp_regs[0] = esame_info->esame.fp_regs[0];
	esa_info->esa.fp_regs[1] = esame_info->esame.fp_regs[2];
	esa_info->esa.fp_regs[2] = esame_info->esame.fp_regs[4];
	esa_info->esa.fp_regs[3] = esame_info->esame.fp_regs[6];
	memcpy(&(esa_info->esa.psw[0]),&(esame_info->esame.psw[0]),4);
	esa_info->esa.psw[1] |= 0x8; /* set bit 12 */
	memcpy(&(esa_info->esa.psw[4]),&(esame_info->esame.psw[12]),4);
	esa_info->esa.psw[4] |= 0x80; /* set (31bit) addressing bit */
	esa_info->esa.pref_reg  = dump_prefix_array[cpu-1];
	DUMP_PRINT_TRACE("CPU %i - prefix = %x\n",cpu,esa_info->esa.pref_reg);
	esa_info->esa.timer = esame_info->esame.timer;
	esa_info->esa.clock_comparator = esame_info->esame.clock_comparator;
}

/*
 * Collect and return cpu info (registers/PSWs) for all existing CPUs
 */
static dump_cpu_info_t* 
dump_get_cpu_info(int nr_cpus)
{
	dump_cpu_info_t *info = NULL;
	int i,dump_cpu = 0; /* locical cpu numbering! */

	info  = kmalloc(nr_cpus * sizeof(dump_cpu_info_t), GFP_KERNEL);
	if(!info) {
		DUMP_PRINT_ERR("kmalloc failed: %s: %i\n",__FUNCTION__,
				__LINE__);
		goto error;
	}

	/* get first lowcore from hsa */

	switch(dump.arch_info.arch){
		case esa:
			if(sdias_memcpy((char*)&(info[dump_cpu].esa),
				(char*)dump.arch_info.save_area_base,
				sizeof(info[0].esa)) < 0){
				DUMP_PRINT_ERR("could not copy from HSA\n");
				goto error;
			}
			break;
		case esame:
			if(sdias_memcpy((char*)&(info[dump_cpu].esame),
				(void*)dump.arch_info.save_area_base,
				sizeof(info[0].esame)) < 0){
				DUMP_PRINT_ERR("could not copy from HSA\n");
				goto error;
			}
			break;
		default:
			DUMP_PRINT_ERR("dump: unknown arch %x\n",
				dump.arch_info.arch);
			goto error;
	}

	if(dump.arch_info.arch == esa){
		dump_print_cpu_info_esa(&(info[dump_cpu]));
	} else {
		dump_print_cpu_info_esame(&(info[dump_cpu]));	
	}

	/* now get the other lowcores with sigp store status */

	for(i=0 ; i< dump.nr_cpus; i++) {
		if(i == dump_cpu)
			continue;
			
		dump_cpu_stop_and_store_status(i);
		switch(dump.arch_info.arch){
#ifdef __s390x__ 
			dump_cpu_info_t tmp_info;
#endif
			case esa:
#ifdef __s390x__
				memcpy_absolute(&(tmp_info.esame),
					(char*)DUMP_SAVE_AREA_BASE_ESAME ,
					sizeof(tmp_info.esame));
				dump_copy_esame_to_esa(&info[i],&tmp_info,i);
#else
				memcpy_absolute(&(info[i].esa),
					(char*)dump.arch_info.save_area_base,
					sizeof(info[0].esa));
#endif
				break;
			case esame:
				memcpy_absolute(&(info[i].esame),
					(char*)dump.arch_info.save_area_base,
					sizeof(info[0].esame));
				break;
			default:
				DUMP_PRINT_ERR("dump: unknown arch %x\n",
					dump.arch_info.arch);
				goto error;
		}
		DUMP_PRINT_TRACE("cpu: %i\n",i);
		if(dump.arch_info.arch == esa){
			dump_print_cpu_info_esa(&(info[i]));
		} else {
			dump_print_cpu_info_esame(&(info[i]));	
		}
	}
	return info;
error:
	if(info){
		kfree(info);
	}
	return NULL;
}

void setup_dump_devnos(char *cmdline, unsigned int init_devno,
		       unsigned int cons_devno)
{
	char str[100] = {0};
	if (cons_devno == -1)
		sprintf(str," cio_ignore=all,!0.0.%04x", init_devno);
	else
		sprintf(str," cio_ignore=all,!0.0.%04x,!0.0.%04x", init_devno,
			cons_devno);
	strcat(cmdline, str);
}

void setup_dump_base(void)
{
	char str[100] = {0};
#ifdef __s390x__
	printk("System Dumper s390x starting...\n");
#else
	printk("System Dumper s390 starting...\n");
#endif
	sprintf(str," root=/dev/ram0 rw mem=%liM maxcpus=1", DUMP_MEM_SIZE_MB);
	strcat(COMMAND_LINE, str);
	console_loglevel = DEBUG_LEVEL_DEFAULT;
	dump.debug_level = DEBUG_LEVEL_DEFAULT;
}

/*
 * Provide IPL parameter information block from either HSA or memory
 * for future reipl
 */
static int __init zcore_reipl_init(void)
{
	struct ipib_info ipib_info;
	int rc;

	rc = sdias_memcpy((char *) &ipib_info, (void *) DUMP_LC_DUMP_REIPL,
			  sizeof(ipib_info));
	if (rc) {
		DUMP_PRINT_ERR("sdias memcpy of ipib address and checksum "
			       "failed\n");
		return rc;
	}
	if (ipib_info.ipib == 0)
		return 0;
	ipl_block = (void *) __get_free_page(GFP_KERNEL);
	if (!ipl_block)
		return -ENOMEM;
	if (ipib_info.ipib < dump.sa_mem_size)
		rc = sdias_memcpy((char *) ipl_block, (void *) ipib_info.ipib,
				  PAGE_SIZE);
	else {
		memcpy_absolute(ipl_block, (void *) ipib_info.ipib,
				PAGE_SIZE);
		rc = 0;
	}
	if (rc) {
		DUMP_PRINT_ERR("Copy of IPL parameter information block "
			       "failed\n");
		free_page((unsigned long) ipl_block);
		return rc;
	}
	if (csum_partial((char *) ipl_block, ipl_block->header.length, 0) !=
	    ipib_info.checksum) {
		DUMP_PRINT_ERR("Checksum does not match\n");
		free_page((unsigned long) ipl_block);
		ipl_block = NULL;
	}
	return 0;
}

/*
 * Initialize dump globals for a given architecture
 */

int __init
dump_init(void)
{
	unsigned char arch_id = 0x88;
	long hsa_nr_of_blocks;
	int rc = 0;

	fcp_data = &(((struct ipl_parameter_block *)ldipl_parm)->fcp);
	DUMP_PRINT_TRACE("LEN  : %x\n",fcp_data->length);
	DUMP_PRINT_TRACE("DEVNO: %x\n",fcp_data->devno);
	DUMP_PRINT_TRACE("WWPN : %lx\n",fcp_data->wwpn);
	DUMP_PRINT_TRACE("LUN  : %lx\n",fcp_data->lun);

	dump.dbf = debug_register("dump", 1, 1, 8);
	if(dump.dbf)
		debug_register_view(dump.dbf, &debug_hex_ascii_view);
	DUMP_PRINT_TRACE("DEBUGAREA: %p\n",dump.dbf->areas[0]);

	debug_text_event(dump.dbf, 1, "startdbf");

	DUMP_PRINT_TRACE("DUMP init\n");

	if(sdias_init()){
		DUMP_PRINT_ERR("could not initialize sclp\n");
		rc = -1; goto out;
	}
	hsa_nr_of_blocks = sdias_get_nr_blocks();
	if(hsa_nr_of_blocks == -1){
		DUMP_PRINT_ERR("could not determine HSA size\n");
		dump.sa_mem_size = 0;
		dump.error_code = ZCORE_ERR_HSA_FEATURE;
		rc = -1; goto out;
	}
	dump.sa_mem_size = (hsa_nr_of_blocks - 1) *  4096;

	if(dump.sa_mem_size < (DUMP_MEM_SIZE_MB * 1024 * 1024)){
		DUMP_PRINT_ERR("hsa size too small! Should be at least %li - is %li\n",(DUMP_MEM_SIZE_MB * 1024UL * 1024UL), dump.sa_mem_size);
		dump.error_code = ZCORE_ERR_HSA_FEATURE;
		rc = -1; goto out;
	} else {
		/* We need only copy DUMP_MEM_SIZE_MB from hsa */
		dump.sa_mem_size = (DUMP_MEM_SIZE_MB * 1024 * 1024);
	}

	/* check arch of operating system */

	DUMP_PRINT_TRACE("prefix of dump cpu: %lx\n",
				(unsigned long)lowcore_ptr[0]);
#ifdef __s390x__ /* 64 bit */

	DUMP_PRINT_TRACE("64 bit System dumper\n");
	if(sdias_memcpy(&arch_id,(void*)(DUMP_LC_ARCH_MODE_ADDR),1) < 0){
		DUMP_PRINT_ERR("sdias memcpy for arch id failed\n");
		dump.error_code = ZCORE_ERR_HSA_FEATURE;
		rc = -1; goto out;
	}

	DUMP_PRINT_TRACE("arch id: %x: %x\n",DUMP_LC_ARCH_MODE_ADDR, arch_id);

#else /* 32 bit */

	DUMP_PRINT_TRACE("32 bit System dumper\n");
	arch_id = DUMP_LC_ARCH_MODE_ESA;

#endif

	DUMP_PRINT_TRACE("set prefix to 0\n");
	memcpy(lowcore_ptr[0],0,LOWCORE_SIZE);
	set_prefix(0);
	lowcore_ptr[0] = 0;
	DUMP_PRINT_TRACE("prefix of dump cpu: %lx\n",
		(unsigned long)lowcore_ptr[0]);

	switch(arch_id){
	case DUMP_LC_ARCH_MODE_ESAME:
		DUMP_PRINT("DETECTED 'ESAME (64 bit) OS'...\n");
		dump.arch_info.arch = esame;
		dump.arch_info.save_area_base = DUMP_SAVE_AREA_BASE_ESAME;
		dump.arch_info.save_area_size = 
			sizeof(dump.arch_info.cpu_info[0].esame);
		break;
	case DUMP_LC_ARCH_MODE_ESA:
		DUMP_PRINT("DETECTED 'ESA (32 bit) OS'...\n");
		dump.arch_info.arch = esa;
		dump.arch_info.save_area_base = DUMP_SAVE_AREA_BASE_ESA;
		dump.arch_info.save_area_size =
			sizeof(dump.arch_info.cpu_info[0].esa);
		break;
	default:
		DUMP_PRINT_WARN("unknown architecture 0x%x.\n",arch_id);
		DUMP_PRINT_WARN("probably operator has forgot store status.\n");
		DUMP_PRINT_WARN("Assuming ESAME architecture now...\n");
		dump.arch_info.arch = esame;
		dump.arch_info.save_area_base = DUMP_SAVE_AREA_BASE_ESAME;
		dump.arch_info.save_area_size =
			sizeof(dump.arch_info.cpu_info[0].esame);
		break;
	}

	dump_smp_count_cpus();

	dump.arch_info.cpu_info = dump_get_cpu_info(dump.nr_cpus);
	if(!dump.arch_info.cpu_info){
		DUMP_PRINT_ERR("get cpu info failed\n");
		dump.error_code = ZCORE_ERR_CPU_INFO;
		rc = -1; goto out;
	}
	dump.mem_size = memory_size;
	dump_print_glob_info();
	if (zcore_reipl_init()) {
		DUMP_PRINT_ERR("zcore_reipl_init failed\n");
		dump.error_code = ZCORE_ERR_REIPL;
		rc = -1;
		goto out;
	}
	if(zcore_init() < 0){
		DUMP_PRINT_ERR("zcore init failed\n");
		dump.error_code = ZCORE_ERR_OTHER;
		rc = -1; goto out;
	}
out:
	return rc;
}

/*
 * cleanup everything
 */

void __exit
dump_exit(void)
{
	free_page((unsigned long) ipl_block);
	zcore_exit();
	kfree(dump.arch_info.cpu_info);
}

/*
 * stop a cpu and store status of the cpu
 */

static void 
dump_cpu_stop_and_store_status(int cpu)
{
	int ccode = 0;

	do {
		ccode = dump_sigp(cpu, sigp_stop_and_store_status);
	} while(ccode == sigp_busy);
}

/****************************************************************************/
/* zcore code:								    */
/****************************************************************************/


/* definitions */
#define ZCORE_MAJOR 200
#define KL_DUMP_VERSION_NUMBER_S390  0x3	    /* version number */
#define KL_DUMP_MAGIC_S390     0xa8190173618f23fdULL  /* s390 magic number  */
#define KL_DUMP_HEADER_SZ_S390 4096
 
/* header dumped at the top of every valid Linux/s390 crash dump created
 * with standalone dump tools for s390.
 */
typedef struct dump_header_s390_s {
	uint64_t magic_number; /* magic number for this dump (unique)*/
	uint32_t version;      /* version number of this dump */
	uint32_t header_size;  /* size of this header */
	uint32_t dump_level;   /* the level of this dump (just a header?) */
	uint32_t page_size;    /* page size of dumped Linux (4K,8K,16K etc.) */
	uint64_t memory_size;  /* the size of all physical memory */
	uint64_t memory_start; /* the start of physical memory */
	uint64_t memory_end;   /* the end of physical memory */
	uint32_t num_pages;    /* number of pages in this dump */
	uint32_t pad;	       /* ensure 8 byte alignment for tod and cpu_id */
	uint64_t tod;	       /* the time of the dump generation */
	uint64_t cpu_id;       /* cpu id */
	uint32_t arch_id;
	uint32_t build_arch;
	char	 filler[4016];
#define KL_DH_ARCH_ID_S390X 2
#define KL_DH_ARCH_ID_S390  1
} __attribute__((packed,__aligned__(16)))  dump_header_s390_t;
 
static dump_header_s390_t zcore_dump_header = {
	magic_number: KL_DUMP_MAGIC_S390,
	version:      KL_DUMP_VERSION_NUMBER_S390,
	header_size:  4096,
	dump_level:   0,
	page_size:    4096,
	memory_start: 0,
};


static struct file_operations zcore_fops = {
	llseek:		zcore_lseek,
	read:		zcore_read,
	ioctl:		zcore_ioctl,
	open:		zcore_open
};

/*
 * Init character device
 */
static int 
zcore_init(void)
{
	int rc = 0;
	zcore_dump_header.arch_id	 = (dump.arch_info.arch == esame) ? KL_DH_ARCH_ID_S390X : KL_DH_ARCH_ID_S390;
#ifdef __s390x__
	zcore_dump_header.build_arch  = KL_DH_ARCH_ID_S390X;
#else
	zcore_dump_header.build_arch  = KL_DH_ARCH_ID_S390;
#endif
	zcore_dump_header.memory_size = dump.mem_size;
	zcore_dump_header.memory_end  = dump.mem_size;
	zcore_dump_header.num_pages	 = dump.mem_size / 4096;
	STCK(zcore_dump_header.tod);
	STIDP(zcore_dump_header.cpu_id);
	if (register_chrdev(ZCORE_MAJOR,"zcore",&zcore_fops)){
		DUMP_PRINT_ERR("unable to get major %d for memory devs\n",
			MEM_MAJOR);
		rc = -1; goto out;
	}
out:
	return rc;
}

/*
 *  Unregister zcore device
 */
static void 
zcore_exit(void)
{
	unregister_chrdev (ZCORE_MAJOR, "zcore");
}


/*
 * Copy lowocre info to memory, if necessary
 */
static int 
zcore_add_lowcores_to_output(char* buf, size_t count, unsigned long mem_start)
{
	unsigned long mem_end = mem_start + count;
	int i;

	for(i=0 ; i< dump.nr_cpus; i++) {
		unsigned long new_end, new_start;
		unsigned long save_start, save_end;
		unsigned long prefix;

		if(dump.arch_info.arch == esa){
			prefix = dump.arch_info.cpu_info[i].esa.pref_reg;
		} else { /* esame */
			prefix = dump.arch_info.cpu_info[i].esame.pref_reg;
		}
		save_start= prefix + dump.arch_info.save_area_base;
		save_end  = prefix + dump.arch_info.save_area_base + dump.arch_info.save_area_size;

		if(mem_end < save_start)
			continue;
		if(mem_start > save_end)
			continue;
		new_start= MAX(mem_start,save_start);
		new_end  = MIN(mem_end,save_end);
		if(copy_to_user((buf + (new_start - mem_start)),
			((char*)&(dump.arch_info.cpu_info[i])) +
			(new_start - save_start),(new_end-new_start)))
			return -EFAULT;
	}
	return 0;
}

/*
 * Read routine for zcore character device
 * First 4K are dump header
 * Next 32MB are HSA Memory
 * Rest is read from absolute Memory
 */
#define MEM_LOC(p) ((p)-KL_DUMP_HEADER_SZ_S390)
static ssize_t 
zcore_read(struct file * file, char * buf, size_t count, loff_t *ppos)
{
	unsigned long start_pos = *ppos;
	unsigned long end;
	size_t rel_pos = 0;

	if (start_pos >= (dump.mem_size + KL_DUMP_HEADER_SZ_S390)){
		goto out;
	}

	if(start_pos < KL_DUMP_HEADER_SZ_S390){
		/* Copy dump header */
		DUMP_PRINT_TRACE("zcore_read: (header) : %li\n",start_pos);
		end = MIN(count, KL_DUMP_HEADER_SZ_S390-start_pos);
		if(copy_to_user(buf,&zcore_dump_header + start_pos, end))
			return -EFAULT;
		rel_pos+=end;
	}
	if((count - rel_pos) == 0)
		goto out;

	if (MEM_LOC(start_pos+rel_pos) < dump.sa_mem_size){
		DUMP_PRINT_TRACE("zcore_read: (sdias) : %li\n",(start_pos +
								rel_pos));
		end = rel_pos + MIN((count-rel_pos), (dump.sa_mem_size -
						MEM_LOC(start_pos + rel_pos)));
		for(;rel_pos < end;){
			int size = MIN(sizeof(dump.buffer), (end-rel_pos));
			int rc;
			rc = sdias_memcpy(dump.buffer,
				(void*)MEM_LOC(start_pos + rel_pos) , size);
			if(rc<0){
				DUMP_PRINT_ERR("sdias_memcpy failed\n");
				return -EIO;
			}
			if (copy_to_user(buf + rel_pos, dump.buffer, size))
				return -EFAULT;
			rel_pos += size;
		}
	}

	if((count - rel_pos) == 0)
		goto out;

	/* Copy from real mem */
	if (MEM_LOC(start_pos+rel_pos) < dump.mem_size){
		DUMP_PRINT_TRACE("zcore_read: (real mem) : %li\n",
			(start_pos + rel_pos));
		end = rel_pos + MIN((count-rel_pos), (dump.mem_size -
			MEM_LOC(start_pos + rel_pos)));
		for(;rel_pos < end;){
			int size = MIN(sizeof(dump.buffer), (end-rel_pos));
			memcpy_absolute(dump.buffer, (void*)MEM_LOC(start_pos +
							rel_pos) , size);
			if (copy_to_user(buf + rel_pos, dump.buffer, size))
				return -EFAULT;
			rel_pos += size;
		}
	}
out:
	if(*ppos > 4096) {
		if( zcore_add_lowcores_to_output(buf, rel_pos,MEM_LOC(*ppos))
			== -EFAULT){
			return -EFAULT;
		}
	}
	*ppos += rel_pos;
	return rel_pos;
}

/*
 * zcore_open()
 */
static int 
zcore_open(struct inode * inode, struct file * filp)
{
	return capable(CAP_SYS_RAWIO) ? 0 : -EPERM;
}

/*
 * zcore_lseek()
 */
static loff_t zcore_lseek(struct file * file, loff_t offset, int orig)
{
	switch (orig) {
		case 0:
			file->f_pos = offset;
			return file->f_pos;
		case 1:
			file->f_pos += offset;
			return file->f_pos;
		default:
			return -EINVAL;
	}
}

/*
 * zcore_ioctl()
 */
static int
zcore_ioctl(struct inode *inode, struct file *filp,
	    unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	int hsa_size = (int)dump.sa_mem_size;
	DUMP_PRINT_TRACE("ioctl: %x\n",cmd);
	switch(cmd){
		case ZCORE_IOCTL_HSASIZE:
			DUMP_PRINT_TRACE("hsa size: %i\n",hsa_size);
			if(copy_to_user ((char *) arg, &hsa_size, sizeof(int))){
				DUMP_PRINT_ERR("ZCORE_IOCTL_HSASIZE: failed\n");
				rc = -EFAULT;
			}
			break;
		case ZCORE_IOCTL_GET_PARMS:
			if(copy_to_user ((char *) arg, fcp_data,
				sizeof(*fcp_data))){
				DUMP_PRINT_ERR("ZCORE_IOCTL_GETXML: faled\n");
				rc = -EFAULT;
			}
			break;
		case ZCORE_IOCTL_RELEASE_HSA:
		{
			uint32_t* psw;

			/* set disabled wait psw in lowcore */
			smp_ctl_clear_bit(0,28); /* disable lc protection */
			psw=(uint32_t*)0x0;
			*psw=0x000a0000;
			psw=(uint32_t*)0x4;
			*psw=0x00004711;
			smp_ctl_set_bit(0,28); /* enable lc protection again */

			DUMP_PRINT_TRACE("release HSA\n");
#ifdef CONFIG_DUMP_TEST
			asm volatile ( "lpsw 0 \n"
			       :
			       :
			       : "cc"
			);
#else
			diag308(DIAG308_REL_HSA, NULL);
#endif
			break;
		}
		case ZCORE_IOCTL_GET_ERROR:
			if(copy_to_user ((char *) arg, &dump.error_code,
				sizeof(dump.error_code))){
				DUMP_PRINT_ERR("ZCORE_IOCTL_GET_ERROR: "
						"failed\n");
				rc = -EFAULT;
			}
			break;
		case ZCORE_IOCTL_REIPL:
			if (ipl_block) {
				diag308(DIAG308_SET, ipl_block);
				diag308(DIAG308_IPL, NULL);
			}
			break;
		default:
			DUMP_PRINT_ERR("invalid zcore ioctl: %x\n",cmd);
			rc = -EINVAL;
	}
	return rc;
}

/************************************************************************/
/* Write Event Data                                                     */
/************************************************************************/

#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <asm/semaphore.h>
#include <asm/ebcdic.h>
#include "sclp.h"
#include "sclp_rw.h"

#define SDIAS_RETRIES	    300
#define SDIAS_SLEEP_TICKS   50

#define SDIAS_OP_CODE 0x1c
#define SDIAS_MASK    0x00000010
#define EQ_STORE_DATA 0x0
#define EQ_SIZE       0x1
#define DI_FCP_DUMP   0x0
#define ASA_SIZE_32   0x0
#define ASA_SIZE_64   0x1
#define EVSTATE_ALL_STORED   0x0  
#define EVSTATE_NO_DATA      0x3
#define EVSTATE_PART_STORED  0x10


struct sclp_register sclp_sdias_register = {
	.send_mask = SDIAS_MASK,
};

struct sdias_evbuf {
	struct  evbuf_header header;
	u8	event_qualifier;
	u8	data_identifier;
	u64	reserved2;
	u32	event_identifier;
	u16	reserved3;
	u8	asa_size;
	u8	event_status;
	u32	reserved4;
	u32	data_block_count;
	u64	asa;
	u32	reserved5;
	u32	fbn;
	u32	reserved6;
	u32	lbn;
	u16	reserved7;
	u16	dbs;
} __attribute__((packed));

struct sdias_sccb {
	struct sccb_header  header;
	struct sdias_evbuf  sdias_evbuf;
} __attribute__((packed));

/* Globals */
 
static int sclp_req_done = 0;
static wait_queue_head_t sdias_wq;

/***************************************************************************/
/* Functions                                                               */
/***************************************************************************/
 
/*
 * SCLP callback
 */
 
static void 
sdias_callback(struct sclp_req* request, void* data)
{
	struct sdias_sccb* sccb;
	sccb = (struct sdias_sccb*) request->sccb;
	debug_text_event(dump.dbf,1,"callb1");

	sclp_req_done = 1;
	wake_up(&sdias_wq); /* Inform caller, that request is complete */
	debug_text_event(dump.dbf,1,"callb2");
}


/*
 *  SCLP send command
 */
 
static int 
dump_sclp_send(struct sclp_req *req)
{
	int retries;
	int rc;
	debug_text_event(dump.dbf,1,"sclp1");
	for (retries = SDIAS_RETRIES; retries; retries--) {
		sclp_req_done = 0;
		rc = sclp_add_request(req);
		debug_text_event(dump.dbf,1,"sclp2");
		if (rc) {
			/* not initiated, wait some time and retry */
			set_current_state(TASK_INTERRUPTIBLE);
			DUMP_PRINT_TRACE("SCLP call failed (rc = %i). "
					"waiting..\n",rc);
			debug_text_event(dump.dbf,1,"FAIL");
			schedule_timeout(SDIAS_SLEEP_TICKS);
			debug_text_event(dump.dbf,1,"WAIT");
		} else	{
			debug_text_event(dump.dbf,1,"sclp3");
			/* initiated, wait for completion of service call */
			wait_event(sdias_wq,(sclp_req_done == 1));
			debug_text_event(dump.dbf,1,"sclp4");
			if (req->status == SCLP_REQ_FAILED){
				DUMP_PRINT_TRACE("SCLP call failed\n");
				continue;	
			} 
			goto out;
		} 
	}
out:
	return rc;
}

/*
 * The init routine: Call before useing hsa_sdias_copy_blocks()
 */

static int
sdias_init(void)
{
	
	if(sclp_register(&sclp_sdias_register) != 0)
		return -1;
	debug_text_event(dump.dbf,1,"sdinit1");
	init_waitqueue_head(&sdias_wq);
	debug_text_event(dump.dbf,1,"sdinit2");
	return 0;
}

/*
 * Get number of blocks (4K) available in the HSA
 * - not reentrant! -
 */

static struct sdias_sccb sccb __attribute__((__aligned__(4096)));
static int 
sdias_get_nr_blocks(void)
{
	struct sclp_req sdias_request = {};
	int rc;

	debug_text_event(dump.dbf,1,"getblk1");
	memset(&sccb,0,sizeof(sccb));
 
	sccb.header.length = sizeof(sccb);
 
	sccb.sdias_evbuf.header.length	  = sizeof(struct sdias_evbuf);
	sccb.sdias_evbuf.header.type	  = SDIAS_OP_CODE;
	sccb.sdias_evbuf.event_qualifier  = EQ_SIZE;
	sccb.sdias_evbuf.data_identifier  = DI_FCP_DUMP;
	sccb.sdias_evbuf.event_identifier = 4712; 
	sccb.sdias_evbuf.dbs		  = 1;
 
	sdias_request.sccb     = &sccb;
	sdias_request.command  = SCLP_CMDW_WRITEDATA;
	sdias_request.status   = SCLP_REQ_FILLED;
	sdias_request.callback = sdias_callback;
	debug_text_event(dump.dbf,1,"getblk2");	 
	if(dump_sclp_send(&sdias_request) != 0){
		debug_text_event(dump.dbf,1,"getblk3");
		DUMP_PRINT_ERR("sclp_send failed for get_nr_blocks\n");
		rc = -1;
		goto out;
	}
	if(sccb.header.response_code != 0x0020) {
		DUMP_PRINT_TRACE("SCLP get nr of blocks request failed "
				"(rc = %x)! waiting..\n",
				(sccb.header.response_code));
		rc = -1;
		goto out;
	}

	debug_text_event(dump.dbf,1,"getblk4");
	switch(sccb.sdias_evbuf.event_status){
		case 0:
			rc = sccb.sdias_evbuf.data_block_count;
			goto out;
		default:
			DUMP_PRINT_ERR("Error from SCLP. Event status = %x\n",
				sccb.sdias_evbuf.event_status);
			rc = -1;
			goto out;
	}
out:
	debug_text_event(dump.dbf,1,"getblk5");
	DUMP_PRINT_TRACE("got: %i blocks\n",rc);
#ifdef CONFIG_DUMP_TEST
	return (32*1024 + 1); /* 32 MB + 1K */
#endif
	return rc;
}

/*
 * Copy from HSA to absolute storage: not reentrant!!
 * -target   : Address of buffer where data should be copied
 * -start_blk: Start Block (beginning with 1)
 * -nr_blks  : Number of 4K blocks to copy
 *
 * Return Value: - 0: requested 'number' of blocks of data copied
 *		 - < 0: ERROR - negative event status
 */

static struct sdias_sccb sccb_cb __attribute__((__aligned__(4096)));
static int 
sdias_copy_blocks(void* target,int start_blk, int nr_blks)
{
#ifdef CONFIG_DUMP_TEST
	memcpy_absolute(target, (start_blk-2)*4096, nr_blks * 4096);
	return 0;
#else
	struct sclp_req sdias_request = {};
	int rc = 0;

	memset(&sccb_cb,0,sizeof(sccb_cb));

	sccb_cb.header.length  = sizeof(sccb_cb);

	sccb_cb.sdias_evbuf.header.length	= sizeof(struct sdias_evbuf);
	sccb_cb.sdias_evbuf.header.type		= SDIAS_OP_CODE;
	sccb_cb.sdias_evbuf.header.flags 	= 0;
	sccb_cb.sdias_evbuf.event_qualifier	= EQ_STORE_DATA;
	sccb_cb.sdias_evbuf.data_identifier	= DI_FCP_DUMP;
	sccb_cb.sdias_evbuf.event_identifier	= 4712;
#ifdef __s390x__
	sccb_cb.sdias_evbuf.asa_size		= ASA_SIZE_64;
#else
	sccb_cb.sdias_evbuf.asa_size		= ASA_SIZE_32;
#endif
	sccb_cb.sdias_evbuf.event_status	= 0;
	sccb_cb.sdias_evbuf.data_block_count	= nr_blks;
	sccb_cb.sdias_evbuf.asa			= (u64)target;
	sccb_cb.sdias_evbuf.fbn			= start_blk;
	sccb_cb.sdias_evbuf.lbn			= 0;
	sccb_cb.sdias_evbuf.dbs			= 1;

	sdias_request.sccb     = &sccb_cb;
	sdias_request.command  = SCLP_CMDW_WRITEDATA;
	sdias_request.status   = SCLP_REQ_FILLED;
	sdias_request.callback = sdias_callback;

	if(dump_sclp_send(&sdias_request) != 0){
		DUMP_PRINT_ERR("sclp_send failed\n");
		rc = -1;
		goto out;
	}
	if(sccb_cb.header.response_code != 0x0020) {
		DUMP_PRINT_TRACE("SCLP copy blocks request failed (rc = %x)! "
				"waiting..\n", (sccb_cb.header.response_code));
		rc = -1;
		goto out;
	}

	switch(sccb_cb.sdias_evbuf.event_status){
		case EVSTATE_ALL_STORED:
			DUMP_PRINT_TRACE("EVSTATE_ALL_STORED:\n");
			debug_text_event(dump.dbf, 1, "st:all");
		case EVSTATE_PART_STORED:
			DUMP_PRINT_TRACE("EVSTATE_PART_STORED: %i\n",
				sccb_cb.sdias_evbuf.data_block_count);
			debug_text_event(dump.dbf, 1, "st:part");
			break;
		case EVSTATE_NO_DATA:
			DUMP_PRINT_TRACE("EVSTATE_NO_DATA\n");
		default:
			DUMP_PRINT_ERR("Error from SCLP while copying hsa. "
				"Event status = %x\n",
				sccb_cb.sdias_evbuf.event_status);
			rc = -1;
			goto out;
	}
out:
	return rc;
#endif
}

/*
 * sdias_memcpy()
 */
static char sdias_memcpy_buf[4096] __attribute__((__aligned__(4096)));
static int sdias_memcpy(char* target, char* src, int count)
{
	int rel_pos = 0;
	int start_blk;
	int rc = 0;
	unsigned long src_ul = (unsigned long)src;

	/* copy first block */
	if((src_ul % 4096) != 0){
		start_blk = src_ul / 4096 + 2; 
		if((rc = sdias_copy_blocks(sdias_memcpy_buf, start_blk,1)) < 0 ){
			DUMP_PRINT_ERR("sdias_copy_blocks() failed: rc = %i\n",
					rc);
			goto out;
		}
		rel_pos = MIN((4096 - (src_ul % 4096)), count);
		memcpy(target, sdias_memcpy_buf + (src_ul % 4096), rel_pos);
		if(rel_pos == count)
			goto out;
	}
	/* copy middle */
	for(;(rel_pos+4096) <= count; rel_pos += 4096){
		start_blk = (src_ul + rel_pos) / 4096 + 2;
		if((rc = sdias_copy_blocks(sdias_memcpy_buf, start_blk,1)) < 0 ){
			DUMP_PRINT_ERR("sdias_copy_blocks() failed: rc = %i\n",
					rc);
			goto out;
		}
		memcpy(target + rel_pos, sdias_memcpy_buf, 4096);
	}
	if(rel_pos == count)
		goto out;

	/* copy last block */
	start_blk = (src_ul + rel_pos) / 4096 + 2;
	if((rc = sdias_copy_blocks(sdias_memcpy_buf, start_blk,1)) < 0 ){
		DUMP_PRINT_ERR("sdias_copy_blocks() failed: rc = %i\n",rc);
		goto out;
	}
	memcpy(target + rel_pos, sdias_memcpy_buf, count - rel_pos);
out:
	return rc;
}

late_initcall(dump_init);
module_exit(dump_exit);
