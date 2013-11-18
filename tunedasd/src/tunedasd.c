/*
 * s390-tools/tunedasd/src/tunedasd.c
 *   zSeries DASD tuning program.
 *
 * Copyright IBM Corp. 2004, 2006.
 *
 * Author(s): Horst Hummel (horst.hummel@de.ibm.com)
 */

#include "tunedasd.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>

#include "disk.h"
#include "error.h"
#include "zt_common.h"

/* Full tool name */
static const char tool_name[] = "tunedasd: zSeries DASD tuning program";

/* Copyright notice */
static const char copyright_notice[] = "Copyright IBM Corp. 2004, 2006";

/* Usage information */
static const char* usage_text[] = {
	"Usage: tunedasd [OPTION] COMMAND device",
	"",
	"Adjust tunable DASD parameters. The DEVICE is the node of the device "
	"(e.g. '/dev/dasda') or a list of devices seperated by a space "
	"character.",
	"",
	"-h, --help               Print this help, then exit",
	"-v, --version            Print version information, then exit",
	"-g, --get_cache          Get current storage server caching behaviour",
	"-c, --cache <behavior>   Define caching behavior on storage server",
	"                         (normal/bypass/inhibit/sequential/prestage/"
	                          "record)",
	"-n, --no_cyl <n>         Number of cylinders to be cached ",
	"                         (only valid together with --cache)",
	"-S, --reserve            Reserve device",
	"-L, --release            Release device",
	"-O, --slock              Unconditional reserve device",
	"                         Note: Use with care, this breaks an existing "
	                          "lock",
	"-Q, --query_reserve      Print reserve status of device ",
	"-P, --profile            Print profile info of device",
	"-I, --prof_item          Print single profile item",
	"                         (reqs/sects/sizes/total/totsect/start/irq/",
	"                         irqsect/end/queue)",
	"-R, --reset_prof         Reset profile info of device"
};

#define CMD_KEYWORD_NUM		12
#define DEVICES_NUM		256

enum cmd_keyword_id {
	cmd_keyword_help           =  0,
	cmd_keyword_version        =  1,
	cmd_keyword_get_cache      =  2,
	cmd_keyword_cache          =  3,
	cmd_keyword_no_cyl         =  4,
	cmd_keyword_reserve        =  5,
	cmd_keyword_release        =  6,
	cmd_keyword_slock          =  7,
	cmd_keyword_profile        =  8,
	cmd_keyword_prof_item      =  9,
	cmd_keyword_reset_prof     = 10,
	cmd_keyword_query_reserve  = 11,
};


/* Mapping of keyword IDs to strings */
static const struct {
	char* keyword;
	enum cmd_keyword_id id;
} keyword_list[] = {
	{ "help",          cmd_keyword_help },
	{ "version",       cmd_keyword_version },
	{ "get_cache",     cmd_keyword_get_cache },
	{ "cache",         cmd_keyword_cache },
	{ "no_cyl",        cmd_keyword_no_cyl },
	{ "reserve",       cmd_keyword_reserve },
	{ "release",       cmd_keyword_release },
	{ "slock",         cmd_keyword_slock },
	{ "profile",       cmd_keyword_profile },
	{ "prof_item",     cmd_keyword_prof_item },
	{ "reset_prof",    cmd_keyword_reset_prof },
	{ "query_reserve", cmd_keyword_query_reserve }
};	


enum cmd_key_state {
	req, /* Keyword is required */
	opt, /* Keyword is optional */
	inv  /* Keyword is invalid */
};


/* Determines which combination of keywords are valid */
enum cmd_key_state cmd_key_table[CMD_KEYWORD_NUM][CMD_KEYWORD_NUM] = {
	/*		     help vers get_ cach no_c rese rele sloc prof prof rese quer
	 *		          ion  cach e    yl   rve  ase  k    ile  _ite t_pr y_re
	 *		               	e                                  m    of  serv
	 */
	/* help  	 */ { req, opt, opt, opt, opt, opt, opt, opt, opt, opt, opt, inv },
	/* version	 */ { inv, req, inv, inv, inv, inv, inv, inv, inv, inv, inv, inv },
	/* get_cache	 */ { opt, opt, req, inv, inv, inv, inv, inv, inv, inv, inv, inv },
	/* cache 	 */ { opt, opt, inv, req, opt, inv, inv, inv, inv, inv, inv, inv },
	/* no_cyl	 */ { opt, opt, inv, req, req, inv, inv, inv, inv, inv, inv, inv },
	/* reserve	 */ { opt, opt, inv, inv, inv, req, inv, inv, inv, inv, inv, inv },
	/* release	 */ { opt, opt, inv, inv, inv, inv, req, inv, inv, inv, inv, inv },
	/* slock 	 */ { opt, opt, inv, inv, inv, inv, inv, req, inv, inv, inv, inv },
	/* profile	 */ { opt, opt, inv, inv, inv, inv, inv, inv, req, opt, inv, inv },
	/* prof_item	 */ { opt, opt, inv, inv, inv, inv, inv, inv, req, req, inv, inv },
	/* reset_prof	 */ { opt, opt, inv, inv, inv, inv, inv, inv, inv, inv, req, inv },
	/* query_reserve */ { inv, inv, inv, inv, inv, inv, inv, inv, inv, inv, inv, req },
};

