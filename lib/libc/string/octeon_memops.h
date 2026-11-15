/* Shared by the Octeon libc memory routines. */
#ifndef _OCTEON_MEMOPS_H_
#define _OCTEON_MEMOPS_H_

#include <stdint.h>

typedef uint64_t octeon_word __attribute__((__may_alias__));

#define OCTEON_WORD_SIZE sizeof(octeon_word)
#define OCTEON_WORD_MASK (OCTEON_WORD_SIZE - 1)

/* Big-endian MIPS64 loads the two parts of an unaligned word. */
static inline __attribute__((__always_inline__)) octeon_word
octeon_load(const void *p)
{
	octeon_word word;

	__asm__("ldl %0, 0(%1)\n\t"
	    "ldr %0, 7(%1)"
	    : "=&r"(word) : "r"(p), "m"(*(const unsigned char (*)[8])p));
	return word;
}

#endif
