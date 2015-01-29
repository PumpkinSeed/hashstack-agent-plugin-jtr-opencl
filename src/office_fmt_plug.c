/* Office 2007 cracker patch for JtR. Hacked together during March of 2012 by
 * Dhiru Kholia <dhiru.kholia at gmail.com> */

#if FMT_EXTERNS_H
extern struct fmt_main fmt_office;
#elif FMT_REGISTERS_H
john_register_one(&fmt_office);
#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <openssl/aes.h>
#ifdef _OPENMP
#include <omp.h>
#define OMP_SCALE               4
#endif

#include "arch.h"
#include "misc.h"
#include "common.h"
#include "formats.h"
#include "params.h"
#include "options.h"
#include "unicode.h"
#include "sha.h"
#include "sha2.h"
#include "johnswap.h"
#include "office_common.h"
#include "memdbg.h"

#define FORMAT_LABEL		"Office"
#define FORMAT_NAME		"2007/2010 (SHA-1) / 2013 (SHA-512), with AES"
#define ALGORITHM_NAME		"32/" ARCH_BITS_STR " " SHA2_LIB
#define BENCHMARK_COMMENT	""
#define BENCHMARK_LENGTH	-1
#define PLAINTEXT_LENGTH	32
#define BINARY_SIZE		16
#define SALT_SIZE		sizeof(*cur_salt)
#define BINARY_ALIGN	4
#define SALT_ALIGN	sizeof(int)
#define MIN_KEYS_PER_CRYPT	1
#define MAX_KEYS_PER_CRYPT	1

#undef MIN
#define MIN(a, b)		(((a) > (b)) ? (b) : (a))

static struct fmt_tests office_tests[] = {
	{"$office$*2007*20*128*16*8b2c9e8c878844fc842012273be4bea8*aa862168b80d8c45c852696a8bb499eb*a413507fabe2d87606595f987f679ff4b5b4c2cd", "Password"},
	/* 2007-Default_myhovercraftisfullofeels_.docx */
	{"$office$*2007*20*128*16*91f095a1fd02595359fe3938fa9236fd*e22668eb1347957987175079e980990f*659f50b9062d36999bf3d0911068c93268ae1d86", "myhovercraftisfullofeels"},
	/* 2007-Default_myhovercraftisfullofeels_.dotx */
	{"$office$*2007*20*128*16*56ea65016fbb4eac14a6770b2dbe7e99*8cf82ce1b62f01fd3b2c7666a2313302*21443fe938177e648c482da72212a8848c2e9c80", "myhovercraftisfullofeels"},
	/* 2007-Default_myhovercraftisfullofeels_.xlsb */
	{"$office$*2007*20*128*16*fbd4cc5dab9b8e341778ddcde9eca740*3a040a9cef3d3675009b22f99718e39c*48053b27e95fa53b3597d48ca4ad41eec382e0c8", "myhovercraftisfullofeels"},
	/* 2007-Default_myhovercraftisfullofeels_.xlsm */
	{"$office$*2007*20*128*16*fbd4cc5dab9b8e341778ddcde9eca740*92bb2ef34ca662ca8a26c8e2105b05c0*0261ba08cd36a324aa1a70b3908a24e7b5a89dd6", "myhovercraftisfullofeels"},
	/* 2007-Default_myhovercraftisfullofeels_.xlsx */
	{"$office$*2007*20*128*16*fbd4cc5dab9b8e341778ddcde9eca740*46bef371486919d4bffe7280110f913d*b51af42e6696baa097a7109cebc3d0ff7cc8b1d8", "myhovercraftisfullofeels"},
	/* 2007-Default_myhovercraftisfullofeels_.xltx */
	{"$office$*2007*20*128*16*fbd4cc5dab9b8e341778ddcde9eca740*1addb6823689aca9ce400be8f9e55fc9*e06bf10aaf3a4049ffa49dd91cf9e7bbf88a1b3b", "myhovercraftisfullofeels"},
	/* 2010-Default_myhovercraftisfullofeels_.docx */
	{"$office$*2010*100000*128*16*213aefcafd9f9188e78c1936cbb05a44*d5fc7691292ab6daf7903b9a8f8c8441*46bfac7fb87cd43bd0ab54ebc21c120df5fab7e6f11375e79ee044e663641d5e", "myhovercraftisfullofeels"},
	/* 2010-Default_myhovercraftisfullofeels_.dotx */
	{"$office$*2010*100000*128*16*0907ec6ecf82ede273b7ee87e44f4ce5*d156501661638cfa3abdb7fdae05555e*4e4b64e12b23f44d9a8e2e00196e582b2da70e5e1ab4784384ad631000a5097a", "myhovercraftisfullofeels"},
	/* 2010-Default_myhovercraftisfullofeels_.xlsb */
	{"$office$*2010*100000*128*16*71093d08cf950f8e8397b8708de27c1f*00780eeb9605c7e27227c5619e91dc21*90aaf0ea5ccc508e699de7d62c310f94b6798ae77632be0fc1a0dc71600dac38", "myhovercraftisfullofeels"},
	/* 2010-Default_myhovercraftisfullofeels_.xlsx */
	{"$office$*2010*100000*128*16*71093d08cf950f8e8397b8708de27c1f*ef51883a775075f30d2207e87987e6a3*a867f87ea955d15d8cb08dc8980c04bf564f8af060ab61bf7fa3543853e0d11a", "myhovercraftisfullofeels"},
	/* 2013-openwall.pptx */
	{"$office$*2013*100000*256*16*9b12805dd6d56f46d07315153f3ecb9c*c5a4a167b51faa6629f6a4caf0b4baa8*87397e0659b2a6fff90291f8e6d6d0018b750b792fefed77001edbafba7769cd", "openwall"},
	/* 365-2013-openwall.docx */
	{"$office$*2013*100000*256*16*774a174239a7495a59cac39a122d991c*b2f9197840f9e5d013f95a3797708e83*ecfc6d24808691aac0daeaeba72aba314d72c6bbd12f7ff0ea1a33770187caef", "openwall"},
	/* 365-2013-password.docx */
	{"$office$*2013*100000*256*16*d4fc9302eedabf9872b24ca700a5258b*7c9554d582520747ec3e872f109a7026*1af5b5024f00e35eaf5fd8148b410b57e7451a32898acaf14275a8c119c3a4fd", "password"},
	/* 365-2013-password.xlsx */
	{"$office$*2013*100000*256*16*59b49c64c0d29de733f0025837327d50*70acc7946646ea300fc13cfe3bd751e2*627c8bdb7d9846228aaea81eeed434d022bb93bb5f4da146cb3ad9d847de9ec9", "password"},
	/* 365-2013-strict-password.docx */
	{"$office$*2013*100000*256*16*f1c23049d85876e6b20e95ab86a477f1*13303dbd27a38ea86ef11f1b2bc56225*9a69596de0655a6c6a5b2dc4b24d6e713e307fb70af2d6b67b566173e89f941d", "password"},
	{NULL}
};

