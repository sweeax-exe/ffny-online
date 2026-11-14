/* SPDX-License-Identifier: GPL-3.0-or-later
 * POSIX twin of account.c's Windows-only pieces (see account_posix.h for the
 * small macros). HTTPS goes through OpenSSL in place of WinHTTP; the session
 * file is AES-256-CBC with a random key kept beside it (chmod 600 both), the
 * Linux stand-in for DPAPI's per-user protection. Included by account.c on
 * non-Windows, inside its single translation unit. */
#if !defined(_WIN32)
#ifndef ACCOUNT_POSIX_IMPL_H
#define ACCOUNT_POSIX_IMPL_H

#include "account_posix.h"
#include <wchar.h>
#include <time.h>

/* ---- wide-string helpers (the shared code keeps WinHTTP's wchar_t API) --- */
static int wide_to_utf8(const wchar_t *w, char *out, int size)
{
    int n = 0;
    if (!w) { if (size > 0) out[0] = 0; return 0; }
    while (*w && n < size - 1) {
        unsigned c = (unsigned)*w++;
        if (c < 0x80) out[n++] = (char)c;
        else if (c < 0x800) { out[n++] = (char)(0xC0 | c >> 6); out[n++] = (char)(0x80 | (c & 0x3F)); }
        else { out[n++] = (char)(0xE0 | c >> 12); out[n++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[n++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[n] = 0;
    return n;
}

/* UTF-8 to this platform's wchar_t (the compat layer's MB2WC writes 16-bit). */
#define CP_UTF8 65001u
static __inline int utf8_to_wide(const char *s, int slen, wchar_t *out, int size)
{
    int n = 0;
    if (slen < 0) slen = (int)strlen(s) + 1;
    while (n < size - 1 && *s && slen-- > 0) {
        unsigned char c = (unsigned char)*s++;
        unsigned v;
        if (c < 0x80) v = c;
        else if ((c & 0xE0) == 0xC0) { v = (c & 0x1F) << 6; if (*s) v |= (unsigned)(*s++ & 0x3F); }
        else if ((c & 0xF0) == 0xE0) { v = (c & 0x0F) << 12; if (*s) v |= (unsigned)(*s++ & 0x3F) << 6; if (*s) v |= (unsigned)(*s++ & 0x3F); }
        else { v = 0xFFFD; if (*s) s++; if (*s) s++; if (*s) s++; }
        out[n++] = (wchar_t)v;
    }
    if (n < size) out[n] = 0;
    return n;
}

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#define Sleep(ms) sleep_ms(ms)

/* the compat layer has no TryAcquireSRWLockShared; pthread once more */
static __inline int TryAcquireSRWLockShared(PSRWLOCK lock)
{
    return pthread_mutex_trylock((pthread_mutex_t *)lock) == 0;
}

/* ---- random bytes (BCryptGenRandom's stand-in) ---------------------------- */
static int random_bytes(unsigned char *b, int n) { return RAND_bytes(b, n) == 1; }

/* ---- the session file's key ---------------------------------------------- */
static int session_key_path(char *path, int size, const char *dat_path)
{
    return snprintf(path, (size_t)size, "%s.key", dat_path);
}

static int session_key_load(const char *kpath, unsigned char key[32])
{
    FILE *f = fopen(kpath, "rb");
    if (!f) return 0;
    if (fread(key, 1, 32, f) != 32) { fclose(f); return 0; }
    fclose(f);
    return 1;
}

static int session_key_make(const char *kpath, unsigned char key[32])
{
    FILE *f;
    if (!RAND_bytes(key, 32)) return 0;
    f = fopen(kpath, "wb");
    if (!f) return 0;
    if (fwrite(key, 1, 32, f) != 32) { fclose(f); return 0; }
    fclose(f);
    chmod(kpath, 0600);
    return 1;
}

/* AES-256-CBC with a fresh IV per file; out holds iv+ciphertext. */
static unsigned char *session_crypt(const unsigned char *in, int inlen, const unsigned char key[32], int *outlen)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char iv[16], *out;
    int len1 = 0, len2 = 0;
    if (!ctx || !RAND_bytes(iv, 16)) { if (ctx) EVP_CIPHER_CTX_free(ctx); return NULL; }
    out = (unsigned char *)malloc((size_t)inlen + 16 + 16);
    if (!out) { EVP_CIPHER_CTX_free(ctx); return NULL; }
    memcpy(out, iv, 16);
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv) != 1 ||
        EVP_EncryptUpdate(ctx, out + 16, &len1, in, inlen) != 1 ||
        EVP_EncryptFinal_ex(ctx, out + 16 + len1, &len2) != 1) {
        EVP_CIPHER_CTX_free(ctx); free(out); return NULL;
    }
    EVP_CIPHER_CTX_free(ctx);
    *outlen = 16 + len1 + len2;
    return out;
}

static unsigned char *session_decrypt(const unsigned char *in, int inlen, const unsigned char key[32], int *outlen)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char *out;
    int len1 = 0, len2 = 0;
    if (inlen < 32) { EVP_CIPHER_CTX_free(ctx); return NULL; }
    out = (unsigned char *)malloc((size_t)inlen);
    if (!out) { EVP_CIPHER_CTX_free(ctx); return NULL; }
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, in) != 1 ||
        EVP_DecryptUpdate(ctx, out, &len1, in + 16, inlen - 16) != 1 ||
        EVP_DecryptFinal_ex(ctx, out + len1, &len2) != 1) {
        EVP_CIPHER_CTX_free(ctx); free(out); return NULL;
    }
    EVP_CIPHER_CTX_free(ctx);
    *outlen = len1 + len2;
    return out;
}

