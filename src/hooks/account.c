/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/*
 * FFNY Online account: sign-in with Discord, through the project's account service
 * (server/relay/accounts, behind https://play.defjam.online).
 *
 * The launcher never holds Discord's client secret. It makes a random secret, opens the browser
 * on /auth/discord/start with only the secret's SHA-256, and polls /auth/poll with the secret
 * itself until the player has signed in with Discord; the answer is a session token. The token,
 * the name and the username are kept in account.dat beside settings.ini, encrypted with DPAPI
 * for the Windows user, so a settings file shared in a bug report carries no session.
 */
#include "account.h"
#include "account_pw.h"
#include "anticheat.h"
#include "pc_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <shellapi.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

#define ACCOUNT_HOST L"play.defjam.online"
#define ACCOUNT_URL "https://play.defjam.online"
#define POLL_MS 2000
#define POLL_COUNT 300          /* ten minutes, the service's own limit */

static SRWLOCK g_lock = SRWLOCK_INIT;
static volatile LONG g_state = ACCOUNT_OUT;
static volatile LONG g_cancel;
static char g_token[65], g_name[129], g_username[65], g_reason[160];
/* How the account signs in (sign-in without Discord, account_password): with Discord, with a username and
 * password (g_login), or both. Saved as the fourth line of account.dat ("d", "p", "dp"). */
static int g_has_discord = 1, g_has_password;
static char g_login[65];

static void set_state(enum AccountState s) { InterlockedExchange(&g_state, (LONG)s); }

static void set_reason(const char *text)
{
    AcquireSRWLockExclusive(&g_lock);
    snprintf(g_reason, sizeof(g_reason), "%s", text);
    ReleaseSRWLockExclusive(&g_lock);
}

/* One HTTPS request to the account service, with a body of blen bytes of type ctype (if any).
 * Returns the HTTP status, 0 when the service could not be reached; the answer goes to buf.
 * limit_ms > 0 caps each of WinHTTP's waits (the crash report); 0 keeps the usual ones. */
/* The social long-poll's request (account_call with a live slot): another thread may close it to
 * cancel a request that waits (social_shutdown); the owner then leaves that handle alone. */
static SRWLOCK g_live_lock = SRWLOCK_INIT;
/* How many bytes the last request_full of this thread read (account_fetch: binary answers). */
static __declspec(thread) int g_last_len;
/* Extra request headers of this thread's next request_full ("Name: value\r\n" lines; account_send). */
static __declspec(thread) const char *g_extra_headers;

static int request_full(const wchar_t *method, const wchar_t *path, const char *token, const char *ctype,
                        const void *body, DWORD blen, char *buf, int size, int limit_ms, int receive_ms,
                        void *volatile *live)
{
    HINTERNET session = NULL, connect = NULL, req = NULL;
    int canceled = 0;
    DWORD status = 0, ssize = sizeof(status);
    int n = 0;
    buf[0] = 0;
    /* RECOMP_ACCOUNT_SERVER=127.0.0.1:8095: tests talk plain HTTP to a local service with test
     * accounts, so they never touch the players' real statistics. */
    const char *test = getenv("RECOMP_ACCOUNT_SERVER");
    wchar_t host[64] = ACCOUNT_HOST;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    DWORD secure = WINHTTP_FLAG_SECURE;
    if (test && *test) {
        const char *colon = strchr(test, ':');
        int len = colon ? (int)(colon - test) : (int)strlen(test);
        swprintf(host, 64, L"%.*hs", len, test);
        port = (INTERNET_PORT)(colon ? atoi(colon + 1) : 80);
        secure = 0;
    }
    session = WinHttpOpen(L"FFNY-Online-Launcher/1", test && *test ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    /* A bug report of a few MB may take a while on a slow line. */
    if (session && limit_ms > 0) WinHttpSetTimeouts(session, limit_ms, limit_ms, limit_ms, limit_ms);
    else if (session) WinHttpSetTimeouts(session, 5000, 5000, blen > 65536 ? 120000 : 10000,
                                         receive_ms > 0 ? receive_ms : 15000);
    if (session) connect = WinHttpConnect(session, host, port, 0);
    if (connect) req = WinHttpOpenRequest(connect, method, path, NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, secure);
    if (req && live) {
        AcquireSRWLockExclusive(&g_live_lock);
        if (*live == (void *)1) canceled = 1;           /* canceled before it started */
        else *live = req;
        ReleaseSRWLockExclusive(&g_live_lock);
    }
    if (req && !canceled) {
        wchar_t headers[1024] = L"";
        if (token && *token) swprintf(headers, 200, L"Authorization: Bearer %hs\r\n", token);
        if (body) swprintf(headers + wcslen(headers), 300 - wcslen(headers), L"Content-Type: %hs\r\n", ctype);
        if (g_extra_headers && strlen(g_extra_headers) < 600)
            swprintf(headers + wcslen(headers), 1024 - wcslen(headers), L"%hs", g_extra_headers);
        if (WinHttpSendRequest(req, headers[0] ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers[0] ? (DWORD)-1L : 0,
                               (LPVOID)body, blen, blen, 0) &&
            WinHttpReceiveResponse(req, NULL) &&
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &ssize, NULL)) {
            for (;;) {
                DWORD avail = 0, got = 0;
                if (!WinHttpQueryDataAvailable(req, &avail) || !avail || n >= size - 1) break;
                if (avail > (DWORD)(size - 1 - n)) avail = (DWORD)(size - 1 - n);
                if (!WinHttpReadData(req, buf + n, avail, &got) || !got) break;
                n += (int)got;
            }
            buf[n] = 0;
        }
    }
    if (req && live) {
        AcquireSRWLockExclusive(&g_live_lock);
        if (*live == req) *live = NULL;
        else if (!canceled) canceled = 2;               /* account_cancel_call closed it */
        ReleaseSRWLockExclusive(&g_live_lock);
    }
    if (req && canceled != 2) WinHttpCloseHandle(req);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    g_last_len = n;
    return canceled ? 0 : (int)status;
}

