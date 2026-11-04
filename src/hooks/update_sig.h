/* The project's signature check for what the update server hands out: latest.json (updater.c) and
 * graphics.json (graphics_mod.cpp). An ECDSA P-256 signature over the exact bytes, made with
 * server/relay/cmd/updatesign; the .sig file is the 64-byte r||s as 128 hex digits. Windows only. */
#ifndef DEFJAM_UPDATE_SIG_H
#define DEFJAM_UPDATE_SIG_H
#ifdef __cplusplus
extern "C" {
#endif

/* updatesign's public key: X and Y of the P-256 point, big-endian, as 128 hex digits. */
extern const char update_public_key[];

/* 1 when sig_hex is a valid signature of data by key_hex (NULL: the project's key). */
int update_signature_ok(const char *data, unsigned long size, const char *sig_hex, const char *key_hex);
/* hex text to bytes; 0 on a bad digit. */
int update_unhex(const char *text, unsigned char *out, unsigned long bytes);

#ifdef __cplusplus
}
#endif
#endif