static ms_office_custom_salt *cur_salt;

#define MS_OFFICE_2007_ITERATIONS	50000

#if defined (_OPENMP)
static int omp_t = 1;
#endif
/* Password encoded in UCS-2 */
static UTF16 (*saved_key)[PLAINTEXT_LENGTH + 1];
/* UCS-2 password length, in octets */
static int *saved_len;
static ARCH_WORD_32 (*crypt_key)[4];
static int *cracked;

/* Office 2010/2013 */
static const unsigned char encryptedVerifierHashInputBlockKey[] = { 0xfe, 0xa7, 0xd2, 0x76, 0x3b, 0x4b, 0x9e, 0x79 };
static const unsigned char encryptedVerifierHashValueBlockKey[] = { 0xd7, 0xaa, 0x0f, 0x6d, 0x30, 0x61, 0x34, 0x4e };

static unsigned char *DeriveKey(unsigned char *hashValue, unsigned char *X1)
{
	int i;
	unsigned char derivedKey[64];
	SHA_CTX ctx;

	// This is step 4a in 2.3.4.7 of MS_OFFCRYPT version 1.0
	// and is required even though the notes say it should be
	// used only when the encryption algorithm key > hash length.
	for (i = 0; i < 64; i++)
		derivedKey[i] = (i < 20 ? 0x36 ^ hashValue[i] : 0x36);

	SHA1_Init(&ctx);
	SHA1_Update(&ctx, derivedKey, 64);
	SHA1_Final(X1, &ctx);

	if (cur_salt->verifierHashSize > cur_salt->keySize/8)
		return X1;

	/* TODO: finish up this function */
	//for (i = 0; i < 64; i++)
	//	derivedKey[i] = (i < 30 ? 0x5C ^ hashValue[i] : 0x5C);

	fprintf(stderr, "\n\n*** ERROR: DeriveKey() entered Limbo.\n");
	fprintf(stderr, "Please report to john-dev mailing list.\n");
	error();

	return NULL;
}