/* ---- one HTTPS request (WinHTTP's stand-in) ------------------------------- */
typedef struct { volatile long fd; } HttpLive;      /* the socket the owner may cancel */

static int request_full_posix(const wchar_t *wmethod, const wchar_t *wpath, const char *token, const char *ctype,
                              const void *body, unsigned blen, char *buf, int size, int limit_ms,
                              void *volatile *live)
{
    char method[8], path[512], host[64], port_text[8] = "443";
    int port = 443, secure = 1, n = 0, status = 0, canceled = 0;
    const char *test = getenv("RECOMP_ACCOUNT_SERVER");
    struct addrinfo hints, *res = NULL, *ai;
    int sock = -1;
    SSL *ssl = NULL;

    buf[0] = 0;
    wide_to_utf8(wmethod, method, (int)sizeof(method));
    wide_to_utf8(wpath, path, (int)sizeof(path));
    snprintf(host, sizeof(host), "%s", "play.defjam.online");
    if (test && *test) {
        const char *colon = strchr(test, ':');
        snprintf(host, sizeof(host), "%.*s", colon ? (int)(colon - test) : (int)strlen(test), test);
        port = colon ? atoi(colon + 1) : 80;
        snprintf(port_text, sizeof(port_text), "%d", port);
        secure = 0;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_text, &hints, &res) || !res) return 0;
    for (ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) continue;
        {
            int ms = limit_ms > 0 ? limit_ms : 5000;
            struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) return 0;

    {
        HttpLive hl;
        hl.fd = sock;
        if (live) {
            AcquireSRWLockExclusive(&g_live_lock);
            if (*live == (void *)1) canceled = 1;
            else *live = (void *)&hl;
            ReleaseSRWLockExclusive(&g_live_lock);
        }
        if (!canceled) {
            if (secure) {
                SSL_CTX *c = SSL_CTX_new(TLS_client_method());
                if (!c || !(ssl = SSL_new(c))) { if (c) SSL_CTX_free(c); goto done; }
                SSL_CTX_free(c);        /* SSL holds its own reference */
                SSL_set_fd(ssl, sock);
                SSL_set1_host(ssl, host);
                if (SSL_connect(ssl) != 1) goto done;
            }
            {
                char head[768];
                int hn = snprintf(head, sizeof(head),
                                  "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
                                  "User-Agent: FFNY-Online-Launcher/1\r\n",
                                  method, path, host);
                if (token && *token) hn += snprintf(head + hn, sizeof(head) - (size_t)hn,
                                                    "Authorization: Bearer %s\r\n", token);
                if (body) hn += snprintf(head + hn, sizeof(head) - (size_t)hn,
                                         "Content-Type: %s\r\nContent-Length: %u\r\n",
                                         ctype ? ctype : "application/octet-stream", blen);
                hn += snprintf(head + hn, sizeof(head) - (size_t)hn, "\r\n");
                int wok = ssl ? SSL_write(ssl, head, hn) == hn : send(sock, head, (size_t)hn, 0) == hn;
                if (!wok) goto done;
                if (body && blen) {
                    wok = ssl ? SSL_write(ssl, body, (int)blen) == (int)blen : send(sock, body, blen, 0) == (int)blen;
                    if (!wok) goto done;
                }
            }
            {
                char resp[16384];
                int rn = 0, hdr = -1;
                for (;;) {
                    int got = ssl ? SSL_read(ssl, resp + rn, (int)sizeof(resp) - 1 - rn)
                                  : (int)recv(sock, resp + rn, sizeof(resp) - 1 - rn, 0);
                    if (got <= 0) break;
                    rn += got;
                    resp[rn] = 0;
                    if (hdr < 0) {
                        char *sp = strstr(resp, "\r\n\r\n");
                        if (!sp) { if (rn >= (int)sizeof(resp) - 1) break; rn = 0; continue; }
                        if (sscanf(resp, "HTTP/%*d.%*d %d", &status) != 1) status = 0;
                        hdr = (int)(sp - resp) + 4;
                        {
                            int take = rn - hdr;
                            if (take > size - 1) take = size - 1;
                            if (take > 0) memcpy(buf, resp + hdr, (size_t)take);
                            n = take > 0 ? take : 0;
                        }
                        rn = 0;
                    } else {
                        int take = got;
                        if (n + take > size - 1) take = size - 1 - n;
                        if (take > 0) memcpy(buf + n, resp, (size_t)take);
                        n += take > 0 ? take : 0;
                        rn = 0;
                    }
                }
                buf[n] = 0;
            }
        }
done:
        if (live) {
            AcquireSRWLockExclusive(&g_live_lock);
            if (*live == (void *)&hl) *live = NULL;
            else if (!canceled) canceled = 2;
            ReleaseSRWLockExclusive(&g_live_lock);
        }
    }
    if (ssl) SSL_free(ssl);
    if (sock >= 0) close(sock);
    g_last_len = n;
    return canceled ? 0 : status;
}

/* ---- streaming download (account_download's stand-in) --------------------- */
static int account_download_posix(const char *host, const char *path, unsigned long long from,
                                  int (*sink)(void *ctx, const char *data, unsigned n), void *ctx,
                                  int *partial, const char *token)
{
    char port_text[8] = "443";
    int port = 443, sock = -1, status = 0;
    const char *test = getenv("RECOMP_ACCOUNT_SERVER");
    struct addrinfo hints, *res = NULL, *ai;
    SSL *ssl = NULL;
    if (partial) *partial = 0;
    if (test && *test) {
        const char *colon = strchr(test, ':');
        snprintf(port_text, sizeof(port_text), "%d", colon ? atoi(colon + 1) : 80);
        port = atoi(port_text);
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_text, &hints, &res) || !res) return 0;
    for (ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0) continue;
        {
            struct timeval tv = { 30, 0 };
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) return 0;
    if (port == 443 && !(test && *test)) {
        SSL_CTX *c = SSL_CTX_new(TLS_client_method());
        if (!c || !(ssl = SSL_new(c))) { if (c) SSL_CTX_free(c); close(sock); return 0; }
        SSL_CTX_free(c);
        SSL_set_fd(ssl, sock);
        SSL_set1_host(ssl, host);
        if (SSL_connect(ssl) != 1) { SSL_free(ssl); close(sock); return 0; }
    }
    {
        char head[1024];
        int hn = snprintf(head, sizeof(head),
                          "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: FFNY-Online-Launcher/1\r\nConnection: close\r\n",
                          path, host);
        int ok;
        if (token && *token) hn += snprintf(head + hn, sizeof(head) - (size_t)hn, "Authorization: Bearer %s\r\n", token);
        if (from) hn += snprintf(head + hn, sizeof(head) - (size_t)hn, "Range: bytes=%llu-\r\n", (unsigned long long)from);
        hn += snprintf(head + hn, sizeof(head) - (size_t)hn, "\r\n");
        ok = ssl ? SSL_write(ssl, head, hn) == hn : send(sock, head, (size_t)hn, 0) == hn;
        if (ok) {
            char b[32768];
            int rn = 0, hdr = -1;
            for (;;) {
                int got = ssl ? SSL_read(ssl, b + rn, (int)sizeof(b) - 1 - rn)
                              : (int)recv(sock, b + rn, sizeof(b) - 1 - rn, 0);
                if (got <= 0) break;
                rn += got;
                b[rn] = 0;
                if (hdr < 0) {
                    char *sp = strstr(b, "\r\n\r\n");
                    if (!sp) { if (rn >= (int)sizeof(b) - 1) break; continue; }
                    if (sscanf(b, "HTTP/%*d.%*d %d", &status) != 1) status = 0;
                    hdr = (int)(sp - b) + 4;
                    if (partial) *partial = status == 206;
                    if ((status == 200 || status == 206) && rn > hdr)
                        if (!sink(ctx, b + hdr, (unsigned)(rn - hdr))) { status = 0; break; }
                } else {
                    if ((status == 200 || status == 206) && !sink(ctx, b, (unsigned)got)) { status = 0; break; }
                }
                rn = 0;
            }
        }
    }
    if (ssl) SSL_free(ssl);
    close(sock);
    return status;
}

#endif
#endif
