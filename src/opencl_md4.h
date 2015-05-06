/*
 * OpenCL MD4
 *
 * Copyright (c) 2014, magnum
 * This software is hereby released to the general public under
 * the following terms: Redistribution and use in source and binary
 * forms, with or without modification, are permitted.
 *
 * NOTICE: After changes in headers, you probably need to drop cached
 * kernels to ensure the changes take effect.
 *
 */

#ifndef _OPENCL_MD4_H
#define _OPENCL_MD4_H

#include "opencl_misc.h"

#ifdef USE_BITSELECT
#define MD4_F(x, y, z)	bitselect((z), (y), (x))
#else
#define MD4_F(x, y, z)	((z) ^ ((x) & ((y) ^ (z))))
#endif

#define MD4_H(x, y, z)	(((x) ^ (y)) ^ (z))
#define MD4_H2(x, y, z)	((x) ^ ((y) ^ (z)))


/* The basic MD4 functions */
#define MD4_G(x, y, z)	(((x) & ((y) | (z))) | ((y) & (z)))


/* The MD4 transformation for all three rounds. */
#define MD4STEP(f, a, b, c, d, x, s)  \
	(a) += f((b), (c), (d)) + (x); \
	    (a) = rotate((a), (uint)(s))

#define MD4(a, b, c, d, W)	  \
	MD4STEP(MD4_F, a, b, c, d, W[0], 3); \
	MD4STEP(MD4_F, d, a, b, c, W[1], 7); \
	MD4STEP(MD4_F, c, d, a, b, W[2], 11); \
	MD4STEP(MD4_F, b, c, d, a, W[3], 19); \
	MD4STEP(MD4_F, a, b, c, d, W[4], 3); \
	MD4STEP(MD4_F, d, a, b, c, W[5], 7); \
	MD4STEP(MD4_F, c, d, a, b, W[6], 11); \
	MD4STEP(MD4_F, b, c, d, a, W[7], 19); \
	MD4STEP(MD4_F, a, b, c, d, W[8], 3); \
	MD4STEP(MD4_F, d, a, b, c, W[9], 7); \
	MD4STEP(MD4_F, c, d, a, b, W[10], 11); \
	MD4STEP(MD4_F, b, c, d, a, W[11], 19); \
	MD4STEP(MD4_F, a, b, c, d, W[12], 3); \
	MD4STEP(MD4_F, d, a, b, c, W[13], 7); \
	MD4STEP(MD4_F, c, d, a, b, W[14], 11); \
	MD4STEP(MD4_F, b, c, d, a, W[15], 19); \
	MD4STEP(MD4_G, a, b, c, d, W[0] + 0x5a827999, 3); \
	MD4STEP(MD4_G, d, a, b, c, W[4] + 0x5a827999, 5); \
	MD4STEP(MD4_G, c, d, a, b, W[8] + 0x5a827999, 9); \
	MD4STEP(MD4_G, b, c, d, a, W[12] + 0x5a827999, 13); \
	MD4STEP(MD4_G, a, b, c, d, W[1] + 0x5a827999, 3); \
	MD4STEP(MD4_G, d, a, b, c, W[5] + 0x5a827999, 5); \
	MD4STEP(MD4_G, c, d, a, b, W[9] + 0x5a827999, 9); \
	MD4STEP(MD4_G, b, c, d, a, W[13] + 0x5a827999, 13); \
	MD4STEP(MD4_G, a, b, c, d, W[2] + 0x5a827999, 3); \
	MD4STEP(MD4_G, d, a, b, c, W[6] + 0x5a827999, 5); \
	MD4STEP(MD4_G, c, d, a, b, W[10] + 0x5a827999, 9); \
	MD4STEP(MD4_G, b, c, d, a, W[14] + 0x5a827999, 13); \
	MD4STEP(MD4_G, a, b, c, d, W[3] + 0x5a827999, 3); \
	MD4STEP(MD4_G, d, a, b, c, W[7] + 0x5a827999, 5); \
	MD4STEP(MD4_G, c, d, a, b, W[11] + 0x5a827999, 9); \
	MD4STEP(MD4_G, b, c, d, a, W[15] + 0x5a827999, 13); \
	MD4STEP(MD4_H, a, b, c, d, W[0] + 0x6ed9eba1, 3); \
	MD4STEP(MD4_H2, d, a, b, c, W[8] + 0x6ed9eba1, 9); \
	MD4STEP(MD4_H, c, d, a, b, W[4] + 0x6ed9eba1, 11); \
	MD4STEP(MD4_H2, b, c, d, a, W[12] + 0x6ed9eba1, 15); \
	MD4STEP(MD4_H, a, b, c, d, W[2] + 0x6ed9eba1, 3); \
	MD4STEP(MD4_H2, d, a, b, c, W[10] + 0x6ed9eba1, 9); \
	MD4STEP(MD4_H, c, d, a, b, W[6] + 0x6ed9eba1, 11); \
	MD4STEP(MD4_H2, b, c, d, a, W[14] + 0x6ed9eba1, 15); \
	MD4STEP(MD4_H, a, b, c, d, W[1] + 0x6ed9eba1, 3); \
	MD4STEP(MD4_H2, d, a, b, c, W[9] + 0x6ed9eba1, 9); \
	MD4STEP(MD4_H, c, d, a, b, W[5] + 0x6ed9eba1, 11); \
	MD4STEP(MD4_H2, b, c, d, a, W[13] + 0x6ed9eba1, 15); \
	MD4STEP(MD4_H, a, b, c, d, W[3] + 0x6ed9eba1, 3); \
	MD4STEP(MD4_H2, d, a, b, c, W[11] + 0x6ed9eba1, 9); \
	MD4STEP(MD4_H, c, d, a, b, W[7] + 0x6ed9eba1, 11); \
	MD4STEP(MD4_H2, b, c, d, a, W[15] + 0x6ed9eba1, 15);

/*
 * Raw'n'lean MD4 with context in output buffer
 * NOTE: This version thrashes the input block!
 *
 * Needs caller to have these defined:
 *	uint a, b, c, d;
 *
 * or perhaps:
 *	MAYBE_VECTOR_UINT a, b, c, d;
 *
 */
#define	md4_block(W, ctx) { \
		a = ctx[0]; \
		b = ctx[1]; \
		c = ctx[2]; \
		d = ctx[3]; \
		MD4(a, b, c, d, W); \
		ctx[0] += a; \
		ctx[1] += b; \
		ctx[2] += c; \
		ctx[3] += d; \
	}

#define md4_init(ctx) {	  \
		ctx[0] = 0x67452301; \
		ctx[1] = 0xefcdab89; \
		ctx[2] = 0x98badcfe; \
		ctx[3] = 0x10325476; \
	}

#define	md4_single(W, out) { \
		a = 0x67452301; \
		b = 0xefcdab89; \
		c = 0x98badcfe; \
		d = 0x10325476; \
		MD4(a, b, c, d, W); \
		out[0] = 0x67452301 + a; \
		out[1] = 0xefcdab89 + b; \
		out[2] = 0x98badcfe + c; \
		out[3] = 0x10325476 + d; \
	}

#endif
