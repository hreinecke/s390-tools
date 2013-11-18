/*
 * hyptop - Show hypervisor performance data on System z
 *
 * Hyptop z/VM data gatherer that operates on debugfs
 *
 * Copyright IBM Corp. 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "hyptop.h"
#include "sd.h"
#include "helper.h"
#include "dg_debugfs.h"

#define VM_CPU_TYPE	"UN"
#define VM_CPU_ID	"ALL"
#define NAME_LEN	8
#define DEBUGFS_FILE	"diag_2fc"

static u64 l_update_time_us;
static long l_2fc_buf_size;

/*
 * Diag 2fc data structure definition
 */
struct l_diag2fc_data {
	u32	version;
	u32	flags;
	u64	used_cpu;
	u64	el_time;
	u64	mem_min_kb;
	u64	mem_max_kb;
	u64	mem_share_kb;
	u64	mem_used_kb;
	u32	pcpus;
	u32	lcpus;
	u32	vcpus;
	u32	cpu_min;
	u32	cpu_max;
	u32	cpu_shares;
	u32	cpu_use_samp;
	u32	cpu_delay_samp;
	u32	page_wait_samp;
	u32	idle_samp;
	u32	other_samp;
	u32	total_samp;
	char	guest_name[NAME_LEN];
};

/*
 * Header for debugfs file "diag_2fc"
 */
struct l_debugfs_d2fc_hdr {
	u64	len;
	u16	version;
	char	tod_ext[16];
	u64	count;
	char	reserved[30];
} __attribute__ ((packed));

struct l_debugfs_d2fc {
	struct l_debugfs_d2fc_hdr	h;
	char			diag2fc_buf[];
} __attribute__ ((packed));

/*
 * Fill "guest" with data
 */
static void l_sd_sys_fill(struct sd_sys *guest, struct l_diag2fc_data *data)
{
	struct sd_cpu *cpu;

	cpu = sd_cpu_get(guest, VM_CPU_ID);
	if (!cpu)
		cpu = sd_cpu_new(guest, VM_CPU_ID, SD_CPU_TYPE_STR_UN,
				 data->vcpus);

	sd_cpu_cnt(cpu) = data->vcpus;
	sd_cpu_cpu_time_us_set(cpu, data->used_cpu);
	sd_cpu_online_time_us_set(cpu, data->el_time);

	sd_sys_weight_cur_set(guest, data->cpu_shares);
	sd_sys_weight_min_set(guest, data->cpu_min);
	sd_sys_weight_max_set(guest, data->cpu_max);

	sd_sys_mem_min_kib_set(guest, data->mem_min_kb);
	sd_sys_mem_max_kib_set(guest, data->mem_max_kb);
	sd_sys_mem_use_kib_set(guest, data->mem_used_kb);

	sd_sys_update_time_us_set(guest, l_update_time_us);
	sd_sys_commit(guest);
}

/*
 * Read debugfs file
 */
static void l_read_debugfs(struct l_debugfs_d2fc_hdr **hdr,
			   struct l_diag2fc_data **data)
{
	long real_buf_size;
	ssize_t rc;
	void *buf;
	int fh;

	do {
		fh = dg_debugfs_open(DEBUGFS_FILE);
		*hdr = buf = ht_alloc(l_2fc_buf_size);
		rc = read(fh, buf, l_2fc_buf_size);
		if (rc == -1)
			ERR_EXIT_ERRNO("Reading hypervisor data failed");
		close(fh);
		real_buf_size = (*hdr)->len + sizeof(struct l_debugfs_d2fc_hdr);
		if (rc == real_buf_size)
			break;
		l_2fc_buf_size = real_buf_size;
		ht_free(buf);
	} while (1);
	*data = buf + sizeof(struct l_debugfs_d2fc_hdr);
}

/*
 * Fill System Data
 */
