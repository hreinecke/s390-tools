/*
 * s390-tools/zipl/src/error.c
 *   Functions to handle error messages.
 *
 * Copyright IBM Corp. 2001, 2006.
 *
 * Author(s): Carsten Otte <cotte@de.ibm.com>
 *            Peter Oberparleiter <Peter.Oberparleiter@de.ibm.com>
 */

#include "error.h"

#include <stdio.h>
#include <stdarg.h>


#define ERROR_STRING_SIZE	1024

static char error_reason_string[ERROR_STRING_SIZE];
static char error_text_string[ERROR_STRING_SIZE];

static int error_is_reason = 0;
static int error_is_text = 0;


/* Specify the actual reason why an operation failed by providing a formatted
 * text string FMT and a variable amount of extra arguments. */
void
error_reason(const char* fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(error_reason_string, ERROR_STRING_SIZE, fmt, args);
	va_end(args);
	error_is_reason = 1;
}


/* Specify the (higher level) tool operation failure by providing a formatted
 * text string FMT and a variable amount of extra arguments. */
void
error_text(const char* fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(error_text_string, ERROR_STRING_SIZE, fmt, args);
	va_end(args);
	error_is_text = 1;
}


/* Clear a previously specified error_reason() message. */
void
error_clear_reason(void)
{
	error_is_reason = 0;
}


/* Clear a previously specified error_text() message. */
void
error_clear_text(void)
{
	error_is_text = 0;
}


/* Print out the error reason and text message to stderr. */
void
error_print(void)
{
	if (error_is_text && error_is_reason) {
		fprintf(stderr, "Error: %s: %s\n", error_text_string,
			error_reason_string);
	} else if (error_is_text)
		fprintf(stderr, "Error: %s\n", error_text_string);
	else if (error_is_reason)
		fprintf(stderr, "Error: %s\n", error_reason_string);
	else
		fprintf(stderr, "Error: An unspecified error occurred\n");
}