struct parameter {
	int kw_given;
	char *data;
};

struct command_line {
	struct parameter parm[CMD_KEYWORD_NUM];
	char * devices[DEVICES_NUM];
	int device_id;
};


static struct option options[] = {
	{ "help",		no_argument,		NULL, 'h'},
	{ "version",		no_argument,		NULL, 'v'},
	{ "get_cache",		no_argument,	        NULL, 'g'},
	{ "cache",		required_argument,	NULL, 'c'},
	{ "no_cyl",		required_argument,	NULL, 'n'},
	{ "reserve",		no_argument,	        NULL, 'S'},
	{ "release",		no_argument,	        NULL, 'L'},
	{ "slock",		no_argument,	        NULL, 'O'},
	{ "profile",		no_argument,	        NULL, 'P'},
	{ "prof_item",		required_argument,      NULL, 'I'},
	{ "reset_prof",		no_argument,	        NULL, 'R'},
	{ "query_reserve",	no_argument,	        NULL, 'Q'},
	{ NULL,			0,			NULL, 0 }
};

/* Command line option abbreviations */
static const char option_string[] = "hvgc:n:SLOPI:RQ";


/* Error message string */
#define ERROR_STRING_SIZE	1024
static char error_string[ERROR_STRING_SIZE];


/*
 * Generate and print an error message based on the formatted
 * text string FMT and a variable amount of extra arguments. 
 */
void
error_print (const char* fmt, ...)
{
	va_list args;

	va_start (args, fmt);
	vsnprintf (error_string, ERROR_STRING_SIZE, fmt, args);
	va_end (args);

	fprintf (stderr, "Error: %s\n", error_string);
}


/* 
 * Print usage information.
 */
static void
print_usage (void)
{
	unsigned int i;

	for (i = 0; i < sizeof (usage_text) / sizeof (usage_text[0]); i++) {
		printf ("%s\n", usage_text[i]);
	}
}


/* 
 * Print version information.
 */
static void
print_version (void)
{
	printf ("%s version %s\n", tool_name, RELEASE_STRING);
	printf ("%s\n", copyright_notice);
}


/* 
 * Check whether calling user is root. Return 0 if user is root, non-zero
 * otherwise. 
 */
static int
check_for_root (void)
{
	if (geteuid () != 0) {
		error_print ("Must be root to perform this operation");
		return -1;
	} else {
		return 0;
	}
}


/* 
 * Retrieve name of keyword identified by ID.
 */
char *
get_keyword_name (enum cmd_keyword_id id)
{
	unsigned int i;

	for (i = 0; i < sizeof (keyword_list) / sizeof (keyword_list[0]);
	     i++) {
		if (id == keyword_list[i].id) {
			return keyword_list[i].keyword;
		}
	}
	return "<unknown>";
}


/* 
 * Check the given function for given options and valid combinations of 
 * options
 */
int
check_key_state (struct command_line *cmdline)
{
	int i,j;

	/* Find first given keyword */
	for (i = 0; i < CMD_KEYWORD_NUM && !cmdline->parm[i].kw_given; i++);
	
	if (i >= CMD_KEYWORD_NUM) {
		error_print ("No valid parameter specified");
		print_usage ();
		return -1;
	}

	/* Check keywords */
	for (j = 0; j < CMD_KEYWORD_NUM; j++) {

		switch (cmd_key_table[i][j]) {
		case req:
			/* Missing keyword on command line */
			if (!(cmdline->parm[j].kw_given)) {
				error_print ("Option '%s' required when "
					     "specifying '%s'",
					     get_keyword_name (j),
					     get_keyword_name (i));
				return -1;
			}
			break;
		case inv:
			/* Invalid keyword on command line */
			if (cmdline->parm[j].kw_given) {
				error_print ("Only one of options '%s' and "
					     "'%s' allowed",
					     get_keyword_name (i),
					     get_keyword_name (j));
				return -1;
			}
			break;
		case opt:
			break;
		}
	}

	return 0;
}


/*
 * Save the given command together with its parameter. 
 */
static int
store_option (struct command_line* cmdline, enum cmd_keyword_id keyword,
	      char* value)
{
	if ((cmdline->parm[(int) keyword]).kw_given) {
		error_print ("Option '%s' specified more than once",
			     get_keyword_name (keyword));
		return -1;
	}
	cmdline->parm[(int) keyword].kw_given = 1;
	cmdline->parm[(int) keyword].data = value;
	return 0;
}