static int request_body_ms(const wchar_t *method, const wchar_t *path, const char *token, const char *ctype,
                           const void *body, DWORD blen, char *buf, int size, int limit_ms)
{
    return request_full(method, path, token, ctype, body, blen, buf, size, limit_ms, 0, NULL);
}

static int request_body(const wchar_t *method, const wchar_t *path, const char *token, const char *ctype,
                        const void *body, DWORD blen, char *buf, int size)
{
    return request_body_ms(method, path, token, ctype, body, blen, buf, size, 0);
}

/* The same with a JSON body. */
static int request(const wchar_t *method, const wchar_t *path, const char *token, const char *body,
                   char *buf, int size)
{
    return request_body(method, path, token, "application/json", body, body ? (DWORD)strlen(body) : 0, buf, size);
}

static void put_utf8(char *out, int size, int *n, unsigned cp)
{
    char b[4];
    int len;
    if (cp < 0x80) { b[0] = (char)cp; len = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 0x3F)); len = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | (cp >> 6 & 0x3F));
                             b[2] = (char)(0x80 | (cp & 0x3F)); len = 3; }
    else { b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | (cp >> 12 & 0x3F));
           b[2] = (char)(0x80 | (cp >> 6 & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); len = 4; }
    if (*n + len < size) { memcpy(out + *n, b, len); *n += len; }
}

/* A string member of the service's JSON, decoded to UTF-8 (the first one of that key). */
static int json_str(const char *json, const char *key, char *out, int size)
{
    char pat[48];
    const char *p;
    int n = 0;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p++ != '"') return 0;
    while (*p && *p != '"') {
        if (*p != '\\') { if (n < size - 1) out[n++] = *p; p++; continue; }
        p++;
        if (*p == 'u') {
            unsigned cp = 0, lo = 0;
            if (sscanf(p + 1, "%4x", &cp) != 1) return 0;
            p += 5;
            if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u' && sscanf(p + 2, "%4x", &lo) == 1) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                p += 6;
            }
            put_utf8(out, size, &n, cp);
            continue;
        }
        if (n < size - 1) out[n++] = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p == 'r' ? '\r' : *p == 'b' ? '\b' : *p == 'f' ? '\f' : *p;
        p++;
    }
    out[n] = 0;
    return *p == '"';
}

/* ---- the saved session ---- */

static void data_path(char *out, int size)
{
    const char *settings = pc_settings_path(), *data = getenv("DEFJAM_DATA");
    const char *slash = settings ? strrchr(settings, '\\') : NULL;
    if (!slash && settings) slash = strrchr(settings, '/');
    if (slash) snprintf(out, size, "%.*s\\account.dat", (int)(slash - settings), settings);
    else if (data && *data) snprintf(out, size, "%s\\account.dat", data);     /* runs without settings.ini */
    else snprintf(out, size, "account.dat");
}

