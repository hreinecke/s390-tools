/*
 * hyptop - Show hypervisor performance data on System z
 *
 * Command line parsing
 *
 * Copyright IBM Corp. 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <stdio.h>
#include <ctype.h>
#include "zt_common.h"
#include "helper.h"
#include "hyptop.h"
#include "getopt.h"
#include "opts.h"
#include "sd.h"

static const char l_copyright_str[] = "Copyright IBM Corp. 2010";

/*
 * Help text for tool
 */
static char HELP_TEXT[] =
"Usage: hyptop [OPTIONS]\n"
"\n"
"Show hypervisor performance data on System z.\n"
"\n"
"-h, --help                      Print this help, then exit\n"
"-v, --version                   Print version information, then exit\n"
"-w, --window WIN_NAME           Current window (\"sys\" or \"sys_list\")\n"
"-s, --sys SYSTEM[,..]           Systems for current window\n"
"-f, --fields LETTER[:UNIT][,..] Fields and units for current window\n"
"-S, --sort LETTER               Sort field for current window\n"
"-t, --cpu_types TYPE[,..]       CPU types used for time calculations\n"
"-b, --batch_mode                Use batch mode (no curses)\n"
"-d, --delay SECONDS             Delay time between screen updates\n"
"-n, --iterations NUMBER         Number of iterations before ending\n";

/*
 * Initialize default settings
 */
static void l_init_defaults(void)
{
	g.prog_name = PROG_NAME;
	g.o.delay_s = HYPTOP_OPT_DEFAULT_DELAY;
	g.w.cur = &win_sys_list;
	g.o.cur_win = &win_sys_list;
}

/*
 * Print "help" hint
 */
static void l_std_usage_exit(void)
{
	fprintf(stderr, "Try '%s --help' for more information.\n",
		g.prog_name);
	hyptop_exit(1);
}

/*
 * Print help text
 */
static void l_usage(void)
{
	printf("%s", HELP_TEXT);
}

/*
 * Print version information
 */
static void l_print_version(void)
{
	printf("%s: Hypervisor Top version %s\n", g.prog_name, RELEASE_STRING);
	printf("%s\n", l_copyright_str);
}

/*
 * Check if string is a number
 */
static int l_number_check(const char *str)
{
	const char *ptr = str;
	while (*ptr) {
		if (!isdigit(*ptr))
			ERR_EXIT("The argument \"%s\" is not an integer\n",
				 str);
		ptr++;
	}
	return 1;
}

/*
 * Set delay option
 */
static void l_delay_set(char *delay_string)
{
	int secs;

	l_number_check(delay_string);
	if (sscanf(delay_string, "%i", &secs) != 1)
		ERR_EXIT("The delay value \"%s\" is invalid\n", delay_string);
	g.o.delay_s = secs;
	g.o.delay_us = 0;
}

/*
 * Get number of occurrences of character 'c' in "str"
 */
static int l_get_char_cnt(char *str, char c)
{
	unsigned int i;
	int cnt = 0;

	for (i = 0; str[i] != 0; i++) {
		if (str[i] == c)
			cnt++;
	}
	return cnt;
}

/*
 * Return copy of string with removed trailing and leading blanks
 */
static char *l_trim_str_new(char *str)
{
	char *rc;
	int i;

	for (i = 0; *(str + i) == ' '; i++) {}
	rc = ht_strdup(str + i);
	ht_strstrip(rc);
	if (strlen(rc) == 0)
		ERR_EXIT("The argument \"%s\" is invalid\n", str);
	return rc;
}

/*
 * Get column specification for string
 */
static struct table_col_spec *l_get_col_spec(char *str)
{
	struct table_col_spec *col_spec;
	unsigned int i;
	char *key_str;

	col_spec = ht_zalloc(sizeof(*col_spec));

	for (i = strlen(str); i > 0; i--) {
		if (str[i] == ':') {
			col_spec->unit_str = l_trim_str_new(&str[i + 1]);
			str[i] = 0;
		}
	}
	key_str = l_trim_str_new(str);
	if (strlen(key_str) > 1)
		ERR_EXIT("The field key \"%s\" is invalid\n", key_str);
	col_spec->hotkey = key_str[0];
	ht_free(key_str);
	return col_spec;
}

/*
 * Set the "--fields" option
 */
static void l_fields_set(char *str)
{
	struct hyptop_col_vec_opt *opt = &g.o.cur_win->opts.fields;
	unsigned int i, j;

	opt->cnt = l_get_char_cnt(str, ',') + 1;
	opt->vec = ht_zalloc(sizeof(void *) * (opt->cnt + 1));

	j = 0;
	for (i = strlen(str); i > 0; i--) {
		if (str[i] != ',')
			continue;
		opt->vec[j] = l_get_col_spec(&str[i + 1]);
		str[i] = 0;
		j++;
	}
	opt->vec[j] = l_get_col_spec(str);
	opt->specified = 1;
}

/*
 * Set the "--sort_field" option
 */
static void l_sort_field_set(char *str)
{
	if (strlen(str) > 1)
		ERR_EXIT("The sort field \"%s\" is invalid\n", str);
	if (g.o.cur_win->opts.sort_field_specified &&
	    g.o.cur_win->opts.sort_field != str[0])
			g.o.cur_win->opts.sort_field_specified = 0;
	g.o.cur_win->opts.sort_field_specified++;
	g.o.cur_win->opts.sort_field = str[0];
}

/*
 * Setup a string vector out of a comma separated list in "str"
 */
