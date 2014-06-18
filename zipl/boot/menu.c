/*
 * zipl - zSeries Initial Program Loader tool
 *
 * Bootmenu Subroutines
 *
 * Copyright IBM Corp. 2013
 * Author(s): Stefan Haberland <stefan.haberland@de.ibm.com>
 */

#include "sclp.h"
#include "menu.h"
#include "libc.h"

static const char *msg_econfig = "Error: undefined configuration\n";

static void menu_prompt(int timeout)
{
	if (timeout)
		printf("Please choose (default will boot in %u seconds):",
		       timeout);
	else
		printf("Please choose:");
}

static int menu_read(void)
{
	char *temp_area =  (char *)get_zeroed_page();
	uint16_t *configs = __stage2_params.config;
	int timeout, rc, i, count = 0;
	char *endptr;
	int value;

	timeout = __stage2_params.timeout;

	while (1) {
		/* print prompt */
		menu_prompt(timeout);

		/* wait for input or timeout */
		while (count == 0) {
			rc = sclp_read(timeout, temp_area, &count);
			if (rc != 0) {
				/* timeout or error during read, boot default */
				value = 0;
				goto out_free_page;
			}
			/* disable timeout in case of retry */
			timeout = 0;
		}

		if (count > LINE_LENGTH)
			count = LINE_LENGTH;

		/* input under zVM needs to be converted to lower case */
		if (is_zvm())
			for (i = 0; i < count; i++)
				temp_area[i] = ebc_tolower(temp_area[i]);
		value = ebcstrtoul((char *)temp_area, &endptr, 10);

		if ((endptr != temp_area) && (value < BOOT_MENU_ENTRIES - 1) &&
		    (configs[value] != 0)) {
			/* valid config found - finish */
			break;
		} else {
			/* no valid config retry */
			printf(msg_econfig);
			count = 0;
		}
	}

	if (temp_area + count > endptr)
		memcpy((void *)COMMAND_LINE_EXTRA, endptr,
		       (temp_area + count - endptr));
out_free_page:
	free_page((unsigned long) temp_area);
	return value;
}

static int menu_list(void)
{
	uint16_t *configs = __stage2_params.config;
	char *name;
	int i;

	for (i = 0; i < BOOT_MENU_ENTRIES; i++) {
		if (configs[i] == 0)
			continue;
		name = configs[i] + ((void *)&__stage2_params);
		printf("%s\n", name);
		if (i == 0)
			printf("\n");
	}

	return 0;
}

/*
 * Interpret loadparm
 *
 * Parameter
 *     value - to store configuration number
 *
 * Return
 *     0 - found number to boot, stored in value
 *     1 - print prompt
 *     2 - nothing found
 */
static int menu_param(unsigned long *value)
{
	char loadparm[PARAM_SIZE];
	char *endptr;
	int i;

	if (!sclp_param(loadparm))
		*value = ebcstrtoul(loadparm, &endptr, 10);

	/* got number, done */
	if (endptr != loadparm)
		return NUMBER_FOUND;

	/* no number, check for keyword */
	i = 0;
	/* skip leading whitespaces */
	while (ebc_isspace(loadparm[i]) && (i < PARAM_SIZE))
		i++;

	if (!strncmp(&loadparm[i], "PROMPT", 6)) {
		*value = 0;
		return PRINT_PROMPT;
	}

	return NOTHING_FOUND;
}

int menu(void)
{
	uint16_t *configs = __stage2_params.config;
	unsigned long value = 0;
	char *cmd_line_extra;
	char endstring[15];
	int rc;

	cmd_line_extra = (char *)COMMAND_LINE_EXTRA;
	rc = sclp_setup(SCLP_INIT);
	if (rc)
		/* sclp setup failed boot default */
		goto boot;

	memset(cmd_line_extra, 0, COMMAND_LINE_SIZE);
	rc = menu_param(&value);
	if (rc == 0) {
		/* got number from loadparm, boot it */
		goto boot;
	} else if (rc == 1 && value == 0) {
		/* keyword "prompt", show menu */
	} else if (__stage2_params.flag == 0) {
		/* menu disabled, boot default */
		value = 0;
		goto boot;
	}

	/* print banner */
	printf("%s\n", ((void *)&__stage2_params) + __stage2_params.banner);

	/* print config list */
	menu_list();

	if (is_zvm())
		printf(" \n");
		printf("Note: VM users please use '#cp vi vmsg <input> <kernel-parameters>'\n");
		printf(" \n");

	value = menu_read();

	/* sanity - value too big */
	if (value > BOOT_MENU_ENTRIES)
		panic(EINTERNAL, "%s", msg_econfig);

boot:
	/* sanity - config entry not valid */
	if (configs[value] == 0)
		panic(EINTERNAL, "%s", msg_econfig);

	printf("Booting %s\n",
	       (char *)(configs[value] +
			(void *)&__stage2_params + TEXT_OFFSET));

	/* append 'BOOT_IMAGE=<num>' to parmline */
	sprintf(endstring, " BOOT_IMAGE=%u", value);
	if ((strlen(cmd_line_extra) + strlen(endstring)) < COMMAND_LINE_SIZE)
		strcat(cmd_line_extra, endstring);

	sclp_setup(SCLP_DISABLE);

	return value;
}