static void l_sd_sys_root_fill(struct sd_sys *sys)
{
	struct l_diag2fc_data *d2fc_data;
	struct l_debugfs_d2fc_hdr *hdr;
	struct sd_cpu *cpu;
	unsigned int i;

	do {
		l_read_debugfs(&hdr, &d2fc_data);
		if (l_update_time_us != ht_ext_tod_2_us(&hdr->tod_ext)) {
			l_update_time_us = ht_ext_tod_2_us(&hdr->tod_ext);
			break;
		}
		/*
		 * Got old snapshot from kernel. Wait some time until
		 * new snapshot is available.
		 */
		ht_free(hdr);
		usleep(DBFS_WAIT_TIME_US);
	} while (1);

	cpu = sd_cpu_get(sys, VM_CPU_ID);
	if (!cpu)
		cpu = sd_cpu_new(sys, VM_CPU_ID, SD_CPU_TYPE_STR_UN,
				 d2fc_data[0].lcpus);

	for (i = 0; i < hdr->count; i++) {
		struct l_diag2fc_data *data = &d2fc_data[i];
		char guest_name[NAME_LEN + 1];
		struct sd_sys *guest;

		guest_name[NAME_LEN] = 0;
		memcpy(guest_name, data->guest_name, NAME_LEN);
		ht_ebcdic_to_ascii(guest_name, NAME_LEN);
		ht_strstrip(guest_name);

		guest = sd_sys_get(sys, guest_name);
		if (!guest)
			guest = sd_sys_new(sys, guest_name);
		l_sd_sys_fill(guest, data);
	}
	ht_free(hdr);
	sd_sys_commit(sys);
}

/*
 * Update system data
 */
static void l_sd_update(void)
{
	struct sd_sys *root = sd_sys_root_get();

	sd_sys_update_start(root);
	l_sd_sys_root_fill(root);
	sd_sys_update_end(root, l_update_time_us);
}

/*
 * Supported system items
 */
static struct sd_sys_item *l_sys_item_vec[] = {
	&sd_sys_item_cpu_cnt,
	&sd_sys_item_cpu_diff,
	&sd_sys_item_cpu,
	&sd_sys_item_online,
	&sd_sys_item_mem_use,
	&sd_sys_item_mem_max,
	&sd_sys_item_weight_min,
	&sd_sys_item_weight_cur,
	&sd_sys_item_weight_max,
	NULL,
};

/*
 * Default system items
 */
static struct sd_sys_item *l_sys_item_enable_vec[] = {
	&sd_sys_item_cpu_cnt,
	&sd_sys_item_cpu_diff,
	&sd_sys_item_cpu,
	&sd_sys_item_online,
	&sd_sys_item_mem_max,
	&sd_sys_item_mem_use,
	&sd_sys_item_weight_cur,
	NULL,
};

/*
 * Supported CPU items
 */
static struct sd_cpu_item *l_cpu_item_vec[] = {
	&sd_cpu_item_cpu_diff,
	&sd_cpu_item_cpu,
	&sd_cpu_item_online,
	NULL,
};

/*
 * Default CPU items
 */
static struct sd_cpu_item *l_cpu_item_enable_vec[] = {
	&sd_cpu_item_cpu_diff,
	NULL,
};

/*
 * Supported CPU types
 */
static struct sd_cpu_type *l_cpu_type_vec[] = {
	&sd_cpu_type_un,
	NULL,
};

/*
 * Define data gatherer structure
 */
static struct sd_dg dg_debugfs_vm_dg = {
	.update_sys		= l_sd_update,
	.cpu_type_vec		= l_cpu_type_vec,
	.sys_item_vec		= l_sys_item_vec,
	.sys_item_enable_vec	= l_sys_item_enable_vec,
	.cpu_item_vec		= l_cpu_item_vec,
	.cpu_item_enable_vec	= l_cpu_item_enable_vec,
};

/*
 * Initialize z/VM debugfs data gatherer
 */
int dg_debugfs_vm_init(void)
{
	int fh;

	fh = dg_debugfs_open(DEBUGFS_FILE);
	if (fh < 0)
		return fh;
	else
		close(fh);
	l_2fc_buf_size = sizeof(struct l_debugfs_d2fc_hdr);
	sd_dg_register(&dg_debugfs_vm_dg);
	return 0;
}
