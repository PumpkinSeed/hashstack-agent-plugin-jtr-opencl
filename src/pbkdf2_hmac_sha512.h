/*
 * This code release under the following terms:
 * No copyright is claimed, and the software is hereby placed in the public domain.
 * In case this attempt to disclaim copyright and place the software in the public
 * domain is deemed null and void, then the software is Copyright (c) 2013 JimF
 * and it is hereby released to the general public under the following
 * terms: This software may be modified, redistributed, and used for any
 * purpose, in source and binary forms, with or without modification.
 *
 * This new code is 3x to 4x FASTER than the original oSSL code. Even though it is
 * only using oSSL functions.  A lot of the high level stuff in oSSL sux for speed.
 *
 * SSE2 intrinsic code, May, 2013, Jim Fougeron.
 */


#include <string.h>
#include "arch.h"
#include "sha2.h"
#include "stdint.h"
#include "johnswap.h"
#include "sse-intrinsics.h"

#ifndef SHA512_CBLOCK
#define SHA512_CBLOCK 128
#endif
#ifndef SHA512_DIGEST_LENGTH
#define SHA512_DIGEST_LENGTH 64
#endif

#if !defined(SIMD_COEF_64) || defined (PBKDF2_HMAC_SHA512_ALSO_INCLUDE_CTX)

static void _pbkdf2_sha512_load_hmac(const unsigned char *K, int KL, SHA512_CTX *pIpad, SHA512_CTX *pOpad) {
	unsigned char ipad[SHA512_CBLOCK], opad[SHA512_CBLOCK], k0[SHA512_DIGEST_LENGTH];
	unsigned i;

	memset(ipad, 0x36, SHA512_CBLOCK);
	memset(opad, 0x5C, SHA512_CBLOCK);

	if (KL > SHA512_CBLOCK) {
		SHA512_CTX ctx;
		SHA512_Init( &ctx );
		SHA512_Update( &ctx, K, KL);
		SHA512_Final( k0, &ctx);
		KL = SHA512_DIGEST_LENGTH;
		K = k0;
	}
	for(i = 0; i < KL; i++) {
		ipad[i] ^= K[i];
		opad[i] ^= K[i];
	}
	// save off the first 1/2 of the ipad/opad hashes.  We will NEVER recompute this
	// again, during the rounds, but reuse it. Saves 1/4 the SHA1's
	SHA512_Init(pIpad);
	SHA512_Update(pIpad, ipad, SHA512_CBLOCK);
	SHA512_Init(pOpad);
	SHA512_Update(pOpad, opad, SHA512_CBLOCK);
}

static void _pbkdf2_sha512(const unsigned char *S, int SL, int R, ARCH_WORD_64 *out,
	                     unsigned char loop, const SHA512_CTX *pIpad, const SHA512_CTX *pOpad) {
	SHA512_CTX ctx;
	unsigned char tmp_hash[SHA512_DIGEST_LENGTH];
	unsigned i, j;

	memcpy(&ctx, pIpad, sizeof(SHA512_CTX));
	SHA512_Update(&ctx, S, SL);
	// this 4 byte BE 'loop' appended to the salt
	SHA512_Update(&ctx, "\x0\x0\x0", 3);
	SHA512_Update(&ctx, &loop, 1);
	SHA512_Final(tmp_hash, &ctx);

	memcpy(&ctx, pOpad, sizeof(SHA512_CTX));
	SHA512_Update(&ctx, tmp_hash, SHA512_DIGEST_LENGTH);
	SHA512_Final(tmp_hash, &ctx);

	memcpy(out, tmp_hash, SHA512_DIGEST_LENGTH);

	for(i = 1; i < R; i++) {
#if !defined(COMMON_DIGEST_FOR_OPENSSL)
		memcpy(&ctx, pIpad, 80);
#if defined(__JTR_SHA2___H_)
		ctx.total = pIpad->total;
		ctx.bIs512 = pIpad->bIs512;
#else
		ctx.num = pIpad->num;
		ctx.md_len = pIpad->md_len;
#endif
#else
		memcpy(&ctx, pIpad, sizeof(SHA512_CTX));
#endif
		SHA512_Update(&ctx, tmp_hash, SHA512_DIGEST_LENGTH);
		SHA512_Final(tmp_hash, &ctx);

#if !defined(COMMON_DIGEST_FOR_OPENSSL)
		memcpy(&ctx, pOpad, 80);
#if defined(__JTR_SHA2___H_)
		ctx.total = pOpad->total;
		ctx.bIs512 = pOpad->bIs512;
#else
		ctx.num = pOpad->num;
		ctx.md_len = pOpad->md_len;
#endif
#else
		memcpy(&ctx, pOpad, sizeof(SHA512_CTX));
#endif
		SHA512_Update(&ctx, tmp_hash, SHA512_DIGEST_LENGTH);
		SHA512_Final(tmp_hash, &ctx);

		for(j = 0; j < SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64); j++)
			out[j] ^= ((ARCH_WORD_64*)tmp_hash)[j];
	}

}

