/*
 * hyptop - Show hypervisor performance data on System z
 *
 * Text box: Provide scrollable text window under curses.
 *
 * Copyright IBM Corp. 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#ifndef TBOX_H
#define TBOX_H

#include "util.h"

#define TBOX_MAX_STR	120

struct tbox_line {
	struct util_list_node	list;
	char			*str;
};

struct tbox {
	struct util_list	line_list;
	int			line_cnt;
	int			line_start;
	int			tbox_ready;
	struct tbox_line	*last_line;
};

enum tbox_scroll_unit {
	TBOX_SCROLL_LINE,
	TBOX_SCROLL_PAGE,
	TBOX_SCROLL_LAST,
};

struct tbox *tbox_new(void);
void tbox_line_del_all(struct tbox *tb);
void tbox_line_add(struct tbox *tb, const char *str);
void tbox_finish(struct tbox *tb);
void tbox_scroll_down(struct tbox *tb, enum tbox_scroll_unit);
void tbox_scroll_up(struct tbox *tb, enum tbox_scroll_unit);
void tbox_term_resize(struct tbox *tb);
void tbox_print(struct tbox *tb);

#define tbox_printf(tb, x...) \
{ \
	char line[TBOX_MAX_STR + 1]; \
	sprintf(line, x); \
	tbox_line_add(tb, line); \
}

#endif /* TBOX_H */
