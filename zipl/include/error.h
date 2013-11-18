/*
 * s390-tools/zipl/include/error.h
 *   Functions to handle error messages.
 *
 * Copyright IBM Corp. 2001, 2006.
 *
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 *            Peter Oberparleiter <Peter.Oberparleiter@de.ibm.com>
 */

#ifndef ERROR_H
#define ERROR_H

#include "zipl.h"


void error_reason(const char* fmt, ...);
void error_text(const char* fmt, ...);
void error_clear_reason(void);
void error_clear_text(void);
void error_print(void);

#endif /* not ERROR_H */
