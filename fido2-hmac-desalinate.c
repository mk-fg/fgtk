// Small C binary to be compiled with fido2 assertion parameters,
//   to decrypt/encrypt strings using different (supplied) hmac-secret salt values.
//
// Should be compiled with the following -D options:
//   -DFHD_RPID=<hostname> - Relying Party ID string, e.g. fhd.mysite.com
//   (optional) -DFHD_TIMEOUT=30 - timeout for user presence check (touch)
//   (optional) -DFHD_UP=<y/n> - user presence check (touch), left up to device by default
//   (optional) -DFHD_UV=<y/n> - user verification via PIN, up to device settings by default
//   (optional) -DFHD_CID=<base64-blob> - Credential ID base64 blob from fido2-cred
//   (optional) -DFHD_DEV=<device> - default device, e.g. "/dev/yubikey" or "pcsc:#slot0"
//     NOTE: "pcsc://slot0" value is not allowed by C macro system, hence # replacing //
// Defaults are to use resident key and require device arg, if latter two are not set.
//
// Build with: gcc -O2 -Wall -lfido2 -lcrypto -D... fido2-hmac-desalinate.c -o fhd && strip fhd
// Usage info: ./fdh -h
// Intended to complement libfido2 cli tools, like fido2-token and fido2-cred.

#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <fido.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <err.h>


#ifndef FHD_RPID
#error -DFHD_RPID=... must be set to Replying Party ID (hostname) during compilation.
#endif
#ifndef FHD_DEV
#define FHD_DEV
#endif
#ifndef FHD_CID
#define FHD_CID
#endif
#ifndef FHD_TIMEOUT
#define FHD_TIMEOUT 30
#endif
#ifndef FHD_UP
#define FHD_UP
#endif
#ifndef FHD_UV
#define FHD_UV
#endif


#define _STR(x) #x
#define STR(x) _STR(x)

#define FIDO_YN(x) ({ \
	!memcmp(_STR(x), "y", 1) ? FIDO_OPT_TRUE : \
	!memcmp(_STR(x), "n", 1) ? FIDO_OPT_FALSE : FIDO_OPT_OMIT; })

#define FIDO_CHK(x) if ((r = x) != FIDO_OK) errx(39, #x " = %s", fido_strerr(r));

// Challenge value has to be sent, but is not used here
static const unsigned char client_data_hash[32] = "fido2-hmac-desalinate.cd-hash.1";


int b64_decode(unsigned char *src, unsigned char **dst, int *dst_len) {
	EVP_ENCODE_CTX *ctx = EVP_ENCODE_CTX_new();
	int dst_len_ext;
	EVP_DecodeInit(ctx);
	if ( !(*dst = malloc(3 * strlen((char*) src) / 4 + 1))
		|| EVP_DecodeUpdate(ctx, *dst, dst_len, src, strlen((char*) src)) < 0
		|| EVP_DecodeFinal(ctx, *dst, &dst_len_ext) < 0
		|| (*dst_len += dst_len_ext) <= 0 ) return -1;
	EVP_ENCODE_CTX_free(ctx);
	return 0;
}

char *str_replace(char *s, char *src, char *dst) {
	int s_alloc = strlen(s) + 1, src_len = strlen(src), dst_len = strlen(dst);
	unsigned long n = 0, s_len = s_alloc - 1, len_diff = 0;
	char *m, *res;
	if (!(res = malloc(s_len * (float) strlen(dst) / (float) strlen(src) + 1)))
		err(1, "malloc: str_replace");
	while ((m = strstr(s, src))) {
		memcpy(res + n, s, m - s);
		n += m - s;
		s_len -= (m - s) + src_len;
		s = m + src_len;
		len_diff += dst_len - src_len;
		memcpy(res + n, dst, dst_len);
		n += dst_len; }
	strcpy(res + n, s);
	return realloc(res, s_alloc + len_diff);
}

