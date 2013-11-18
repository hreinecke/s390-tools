/*
 * Copyright 2006 IBM Corporation
 * IUCV protocol stack for Linux on zSeries
 * Version 1.0
 * Author(s): Jennifer Hunt <jenhunt@us.ibm.com>
 *
 */

#ifndef __AFIUCV_H
#define __AFIUCV_H

#include <sys/socket.h>

#ifndef AF_IUCV
#	define AF_IUCV		32
#	define PF_IUCV		AF_IUCV
#endif

/* IUCV socket address */
struct sockaddr_iucv {
	sa_family_t	siucv_family;
	unsigned short	siucv_port;		/* Reserved */
	unsigned int	siucv_addr;		/* Reserved */
	char		siucv_nodeid[8];	/* Reserved */
	char		siucv_user_id[8];	/* Guest User Id */
	char		siucv_name[8];		/* Application Name */
};

#endif
