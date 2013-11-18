/*
 * hyptop - Show hypervisor performance data on System z
 *
 * Window "fields": Select fields dialog.
 *
 * Copyright IBM Corp. 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#ifndef WIN_FIELDS_H
#define WIN_FIELDS_H

#include "table.h"
#include "hyptop.h"
#include "win_help.h"

struct win_fields {
	struct hyptop_win	win;
	struct table		*t;
	struct table		*table;
	struct table_col	**col_vec;
	char			**col_desc_vec;
	int			mode_unit_change;
	int			in_select;
	struct hyptop_win	*win_help;
};

struct hyptop_win *win_fields_new(struct table *t, struct table_col **col_vec,
				  char **col_desc_vec);

#endif /* WIN_FIELDS_H */