static unsigned char* GeneratePasswordHashUsingSHA1(UTF16 *passwordBuf, int passwordBufSize, unsigned char *final)
{
	unsigned char hashBuf[20], *key;
	/* H(0) = H(salt, password)
	 * hashBuf = SHA1Hash(salt, password);
	 * create input buffer for SHA1 from salt and unicode version of password */
	unsigned int inputBuf[(0x14 + 0x04 + 4) / sizeof(int)];
	unsigned char X1[20];
	int i;
	SHA_CTX ctx;

	SHA1_Init(&ctx);
	SHA1_Update(&ctx, cur_salt->osalt, cur_salt->saltSize);
	SHA1_Update(&ctx, passwordBuf, passwordBufSize);
	SHA1_Final(hashBuf, &ctx);

	/* Generate each hash in turn
	 * H(n) = H(i, H(n-1))
	 * hashBuf = SHA1Hash(i, hashBuf); */

	// Create a byte array of the integer and put at the front of the input buffer
	// 1.3.6 says that little-endian byte ordering is expected
	memcpy(&inputBuf[1], hashBuf, 20);
	for (i = 0; i < MS_OFFICE_2007_ITERATIONS; i++) {
#if ARCH_LITTLE_ENDIAN
		*inputBuf = i;
#else
		*inputBuf = JOHNSWAP(i);
#endif
		// 'append' the previously generated hash to the input buffer
		SHA1_Init(&ctx);
		SHA1_Update(&ctx, inputBuf, 0x14 + 0x04);
		SHA1_Final((unsigned char*)&inputBuf[1], &ctx);
	}
	// Finally, append "block" (0) to H(n)
	// hashBuf = SHA1Hash(hashBuf, 0);
	memset(&inputBuf[6], 0, 4);
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, &inputBuf[1], 0x14 + 0x04);
	SHA1_Final(hashBuf, &ctx);

	key = DeriveKey(hashBuf, X1);

	// Should handle the case of longer key lengths as shown in 2.3.4.9
	// Grab the key length bytes of the final hash as the encrypytion key
	memcpy(final, key, cur_salt->keySize/8);

	return final;
}

static void GenerateAgileEncryptionKey(UTF16 *passwordBuf, int passwordBufSize, int hashSize, unsigned char *hashBuf)
{
	/* H(0) = H(salt, password)
	 * hashBuf = SHA1Hash(salt, password);
	 * create input buffer for SHA1 from salt and unicode version of password */
	unsigned int inputBuf[(28 + 4) / sizeof(int)];
	int i;
	SHA_CTX ctx;

	SHA1_Init(&ctx);
	SHA1_Update(&ctx, cur_salt->osalt, cur_salt->saltSize);
	SHA1_Update(&ctx, passwordBuf, passwordBufSize);
	SHA1_Final(hashBuf, &ctx);

	/* Generate each hash in turn
	 * H(n) = H(i, H(n-1))
	 * hashBuf = SHA1Hash(i, hashBuf); */

	// Create a byte array of the integer and put at the front of the input buffer
	// 1.3.6 says that little-endian byte ordering is expected
	memcpy(&inputBuf[1], hashBuf, 20);
	for (i = 0; i < cur_salt->spinCount; i++) {
#if ARCH_LITTLE_ENDIAN
		*inputBuf = i;
#else
		*inputBuf = JOHNSWAP(i);
#endif
		// 'append' the previously generated hash to the input buffer
		SHA1_Init(&ctx);
		SHA1_Update(&ctx, inputBuf, 0x14 + 0x04);
		SHA1_Final((unsigned char*)&inputBuf[1], &ctx);
	}
	// Finally, append "block" (0) to H(n)
	memcpy(&inputBuf[6], encryptedVerifierHashInputBlockKey, 8);
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, &inputBuf[1], 28);
	SHA1_Final(hashBuf, &ctx);

	// And second "block" (0) to H(n)
	memcpy(&inputBuf[6], encryptedVerifierHashValueBlockKey, 8);
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, &inputBuf[1], 28);
	SHA1_Final(&hashBuf[32], &ctx);

	// Fix up the size per the spec
	if (20 < hashSize) { // FIXME: Is this ever true?
		for(i = 20; i < hashSize; i++) {
			hashBuf[i] = 0x36;
			hashBuf[32 + i] = 0x36;
		}
	}
}

