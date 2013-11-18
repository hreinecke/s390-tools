/*
 * test_afiucv - Test program for the IUCV Terminal Applications
 *
 * Test program to check if the AF_IUCV family is supported by
 * the running Linux kernel
 *
 * Copyright IBM Corp. 2008
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include "af_iucv.h"


int main(void)
{
	int sk;

	sk = socket(AF_IUCV, SOCK_STREAM, 0);
	if (sk == -1) {
		if (errno == EAFNOSUPPORT)
			perror("AF_IUCV address family not supported");
		return -1;
	} else
		close(sk);

	return 0;
}
