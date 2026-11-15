/*	$OpenBSD: memchr.c,v 1.8 2015/08/31 02:53:57 guenther Exp $ */
/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <string.h>

#ifdef OCTEON_MEMOPS
#include "octeon_memops.h"
#endif

void *
memchr(const void *s, int c, size_t n)
{
	if (n != 0) {
		const unsigned char *p = s;

#ifdef OCTEON_MEMOPS
		if (n >= OCTEON_WORD_SIZE) {
			const octeon_word ones = 0x0101010101010101ULL;
			const octeon_word highs = 0x8080808080808080ULL;
			octeon_word mask = ones * (unsigned char)c;

			while ((uintptr_t)p & OCTEON_WORD_MASK) {
				if (*p == (unsigned char)c)
					return ((void *)p);
				p++;
				n--;
			}
			while (n >= OCTEON_WORD_SIZE) {
				octeon_word x = *(const octeon_word *)p ^ mask;

				/* Mask each matching byte without cross-byte borrows. */
				x = ~(((x & ~highs) + ~highs) | x) & highs;
				if (x != 0)
					return ((void *)(p + __builtin_clzll(x) / 8));
				p += OCTEON_WORD_SIZE;
				n -= OCTEON_WORD_SIZE;
			}
			if (n == 0)
				return (NULL);
		}
#endif
		do {
			if (*p++ == (unsigned char)c)
				return ((void *)(p - 1));
		} while (--n != 0);
	}
	return (NULL);
}
DEF_STRONG(memchr);
