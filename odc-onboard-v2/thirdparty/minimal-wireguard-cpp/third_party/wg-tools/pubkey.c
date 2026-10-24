// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2020 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "curve25519.h"
#include "encoding.h"
#include "subcommands.h"
#include "sec_store.h"
#include "wg_ctype.h"

int pubkey_main(int argc, const char *argv[])
{
	uint8_t key[WG_KEY_LEN] __attribute__((aligned(sizeof(uintptr_t))));
	char base64[WG_KEY_LEN_BASE64];
	int trailing_char;

	if (argc != 1) {
		//fprintf(stderr, "Usage: %s %s\n", PROG_NAME, argv[0]);
		return 1;
	}

	if (fread(base64, 1, sizeof(base64) - 1, stdin) != sizeof(base64) - 1) {
		errno = EINVAL;
		fprintf(stderr, "pubkey: Key is not the correct length or format\n");
		return 1;
	}
	base64[WG_KEY_LEN_BASE64 - 1] = '\0';

	for (;;) {
		trailing_char = getc(stdin);
		if (!trailing_char || char_is_space(trailing_char))
			continue;
		if (trailing_char == EOF)
			break;
		fprintf(stderr, "pubkey: Trailing characters found after key\n");
		return 1;
	}

	if (!key_from_base64(key, base64)) {
		fprintf(stderr, "pubkey: Key is not the correct length or format\n");
		return 1;
	}
	curve25519_generate_public(key, key);
	key_to_base64(base64, key);
	puts(base64);
	return 0;
}

/* Derive a public key from a private key. */
int get_pubkey(char *b64key)
{
	uint8_t key[WG_KEY_LEN] __attribute__((aligned(sizeof(uintptr_t))));
	char base64[WG_KEY_LEN_BASE64];

	memcpy(base64, b64key, WG_KEY_LEN_BASE64 - 1);
	base64[WG_KEY_LEN_BASE64 - 1] = '\0';

	if (!key_from_base64(key, base64)) {
		fprintf(stderr, "Key is not the correct length or format\n");
		return 1;
	}
	curve25519_generate_public(key, key);
	key_to_base64(base64, key);
	//puts(base64);

	/* b64key <- public key */
	memcpy(b64key, base64, WG_KEY_LEN_BASE64 - 1);

	store_curve25519_public(base64);

	return 0;
}