static void save(void)
{
    char path[MAX_PATH], plain[400];
    DATA_BLOB in, out = { 0 };
    FILE *f;
    AcquireSRWLockShared(&g_lock);
    snprintf(plain, sizeof(plain), "%s\n%s\n%s\n%s%s\n%s\n", g_token, g_name, g_username,
             g_has_discord ? "d" : "", g_has_password ? "p" : "", g_login);
    ReleaseSRWLockShared(&g_lock);
    in.pbData = (BYTE *)plain;
    in.cbData = (DWORD)strlen(plain);
    data_path(path, sizeof(path));
    if (!CryptProtectData(&in, L"FFNY Online session", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return;
    f = fopen(path, "wb");
    if (f) { fwrite(out.pbData, 1, out.cbData, f); fclose(f); }
    SecureZeroMemory(plain, sizeof(plain));
    LocalFree(out.pbData);
}

static int load(void)
{
    char path[MAX_PATH], blob[2048];
    DATA_BLOB in, out = { 0 };
    size_t got;
    FILE *f;
    {   /* tests: RECOMP_ACCOUNT_TEST_TOKEN, a session of a local test account service (accounts
         * -test-accounts), only with RECOMP_ACCOUNT_SERVER on this machine; never saved */
        const char *srv = getenv("RECOMP_ACCOUNT_SERVER"), *tok = getenv("RECOMP_ACCOUNT_TEST_TOKEN");
        if (srv && tok && strlen(tok) == 64 && (!strncmp(srv, "127.0.0.1:", 10) || !strncmp(srv, "localhost:", 10))) {
            const char *who = getenv("RECOMP_NETPLAY_NAME");
            AcquireSRWLockExclusive(&g_lock);
            snprintf(g_token, sizeof(g_token), "%s", tok);
            snprintf(g_name, sizeof(g_name), "%s", who && *who ? who : "Test player");
            snprintf(g_username, sizeof(g_username), "%s", g_name);
            ReleaseSRWLockExclusive(&g_lock);
            return 1;
        }
    }
    data_path(path, sizeof(path));
    f = fopen(path, "rb");
    if (!f) return 0;
    got = fread(blob, 1, sizeof(blob), f);
    fclose(f);
    in.pbData = (BYTE *)blob;
    in.cbData = (DWORD)got;
    if (!got || !CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return 0;
    {
        char plain[400];
        char *a, *b, *c;
        int len = out.cbData < sizeof(plain) - 1 ? (int)out.cbData : (int)sizeof(plain) - 1;
        memcpy(plain, out.pbData, len);
        plain[len] = 0;
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
        a = plain;
        b = strchr(a, '\n');
        c = b ? strchr(b + 1, '\n') : NULL;
        if (!b || !c || b - a != 64) return 0;
        *b = *c = 0;
        AcquireSRWLockExclusive(&g_lock);
        snprintf(g_token, sizeof(g_token), "%s", a);
        snprintf(g_name, sizeof(g_name), "%s", b + 1);
        snprintf(g_username, sizeof(g_username), "%.64s", c + 1);
        { char *nl = strchr(g_username, '\n'); if (nl) *nl = 0; }
        {   /* build 84 on: how it signs in, and the username (an older file: Discord) */
            char *d = strchr(c + 1, '\n'), *e = d ? strchr(d + 1, '\n') : NULL;
            g_has_discord = 1;
            g_has_password = 0;
            g_login[0] = 0;
            if (d && e) {
                *e = 0;
                g_has_discord = strchr(d + 1, 'd') != NULL;
                g_has_password = strchr(d + 1, 'p') != NULL;
                snprintf(g_login, sizeof(g_login), "%.64s", e + 1);
                { char *nl = strchr(g_login, '\n'); if (nl) *nl = 0; }
            }
        }
        ReleaseSRWLockExclusive(&g_lock);
        SecureZeroMemory(plain, sizeof(plain));
    }
    return 1;
}

static void forget(void)
{
    char path[MAX_PATH];
    AcquireSRWLockExclusive(&g_lock);
    SecureZeroMemory(g_token, sizeof(g_token));
    g_name[0] = g_username[0] = g_login[0] = 0;
    g_has_discord = 1;
    g_has_password = 0;
    ReleaseSRWLockExclusive(&g_lock);
    data_path(path, sizeof(path));
    DeleteFileA(path);
}

static void take_account(const char *json)
{
    AcquireSRWLockExclusive(&g_lock);
    if (!json_str(json, "name", g_name, sizeof(g_name))) g_name[0] = 0;
    if (!json_str(json, "username", g_username, sizeof(g_username))) g_username[0] = 0;
    /* an older service sends neither flag: a Discord account */
    g_has_discord = strstr(json, "\"has_discord\"") ? account_pw_flag(json, "has_discord") : 1;
    g_has_password = account_pw_flag(json, "has_password");
    if (!json_str(json, "login", g_login, sizeof(g_login))) g_login[0] = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

/* ---- background work ---- */

static DWORD WINAPI check_thread(LPVOID unused)
{
    char buf[4096], token[65];
    int status;
    (void)unused;
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    status = request(L"GET", L"/api/me", token, NULL, buf, sizeof(buf));
    if (status == 200) {
        take_account(buf);
        save();
        set_state(ACCOUNT_IN);
    } else if (status == 401) {
        forget();
        set_reason("Your sign-in ran out; sign in again.");
        set_state(ACCOUNT_OUT);
    } else {
        set_state(ACCOUNT_IN);      /* the service is unreachable: keep the saved name */
    }
    fprintf(stderr, "[ACCOUNT] saved session checked: HTTP %d\n", status);
    SecureZeroMemory(token, sizeof(token));
    return 0;
}

static void random_hex(char *out)       /* 64 hex digits */
{
    unsigned char b[32];
    int i;
    BCryptGenRandom(NULL, b, sizeof(b), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    for (i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", b[i]);
    SecureZeroMemory(b, sizeof(b));
}

static int sha256_hex(const char *text, char *out)
{
    BCRYPT_ALG_HANDLE alg = NULL;
    unsigned char h[32];
    int i, ok;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return 0;
    ok = BCryptHash(alg, NULL, 0, (PUCHAR)text, (ULONG)strlen(text), h, sizeof(h)) == 0;
    BCryptCloseAlgorithmProvider(alg, 0);
    for (i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", h[i]);
    return ok;
}

int account_pair_secret(char secret[65], char login[65])
{
    random_hex(secret);
    if (sha256_hex(secret, login)) return 1;
    SecureZeroMemory(secret, 65);
    return 0;
}

static DWORD WINAPI sign_in_thread(LPVOID param)
{
    char secret[65], login[65], url[160], body[96], buf[4096];
    int i, misses = 0, link = (int)(INT_PTR)param;
    random_hex(secret);
    if (!sha256_hex(secret, login)) { set_reason("Windows could not start the sign-in."); set_state(ACCOUNT_FAILED); return 0; }
    if (link) {     /* linking Discord to this signed-in account (server password_link.go): register it first */
        char token[65], lb[96];
        int status;
        AcquireSRWLockShared(&g_lock);
        memcpy(token, g_token, sizeof(token));
        ReleaseSRWLockShared(&g_lock);
        snprintf(lb, sizeof(lb), "{\"login\":\"%s\"}", login);
        status = request(L"POST", L"/api/me/link/discord", token, lb, buf, sizeof(buf));
        SecureZeroMemory(token, sizeof(token));
        fprintf(stderr, "[ACCOUNT] link Discord: HTTP %d\n", status);
        if (status != 200) {
            char why[160] = "";
            json_str(buf, "error", why, sizeof(why));
            set_reason(why[0] ? why : "Discord could not be linked right now; try again later.");
            set_state(ACCOUNT_IN);
            return 0;
        }
    }
    snprintf(url, sizeof(url), ACCOUNT_URL "/auth/discord/start?login=%s", login);
    if ((INT_PTR)ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL) <= 32) {
        set_reason("The browser did not open.");
        set_state(ACCOUNT_FAILED);
        return 0;
    }
    snprintf(body, sizeof(body), "{\"secret\":\"%s\"}", secret);
    for (i = 0; i < POLL_COUNT && !g_cancel; i++) {
        int status;
        Sleep(POLL_MS);
        if (g_cancel) break;
        status = request(L"POST", L"/auth/poll", NULL, body, buf, sizeof(buf));
        if (status == 202) { misses = 0; continue; }
        if (status == 200) {
            AcquireSRWLockExclusive(&g_lock);
            if (!json_str(buf, "token", g_token, sizeof(g_token))) g_token[0] = 0;
            ReleaseSRWLockExclusive(&g_lock);
            take_account(buf);
            SecureZeroMemory(buf, sizeof(buf));
            if (strlen(g_token) != 64) break;
            save();
            set_reason("");
            set_state(ACCOUNT_IN);
            fprintf(stderr, "[ACCOUNT] signed in\n");
            SecureZeroMemory(secret, sizeof(secret));
            SecureZeroMemory(body, sizeof(body));
            return 0;
        }
        if (status == 404 || status == 410) {
            char why[160] = "";
            json_str(buf, "error", why, sizeof(why));
            set_reason(status == 410 && why[0] ? why : "The sign-in ran out; try again.");
            set_state(link ? ACCOUNT_IN : ACCOUNT_FAILED);
            return 0;
        }
        if (++misses >= 15) { set_reason("Cannot reach the FFNY Online account server."); set_state(link ? ACCOUNT_IN : ACCOUNT_FAILED); return 0; }
    }
    SecureZeroMemory(secret, sizeof(secret));
    SecureZeroMemory(body, sizeof(body));
    if (g_cancel) { set_reason(""); set_state(link ? ACCOUNT_IN : ACCOUNT_OUT); }
    else { set_reason("The sign-in ran out; try again."); set_state(link ? ACCOUNT_IN : ACCOUNT_FAILED); }
    return 0;
}

static DWORD WINAPI sign_out_thread(LPVOID param)
{
    char buf[256], token[65];
    int del = (int)(INT_PTR)param, status;
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    status = request(del ? L"DELETE" : L"POST", del ? L"/api/me" : L"/auth/logout", token, del ? NULL : "{}",
                     buf, sizeof(buf));
    SecureZeroMemory(token, sizeof(token));
    if (del && status != 204) {
        set_reason("The account could not be deleted right now; try again later.");
        set_state(ACCOUNT_IN);
        return 0;
    }
    forget();
    set_reason(del ? "Your account is deleted." : "");
    set_state(ACCOUNT_OUT);
    fprintf(stderr, "[ACCOUNT] %s\n", del ? "account deleted" : "signed out");
    return 0;
}

static void run(LPTHREAD_START_ROUTINE fn, void *param)
{
    HANDLE t = CreateThread(NULL, 0, fn, param, 0, NULL);
    if (t) CloseHandle(t);
}

/* ---- sign-in without Discord (server relay/accounts/password.go) ---- */

static void pc_text(char out[33])
{
    unsigned char id[16];
    if (ac_pc_id(id)) account_pw_hex16(id, out);
    else out[0] = 0;
}

int account_password(enum AccountPwMode mode, const char *user, const char *pass, const char *code,
                     char *why, int size)
{
    char body[1024], buf[4096], pc[33], token[65] = "";
    const wchar_t *path = mode == ACCOUNT_PW_CREATE ? L"/auth/password/create" : mode == ACCOUNT_PW_RESET
                        ? L"/auth/password/reset" : L"/auth/password/signin";
    int status;
    if (why && size > 0) why[0] = 0;
    pc_text(pc);
    if (account_pw_json(body, sizeof(body), user, pass, mode == ACCOUNT_PW_RESET ? code : NULL, pc) < 0) {
        if (why) snprintf(why, size, "That is too long.");
        return -1;
    }
    status = request(L"POST", path, NULL, body, buf, sizeof(buf));
    SecureZeroMemory(body, sizeof(body));
    fprintf(stderr, "[ACCOUNT] sign-in without Discord (%s): HTTP %d\n",
            mode == ACCOUNT_PW_CREATE ? "new account" : mode == ACCOUNT_PW_RESET ? "reset" : "sign-in", status);
    if ((status == 200 || status == 201) && json_str(buf, "token", token, sizeof(token)) && strlen(token) == 64) {
        AcquireSRWLockExclusive(&g_lock);
        memcpy(g_token, token, sizeof(token));
        ReleaseSRWLockExclusive(&g_lock);
        take_account(buf);
        SecureZeroMemory(token, sizeof(token));
        SecureZeroMemory(buf, sizeof(buf));
        save();
        set_reason("");
        set_state(ACCOUNT_IN);
        fprintf(stderr, "[ACCOUNT] signed in without Discord\n");
        return status;
    }
    if (why && size > 0) {
        if (!json_str(buf, "error", why, size) || !why[0])
            snprintf(why, size, "%s", status == 0 ? "Cannot reach the FFNY Online account server. Check your connection."
                                    : "That did not work; try again later.");
    }
    SecureZeroMemory(buf, sizeof(buf));
    return status ? status : 0;
}

int account_password_status(int *create, char *forgot, int size)
{
    char buf[2048];
    int status = request(L"GET", L"/auth/password/status", NULL, NULL, buf, sizeof(buf));
    if (create) *create = status == 200 && account_pw_flag(buf, "create");
    if (forgot && size > 0 && !(status == 200 && json_str(buf, "forgot", forgot, size))) forgot[0] = 0;
    return status == 200 && account_pw_flag(buf, "enabled");
}

int account_set_password(const char *user_or_old, const char *pass, int adding, char *why, int size)
{
    char body[600], buf[4096], token[65];
    int status;
    if (why && size > 0) why[0] = 0;
    if (g_state != ACCOUNT_IN) return -1;
    if ((adding ? account_pw_json(body, sizeof(body), user_or_old, pass, NULL, NULL)
                : account_pw_change_json(body, sizeof(body), user_or_old, pass)) < 0) return -1;
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    status = request(L"POST", L"/api/me/password", token, body, buf, sizeof(buf));
    SecureZeroMemory(body, sizeof(body));
    SecureZeroMemory(token, sizeof(token));
    fprintf(stderr, "[ACCOUNT] %s: HTTP %d\n", adding ? "add a password" : "change the password", status);
    if (status == 200) {
        take_account(strstr(buf, "\"account\"") ? strstr(buf, "\"account\"") : buf);
        save();
        return status;
    }
    if (status == 401 && !strstr(buf, "\"code\"")) { forget(); set_reason("Your sign-in ran out; sign in again."); set_state(ACCOUNT_OUT); }
    if (why && size > 0 && (!json_str(buf, "error", why, size) || !why[0]))
        snprintf(why, size, "%s", status == 0 ? "Cannot reach the FFNY Online account server." : "That did not work; try again later.");
    return status;
}

/* The service's offer, asked once in the background (account_password_offered). */
static volatile LONG g_offer_asked, g_offer_known, g_offer_on, g_offer_create;
static char g_offer_forgot[400];

static DWORD WINAPI offer_thread(LPVOID unused)
{
    char forgot[400];
    int create = 0, on;
    (void)unused;
    on = account_password_status(&create, forgot, sizeof(forgot));
    AcquireSRWLockExclusive(&g_lock);
    snprintf(g_offer_forgot, sizeof(g_offer_forgot), "%s", forgot);
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedExchange(&g_offer_on, on);
    InterlockedExchange(&g_offer_create, create);
    InterlockedExchange(&g_offer_known, 1);
    fprintf(stderr, "[ACCOUNT] sign-in without Discord offered: %d (new accounts %d)\n", on, create);
    return 0;
}

int account_password_offered(int *create, char *forgot, int size)
{
    if (!InterlockedExchange(&g_offer_asked, 1)) run(offer_thread, NULL);
    if (create) *create = g_offer_known && g_offer_create;
    if (forgot && size > 0) {
        AcquireSRWLockShared(&g_lock);
        snprintf(forgot, size, "%s", g_offer_forgot);
        ReleaseSRWLockShared(&g_lock);
    }
    return g_offer_known && g_offer_on;
}

void account_password_offer_refresh(void) { InterlockedExchange(&g_offer_asked, 0); }

void account_link_discord(void)
{
    if (g_state != ACCOUNT_IN || g_has_discord) return;
    g_cancel = 0;
    set_reason("");
    set_state(ACCOUNT_WAITING);
    run(sign_in_thread, (void *)1);
}

int account_has_discord(void) { return g_has_discord; }
int account_has_password(void) { return g_has_password; }

void account_login(char *out, int size)
{
    AcquireSRWLockShared(&g_lock);
    snprintf(out, size, "%s", g_login);
    ReleaseSRWLockShared(&g_lock);
}

/* tests: RECOMP_TEST_PASSWORD="create|signin:username:password" signs in that way at start, only against a
 * local test account service (RECOMP_ACCOUNT_SERVER on this PC); never with the saved session */
static DWORD WINAPI test_password_thread(LPVOID unused)
{
    char spec[300], why[200];
    const char *t = getenv("RECOMP_TEST_PASSWORD");
    char *user, *pass;
    int status;
    (void)unused;
    snprintf(spec, sizeof(spec), "%s", t);
    user = strchr(spec, ':');
    pass = user ? strchr(user + 1, ':') : NULL;
    if (!user || !pass) { set_state(ACCOUNT_OUT); return 0; }
    *user++ = 0;
    *pass++ = 0;
    status = account_password(!strcmp(spec, "create") ? ACCOUNT_PW_CREATE : ACCOUNT_PW_SIGNIN, user, pass, NULL,
                              why, sizeof(why));
    if (status != 200 && status != 201) {
        fprintf(stderr, "[ACCOUNT] test password sign-in refused: %s\n", why);
        set_reason(why);
        set_state(ACCOUNT_FAILED);
    }
    SecureZeroMemory(spec, sizeof(spec));
    return 0;
}

/* Start-up only reads the saved session: no network while the game boots, which is when the
 * runtime is most sensitive to other threads (docs/research/boot-intermittents.md). The session
 * is checked with the service the first time the online screens or the Online page need it. */
void account_init(void)
{
    static LONG once;
    if (InterlockedExchange(&once, 1)) return;
    {
        const char *t = getenv("RECOMP_TEST_PASSWORD");
        if (t && *t && account_test_server_local()) {
            set_state(ACCOUNT_CHECKING);
            run(test_password_thread, NULL);
            return;
        }
    }
    if (load()) set_state(ACCOUNT_IN);
}

void account_refresh(void)
{
    static LONG once;
    if (g_state != ACCOUNT_IN || InterlockedExchange(&once, 1)) return;
    set_state(ACCOUNT_CHECKING);
    run(check_thread, NULL);
}

void account_sign_in(void)
{
    LONG s = g_state;
    if (s == ACCOUNT_WAITING || s == ACCOUNT_IN || s == ACCOUNT_CHECKING) return;
    g_cancel = 0;
    set_reason("");
    set_state(ACCOUNT_WAITING);
    run(sign_in_thread, NULL);
}

void account_cancel(void) { if (g_state == ACCOUNT_WAITING) g_cancel = 1; }

void account_sign_out(void)
{
    if (g_state == ACCOUNT_IN) { set_state(ACCOUNT_CHECKING); run(sign_out_thread, (void *)0); }
}

void account_delete(void)
{
    if (g_state == ACCOUNT_IN) { set_state(ACCOUNT_CHECKING); run(sign_out_thread, (void *)1); }
}

enum AccountState account_state(void) { return (enum AccountState)g_state; }

int account_game_name(char *out, int size)
{
    char name[129], user[65];
    int n = 0, i;
    if (g_state != ACCOUNT_IN && g_state != ACCOUNT_CHECKING) return 0;
    AcquireSRWLockShared(&g_lock);
    memcpy(name, g_name, sizeof(name));
    memcpy(user, g_username, sizeof(user));
    ReleaseSRWLockShared(&g_lock);
    if (size > 25) size = 25;                   /* the protocol's 24 bytes */
    for (i = 0; name[i] && n < size - 1; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c > 0x7E) continue;     /* the game's fonts: printable ASCII */
        if (c == ' ' && (n == 0 || out[n - 1] == ' ')) continue;
        out[n++] = (char)c;
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    if (n < 2) snprintf(out, size, "%s", user);
    return out[0] != 0;
}

void account_text(char *out, int size)
{
    AcquireSRWLockShared(&g_lock);
    if (g_state == ACCOUNT_IN || g_state == ACCOUNT_CHECKING) {
        if (g_name[0] && strcmp(g_name, g_username)) snprintf(out, size, "%s (@%s)", g_name, g_username);
        else snprintf(out, size, "@%s", g_username);
        if (g_reason[0] && g_state == ACCOUNT_IN) {
            size_t len = strlen(out);
            snprintf(out + len, size - len, " - %s", g_reason);
        }
    } else {
        snprintf(out, size, "%s", g_reason);
    }
    ReleaseSRWLockShared(&g_lock);
}

/* ---- match results and statistics ---- */

static char *g_stats;               /* the service's text form; guarded by g_lock */
static volatile LONG g_stats_busy;
static volatile LONG g_stats_page;  /* the leaderboard page asked for (10 places each) */

static void fetch_stats_now(void)
{
    char token[65], *buf = (char *)malloc(16384);
    int status;
    if (!buf) return;
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    {
        wchar_t path[64];
        swprintf(path, 64, L"/api/me/stats?format=text&page=%ld", (long)g_stats_page);
        status = request(L"GET", path, token, NULL, buf, 16384);
    }
    SecureZeroMemory(token, sizeof(token));
    if (status == 200) {
        AcquireSRWLockExclusive(&g_lock);
        free(g_stats);
        g_stats = buf;
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    free(buf);
    if (status == 401) { forget(); set_reason("Your sign-in ran out; sign in again."); set_state(ACCOUNT_OUT); }
}

static DWORD WINAPI stats_thread(LPVOID unused)
{
    (void)unused;
    fetch_stats_now();
    InterlockedExchange(&g_stats_busy, 0);
    return 0;
}

void account_fetch_stats(void)
{
    if (g_state != ACCOUNT_IN || InterlockedExchange(&g_stats_busy, 1)) return;
    run(stats_thread, NULL);
}

int account_stats_busy(void) { return g_stats_busy != 0; }

void account_fetch_stats_page(int page)
{
    InterlockedExchange(&g_stats_page, page < 0 ? 0 : page);
    account_fetch_stats();
}

int account_stats(char *out, int size)
{
    int ok;
    AcquireSRWLockShared(&g_lock);
    ok = g_stats != NULL;
    if (ok) snprintf(out, size, "%s", g_stats);
    ReleaseSRWLockShared(&g_lock);
    return ok;
}

static DWORD WINAPI report_thread(LPVOID param)
{
    char *json = (char *)param, token[65], buf[512];
    int status;
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    status = request(L"POST", L"/api/matches", token, json, buf, sizeof(buf));
    SecureZeroMemory(token, sizeof(token));
    free(json);
    fprintf(stderr, "[ACCOUNT] match result sent: HTTP %d\n", status);
    if (status == 202) {
        Sleep(50000);               /* the service counts it once every report is in (45 s) */
        if (!InterlockedExchange(&g_stats_busy, 1)) { fetch_stats_now(); InterlockedExchange(&g_stats_busy, 0); }
    }
    return 0;
}

int account_ranked_ticket(char out[33])
{
    char token[65], buf[256];
    int status, i;
    out[0] = 0;
    if (g_state != ACCOUNT_IN) return 0;
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    status = request(L"POST", L"/api/ranked/ticket?format=text", token, NULL, buf, sizeof(buf));
    SecureZeroMemory(token, sizeof(token));
    if (status == 401) { forget(); set_reason("Your sign-in ran out; sign in again."); set_state(ACCOUNT_OUT); }
    if (status != 200) return 0;
    for (i = 0; i < 32; i++)
        if (!isxdigit((unsigned char)buf[i])) return 0;
    memcpy(out, buf, 32);
    out[32] = 0;
    return 1;
}

int account_call(const char *method, const char *path, const char *json, char *buf, int size, int receive_ms,
                 void *volatile *live)
{
    char token[65];
    wchar_t wmethod[16], wpath[512];
    int status;
    if (size > 0) buf[0] = 0;
    if (g_state != ACCOUNT_IN && g_state != ACCOUNT_CHECKING) return -1;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512)) return -1;
    swprintf(wmethod, 16, L"%hs", method);
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    status = request_full(wmethod, wpath, token, "application/json", json, json ? (DWORD)strlen(json) : 0, buf, size,
                          0, receive_ms, live);
    SecureZeroMemory(token, sizeof(token));
    if (status == 401) { forget(); set_reason("Your sign-in ran out; sign in again."); set_state(ACCOUNT_OUT); }
    return status;
}

int account_send(const char *method, const char *path, const char *ctype, const void *body, int len,
                 const char *headers, char *buf, int size)
{
    char token[65];
    wchar_t wmethod[16], wpath[512];
    int status;
    if (size > 0) buf[0] = 0;
    if (g_state != ACCOUNT_IN && g_state != ACCOUNT_CHECKING) return -1;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512)) return -1;
    swprintf(wmethod, 16, L"%hs", method);
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    g_extra_headers = headers;
    status = request_full(wmethod, wpath, token, ctype ? ctype : "application/octet-stream", body, (DWORD)(body ? len : 0),
                          buf, size, 0, 0, NULL);
    g_extra_headers = NULL;
    SecureZeroMemory(token, sizeof(token));
    if (status == 401) { forget(); set_reason("Your sign-in ran out; sign in again."); set_state(ACCOUNT_OUT); }
    return status;
}

/* The launcher's rename (POST /api/me/name) answered with the account: keep its new FFNY name. */
void account_take_renamed(const char *json)
{
    take_account(json);
    save();
}

int account_fetch(const char *path, void *buf, int size, int *len)
{
    wchar_t wpath[512];
    int status;
    if (len) *len = 0;
    if (size < 2 || !MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512)) return -1;
    status = request_full(L"GET", wpath, NULL, NULL, NULL, 0, (char *)buf, size, 0, 0, NULL);
    if (len) *len = status == 200 ? g_last_len : 0;
    return status;
}

int account_test_server_local(void)
{
    const char *srv = getenv("RECOMP_ACCOUNT_SERVER");
    return srv && (!strncmp(srv, "127.0.0.1:", 10) || !strncmp(srv, "localhost:", 10));
}

int account_download(const char *path, unsigned long long from, int (*sink)(void *ctx, const char *data, unsigned n),
                     void *ctx, int *partial)
{
    HINTERNET session = NULL, connect = NULL, req = NULL;
    DWORD status = 0, ssize = sizeof(status);
    const char *test = getenv("RECOMP_ACCOUNT_SERVER");
    wchar_t host[64] = ACCOUNT_HOST, wpath[512], headers[300];
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    DWORD secure = WINHTTP_FLAG_SECURE;
    char token[65];
    char *buf;
    if (partial) *partial = 0;
    if (g_state != ACCOUNT_IN && g_state != ACCOUNT_CHECKING) return -1;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512)) return -1;
    if (test && *test) {
        const char *colon = strchr(test, ':');
        int len = colon ? (int)(colon - test) : (int)strlen(test);
        swprintf(host, 64, L"%.*hs", len, test);
        port = (INTERNET_PORT)(colon ? atoi(colon + 1) : 80);
        secure = 0;
    }
    buf = (char *)malloc(256 * 1024);
    if (!buf) return 0;
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    swprintf(headers, 300, L"Authorization: Bearer %hs\r\n", token);
    SecureZeroMemory(token, sizeof(token));
    if (from) swprintf(headers + wcslen(headers), 300 - wcslen(headers), L"Range: bytes=%llu-\r\n", from);
    session = WinHttpOpen(L"FFNY-Online-Launcher/1", test && *test ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) WinHttpSetTimeouts(session, 5000, 5000, 15000, 30000);
    if (session) connect = WinHttpConnect(session, host, port, 0);
    if (connect) req = WinHttpOpenRequest(connect, L"GET", wpath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure);
    if (req && WinHttpSendRequest(req, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, NULL) &&
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &ssize, NULL)) {
        if (status == 200 || status == 206) {
            if (partial) *partial = status == 206;
            for (;;) {
                DWORD avail = 0, got = 0;
                if (!WinHttpQueryDataAvailable(req, &avail)) { status = 0; break; }
                if (!avail) break;
                if (avail > 256 * 1024) avail = 256 * 1024;
                if (!WinHttpReadData(req, buf, avail, &got)) { status = 0; break; }
                if (!got) break;
                if (!sink(ctx, buf, got)) { status = 0; break; }
            }
        }
    } else {
        status = 0;
    }
    SecureZeroMemory(headers, sizeof(headers));
    free(buf);
    if (req) WinHttpCloseHandle(req);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    if (status == 401) { forget(); set_reason("Your sign-in ran out; sign in again."); set_state(ACCOUNT_OUT); }
    return (int)status;
}

