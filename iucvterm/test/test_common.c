/*
 * test_common library - Test program lib for the IUCV Terminal Applications
 *
 * Common functions for test programs
 *
 * Copyright IBM Corp. 2008
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "iucvterm/proto.h"


int __socketpair(int sv[2])
{
	int rc;

	rc = socketpair(AF_LOCAL, SOCK_STREAM, 0, sv);
	if (rc)
		perror("Could not create socketpair");

	assert(rc == 0);
	return rc;
}

int __msgcmp(const struct iucvtty_msg *m1, const struct iucvtty_msg *m2)
{
	if (m1->type != m2->type)
		return 1;
	if (m1->datalen != m2->datalen)
		return 2;
	if (0 != memcmp(m1->data, m2->data, m1->datalen))
		return 3;
	return 0;
}