static void GenerateAgileEncryptionKey512(UTF16 *passwordBuf, int passwordBufSize, unsigned char *hashBuf)
{
	unsigned int inputBuf[128 / sizeof(int)];
	int i;
	SHA512_CTX ctx;

	SHA512_Init(&ctx);
	SHA512_Update(&ctx, cur_salt->osalt, cur_salt->saltSize);
	SHA512_Update(&ctx, passwordBuf, passwordBufSize);
	SHA512_Final(hashBuf, &ctx);

	// Create a byte array of the integer and put at the front of the input buffer
	// 1.3.6 says that little-endian byte ordering is expected
	memcpy(&inputBuf[1], hashBuf, 64);
	for (i = 0; i < cur_salt->spinCount; i++) {
#if ARCH_LITTLE_ENDIAN
		*inputBuf = i;
#else
		*inputBuf = JOHNSWAP(i);
#endif
		// 'append' the previously generated hash to the input buffer
		SHA512_Init(&ctx);
		SHA512_Update(&ctx, inputBuf, 64 + 0x04);
		SHA512_Final((unsigned char*)&inputBuf[1], &ctx);
	}
	// Finally, append "block" (0) to H(n)
	memcpy(&inputBuf[68/4], encryptedVerifierHashInputBlockKey, 8);
	SHA512_Init(&ctx);
	SHA512_Update(&ctx, &inputBuf[1], 64 + 8);
	SHA512_Final(hashBuf, &ctx);

	// And second "block" (0) to H(n)
	memcpy(&inputBuf[68/4], encryptedVerifierHashValueBlockKey, 8);
	SHA512_Init(&ctx);
	SHA512_Update(&ctx, &inputBuf[1], 64 + 8);
	SHA512_Final(&hashBuf[64], &ctx);
}