/*
 * Parse the command line for valid parameters.
 */
int
get_command_line (int argc, char* argv[], struct command_line* line)
{
	struct command_line cmdline;
	int opt;
	int rc;
	
	memset ((void *) &cmdline, 0, sizeof (struct command_line));

	/* Process options */
	do {
		opt = getopt_long (argc, argv, option_string, options, NULL);
		
		rc = 0;
		switch (opt) {
		case 'h':
			rc = store_option (&cmdline, cmd_keyword_help,
					   optarg);
			break;
		case 'v':
			rc = store_option (&cmdline, cmd_keyword_version,
					   optarg);
			break;
		case 'g':
			rc = store_option (&cmdline, cmd_keyword_get_cache,
					   optarg);
			break;
		case 'c':
			rc = check_cache (optarg);
			if (rc >= 0) {
				rc = store_option (&cmdline, cmd_keyword_cache,
						   optarg);
			}
			break;
		case 'n':
			rc = check_no_cyl (optarg);
			if (rc >= 0) {
				rc = store_option (&cmdline, 
						   cmd_keyword_no_cyl,
						   optarg);
			}
			break;
		case 'S':
			rc = store_option (&cmdline, cmd_keyword_reserve,
					   optarg);
			break;
		case 'L':
			rc = store_option (&cmdline, cmd_keyword_release,
					   optarg);
			break;
		case 'O':
			rc = store_option (&cmdline, cmd_keyword_slock,
					   optarg);
			break;
		case 'P':
			rc = store_option (&cmdline, cmd_keyword_profile,
					   optarg);
			break;
		case 'I':
			rc = check_prof_item (optarg);
			if (rc >= 0) {
				rc = store_option (&cmdline, 
						   cmd_keyword_prof_item,
						   optarg);
			}
			break;
		case 'R':
			rc = store_option (&cmdline, cmd_keyword_reset_prof,
					   optarg);
			break;
		case 'Q':
			rc = store_option (&cmdline, cmd_keyword_query_reserve,
					   optarg);
			break;

		case -1:
			/* End of options string - start of devices list */
			cmdline.device_id = optind;
			break;
		default:
			fprintf(stderr, "Try 'tunedasd --help' for more"
					" information.\n");
			rc = -1;
			break;
		}
		if (rc < 0) {
			return rc;
		}
	} while (opt != -1);

	*line = cmdline;
	return 0;
}


/*
 * Execute the command.
 */
int
do_command (char* device, struct command_line cmdline)
{
	int i, rc;

	rc = 0;
	for (i = 0; !cmdline.parm[i].kw_given; i++);

	switch (i) {
	case cmd_keyword_get_cache:
                rc = disk_get_cache (device); 
		break;
	case cmd_keyword_cache:
		rc = disk_set_cache (device, 
				     cmdline.parm[cmd_keyword_cache].data,
				     cmdline.parm[cmd_keyword_no_cyl].data);
		break;
	case cmd_keyword_no_cyl:
		break;
	case cmd_keyword_reserve:
		rc = disk_reserve (device);
		break;
	case cmd_keyword_release:
		rc = disk_release (device);
		break;
	case cmd_keyword_slock:
		rc = disk_slock (device);
		break;
	case cmd_keyword_profile: 
		rc = disk_profile (device, 
				   cmdline.parm[cmd_keyword_prof_item].data);
		break;
	case cmd_keyword_reset_prof:
		rc = disk_reset_prof (device);
		break;
	case cmd_keyword_prof_item:
		break;
	case cmd_keyword_query_reserve:
		rc = disk_query_reserve_status(device);
		break;
	default:
		error_print ("Unknown command '%s' specified",
			     get_keyword_name (i));
		break;
	}

	return rc;
}


/*
 * Main. 
 */
int
main (int argc, char* argv[])
{
	struct command_line cmdline;
	int rc, finalrc;

	/* Find out what we're supposed to do */
	rc = get_command_line (argc, argv, &cmdline);
	if (rc) {
		return 1;
	}

	rc= check_key_state (&cmdline);
	if (rc) {
		return 1;
	}

	/* Check for priority options --help and --version */
	if (cmdline.parm[cmd_keyword_help].kw_given) {
		print_usage ();
		return 0;
	} else if (cmdline.parm[cmd_keyword_version].kw_given) {
		print_version ();
		return 0;
	}

	/* Make sure we're running as root */
	if (check_for_root ()) {
		return 1;
	}

	/* Do each of the commands on each of the devices 
	 * and don't care about the return codes           */
	if (cmdline.device_id >= argc) {
		error_print ("Missing device");
		print_usage ();
		return 1;
	}

	finalrc = 0;
	while (cmdline.device_id < argc) {
		rc = do_command (argv[cmdline.device_id], cmdline);
		if (rc && !finalrc)
			finalrc = rc;
		cmdline.device_id++;
	}
	return finalrc;
}