static void pbkdf2_sha512(const unsigned char *K, int KL, unsigned char *S, int SL, int R, unsigned char *out, int outlen, int skip_bytes)
{
	union {
		ARCH_WORD_64 x64[SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64)];
		unsigned char out[SHA512_DIGEST_LENGTH];
	} tmp;
	int loop, loops, i, accum=0;
	SHA512_CTX ipad, opad;

	_pbkdf2_sha512_load_hmac(K, KL, &ipad, &opad);

	loops = (skip_bytes + outlen + (SHA512_DIGEST_LENGTH-1)) / SHA512_DIGEST_LENGTH;
	loop = skip_bytes / SHA512_DIGEST_LENGTH + 1;

	while (loop <= loops) {
		_pbkdf2_sha512(S,SL,R,tmp.x64,loop,&ipad,&opad);
		for (i = skip_bytes%SHA512_DIGEST_LENGTH; i < SHA512_DIGEST_LENGTH && accum < outlen; i++) {
#if ARCH_LITTLE_ENDIAN
			out[accum++] = ((uint8_t*)tmp.out)[i];
#else
			out[accum++] = ((uint8_t*)tmp.out)[i^7];
#endif
		}
		loop++;
		skip_bytes = 0;
	}
}

#endif

#ifdef SIMD_COEF_64

#ifndef __JTR_SHA2___H_
// we MUST call our sha2.c functions, to know the layout.  Since it is possible that apple's CommonCrypto lib could
// be used, vs just jts's sha2.c or oSSL, and CommonCrypt is NOT binary compatible, then we MUST use jtr's code here.
// To do that, I have the struture defined here (if the header was not included), and the 'real' functions declared here also.
typedef struct
{
	ARCH_WORD_64 h[8];          // SHA512 state
	ARCH_WORD_64 Nl,Nh;         // UNUSED but here to be compatible with oSSL
	unsigned char buffer[128];  // current/building data 'block'. It IS in alignment
	unsigned int num,md_len;    // UNUSED but here to be compatible with oSSL
	unsigned int total;         // number of bytes processed
	int bIs512;                 // if 1 SHA512, else SHA224
} sha512_ctx;
extern void sha512_init   (sha512_ctx *ctx, int bIs512);
extern void sha512_update (sha512_ctx *ctx, const void *input, int len);
extern void sha512_final  (void *output, sha512_ctx *ctx);
#endif


#if SIMD_PARA_SHA512
#define SSE_GROUP_SZ_SHA512 (SIMD_COEF_64*SIMD_PARA_SHA512)
#else
#define SSE_GROUP_SZ_SHA512 SIMD_COEF_64
#endif

static void _pbkdf2_sha512_sse_load_hmac(const unsigned char *K[SSE_GROUP_SZ_SHA512], int KL[SSE_GROUP_SZ_SHA512], SHA512_CTX pIpad[SSE_GROUP_SZ_SHA512], SHA512_CTX pOpad[SSE_GROUP_SZ_SHA512])
{
	unsigned char ipad[SHA512_CBLOCK], opad[SHA512_CBLOCK], k0[SHA512_DIGEST_LENGTH];
	int i, j;

	for (j = 0; j < SSE_GROUP_SZ_SHA512; ++j) {
		memset(ipad, 0x36, SHA512_CBLOCK);
		memset(opad, 0x5C, SHA512_CBLOCK);

		if (KL[j] > SHA512_CBLOCK) {
			SHA512_CTX ctx;
			SHA512_Init( &ctx );
			SHA512_Update( &ctx, K[j], KL[j]);
			SHA512_Final( k0, &ctx);
			KL[j] = SHA512_DIGEST_LENGTH;
			K[j] = k0;
		}
		for(i = 0; i < KL[j]; i++) {
			ipad[i] ^= K[j][i];
			opad[i] ^= K[j][i];
		}
		// save off the first 1/2 of the ipad/opad hashes.  We will NEVER recompute this
		// again, during the rounds, but reuse it. Saves 1/4 the SHA512's
		SHA512_Init(&(pIpad[j]));
		SHA512_Update(&(pIpad[j]), ipad, SHA512_CBLOCK);
		SHA512_Init(&(pOpad[j]));
		SHA512_Update(&(pOpad[j]), opad, SHA512_CBLOCK);
	}
}