int main(int argc, char *argv[]) {
	int r; unsigned int ru;
	char *rp_id = STR(FHD_RPID); // MUST be defined via -D... for gcc
	int dev_up = FIDO_YN(FHD_UP), dev_uv = FIDO_YN(FHD_UV);
	int dev_timeout = FHD_TIMEOUT;
	char *cred_b64 = STR(FHD_CID);
	char *dev_spec = str_replace(STR(FHD_DEV), "#", "//");

	if ( argc > 2 || (argc == 2 &&
			(!memcmp(argv[1], "-h", 3) || !memcmp(argv[1], "--help", 7))) ) {
		printf("Usage: %s [fido2-token-device]\n\n", argv[0]);
		printf(
			"Tool to do short-string encryption and decryption,"
			"\n using hmac-secret extension of libfido2-supported devices.\n"
			"Reads ( hmac-salt || '.' || string ) line from stdin, with hmac-salt"
			"\n and string base64-encoded, prints raw encrypted/decrypted string to stdout.\n"
			"User checks: presence=%d verify=%d (empty - token default), with %ds timeout.\n"
			"fido2-token-device argument, if any, is same as fido2-token tool uses.\n"
			"Does not print anything to stdout on errors, only stderr + non-zero exit code.\n\n"
			"Symmetric key is produced by the device from hmac-salt value,"
				"\n and will be unguessable, but same for same ( salt, credential-id or stored key ).\n"
			"Actual encryption/decryption is done using simple XOR, with HMAC"
				"\n as PRF to make one-time pad, so it's same operation in both directions.\n\n"
			"Uses static compiled-in rp-id hostname, and cred-id base64, if it's not resident.\n"
			"Default device spec is compiled-in [ %s ].\n"
			"Non-empty FHD_DEBUG environment will enable libfido2 debug-logs to stderr.\n\n",
			dev_up, dev_uv, dev_timeout, dev_spec );
		return argc > 2 ? 1 : 0; }

	// Read/decode inputs

	unsigned char *salt, *data, *cred = NULL; int salt_len, data_len, cred_len;

	if (argc == 2) dev_spec = argv[1];
	if (!memcmp(dev_spec, "", 1)) errx(1, "ERROR: No device path built-in or specified.");
	if (!memcmp(rp_id, "", 1)) errx(1, "ERROR: Empty FHD_RPID compiled into binary.");
	if (memcmp(cred_b64, "", 1))
		if (b64_decode((unsigned char*) cred_b64, &cred, &cred_len))
			errx(1, "ERROR: Failed to b64-decode compiled-in Credential ID value");

	char *s = NULL; size_t s_alloc; ssize_t s_len;
	if ((s_len = (int) getdelim(&s, &s_alloc, ' ', stdin)) <= 0 || s_len != strlen((char*) s))
		errx(1, "ERROR: Failed to read hmac-salt base64 value from stdin");
	if (b64_decode((unsigned char*) s, &salt, &salt_len))
		errx(1, "ERROR: Failed to b64-decode hmac-salt value from stdin");
	if ((s_len = (int) getline(&s, &s_alloc, stdin)) <= 0 || s_len != strlen((char*) s))
		errx(1, "ERROR: Failed to read base64 data from stdin");
	if (b64_decode((unsigned char*) s, &data, &data_len))
		errx(1, "ERROR: Failed to b64-decode data buffer from stdin");

	// libfido2 init

	char *debug_env = getenv("FHD_DEBUG");
	int debug_enabled = debug_env && memcmp(debug_env, "", 1);
	if (debug_enabled) warnx("libfido2 debug-logging enabled [ %s ]", dev_spec);
	fido_init(debug_enabled ? FIDO_DEBUG : 0);

	// Create assertion

	fido_assert_t *assert = NULL;
	if (!(assert = fido_assert_new())) errx(38, "fido_assert_new");

	FIDO_CHK(fido_assert_set_clientdata(assert, client_data_hash, sizeof(client_data_hash)));
	FIDO_CHK(fido_assert_set_rp(assert, rp_id));
	FIDO_CHK(fido_assert_set_extensions(assert, FIDO_EXT_HMAC_SECRET));
	if (dev_up != FIDO_OPT_OMIT) FIDO_CHK(fido_assert_set_up(assert, dev_up));
	if (dev_uv != FIDO_OPT_OMIT) FIDO_CHK(fido_assert_set_uv(assert, dev_uv));
	if (cred) FIDO_CHK(fido_assert_allow_cred(assert, cred, cred_len));

	unsigned char salt_hash[32];
	if (!EVP_Digest(salt, salt_len, salt_hash, &ru, EVP_sha256(),
		NULL) || ru != sizeof(salt_hash)) errx(38, "openssl EVP_Digest failed");
	FIDO_CHK(fido_assert_set_hmac_salt(assert, salt_hash, sizeof(salt_hash)));

	// Get hmac from device

	fido_dev_t *dev = NULL;
	if (!(dev = fido_dev_new())) errx(38, "fido_dev_new");

	FIDO_CHK(fido_dev_set_timeout(dev, dev_timeout * 1000));
	FIDO_CHK(fido_dev_open(dev, dev_spec));

	if ((r = fido_dev_get_assert(dev, assert, NULL)) != FIDO_OK) { // pin=NULL
		fido_dev_cancel(dev); errx(38, "fido_dev_get_assert: %s", fido_strerr(r)); }

	FIDO_CHK(fido_dev_close(dev));
	fido_dev_free(&dev);

	if (fido_assert_count(assert) != 1)
		errx( 38, "fido_assert_count: %d signatures"
			" instead of expected one", (int) fido_assert_count(assert) );

	char *key; int key_len = (int) fido_assert_hmac_secret_len(assert, 0);
	if (!(key = malloc(key_len))) err(38, "malloc: fido key");
	memcpy(key, fido_assert_hmac_secret_ptr(assert, 0), key_len);
	fido_assert_free(&assert);

	/* char *key; int key_len; // quick testing w/o device */
	/* b64_decode("7OeuacOrbOsjJsrjMlIRSv9VsPIE9/dMBZkl/uXTzds=", &key, &key_len); */

	// XOR operation

	unsigned char *seed; int seed_len = 5 + sizeof(int) + salt_len;
	if (!(seed = malloc(seed_len))) err(38, "malloc: prf block seed");
	memcpy(seed, "fhd1.", 5);
	memcpy(seed + 5 + sizeof(int), salt, salt_len);

	unsigned char block[32];
	int data_offset = 0, block_n = 0, n;
	while (data_offset < data_len) {
		memcpy(seed + 5, &block_n, sizeof(int));
		block_n++;
		if (!HMAC( EVP_sha256(), key, key_len, seed, seed_len,
			block, &ru ) || ru != sizeof(block)) errx(38, "openssl HMAC failed");
		for (n=0; n < ru && data_offset < data_len; n++) data[data_offset++] ^= block[n]; }

	// Print resulting raw value

	fwrite(data, data_len, 1, stdout);
	fflush(stdout);

	return 0;
}
