/*
 * FCP adapter trace utility
 *
 * Endianness conversion functions
 *
 * Copyright IBM Corp. 2008
 * Author(s): Martin Peschke <mp3@de.ibm.com>
 *            Stefan Raspl <raspl@linux.vnet.ibm.com>
 */

#include <byteswap.h>
#include <endian.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define cpu_to_be64(x) (x = __bswap_64(x))
#define cpu_to_be32(x) (x = __bswap_32(x))
#define cpu_to_be16(x) (x = __bswap_16(x))
#else
#define cpu_to_be64(x) (x = x)
#define cpu_to_be32(x) (x = x)
#define cpu_to_be16(x) (x = x)
#endif

