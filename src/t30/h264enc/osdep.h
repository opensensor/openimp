#ifndef __OSDEP_H__
#define __OSDEP_H__

#include <stdint.h>

/* The bitstream writer stores uint32_t words regardless of host pointer size.
 * Keeping this fixed also makes its tests valid on 64-bit build hosts. */
#define WORD_SIZE 4


static inline uint32_t endian_fix32( uint32_t x )
{
	return (x<<24) + ((x<<8)&0xff0000) + ((x>>8)&0xff00) + (x>>24);

}
static inline uint64_t endian_fix64( uint64_t x )
{
	return endian_fix32(x>>32) + ((uint64_t)endian_fix32(x)<<32);
}
static inline intptr_t endian_fix( intptr_t x )
{
	return WORD_SIZE == 8 ? endian_fix64(x) : endian_fix32(x);
}
static inline uint16_t endian_fix16( uint16_t x )
{
	return (x<<8)|(x>>8);
}





#endif
