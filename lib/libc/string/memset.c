/*	$OpenBSD: memset.c,v 1.8 2017/11/29 05:13:57 guenther Exp $ */
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

#include <stdint.h>
#include <string.h>

void *
memset(void *dst, int c, size_t n)
{
	unsigned char *d = dst;

#ifdef OCTEON_MEMSET
	if (n >= sizeof(uint64_t)) {
		typedef uint64_t fillword __attribute__((__may_alias__));
		fillword *words, pattern;
		unsigned char byte = (unsigned char)c;

		while ((uintptr_t)d & (sizeof(pattern) - 1)) {
			*d++ = byte;
			n--;
		}
		pattern = (uint64_t)byte * 0x0101010101010101ULL;

		while (n >= 8 * sizeof(pattern)) {
			words = (fillword *)d;
			words[0] = pattern;
			words[1] = pattern;
			words[2] = pattern;
			words[3] = pattern;
			words[4] = pattern;
			words[5] = pattern;
			words[6] = pattern;
			words[7] = pattern;
			d += 8 * sizeof(pattern);
			n -= 8 * sizeof(pattern);
		}
		while (n >= sizeof(pattern)) {
			*(fillword *)d = pattern;
			d += sizeof(pattern);
			n -= sizeof(pattern);
		}
	}
#endif
	if (n != 0) {

		do
			*d++ = (unsigned char)c;
		while (--n != 0);
	}
	return (dst);
}
DEF_BUILTIN(memset);