void account_cancel_call(void *volatile *live)
{
    AcquireSRWLockExclusive(&g_live_lock);
    if (*live && *live != (void *)1) WinHttpCloseHandle((HINTERNET)*live);
    *live = (void *)1;      /* closed, or not to start: the owner leaves the handle alone */
    ReleaseSRWLockExclusive(&g_live_lock);
}

static int send_report_body(const wchar_t *path, const char *ctype, const void *body, int len, char *out, int size)
{
    char token[65];
    int status;
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    status = request_body(L"POST", path, g_state == ACCOUNT_IN ? token : NULL, ctype, body, (DWORD)len, out, size);
    SecureZeroMemory(token, sizeof(token));
    return status;
}

int account_send_bug_report(const char *ctype, const void *body, int len, char *out, int size)
{
    return send_report_body(L"/api/report?format=text", ctype, body, len, out, size);
}

int account_send_report_note(const char *ctype, const void *body, int len, char *out, int size)
{
    return send_report_body(L"/api/report/note?format=text", ctype, body, len, out, size);
}

void account_report_match(const char *json)
{
    char *copy;
    if (g_state != ACCOUNT_IN && g_state != ACCOUNT_CHECKING) return;
    copy = _strdup(json);
    if (copy) run(report_thread, copy);
}

/* The crash report: everything it needs is in these, copied before the request, so the request
 * allocates nothing of ours (the crash may be in the heap). */
static char g_crash_json[2048], g_crash_token[65];
static volatile LONG g_crash_once, g_crash_status;

static DWORD WINAPI crash_thread(LPVOID limit)
{
    char buf[256];
    int status = request_body_ms(L"POST", L"/api/matches", g_crash_token, "application/json", g_crash_json,
                                 (DWORD)strlen(g_crash_json), buf, sizeof(buf), (int)(INT_PTR)limit);
    InterlockedExchange(&g_crash_status, status);
    return 0;
}

int account_report_crash(const char *json, unsigned timeout_ms)
{
    HANDLE t;
    DWORD waited;
    size_t len = strlen(json);
    if (g_state != ACCOUNT_IN && g_state != ACCOUNT_CHECKING) return -1;
    if (len >= sizeof(g_crash_json) || InterlockedExchange(&g_crash_once, 1)) return -1;
    memcpy(g_crash_json, json, len + 1);
    /* Not a blocking acquire: the crashing thread may be the one holding the lock. The token only
     * changes at sign-in and sign-out; a torn copy is refused by the service, nothing worse. */
    if (TryAcquireSRWLockShared(&g_lock)) {
        memcpy(g_crash_token, g_token, sizeof(g_crash_token));
        ReleaseSRWLockShared(&g_lock);
    } else {
        memcpy(g_crash_token, g_token, sizeof(g_crash_token));
    }
    g_crash_token[sizeof(g_crash_token) - 1] = 0;
    /* On a thread of its own, waited for at most timeout_ms: whatever WinHTTP waits on (a name
     * lookup, a lock the crashed thread holds), the crash goes on after that. */
    t = CreateThread(NULL, 256 * 1024, crash_thread, (LPVOID)(INT_PTR)timeout_ms, 0, NULL);
    if (!t) return -1;
    waited = WaitForSingleObject(t, timeout_ms);
    CloseHandle(t);
    if (waited != WAIT_OBJECT_0) return 0;      /* still trying: it may go out while the crash goes on */
    SecureZeroMemory(g_crash_token, sizeof(g_crash_token));
    return (int)g_crash_status;
}

