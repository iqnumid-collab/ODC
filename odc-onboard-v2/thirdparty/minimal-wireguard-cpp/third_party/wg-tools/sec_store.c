/*
 * Routine to store curve25519 keypairs securely based on libsodium
 * Copyright (c) 2025 Chunghan Yi <chunghan.yi@gmail.com>
 */

#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <sodium.h>

#include "curve25519.h"
#include "encoding.h"
#include "subcommands.h"
#include "sec_store.h"

const char *GEN_KEY[1] = { "genkey" };
/* let's create a directory /qrwg/config under the system(Ubuntu 22.04 or OpenWrt) */
char publickey_file_path[256]  = "/qrwg/config/publickey";
char privatekey_file_path[256] = "/qrwg/config/privatekey";

#define NO_FILE_ENCRYPTYION

#ifndef NO_FILE_ENCRYPTYION
const unsigned char __key[crypto_secretstream_xchacha20poly1305_KEYBYTES] = "-xchacha20poly1305-";

#define CHUNK_SIZE 4096

static int encrypt_buffer(const char *target_file, char *source_buffer)
{
	unsigned char  buf_in[CHUNK_SIZE];
	unsigned char  buf_out[CHUNK_SIZE + crypto_secretstream_xchacha20poly1305_ABYTES];
	unsigned char  header[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
	crypto_secretstream_xchacha20poly1305_state st;
	FILE          *fp_t = NULL;
	unsigned long long out_len;
	size_t         rlen;
	unsigned char  tag;

	fp_t = fopen(target_file, "wb");
	if (fp_t) {
		crypto_secretstream_xchacha20poly1305_init_push(&st, header, __key);
		fwrite(header, 1, sizeof header, fp_t);

		rlen = 44;
		memset(buf_in, 0, sizeof(buf_in));
		memcpy(buf_in, source_buffer, rlen);

		tag = crypto_secretstream_xchacha20poly1305_TAG_FINAL;
		crypto_secretstream_xchacha20poly1305_push(&st, buf_out, &out_len,
				buf_in, rlen, NULL, 0, tag);
		fwrite(buf_out, 1, (size_t) out_len, fp_t);
		fclose(fp_t);
	} else {
		fprintf(stderr, "(%s)Opening target file(%s) failed.\n", __func__, target_file);
	}
	return 0;
}

static int decrypt_buffer(char *target_buffer, const char *source_file)
{
	unsigned char  buf_in[CHUNK_SIZE + crypto_secretstream_xchacha20poly1305_ABYTES];
	unsigned char  buf_out[CHUNK_SIZE];
	unsigned char  header[crypto_secretstream_xchacha20poly1305_HEADERBYTES];
	crypto_secretstream_xchacha20poly1305_state st;
	FILE          *fp_s = NULL;
	unsigned long long out_len;
	size_t         rlen = 0;
	int            eof;
	int            ret = -1;
	unsigned char  tag;

	fp_s = fopen(source_file, "rb");
	if (fp_s) {
		rlen = fread(header, 1, sizeof header, fp_s);
		if (crypto_secretstream_xchacha20poly1305_init_pull(&st, header, __key) != 0) {
			/* incomplete header */
			fclose(fp_s);
			return ret;
		}

		rlen = fread(buf_in, 1, sizeof buf_in, fp_s);
		if (crypto_secretstream_xchacha20poly1305_pull(&st, buf_out, &out_len, &tag,
					buf_in, rlen, NULL, 0) != 0) {
			/* corrupted chunk */
			fclose(fp_s);
			return ret;
		}
		eof = 1;
		if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL && ! eof) {
			/* premature end (end of file reached before the end of the stream) */
			fclose(fp_s);
			return ret;
		}
		memcpy(target_buffer, buf_out, out_len);

		ret = 0;
		fclose(fp_s);
	} else {
		fprintf(stderr, "(%s)Opening source file(%s) failed.\n", __func__, source_file);
	}

	return ret;
}
#endif

// Let's store the public key value in a secure storage.
void store_curve25519_public(char *base64)
{
	FILE *fp = NULL;

	fp = fopen(publickey_file_path, "w");
	if (fp) {
		fprintf(fp, "%s", base64);
		fclose(fp);
	}
}

// Let's store the private key value in a secure storage.
int store_curve25519_secret(char *base64)
{
#ifndef NO_FILE_ENCRYPTYION
	if (encrypt_buffer(privatekey_file_path, base64) != 0) {
		return 0;
	}
#else
	FILE *fp = NULL;
	fp = fopen(privatekey_file_path, "w");
	if (fp) {
		fprintf(fp, "%s", base64);
		fclose(fp);
	}
#endif
	return 1;
}

// Let's get the private key from the secure storage.
int get_privatekey(char *private)
{
	memset(private, 0, WG_KEY_LEN_BASE64);
#ifndef NO_FILE_ENCRYPTYION
	if (decrypt_buffer(private, privatekey_file_path) != 0) {
		return 0;
	}
#else
	FILE *fp = NULL;
	char xbuf[WG_KEY_LEN_BASE64+1];
	fp = fopen(privatekey_file_path, "r");
	if (fp) {
		if (fgets(xbuf, sizeof(xbuf), fp)) {
			if (xbuf[strlen(xbuf)-1] == '\n' || xbuf[strlen(xbuf)-1] == '\r')
				xbuf[strlen(xbuf)-1] = '\0';
			memcpy(private, xbuf, WG_KEY_LEN_BASE64 - 1);
			private[WG_KEY_LEN_BASE64 - 1] = '\0';
		}
		fclose(fp);
	}
#endif
	return 1;
}

bool initialize_curve25519(char *pubkey, char *privkey)
{
	char base64[WG_KEY_LEN_BASE64];

	/* If the corresponding file does not exist, an ECC curve25519 private key is generated. */
	if (access(privatekey_file_path, F_OK) != 0) {
		genkey_main(1, GEN_KEY);
		//fprintf(stderr, "A new private key(curve25519) has been made and saved.\n");
		sleep(1);
	}

	if (!get_privatekey(base64)) { /* curve25519 private key */
		fprintf(stderr, "Oops, failed to get a privatekey. :(\n");
		exit(1);
	}
	memcpy(privkey, base64, WG_KEY_LEN_BASE64 - 1);
	privkey[WG_KEY_LEN_BASE64 - 1] = '\0';

	/* Derive a public key from a private key. */
	if (!get_pubkey(base64)) {
		memcpy(pubkey, base64, WG_KEY_LEN_BASE64 - 1);
		pubkey[WG_KEY_LEN_BASE64 - 1] = '\0';
		return true;
	} else {
		return false;
	}
}
