/*
 * test_common - Test program for the IUCV Terminal Applications
 *
 * Definition of common functions for test programs
 *
 * Copyright IBM Corp. 2008
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#ifndef __TEST_H_
#define __TEST_H_


#include <assert.h>
#include <stdlib.h>
#include "iucvterm/proto.h"


#define __fail()	assert(0);


extern int __socketpair(int sv[2]);
extern int __msgcmp(const struct iucvtty_msg *, const struct iucvtty_msg *);

#endif /* __TEST_H_ */
