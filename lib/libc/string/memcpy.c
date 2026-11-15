/*	$OpenBSD: memcpy.c,v 1.4 2017/11/29 05:13:57 guenther Exp $ */
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
#include <stdlib.h>
#include <syslog.h>

/*
 * sizeof(word) MUST BE A POWER OF TWO
 * SO THAT wmask BELOW IS ALL ONES
 */
typedef	long word;		/* "word" used for optimal copy speed */

#define	wsize	sizeof(word)
#define	wmask	(wsize - 1)

static const char backwards_msg[] = ": backwards memcpy";

#ifdef OCTEON_MEMCPY
#define OCTEON_COPY_BLOCK	128
#define OCTEON_PREFETCH_DISTANCE	512

static __inline __attribute__((__always_inline__)) void
octeon_copy64(char *dst, const char *src, int offset)
{
	uint64_t a, b, c, d, e, f, g, h;

	__asm__ volatile(
	    "ld %0, %[off]+0(%[src])\n\t"
	    "ld %1, %[off]+8(%[src])\n\t"
	    "ld %2, %[off]+16(%[src])\n\t"
	    "ld %3, %[off]+24(%[src])\n\t"
	    "ld %4, %[off]+32(%[src])\n\t"
	    "ld %5, %[off]+40(%[src])\n\t"
	    "ld %6, %[off]+48(%[src])\n\t"
	    "ld %7, %[off]+56(%[src])\n\t"
	    "sd %0, %[off]+0(%[dst])\n\t"
	    "sd %1, %[off]+8(%[dst])\n\t"
	    "sd %2, %[off]+16(%[dst])\n\t"
	    "sd %3, %[off]+24(%[dst])\n\t"
	    "sd %4, %[off]+32(%[dst])\n\t"
	    "sd %5, %[off]+40(%[dst])\n\t"
	    "sd %6, %[off]+48(%[dst])\n\t"
	    "sd %7, %[off]+56(%[dst])"
	    : "=&r"(a), "=&r"(b), "=&r"(c), "=&r"(d),
	      "=&r"(e), "=&r"(f), "=&r"(g), "=&r"(h)
	    : [src] "r"(src), [dst] "r"(dst), [off] "i"(offset)
	    : "memory");
}
#endif

/*
 * Copy a block of memory, not handling overlap.
 */
void *
memcpy(void *dst0, const void *src0, size_t length)
{
	char *dst = dst0;
	const char *src = src0;
	size_t t;

	if (length == 0 || dst == src)		/* nothing to do */
		goto done;

	if ((dst < src && dst + length > src) ||
	    (src < dst && src + length > dst)) {
		char buf[1024];

		/* <10> is LOG_CRIT */
		strlcpy(buf, "<10>", sizeof buf);

		/* Make sure progname does not fill the whole buffer */
		strlcat(buf, __progname, sizeof(buf) - sizeof backwards_msg);
		strlcat(buf, backwards_msg, sizeof buf);

		sendsyslog(buf, strlen(buf), LOG_CONS);
		abort();
	}

	/*
	 * Macros: loop-t-times; and loop-t-times, t>0
	 */
#define	TLOOP(s) if (t) TLOOP1(s)
#define	TLOOP1(s) do { s; } while (--t)

	/*
	 * Copy forward.
	 */
	t = (long)src;	/* only need low bits */
	if ((t | (long)dst) & wmask) {
		/*
		 * Try to align operands.  This cannot be done
		 * unless the low bits match.
		 */
		if ((t ^ (long)dst) & wmask || length < wsize)
			t = length;
		else
			t = wsize - (t & wmask);
		length -= t;
		TLOOP1(*dst++ = *src++);
	}
#ifdef OCTEON_MEMCPY
	if (length >= OCTEON_PREFETCH_DISTANCE + OCTEON_COPY_BLOCK) {
		/* The generic alignment path has aligned both operands. */
		t = (-(uintptr_t)dst & (OCTEON_COPY_BLOCK - 1)) / wsize;
		length -= t * wsize;
		TLOOP(*(word *)dst = *(const word *)src;
		    src += wsize; dst += wsize);

		/*
		 * Prefetch the next source cache lines far enough ahead to
		 * cover a DRAM miss. Leave at least one full block in range
		 * beyond the prefetch address and use the generic tail.
		 */
		while (length >= OCTEON_PREFETCH_DISTANCE + OCTEON_COPY_BLOCK) {
			__asm__ volatile(
			    ".set push\n\t.set mips4\n\t"
			    "pref 4, %1(%0)\n\t.set pop"
			    : : "r"(src), "i"(OCTEON_PREFETCH_DISTANCE));
			octeon_copy64(dst, src, 0);
			octeon_copy64(dst, src, 64);
			src += OCTEON_COPY_BLOCK;
			dst += OCTEON_COPY_BLOCK;
			length -= OCTEON_COPY_BLOCK;
		}
	}
#endif
	/*
	 * Copy whole words, then mop up any trailing bytes.
	 */
	t = length / wsize;
	TLOOP(*(word *)dst = *(word *)src; src += wsize; dst += wsize);
	t = length & wmask;
	TLOOP(*dst++ = *src++);
done:
	return (dst0);
}
DEF_BUILTIN(memcpy);
