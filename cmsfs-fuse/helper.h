/*
 * cmsfs-fuse - CMS EDF filesystem support for Linux
 * Common helper functions.
 *
 * Copyright IBM Corp. 2010
 * Author(s): Jan Glauber <jang@linux.vnet.ibm.com>
 */

#ifndef _HELPER_H
#define _HELPER_H

extern FILE *logfile;
#define DEBUG_LOGFILE "/tmp/cmsfs-fuse.log"

#ifdef DEBUG_ENABLED
#define DEBUG(...)							\
	do {								\
		fprintf(logfile, __VA_ARGS__);				\
		fflush(logfile);					\
	} while (0)
#else
#define DEBUG(...)
#endif

#define DIE(...)							\
	do {								\
		fprintf(stderr, COMP __VA_ARGS__);			\
		exit(1);						\
	} while (0)

#define DIE_PERROR(...)							\
	do {								\
		perror(COMP __VA_ARGS__);				\
		exit(1);						\
	} while (0)

#define BUG(x)								\
	if (x) {							\
		fprintf(stderr, COMP " assert failed at "		\
			__FILE__ ":%d in %s()\n", __LINE__, __func__);	\
		exit(1);						\
	}

#define WARN(...)							\
	do {								\
		fprintf(stderr, COMP "Warning, " __VA_ARGS__);		\
	} while (0)

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif
