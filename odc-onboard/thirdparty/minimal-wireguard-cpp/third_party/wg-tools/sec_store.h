/*
 * Routine to store curve25519 keypairs securely based on libsodium
 * Copyright (c) 2025 Chunghan Yi <chunghan.yi@gmail.com>
 */


#ifndef SEC_STORE_H
#define SEC_STORE_H

extern void store_curve25519_public(char *base64);
extern int store_curve25519_secret(char *base64);
extern int get_privatekey(char *private);
extern int get_pubkey(char *private);
extern bool initialize_curve25519(char *pubkey, char *privkey);

#endif
