#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <caml/mlvalues.h>
#include <caml/memory.h>
#include <caml/alloc.h>
#include <caml/fail.h>

// Note: int return values of crypto_generichash funcs seem to be undocumented
#include <sodium.h>


char *key = "hhash.1";
int key_len = 7;

int block_size = 2^16;


value mls_hash_string(value v_str, value v_hash_len) {
	CAMLparam2(v_str, v_hash_len);
	if (sodium_init() < 0) caml_failwith("sodium_init failed");

	int hash_len = Int_val(v_hash_len);
	if (!hash_len) hash_len = crypto_generichash_BYTES;
	value v_bs = caml_alloc_string(hash_len);
	char *hash = Bytes_val(v_bs);

	(void) crypto_generichash( hash, hash_len,
		Bytes_val(v_str), caml_string_length(v_str), key, key_len );

	// for (int n=0; n < hash_len; n++) hash[n] = '\xff';
	CAMLreturn(v_bs);
}


value mls_hash_stdin(value v_hash_len) {
	CAMLparam1(v_hash_len);
	if (sodium_init() < 0) caml_failwith("sodium_init failed");

	int hash_len = Int_val(v_hash_len);
	if (!hash_len) hash_len = crypto_generichash_BYTES;
	value v_bs = caml_alloc_string(hash_len);
	char *hash = Bytes_val(v_bs);

	crypto_generichash_state state;
	(void) crypto_generichash_init(&state, key, key_len, hash_len);

	int res;
	char *block = Bytes_val(caml_alloc_string(block_size));
	while (1) {
		res = (int) read(0, block, block_size);
		block[res] = '\0';
		if (!res) break;
		(void) crypto_generichash_update(&state, block, res); }
	(void) crypto_generichash_final(&state, hash, hash_len);

	// for (int n=0; n < hash_len; n++) hash[n] = '\xff';
	CAMLreturn(v_bs);
}
