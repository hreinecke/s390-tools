/*
 * File...........: ibmOSAMibUtil.h
 * Author(s)......: Thomas Weber <tweber@de.ibm.com>
 * Copyright IBM Corp. 2002,2007
 *
 * History of changes:
 * none 
 *  
 * Prototypes for the utility functions within ibmOSAMibUtil.c
 * It should be included in every module that belonging to the
 * subagent.  
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* standard includes */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <math.h>

/* include socket & ioctl library */
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/if.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h> 
#include <netinet/in.h> /* included because of "parse error before `get_myaddr'" */
                        /* in version 4.2.1 this headerfile was included from    */
			/* ucd-snmp-includes.h                                   */ 

/* include local defintions of data structures for OSA Express subagent */
#include "ibmOSAMibDefs.h"

/************************
* function prototypes   * 
************************/

/* convert OID string returned by IPAssists to ucd-snmp oid type */
int str_to_oid_conv ( char*, oid* );

/* convert ucd-snmp oid type to OID string used by IPAssists */
int oid_to_str_conv ( oid*, size_t, char* );

/* convert IPA access type to ucd-snmp access type */
int get_acc_type ( int );

/* create pseudo node for toplevel OID linked list */
TABLE_OID* init_oid_list();

/* create pseudo node for index linked list */
REG_INDICES* init_ind_list();

/* inserts an OID after a given entry into the linked list */
TABLE_OID* oid_insert_after ( oid*, size_t, TABLE_OID* );

/* delete a whole Toplevel OID from the linked list including indices */
int delete_oid( oid*, size_t, TABLE_OID* );

/* remove index entries from Toplevel OID list */
int clear_oid_list( TABLE_OID* );

/* searches an OID in the OID linked list */
int search_oid ( oid*, size_t, TABLE_OID*, TABLE_OID**  );

/* searches a Toplevel OID in a fully qualified OID */
int search_top_oid ( oid*, size_t, TABLE_OID*, TABLE_OID** );

/* inserts an index into the index linked list */
REG_INDICES* index_insert_after ( char*, int,  REG_INDICES* );

/* delete a single entry from the index list or the whole list */
int delete_index( REG_INDICES*, int, int );

/* searches an index in the OID linked list */
int search_index ( char*, REG_INDICES*, REG_INDICES** );

/* main MIB information registry function */
int register_tables ( void*, TABLE_OID* );

/* validates request and sets up fully qualified query/set OID */
int header_osa_table( struct variable*, oid*, size_t*,int, size_t*, 
		      WriteMethod**, TABLE_OID* );

/* functions checking for interface changes and updating MIB information */
void update_mib_info ( );

/* retrieves interface information from IF-MIB */
int query_IF_MIB( IF_LIST** );

/* retrieves OSA Express interface information from kernel */
int query_OSA_EXP ( IF_LIST** ,int );

/* get time of day */
int get_time( char* );

/* display help message */
void usage();
