/******************************************************************************/
/*  zfcp_dumper.h                                                             */
/*                                                                            */
/*    Copyright IBM Corp. 2003, 2006.                                     */
/*    Author(s): Michael Holzheu <holzheu@de.ibm.com>                         */
/******************************************************************************/

#ifndef __zfcp_dumper_h
#define __zfcp_dumper_h

#include <stdio.h>
#include <signal.h> 
#include <stdint.h>

#define CMDLINE_MAX_LEN 1024

#define PRINT_TRACE(x...) if(g.parm_dump_debug >= 3) {fprintf(stderr,"TRACE: ");fprintf(stderr, ##x);}
#define PRINT_ERR(x...)   {fprintf(stderr,"ERROR: ");fprintf(stderr, ##x);}
#define PRINT_WARN(x...)  {fprintf(stderr,"WARNING: ");fprintf(stderr, ##x);}
#define PRINT_PERR(x...)  {fprintf(stderr,"ERROR: ");fprintf(stderr, ##x);perror("");}
#define PRINT(x...)       fprintf(stderr, ##x)
 
typedef struct _global_t {
        char*    parm_dump_compress;
        char*    parm_dump_dir;
        char*    parm_dump_part;
        int      parm_dump_debug;
	int      parm_dump_mode;
	uint64_t parm_dump_mem;
        char  parmline[CMDLINE_MAX_LEN];
        char  dump_dir[1024];
	int   dump_nr;
	int   last_progress_time;
	struct sigaction dump_sigact;
	int   hsa_released;
	unsigned long hsa_size;
} global_t;

extern global_t g;

#endif /* __zfcp_dumper_h */
