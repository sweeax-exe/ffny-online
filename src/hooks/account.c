/*
 * FFNY Online account: sign-in with Discord, through the project's account service
 * (netplay/relay/accounts, behind https://play.defjam.online).
 *
 * The launcher never holds Discord's client secret. It makes a random secret, opens the browser
 * on /auth/discord/start with only the secret's SHA-256, and polls /auth/poll with the secret
 * itself until the player has signed in with Discord; the answer is a session token. The token,
 * the name and the username are kept in account.dat beside settings.ini, encrypted with DPAPI
 * for the Windows user, so a settings file shared in a bug report carries no session.
 */
#include "account.h"
#include "pc_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void set_state(enum AccountState s) { InterlockedExchange(&g_state, (LONG)s); }

static void set_reason(const char *text)
{
    AcquireSRWLockExclusive(&g_lock);
    snprintf(g_reason, sizeof(g_reason), "%s", text);
    ReleaseSRWLockExclusive(&g_lock);
}

/* One HTTPS request to the account service; the body (if any) as JSON. Returns the HTTP status,
 * 0 when the service could not be reached; the answer goes to buf. */
static int request(const wchar_t *method, const wchar_t *path, const char *token, const char *body,
                   char *buf, int size)
{
    HINTERNET session = NULL, connect = NULL, req = NULL;
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
    if (session) WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);
    if (session) connect = WinHttpConnect(session, host, port, 0);
    if (connect) req = WinHttpOpenRequest(connect, method, path, NULL, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, secure);
    if (req) {
        wchar_t headers[200] = L"";
        DWORD blen = body ? (DWORD)strlen(body) : 0;
        if (token && *token) swprintf(headers, 200, L"Authorization: Bearer %hs\r\n", token);
        if (body) wcscat(headers, L"Content-Type: application/json\r\n");
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
    if (req) WinHttpCloseHandle(req);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return (int)status;
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
    char path[MAX_PATH], plain[300];
    DATA_BLOB in, out = { 0 };
    FILE *f;
    AcquireSRWLockShared(&g_lock);
    snprintf(plain, sizeof(plain), "%s\n%s\n%s\n", g_token, g_name, g_username);
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
    data_path(path, sizeof(path));
    f = fopen(path, "rb");
    if (!f) return 0;
    got = fread(blob, 1, sizeof(blob), f);
    fclose(f);
    in.pbData = (BYTE *)blob;
    in.cbData = (DWORD)got;
    if (!got || !CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return 0;
    {
        char plain[300];
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
    g_name[0] = g_username[0] = 0;
    ReleaseSRWLockExclusive(&g_lock);
    data_path(path, sizeof(path));
    DeleteFileA(path);
}

static void take_account(const char *json)
{
    AcquireSRWLockExclusive(&g_lock);
    if (!json_str(json, "name", g_name, sizeof(g_name))) g_name[0] = 0;
    if (!json_str(json, "username", g_username, sizeof(g_username))) g_username[0] = 0;
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

static DWORD WINAPI sign_in_thread(LPVOID unused)
{
    char secret[65], login[65], url[160], body[96], buf[4096];
    int i, misses = 0;
    (void)unused;
    random_hex(secret);
    if (!sha256_hex(secret, login)) { set_reason("Windows could not start the sign-in."); set_state(ACCOUNT_FAILED); return 0; }
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
            char why[96] = "";
            json_str(buf, "error", why, sizeof(why));
            set_reason(status == 410 && why[0] ? why : "The sign-in ran out; try again.");
            set_state(ACCOUNT_FAILED);
            return 0;
        }
        if (++misses >= 15) { set_reason("Cannot reach play.defjam.online."); set_state(ACCOUNT_FAILED); return 0; }
    }
    SecureZeroMemory(secret, sizeof(secret));
    SecureZeroMemory(body, sizeof(body));
    if (g_cancel) { set_reason(""); set_state(ACCOUNT_OUT); }
    else { set_reason("The sign-in ran out; try again."); set_state(ACCOUNT_FAILED); }
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

/* Start-up only reads the saved session: no network while the game boots, which is when the
 * runtime is most sensitive to other threads (docs/research/boot-intermittents.md). The session
 * is checked with the service the first time the online screens or the Online page need it. */
void account_init(void)
{
    static LONG once;
    if (InterlockedExchange(&once, 1)) return;
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

static void fetch_stats_now(void)
{
    char token[65], *buf = (char *)malloc(16384);
    int status;
    if (!buf) return;
    AcquireSRWLockShared(&g_lock);
    memcpy(token, g_token, sizeof(token));
    ReleaseSRWLockShared(&g_lock);
    status = request(L"GET", L"/api/me/stats?format=text", token, NULL, buf, 16384);
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

void account_report_match(const char *json)
{
    char *copy;
    if (g_state != ACCOUNT_IN && g_state != ACCOUNT_CHECKING) return;
    copy = _strdup(json);
    if (copy) run(report_thread, copy);
}

#else   /* sign-in is Windows only for now, like online play */

void account_init(void) {}
void account_refresh(void) {}
void account_sign_in(void) {}
void account_cancel(void) {}
void account_sign_out(void) {}
void account_delete(void) {}
enum AccountState account_state(void) { return ACCOUNT_OUT; }
int account_game_name(char *out, int size) { (void)out; (void)size; return 0; }
void account_text(char *out, int size) { if (size > 0) out[0] = 0; }
void account_report_match(const char *json) { (void)json; }
void account_fetch_stats(void) {}
int account_stats_busy(void) { return 0; }
int account_stats(char *out, int size) { (void)out; (void)size; return 0; }

#endif

const char *account_privacy_url(void) { return "https://play.defjam.online/privacy"; }
