/*	$OpenBSD: memcmp.c,v 1.6 2015/08/31 02:53:57 guenther Exp $ */
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

/*
 * Compare memory regions.
 */
int
memcmp(const void *s1, const void *s2, size_t n)
{
	if (n != 0) {
		const unsigned char *p1 = s1, *p2 = s2;

#ifdef OCTEON_MEMOPS
		if (n >= 16) {
			if (*p1 != *p2)
				return (*p1 - *p2);
			while ((uintptr_t)p1 & OCTEON_WORD_MASK) {
				if (*p1 != *p2)
					return (*p1 - *p2);
				p1++;
				p2++;
				n--;
			}
			while (n >= OCTEON_WORD_SIZE) {
				octeon_word a = *(const octeon_word *)p1;
				octeon_word b = octeon_load(p2);

				if (a != b) {
					unsigned int shift =
					    56 - (__builtin_clzll(a ^ b) & ~7U);

					return ((int)((a >> shift) & 255) -
					    (int)((b >> shift) & 255));
				}
				p1 += OCTEON_WORD_SIZE;
				p2 += OCTEON_WORD_SIZE;
				n -= OCTEON_WORD_SIZE;
			}
			if (n == 0)
				return (0);
		}
#endif
		do {
			if (*p1++ != *p2++)
				return (*--p1 - *--p2);
		} while (--n != 0);
	}
	return (0);
}
DEF_STRONG(memcmp);
