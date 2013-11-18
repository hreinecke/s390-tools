/*
 * hyptop - Show hypervisor performance data on System z
 *
 * Provide CPU Items
 *
 * Copyright IBM Corp. 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include "sd.h"

/*
 * Return CPU type of "cpu"
 */
static char *l_cpu_type(struct sd_cpu_item *item, struct sd_cpu *cpu)
{
	(void) item;
	return sd_cpu_type_str(cpu);
}

/*
 * Return CPU state of "cpu"
 */
static char *l_cpu_state(struct sd_cpu_item *item, struct sd_cpu *cpu)
{
	(void) item;
	return sd_cpu_state_str(sd_cpu_state(cpu));
}

/*
 * value = (value_current - value_prev) / online_time_diff
 */
static double l_cpu_diff(struct sd_cpu_item *item, struct sd_cpu *cpu, int sign)
{
	u64 online_time_diff_us;
	double factor, diff_us;

	if (sd_cpu_state(cpu) == SD_CPU_STATE_STOPPED)
		return 0;
	if (!cpu->d_prev || !cpu->d_cur)
		return 0;
	if (!sd_cpu_type_selected(cpu->type))
		return 0;
	online_time_diff_us = l_sub_64(cpu->d_cur->online_time_us,
				     cpu->d_prev->online_time_us);
	if (online_time_diff_us == 0)
		return 0;

	factor = ((double) online_time_diff_us) / 1000000;
	if (sign)
		diff_us = l_cpu_info_s64(cpu->d_cur, item->offset) -
			  l_cpu_info_s64(cpu->d_prev, item->offset);
	else
		diff_us = l_sub_64(l_cpu_info_u64(cpu->d_cur, item->offset),
				   l_cpu_info_u64(cpu->d_prev, item->offset));
	diff_us /= factor;
	return diff_us;
}

/*
 * unsigned value = (value_current - value_prev) / online_time_diff
 */
static u64 l_cpu_diff_u64(struct sd_cpu_item *item, struct sd_cpu *cpu)
{
	return l_cpu_diff(item, cpu, 0);
}

/*
 * signed value = (value_current - value_prev) / online_time_diff
 */
static s64 l_cpu_diff_s64(struct sd_cpu_item *item, struct sd_cpu *cpu)
{
	return l_cpu_diff(item, cpu, 1);
}

/*
 * Return cpu item value
 */
static u64 l_cpu_item_64(struct sd_cpu_item *item, struct sd_cpu *cpu)
{
	if (sd_cpu_state(cpu) == SD_CPU_STATE_STOPPED)
		return 0;
	if (!cpu->d_cur)
		return 0;
	if (!sd_cpu_type_selected(cpu->type))
		return 0;
	return l_cpu_info_u64(cpu->d_cur, item->offset);
}

/*
 * CPU item definitions
 */
struct sd_cpu_item sd_cpu_item_type = {
	.table_col	= TABLE_COL_STR('p', "type"),
	.type		= SD_TYPE_STR,
	.desc		= "CPU type",
	.fn_str		= l_cpu_type,
};

struct sd_cpu_item sd_cpu_item_state = {
	.table_col	= TABLE_COL_STR('a', "stat"),
	.type		= SD_TYPE_STR,
	.desc		= "CPU state",
	.fn_str		= l_cpu_state,
};

struct sd_cpu_item sd_cpu_item_cpu_diff = {
	.table_col = TABLE_COL_TIME_DIFF_SUM(table_col_unit_perc, 'c', "cpu"),
	.type	= SD_TYPE_U64,
	.offset = SD_CPU_INFO_OFFSET(cpu_time_us),
	.desc	= "CPU time per second",
	.fn_u64	= l_cpu_diff_u64,
};

struct sd_cpu_item sd_cpu_item_mgm_diff = {
	.table_col = TABLE_COL_TIME_DIFF_SUM(table_col_unit_perc, 'm', "mgm"),
	.type	= SD_TYPE_U64,
	.offset = SD_CPU_INFO_OFFSET(mgm_time_us),
	.desc	= "Management time per second",
	.fn_u64	= l_cpu_diff_u64,
};

struct sd_cpu_item sd_cpu_item_wait_diff = {
	.table_col = TABLE_COL_TIME_DIFF_SUM(table_col_unit_perc, 'w', "wait"),
	.type	= SD_TYPE_U64,
	.offset = SD_CPU_INFO_OFFSET(wait_time_us),
	.desc	= "Wait time per second",
	.fn_u64	= l_cpu_diff_u64,
};

struct sd_cpu_item sd_cpu_item_steal_diff = {
	.table_col = TABLE_COL_STIME_DIFF_SUM(table_col_unit_perc, 's',
					      "steal"),
	.type	= SD_TYPE_S64,
	.offset = SD_CPU_INFO_OFFSET(steal_time_us),
	.desc	= "Steal time per second",
	.fn_s64	= l_cpu_diff_s64,
};

struct sd_cpu_item sd_cpu_item_cpu = {
	.table_col = TABLE_COL_TIME_SUM(table_col_unit_hm, 'C', "cpu+"),
	.type	= SD_TYPE_U64,
	.offset = SD_CPU_INFO_OFFSET(cpu_time_us),
	.desc	= "Total CPU time",
	.fn_u64	= l_cpu_item_64,
};

struct sd_cpu_item sd_cpu_item_mgm = {
	.table_col = TABLE_COL_TIME_SUM(table_col_unit_hm, 'M', "mgm+"),
	.type	= SD_TYPE_U64,
	.offset = SD_CPU_INFO_OFFSET(mgm_time_us),
	.desc	= "Total management time",
	.fn_u64	= l_cpu_item_64,
};

struct sd_cpu_item sd_cpu_item_wait = {
	.table_col = TABLE_COL_TIME_SUM(table_col_unit_hm, 'W', "wait+"),
	.type	= SD_TYPE_U64,
	.offset = SD_CPU_INFO_OFFSET(wait_time_us),
	.desc	= "Total wait time",
	.fn_u64	= l_cpu_item_64,
};

struct sd_cpu_item sd_cpu_item_steal = {
	.table_col = TABLE_COL_STIME_SUM(table_col_unit_hm, 'S', "steal+"),
	.type	= SD_TYPE_U64,
	.offset = SD_CPU_INFO_OFFSET(steal_time_us),
	.desc	= "Total steal time",
	.fn_u64	= l_cpu_item_64,
};

struct sd_cpu_item sd_cpu_item_online = {
	.table_col = TABLE_COL_TIME_MAX(table_col_unit_dhm, 'o', "online"),
	.type	= SD_TYPE_U64,
	.offset = SD_CPU_INFO_OFFSET(online_time_us),
	.desc	= "Online time",
	.fn_u64	= l_cpu_item_64,
};