static void init(struct fmt_main *self)
{
#if defined (_OPENMP)
	omp_t = omp_get_max_threads();
	self->params.min_keys_per_crypt *= omp_t;
	omp_t *= OMP_SCALE;
	self->params.max_keys_per_crypt *= omp_t;
#endif
	saved_key = mem_calloc_tiny(sizeof(*saved_key) *
	                            self->params.max_keys_per_crypt, sizeof(UTF16));
	saved_len = mem_calloc_tiny(sizeof(*saved_len) *
	                            self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	crypt_key = mem_calloc_tiny(sizeof(*crypt_key) *
			self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	cracked = mem_calloc_tiny(sizeof(*cracked) *
			self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	if (pers_opts.target_enc == UTF_8)
		self->params.plaintext_length = MIN(125, PLAINTEXT_LENGTH * 3);
}

static void set_salt(void *salt)
{
	cur_salt = (ms_office_custom_salt *)salt;
}

static int crypt_all(int *pcount, struct db_salt *salt)
{
	int count = *pcount;
	int index = 0;

#ifdef _OPENMP
#pragma omp parallel for
	for (index = 0; index < count; index++)
#endif
	{
		if(cur_salt->version == 2007) {
			unsigned char encryptionKey[256];
			GeneratePasswordHashUsingSHA1(saved_key[index], saved_len[index], encryptionKey);
			ms_office_common_PasswordVerifier(cur_salt, encryptionKey, crypt_key[index]);
		}
		else if (cur_salt->version == 2010) {
			unsigned char verifierKeys[64], decryptedVerifierHashInputBytes[16], decryptedVerifierHashBytes[32];
			unsigned char hash[20];
			SHA_CTX ctx;
			GenerateAgileEncryptionKey(saved_key[index], saved_len[index], cur_salt->keySize >> 3, verifierKeys);
			ms_office_common_DecryptUsingSymmetricKeyAlgorithm(cur_salt, verifierKeys, cur_salt->encryptedVerifier, decryptedVerifierHashInputBytes, 16);
			ms_office_common_DecryptUsingSymmetricKeyAlgorithm(cur_salt, &verifierKeys[32], cur_salt->encryptedVerifierHash, decryptedVerifierHashBytes, 32);
			SHA1_Init(&ctx);
			SHA1_Update(&ctx, decryptedVerifierHashInputBytes, 16);
			SHA1_Final(hash, &ctx);
			cracked[index] = !memcmp(hash, decryptedVerifierHashBytes, 20);
		}
		else if (cur_salt->version == 2013) {
			unsigned char verifierKeys[128], decryptedVerifierHashInputBytes[16], decryptedVerifierHashBytes[32];
			unsigned char hash[64];
			SHA512_CTX ctx;
			GenerateAgileEncryptionKey512(saved_key[index], saved_len[index], verifierKeys);
			ms_office_common_DecryptUsingSymmetricKeyAlgorithm(cur_salt, verifierKeys, cur_salt->encryptedVerifier, decryptedVerifierHashInputBytes, 16);
			ms_office_common_DecryptUsingSymmetricKeyAlgorithm(cur_salt, &verifierKeys[64], cur_salt->encryptedVerifierHash, decryptedVerifierHashBytes, 32);
			SHA512_Init(&ctx);
			SHA512_Update(&ctx, decryptedVerifierHashInputBytes, 16);
			SHA512_Final(hash, &ctx);
			cracked[index] = !memcmp(hash, decryptedVerifierHashBytes, 20);
		}
	}
	return count;
}

static int cmp_all(void *binary, int count)
{
	int index;
	if (cur_salt->version == 2007) {
		for (index = 0; index < count; index++) {
			if ( ((ARCH_WORD_32*)binary)[0] == crypt_key[index][0] )
				return 1;
		}
		return 0;
	}
	for (index = 0; index < count; index++)
		if (cracked[index])
			return 1;
	return 0;
}

static int cmp_one(void *binary, int index)
{
	if (cur_salt->version == 2007) {
		return !memcmp(binary, crypt_key[index], BINARY_SIZE);
	}
	return cracked[index];
}

static int cmp_exact(char *source, int index)
{
	return 1;
}

static int get_hash_0(int index) { if (cur_salt->version!=2007) return 0; return crypt_key[index][0] & 0xf; }
static int get_hash_1(int index) { if (cur_salt->version!=2007) return 0; return crypt_key[index][0] & 0xff; }
static int get_hash_2(int index) { if (cur_salt->version!=2007) return 0; return crypt_key[index][0] & 0xfff; }
static int get_hash_3(int index) { if (cur_salt->version!=2007) return 0; return crypt_key[index][0] & 0xffff; }
static int get_hash_4(int index) { if (cur_salt->version!=2007) return 0; return crypt_key[index][0] & 0xfffff; }
static int get_hash_5(int index) { if (cur_salt->version!=2007) return 0; return crypt_key[index][0] & 0xffffff; }
static int get_hash_6(int index) { if (cur_salt->version!=2007) return 0; return crypt_key[index][0] & 0x7ffffff; }

static void office_set_key(char *key, int index)
{
	/* convert key to UTF-16LE */
	saved_len[index] = enc_to_utf16(saved_key[index], PLAINTEXT_LENGTH, (UTF8*)key, strlen(key));
	if (saved_len[index] < 0)
		saved_len[index] = strlen16(saved_key[index]);
	saved_len[index] <<= 1;
}

static char *get_key(int index)
{
	return (char*)utf16_to_enc(saved_key[index]);
}
#if FMT_MAIN_VERSION > 11
/*
 * MS Office version (2007, 2010, 2013) as first tunable cost
 */
static unsigned int ms_office_version(void *salt)
{
	ms_office_custom_salt *my_salt;

	my_salt = salt;
	return (unsigned int) my_salt->version;
}
#endif

struct fmt_main fmt_office = {
	{
		FORMAT_LABEL,
		FORMAT_NAME,
		ALGORITHM_NAME,
		BENCHMARK_COMMENT,
		BENCHMARK_LENGTH,
		0,
		PLAINTEXT_LENGTH,
		BINARY_SIZE,
		BINARY_ALIGN,
		SALT_SIZE,
		SALT_ALIGN,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
		FMT_CASE | FMT_8_BIT | FMT_OMP | FMT_UNICODE | FMT_UTF8,
#if FMT_MAIN_VERSION > 11
		{
			"MS Office version",
			"iteration count",
		},
#endif
		office_tests
	}, {
		init,
		fmt_default_done,
		fmt_default_reset,
		fmt_default_prepare,
		ms_office_common_valid_all,
		fmt_default_split,
		ms_office_common_binary,
		ms_office_common_get_salt,
#if FMT_MAIN_VERSION > 11
		{
			ms_office_version,
			ms_office_common_iteration_count,
		},
#endif
		fmt_default_source,
		{
			fmt_default_binary_hash_0,
			fmt_default_binary_hash_1,
			fmt_default_binary_hash_2,
			fmt_default_binary_hash_3,
			fmt_default_binary_hash_4,
			fmt_default_binary_hash_5,
			fmt_default_binary_hash_6
		},
		fmt_default_salt_hash,
		NULL,
		set_salt,
		office_set_key,
		get_key,
		fmt_default_clear_keys,
		crypt_all,
		{
			get_hash_0,
			get_hash_1,
			get_hash_2,
			get_hash_3,
			get_hash_4,
			get_hash_5,
			get_hash_6
		},
		cmp_all,
		cmp_one,
		cmp_exact
	}
};

#endif /* plugin stanza */
