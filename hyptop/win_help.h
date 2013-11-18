/*
 * hyptop - Show hypervisor performance data on System z
 *
 * Window "help": Show online help text.
 *
 * Copyright IBM Corp. 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#ifndef WIN_HELP_H
#define WIN_HELP_H

#include "tbox.h"
#include "hyptop.h"

struct win_help {
	struct hyptop_win	win;
	struct tbox		*tb;
};

struct hyptop_win *win_help_new(struct hyptop_win *win);

#endif /* WIN_HELP_H */
