/* The project's signature check: see update_sig.h. */
#define _CRT_SECURE_NO_WARNINGS
#include "update_sig.h"
#include <stdio.h>
#include <string.h>

const char update_public_key[] =
    "6f8ce805a386c294c1a24de83d07ce6a0c296ebf836d00434e9a5f0256c6dd7a"
    "6d60d26ef38562ee1e29b99dddca155d89f2a6a28a32c4559c78ea00d28e5c05";

int update_unhex(const char *text, unsigned char *out, unsigned long bytes)
{
    unsigned long i;
    for (i = 0; i < bytes; i++) {
        unsigned v;
        if (!text[2 * i] || !text[2 * i + 1] || sscanf(text + 2 * i, "%2x", &v) != 1) return 0;
        out[i] = (unsigned char)v;
    }
    return 1;
}

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

int update_signature_ok(const char *data, unsigned long size, const char *sig_hex, const char *key_hex)
{
    struct { BCRYPT_ECCKEY_BLOB head; unsigned char xy[64]; } blob;
    unsigned char sig[64], digest[32];
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    int ok = 0;
    if (!key_hex) key_hex = update_public_key;
    if (strlen(sig_hex) < 128 || strlen(key_hex) < 128 || !update_unhex(sig_hex, sig, 64) ||
        !update_unhex(key_hex, blob.xy, 64))
        return 0;
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (PUCHAR)data, size, digest, 32) != 0) return 0;
    blob.head.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    blob.head.cbKey = 32;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0) == 0 &&
        BCryptImportKeyPair(alg, NULL, BCRYPT_ECCPUBLIC_BLOB, &key, (PUCHAR)&blob, sizeof(blob), 0) == 0)
        ok = BCryptVerifySignature(key, NULL, digest, 32, sig, 64, 0) == 0;
    if (key) BCryptDestroyKey(key);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}
#else
int update_signature_ok(const char *data, unsigned long size, const char *sig_hex, const char *key_hex)
{
    (void)data; (void)size; (void)sig_hex; (void)key_hex;
    return 0;
}
#endif
