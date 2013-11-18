/*
 * File...........: ibmOSAMib.h
 * Author(s)......: Thomas Weber <tweber@de.ibm.com>
 * Copyright IBM Corp. 2002,2007
 *
 * History of changes:
 * none 
 *  
 * Include file for the OSA-E subagent MIB implementaton module. 
 * Defines function prototypes of the basic functions in the MIB
 * implementation module.
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

#ifndef _MIBGROUP_IBMOSAMIB_H
#define _MIBGROUP_IBMOSAMIB_H

/* we may use header_generic and header_simple_table from the util_funcs module */

config_require(util_funcs)


/* function prototypes */

void          init_ibmOSAMib(void);     /* register MIB data */
FindVarMethod var_ibmOSAMib;            /* handle GET and GETNEXT requests */
                                        /* for all SMIv2 standard types */
FindVarMethod var_DisplayStr;           /* handle special case Display String */
WriteMethod   write_ibmOSAMib;          /* handle SET requests             */

/* ioctl for Get/Getnext processing */
int do_GET_ioctl ( int, oid*, size_t, IPA_CMD_GET** ); 

#endif /* _MIBGROUP_IBMOSAMIB_H */