#else   /* sign-in is Windows only for now, like online play */

void account_init(void) {}
void account_refresh(void) {}
void account_sign_in(void) {}
void account_take_renamed(const char *json) { (void)json; }
int account_password(enum AccountPwMode mode, const char *user, const char *pass, const char *code, char *why, int size)
{ (void)mode; (void)user; (void)pass; (void)code; if (why && size > 0) why[0] = 0; return -1; }
int account_password_status(int *create, char *forgot, int size) { if (create) *create = 0; if (forgot && size > 0) forgot[0] = 0; return 0; }
int account_set_password(const char *user_or_old, const char *pass, int adding, char *why, int size)
{ (void)user_or_old; (void)pass; (void)adding; if (why && size > 0) why[0] = 0; return -1; }
void account_link_discord(void) {}
int account_password_offered(int *create, char *forgot, int size) { if (create) *create = 0; if (forgot && size > 0) forgot[0] = 0; return 0; }
void account_password_offer_refresh(void) {}
int account_has_discord(void) { return 1; }
int account_has_password(void) { return 0; }
void account_login(char *out, int size) { if (size > 0) out[0] = 0; }
void account_cancel(void) {}
void account_sign_out(void) {}
void account_delete(void) {}
enum AccountState account_state(void) { return ACCOUNT_OUT; }
int account_game_name(char *out, int size) { (void)out; (void)size; return 0; }
void account_text(char *out, int size) { if (size > 0) out[0] = 0; }
void account_report_match(const char *json) { (void)json; }
int account_report_crash(const char *json, unsigned timeout_ms) { (void)json; (void)timeout_ms; return -1; }
void account_fetch_stats(void) {}
void account_fetch_stats_page(int page) { (void)page; }
int account_stats_busy(void) { return 0; }
int account_stats(char *out, int size) { (void)out; (void)size; return 0; }
int account_ranked_ticket(char out[33]) { out[0] = 0; return 0; }
int account_call(const char *method, const char *path, const char *json, char *buf, int size, int receive_ms,
                 void *volatile *live)
{ (void)method; (void)path; (void)json; (void)receive_ms; (void)live; if (size > 0) buf[0] = 0; return -1; }
void account_cancel_call(void *volatile *live) { (void)live; }
int account_send(const char *method, const char *path, const char *ctype, const void *body, int len,
                 const char *headers, char *buf, int size)
{ (void)method; (void)path; (void)ctype; (void)body; (void)len; (void)headers; if (size > 0) buf[0] = 0; return -1; }
int account_download(const char *path, unsigned long long from, int (*sink)(void *ctx, const char *data, unsigned n),
                     void *ctx, int *partial)
{ (void)path; (void)from; (void)sink; (void)ctx; if (partial) *partial = 0; return -1; }
int account_test_server_local(void) { return 0; }
int account_pair_secret(char secret[65], char login[65]) { secret[0] = login[0] = 0; return 0; }
int account_send_bug_report(const char *ctype, const void *body, int len, char *out, int size)
{ (void)ctype; (void)body; (void)len; if (size > 0) out[0] = 0; return 0; }
int account_send_report_note(const char *ctype, const void *body, int len, char *out, int size)
{ (void)ctype; (void)body; (void)len; if (size > 0) out[0] = 0; return 0; }

#endif

const char *account_privacy_url(void) { return "https://play.defjam.online/privacy"; }
