/*************************************************************************
 *
 * File...........: s390-tools/tape390/tape390_common.h
 * Author(s)......: Frank Munzert <munzert@de.ibm.com>
 *                  Michael Holzheu <holzheu@de.ibm.com>
 *
 * Copyright IBM Corp. 2006,2007
 *
 *************************************************************************/

#ifndef _TAPE390_COMMON_H
#define _TAPE390_COMMON_H

#define ERRMSG(x...) {fflush(stdout);fprintf(stderr,x);}
#define ERRMSG_EXIT(ec,x...) do {fflush(stdout);fprintf(stderr,x);exit(ec);} while(0)
#define EXIT_MISUSE 1

extern int is_not_tape(char *); 
extern int open_tape(char *);
extern void set_prog_name(char *);
extern char *prog_name;

#endif
