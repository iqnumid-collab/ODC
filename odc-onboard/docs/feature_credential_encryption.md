# Feature: Credential Encryption

## Overview

OpenVPN credentials (username/password) can be encrypted at rest using libsodium's authenticated encryption. This prevents plaintext credentials from being stored on disk.

## Algorithm

- **Cipher**: XSalsa20-Poly1305 via `crypto_secretbox_easy`
- **Key size**: 32 bytes (256 bits)
- **Nonce size**: 24 bytes (192 bits)
- **MAC size**: 16 bytes (128 bits)

## File Formats

### Key File (`.key`)

Binary file containing:
```
[32-byte key][24-byte nonce]
```
Total size: 56 bytes

### Encrypted File (`.enc`)

Binary file containing:
```
[24-byte nonce][ciphertext + 16-byte MAC]
```

## Usage

### Encrypt a Credentials File

```bash
odc-onboard --encrypt-creds /path/to/creds.txt /path/to/creds.enc /path/to/creds.key
```

Input file format (plaintext):
```
username
password
```

### Decrypt a Credentials File

```bash
odc-onboard --decrypt-creds /path/to/creds.enc /path/to/creds.key
```

Outputs the decrypted content to stdout.

### CLI Console

The `grant-requirements` command does not handle encryption. Use the command-line flags above.

## Error Handling

| Scenario | Exit Code | Message |
|----------|-----------|---------|
| Wrong key | 1 | `Failed to decrypt: Decryption failed - wrong key or corrupted data` |
| Corrupted file | 1 | `Failed to decrypt: Decryption failed - wrong key or corrupted data` |
| Missing input file | 1 | `Failed to decrypt: Cannot open encrypted file: ...` |
| Missing key file | 1 | `Failed to decrypt: Cannot open key file: ...` |
| Missing input for encrypt | 1 | `Failed to encrypt credentials` |

## Integration with OpenVPN

When adding an OpenVPN connection with encrypted credentials:

```
odc-onboard> vpn add openvpn --id my-ovpn --config /path/to/client.ovpn --creds /path/to/creds.enc
```

The system automatically:
1. Looks for a key file at `<creds_path>.key`
2. Decrypts the credentials using the key
3. Passes the username/password to the OpenVPN3 client

If no key file exists, the system falls back to reading the credentials file as plaintext.

## Source Files

- `src/core/credential_crypto.hpp`
