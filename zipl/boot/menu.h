/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Bootmenu Subroutines
 *
 * Copyright IBM Corp. 2013
 * Author(s): Stefan Haberland <stefan.haberland@de.ibm.com>
 */

#ifndef MENU_H
#define MENU_H

#include "stage2.h"

/* address of extra command line */
#define COMMAND_LINE_EXTRA	        (0xA000-0x400)

/* max command line length */
#define COMMAND_LINE_SIZE	        896
#define BOOT_MENU_ENTRIES		63
#define LINE_LENGTH                     80
#define PARAM_SIZE                      8
#define TEXT_OFFSET                     4

#define NUMBER_FOUND                    0
#define PRINT_PROMPT                    1
#define NOTHING_FOUND                   2

struct boot_stage2_params {
	uint16_t flag;
	uint16_t timeout;
	uint16_t banner;
	uint16_t config[BOOT_MENU_ENTRIES];
	uint64_t config_kdump;
} __packed;

extern struct boot_stage2_params __stage2_params;

int menu();

#endif /* MENU_H */