static void l_str_vec_set(char *str, struct hyptop_str_vec_opt *opt)
{
	unsigned int i, j;

	opt->cnt = l_get_char_cnt(str, ',') + 1;
	opt->vec = ht_zalloc(sizeof(void *) * (opt->cnt + 1));

	j = 0;
	for (i = strlen(str); i > 0; i--) {
		if (str[i] != ',')
			continue;
		opt->vec[j] = l_trim_str_new(&str[i + 1]);
		str[i] = 0;
		j++;
	}
	opt->vec[j] = l_trim_str_new(str);
	opt->specified = 1;
}

/*
 * Set the "--sys" option
 */
static void l_sys_set(char *str)
{
	l_str_vec_set(str, &g.o.cur_win->opts.sys);
}

/*
 * Set the "--cpu_types" option
 */
static void l_cpu_types_set(char *str)
{
	l_str_vec_set(str, &g.o.cpu_types);
}

/*
 * Set the "--window" option
 */
static void l_window_set(const char *str)
{
	g.o.win_specified = 1;
	if (strcmp(str, win_sys_list.id) == 0)
		g.o.cur_win = &win_sys_list;
	else if (strcmp(str, win_sys.id) == 0)
		g.o.cur_win = &win_sys;
	else
		ERR_EXIT("The window \"%s\" is unknown\n", str);
}

/*
 * Set the "--iterations" option
 */
static void l_iterations_set(const char *str)
{
	l_number_check(str);
	g.o.iterations_specified = 1;
	g.o.iterations = atoi(str);
}

/*
 * Set the "--batch_mode" option
 */
static void l_batch_mode_set(void)
{
	g.o.batch_mode_specified = 1;
}

/*
 * Make option consisteny checks at end of command line parsing
 */
static void l_parse_finish(void)
{
	if (g.o.iterations_specified && g.o.iterations == 0)
		hyptop_exit(0);
	if (g.o.cur_win != &win_sys)
		return;
	if (!win_sys.opts.sys.specified)
		ERR_EXIT("Specify a system for window \"sys\"\n");
	if (win_sys.opts.sys.cnt != 1)
		ERR_EXIT("More than one system for window \"sys\" has been "
			 "specified\n");
	win_switch(&win_sys);
}

/*
 * Main command line parsing function
 */
void opts_parse(int argc, char *argv[])
{
	int opt, index;
	static struct option long_options[] = {
		{ "version",     no_argument,       NULL, 'v'},
		{ "help",        no_argument,       NULL, 'h'},
		{ "batch_mode",  no_argument,       NULL, 'b'},
		{ "delay",       required_argument, NULL, 'd'},
		{ "window",      required_argument, NULL, 'w'},
		{ "sys",         required_argument, NULL, 's'},
		{ "iterations",  required_argument, NULL, 'n'},
		{ "fields",      required_argument, NULL, 'f'},
		{ "sort_field",  required_argument, NULL, 'S'},
		{ "cpu_types",   required_argument, NULL, 't'},
		{ NULL,          0,                 NULL, 0  }
	};
	static const char option_string[] = "vhbd:w:s:n:f:t:S:";

	l_init_defaults();
	while (1) {
		opt = getopt_long(argc, argv, option_string,
				  long_options, &index);
		if (opt == -1)
			break;
		switch (opt) {
		case 'v':
			l_print_version();
			hyptop_exit(0);
		case 'h':
			l_usage();
			hyptop_exit(0);
		case 'b':
			l_batch_mode_set();
			break;
		case 'd':
			l_delay_set(optarg);
			break;
		case 'w':
			l_window_set(optarg);
			break;
		case 's':
			l_sys_set(optarg);
			break;
		case 'n':
			l_iterations_set(optarg);
			break;
		case 't':
			l_cpu_types_set(optarg);
			break;
		case 'f':
			l_fields_set(optarg);
			break;
		case 'S':
			l_sort_field_set(optarg);
			break;
		default:
			l_std_usage_exit();
		}
	}
	if (optind != argc)
		ERR_EXIT("Invalid positional parameter \"%s\" specified\n",
			 argv[optind]);
	l_parse_finish();
}

/*
 * Has "sys_name" been specified on command line?
 */
int opts_sys_specified(struct hyptop_win *win, const char* sys_name)
{
	unsigned int i;

	if (!win->opts.sys.specified)
		return 1;
	for (i = 0; i < win->opts.sys.cnt; i++) {
		if (strcmp(win->opts.sys.vec[i], sys_name) == 0)
			return 1;
	}
	return 0;
}

/*
 * Verify that all specified systems are available for window
 */
static void l_verify_systems(struct hyptop_win *win)
{
	char *sys_name;
	unsigned int i;

	for (i = 0; i < win->opts.sys.cnt; i++) {
		if (sd_sys_get(sd_sys_root_get(), win->opts.sys.vec[i]))
			continue;
		sys_name = ht_strdup(win->opts.sys.vec[i]);
		ht_str_to_upper(win->opts.sys.vec[i]);
		if (sd_sys_get(sd_sys_root_get(), win->opts.sys.vec[i])) {
			ht_free(sys_name);
			continue;
		}
		ERR_EXIT("System \"%s\" is not available\n", sys_name);
	 }
}

/*
 * Verify that all specified systems are available for all windows
 */
void opt_verify_systems(void)
{
	l_verify_systems(&win_sys_list);
	l_verify_systems(&win_sys);
	if (g.o.cur_win == &win_sys)
		win_sys_set(win_sys.opts.sys.vec[0]);
}

/*
 * Increase iterations count and exit if necessary
 */
void opts_iterations_next(void)
{
	if (g.o.iterations_specified) {
		g.o.iterations_act++;
		if (g.o.iterations_act >= g.o.iterations)
			hyptop_exit(0);
	}
	if (g.o.batch_mode_specified)
		printf("---------------------------------------------------"
		       "----------------------------\n");
}

