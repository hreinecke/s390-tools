/*
 * hyptop - Show hypervisor performance data on System z
 *
 * Command line options, window definition, print functions, etc.
 *
 * Copyright IBM Corp. 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#ifndef HYPTOP_H
#define HYPTOP_H

#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <termios.h>
#include "util.h"
#include "helper.h"
#include "table.h"
#include "nav_desc.h"

#define HYPTOP_OPT_DEFAULT_DELAY	2
#define HYPTOP_MAX_WIN_DEPTH		4
#define HYPTOP_MAX_LINE			512
#define PROG_NAME			"hyptop"

/*
 * Options info
 */
struct hyptop_str_vec_opt {
	unsigned int		specified;
	char			**vec;
	unsigned int		cnt;
};

struct hyptop_col_vec_opt {
	unsigned int		specified;
	struct table_col_spec	**vec;
	unsigned int		cnt;
};

struct hyptop_win_opts {
	struct hyptop_str_vec_opt	sys;
	struct hyptop_col_vec_opt	fields;
	unsigned int			sort_field_specified;
	char				sort_field;
};

struct hyptop_opts {
	unsigned int			win_specified;
	unsigned int			batch_mode_specified;
	unsigned int			iterations_specified;
	unsigned int			iterations;
	unsigned int			iterations_act;

	struct hyptop_win		*cur_win;
	struct hyptop_str_vec_opt	cpu_types;

	int				delay_s;
	int				delay_us;
};

/*
 * Curses info
 */
struct hyptop_curses {
	int			row_cnt;
	int			col_cnt;
	char			line[HYPTOP_MAX_LINE];
	int			x;
	int			y;
	int			initialized;
};

/*
 * Window info
 */
struct hyptop_win_info {
	struct hyptop_win	*cur;
	struct hyptop_win	*prev[HYPTOP_MAX_WIN_DEPTH];
	unsigned int		prev_cnt;
};

/*
 * Globals definition
 */
struct hyptop_globals {
	struct hyptop_opts	o;
	struct hyptop_curses	c;
	struct hyptop_win_info	w;
	const char		*prog_name;
	struct hyptop_win	*win_cpu_types;
};

extern struct hyptop_globals g;

/*
 * Print functions
 */
#define hyptop_printf_pos(y, x, p...) \
	do { \
		if (g.o.batch_mode_specified) \
			printf(p); \
		else { \
			int len; \
			len = snprintf(g.c.line, sizeof(g.c.line) - 1, p); \
			len = MIN(len, (g.c.col_cnt - (x))); \
			if (len > 0) { \
				mvaddnstr((y), (x), g.c.line, len); \
			} \
		} \
	} while (0)

#define hyptop_printf(p...) \
	do { \
		if (g.o.batch_mode_specified) \
			printf(p); \
		else { \
			int len; \
			len = snprintf(g.c.line, sizeof(g.c.line) - 1, p); \
			len = MIN(len, (g.c.col_cnt - g.c.x)); \
			if (len > 0) { \
				mvaddnstr(g.c.y, g.c.x, g.c.line, len); \
				g.c.x += len; \
			} \
		} \
	} while (0)

static inline void hyptop_printf_init(void)
{
	g.c.x = 0;
	g.c.y = 0;
}

static inline void hyptop_print_seek_back(int i)
{
	unsigned int cnt = MAX(g.c.col_cnt - g.c.x - i, 0);

	if (g.o.batch_mode_specified)
		return;
	if (cnt) {
		memset(g.c.line, ' ', cnt);
		assert(cnt < sizeof(g.c.line));
		g.c.line[cnt] = 0;
		addstr(g.c.line);
	}
	g.c.x = g.c.col_cnt - i;
}

static inline void hyptop_print_nl(void)
{
	unsigned int cnt = MAX(g.c.col_cnt - g.c.x, 0);

	if (g.o.batch_mode_specified) {
		printf("\n");
		return;
	}
	if (cnt) {
		memset(g.c.line, ' ', g.c.col_cnt - g.c.x);
		assert(cnt < sizeof(g.c.line));
		g.c.line[cnt] = 0;
		addstr(g.c.line);
	}
	g.c.x = 0;
	g.c.y++;
}

/*
 * hyptop windows
 */

enum hyptop_win_action {
	WIN_SWITCH,
	WIN_KEEP,
};

extern enum hyptop_win_action hyptop_process_input_timeout(void);
extern enum hyptop_win_action hyptop_process_input(void);
extern enum hyptop_win_action win_switch(struct hyptop_win *w);
extern enum hyptop_win_action win_back(void);

struct hyptop_win;
struct hyptop_win {
	enum hyptop_win_action	(*process_input)(struct hyptop_win *w, int c);
	void			(*update_term)(struct hyptop_win *w);
	void			(*run)(struct hyptop_win *w);
	const char		*id;
	const char		*desc;
	struct nav_desc		**desc_normal_vec;
	struct nav_desc		**desc_select_vec;
	struct nav_desc		**desc_general_vec;
	struct hyptop_win_opts	opts;
};

/*
 * Window sys_list
 */
extern struct hyptop_win win_sys_list;
extern void win_sys_list_init(void);

/*
 * Window sys
 */
extern struct hyptop_win win_sys;
extern void win_sys_set(const char *sys_id);
extern void win_sys_init(void);

/*
 * Window cpu_types
 */
extern void win_cpu_types_init(void);

/*
 * Misc functions
 */
extern void hyptop_update_term(void);
extern void hyptop_exit(int rc);
extern void hyptop_text_mode(void);

#endif /* HYPTOP_H */