static void pbkdf2_sha512_sse(const unsigned char *K[SIMD_COEF_64], int KL[SIMD_COEF_64], unsigned char *S, int SL, int R, unsigned char *out[SIMD_COEF_64], int outlen, int skip_bytes)
{
	unsigned char tmp_hash[SHA512_DIGEST_LENGTH];
	ARCH_WORD_64 *i1, *i2, *o1, *ptmp;
	unsigned int i, j;
	ARCH_WORD_64 dgst[SSE_GROUP_SZ_SHA512][SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64)];
	int loops, accum=0;
	unsigned char loop;
	SHA512_CTX ipad[SSE_GROUP_SZ_SHA512], opad[SSE_GROUP_SZ_SHA512], ctx;

	// sse_hash1 would need to be 'adjusted' for SHA512_PARA
	JTR_ALIGN(MEM_ALIGN_SIMD) unsigned char sse_hash1[SHA512_BUF_SIZ*sizeof(ARCH_WORD_64)*SSE_GROUP_SZ_SHA512];
	JTR_ALIGN(MEM_ALIGN_SIMD) unsigned char sse_crypt1[SHA512_DIGEST_LENGTH*SSE_GROUP_SZ_SHA512];
	JTR_ALIGN(MEM_ALIGN_SIMD) unsigned char sse_crypt2[SHA512_DIGEST_LENGTH*SSE_GROUP_SZ_SHA512];
	i1 = (ARCH_WORD_64*)sse_crypt1;
	i2 = (ARCH_WORD_64*)sse_crypt2;
	o1 = (ARCH_WORD_64*)sse_hash1;

	// we need to set ONE time, the upper half of the data buffer.  We put the 0x80 byte (in BE format), at offset 64,
	// then zero out the rest of the buffer, putting 0x300 (#bits), into the proper location in the buffer.  Once this
	// part of the buffer is setup, we never touch it again, for the rest of the crypt.  We simply overwrite the first
	// half of this buffer, over and over again, with BE results of the prior hash.
	for (j = 0; j < SSE_GROUP_SZ_SHA512/SIMD_COEF_64; ++j) {
		ptmp = &o1[j*SIMD_COEF_64*SHA512_BUF_SIZ];
		for (i = 0; i < SIMD_COEF_64; ++i)
			ptmp[ (SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64))*SIMD_COEF_64 + (i&(SIMD_COEF_64-1))] = 0x8000000000000000ULL;
		for (i = (SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64)+1)*SIMD_COEF_64; i < 15*SIMD_COEF_64; ++i)
			ptmp[i] = 0;
		for (i = 0; i < SIMD_COEF_64; ++i)
			ptmp[15*SIMD_COEF_64 + (i&(SIMD_COEF_64-1))] = ((128+SHA512_DIGEST_LENGTH)<<3); // all encrypts are 128+64 bytes.
	}

	// Load up the IPAD and OPAD values, saving off the first half of the crypt.  We then push the ipad/opad all
	// the way to the end, and that ends up being the first iteration of the pbkdf2.  From that point on, we use
	// the 2 first halves, to load the sha512 2nd part of each crypt, in each loop.
	_pbkdf2_sha512_sse_load_hmac(K, KL, ipad, opad);
	for (j = 0; j < SSE_GROUP_SZ_SHA512; ++j) {
		ptmp = &i1[(j/SIMD_COEF_64)*SIMD_COEF_64*(SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64))+(j&(SIMD_COEF_64-1))];
		for (i = 0; i < (SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64)); ++i) {
#if COMMON_DIGEST_FOR_OPENSSL
			*ptmp = ipad[j].hash[i];
#else
			*ptmp = ipad[j].h[i];
#endif
			ptmp += SIMD_COEF_64;
		}
		ptmp = &i2[(j/SIMD_COEF_64)*SIMD_COEF_64*(SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64))+(j&(SIMD_COEF_64-1))];
		for (i = 0; i < (SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64)); ++i) {
#if COMMON_DIGEST_FOR_OPENSSL
			*ptmp = opad[j].hash[i];
#else
			*ptmp = opad[j].h[i];
#endif
			ptmp += SIMD_COEF_64;
		}
	}

	loops = (skip_bytes + outlen + (SHA512_DIGEST_LENGTH-1)) / SHA512_DIGEST_LENGTH;
	loop = skip_bytes / SHA512_DIGEST_LENGTH + 1;
	while (loop <= loops) {
		for (j = 0; j < SSE_GROUP_SZ_SHA512; ++j) {
			memcpy(&ctx, &ipad[j], sizeof(ctx));
			SHA512_Update(&ctx, S, SL);
			// this BE 1 appended to the salt, allows us to do passwords up
			// to and including 128 bytes long.  If we wanted longer passwords,
			// then we would have to call the HMAC multiple times (with the
			// rounds between, but each chunk of password we would use a larger
			// BE number appended to the salt. The first roung (64 byte pw), and
			// we simply append the first number (0001 in BE)
			SHA512_Update(&ctx, "\x0\x0\x0", 3);
			SHA512_Update(&ctx, &loop, 1);
			SHA512_Final(tmp_hash, &ctx);

			memcpy(&ctx, &opad[j], sizeof(ctx));
			SHA512_Update(&ctx, tmp_hash, SHA512_DIGEST_LENGTH);
			SHA512_Final(tmp_hash, &ctx);

			// now convert this from flat into SIMD_COEF_64 buffers.
			// Also, perform the 'first' ^= into the crypt buffer.  NOTE, we are doing that in BE format
			// so we will need to 'undo' that in the end.
			ptmp = &o1[(j/SIMD_COEF_64)*SIMD_COEF_64*SHA512_BUF_SIZ+(j&(SIMD_COEF_64-1))];
			for (i = 0; i < (SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64)); ++i) {
#if COMMON_DIGEST_FOR_OPENSSL
				*ptmp = dgst[j][i] = ctx.hash[i];
#else
				*ptmp = dgst[j][i] = ctx.h[i];
#endif
				ptmp += SIMD_COEF_64;
			}
		}

		// Here is the inner loop.  We loop from 1 to count.  iteration 0 was done in the ipad/opad computation.
		for(i = 1; i < R; i++) {
			unsigned int k;
			SSESHA512body(o1,o1,i1, SSEi_MIXED_IN|SSEi_RELOAD|SSEi_OUTPUT_AS_INP_FMT);
			SSESHA512body(o1,o1,i2, SSEi_MIXED_IN|SSEi_RELOAD|SSEi_OUTPUT_AS_INP_FMT);
			// only xor first 16 bytes, since that is ALL this format uses
			for (k = 0; k < SSE_GROUP_SZ_SHA512; k++) {
				ARCH_WORD_64 *p = &o1[(k/SIMD_COEF_64)*SIMD_COEF_64*SHA512_BUF_SIZ + (k&(SIMD_COEF_64-1))];
				for(j = 0; j < (SHA512_DIGEST_LENGTH/sizeof(ARCH_WORD_64)); j++)
					dgst[k][j] ^= p[j*SIMD_COEF_64];
			}
		}

		// we must fixup final results.  We have been working in BE (NOT switching out of, just to switch back into it at every loop).
		// for the 'very' end of the crypt, we remove BE logic, so the calling function can view it in native format.
		alter_endianity_to_BE64(dgst, sizeof(dgst)/8);
		for (i = skip_bytes%SHA512_DIGEST_LENGTH; i < SHA512_DIGEST_LENGTH && accum < outlen; ++i) {
			for (j = 0; j < SSE_GROUP_SZ_SHA512; ++j) {
				out[j][accum] = ((unsigned char*)(dgst[j]))[i];
			}
			++accum;
		}
		++loop;
		skip_bytes = 0;
	}
}

#endif
