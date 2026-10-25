/* The launcher's updater. See updater.h and docs/updates.md.
 *
 * Trust: latest.json is only believed when latest.json.sig is a valid ECDSA P-256
 * signature over its exact bytes by the key whose public half is below (made with
 * netplay/relay/cmd/updatesign; the private half stays with whoever publishes). The
 * setup it names is only run when its SHA-256 is the one the signed manifest states.
 * The server and the connection are therefore not trusted; plain HTTP is enough.
 *
 * Layout: the working directory is the source tree (pc_settings_init). A copy installed
 * by setup lives at <install>\versions\<id>\source, with <install>\installed.json
 * naming its data folder; a development checkout has no such receipt and is told to
 * update from its source instead. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "updater.h"

#if !defined(_WIN32)

void updater_check(void) {}
void updater_restart(void) {}
enum UpdaterState updater_state(void) { return UPDATER_CURRENT; }
int updater_local_build(void) { return 0; }
void updater_info(int *b, char *v, int vs, char *n, int ns) { if (b) *b = 0; if (vs > 0) v[0] = 0; if (ns > 0) n[0] = 0; }
int updater_can_install(void) { return 0; }
void updater_install(void) {}
double updater_progress(void) { return 0; }
void updater_message(char *t, int size) { if (size > 0) t[0] = 0; }

#else

#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

/* The update service on the project's server (netplay/relay -updates-listen): its name, its address
 * (for a resolver that does not know the name yet), then the first server, for as long as installs
 * from before the name still ask there.
 * The manifest is signed (below), so the host only needs to be reachable, not trusted. */
static const wchar_t *const k_update_hosts[] = { L"play.defjam.online", L"49.12.78.32", L"142.132.178.17" };
#define UPDATE_PORT 27778
/* updatesign's public key: X and Y of the P-256 point, big-endian. */
static const char k_public_key[] =
    "6f8ce805a386c294c1a24de83d07ce6a0c296ebf836d00434e9a5f0256c6dd7a"
    "6d60d26ef38562ee1e29b99dddca155d89f2a6a28a32c4559c78ea00d28e5c05";

#define MANIFEST_MAX (64 * 1024)
#define SETUP_MAX    (512u * 1024 * 1024)

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static volatile LONG g_state = UPDATER_IDLE;
static char g_message[512];
static int g_build;
static char g_version[64], g_notes[1024], g_setup[96], g_sha256[65];
static unsigned long long g_size;
static volatile LONG g_permille;

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cs);
    return TRUE;
}

static void say(enum UpdaterState state, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    EnterCriticalSection(&g_cs);
    vsnprintf(g_message, sizeof(g_message), format, args);
    LeaveCriticalSection(&g_cs);
    va_end(args);
    InterlockedExchange(&g_state, state);
    fprintf(stderr, "[UPDATE] %s\n", g_message);
    fflush(stderr);
}

int updater_local_build(void)
{
    int build = 0;
    FILE *f;
    /* tests: RECOMP_TEST_LOCAL_BUILD=22 plays an older build, so the published one shows as an update */
    const char *test = getenv("RECOMP_TEST_LOCAL_BUILD");
    if (test && atoi(test) > 0) return atoi(test);
    f = fopen("config\\remaster-version.txt", "r");
    if (f) {
        if (fscanf(f, "%d", &build) != 1) build = 0;
        fclose(f);
    }
    return build;
}

/* ---- the installation ---------------------------------------------------------------- */

/* <install> and its data folder from <install>\installed.json, or 0 for a development checkout. */
static int find_install(char *install, size_t isize, char *data, size_t dsize)
{
    char cwd[MAX_PATH], path[MAX_PATH * 2], buf[16384], *p;
    size_t n, i, o = 0;
    FILE *f;
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) return 0;
    snprintf(path, sizeof(path), "%s\\..\\..\\..\\installed.json", cwd);
    f = fopen(path, "rb");
    if (!f) return 0;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    p = strstr(buf, "\"data\"");
    if (!p || !(p = strchr(p + 6, '"'))) return 0;
    for (i = 1; p[i] && p[i] != '"' && o + 1 < dsize; i++) {
        if (p[i] == '\\' && p[i + 1]) i++;              /* JSON escapes: \\ and \" */
        data[o++] = p[i];
    }
    data[o] = 0;
    if (!GetFullPathNameA(path, (DWORD)isize, install, NULL)) return 0;
    if ((p = strrchr(install, '\\')) != NULL) *p = 0;   /* drop installed.json */
    return data[0] != 0;
}

int updater_can_install(void)
{
    char install[MAX_PATH * 2], data[MAX_PATH * 2];
    return find_install(install, sizeof(install), data, sizeof(data));
}

/* ---- HTTP ------------------------------------------------------------------------------- */

typedef int (*Sink)(const void *data, DWORD size, void *user);

/* One host; *started says whether data reached the sink (then no other host may be tried). */
static int http_get_from(const wchar_t *host, const wchar_t *path, Sink sink, void *user,
                         unsigned long long *length, int *started)
{
    HINTERNET session = NULL, connect = NULL, request = NULL;
    int ok = 0;
    DWORD status = 0, size = sizeof(status);
    session = WinHttpOpen(L"DefJamRecomp-Updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) WinHttpSetTimeouts(session, 5000, 5000, 10000, 30000);
    if (session) connect = WinHttpConnect(session, host, UPDATE_PORT, 0);
    if (connect) request = WinHttpOpenRequest(connect, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &size, NULL) &&
        status == 200) {
        wchar_t text[32];
        DWORD tsize = sizeof(text);
        if (length) {
            *length = 0;
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, NULL, text, &tsize, NULL))
                *length = _wcstoui64(text, NULL, 10);
        }
        ok = 1;
        for (;;) {
            char buf[64 * 1024];
            DWORD avail = 0, got = 0;
            if (!WinHttpQueryDataAvailable(request, &avail)) { ok = 0; break; }
            if (!avail) break;
            if (avail > sizeof(buf)) avail = sizeof(buf);
            if (!WinHttpReadData(request, buf, avail, &got)) { ok = 0; break; }
            if (!got) break;
            *started = 1;
            if (!sink(buf, got, user)) { ok = 0; break; }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

static int http_get(const wchar_t *path, Sink sink, void *user, unsigned long long *length)
{
    size_t i;
    for (i = 0; i < sizeof(k_update_hosts) / sizeof(k_update_hosts[0]); i++) {
        int started = 0;
        if (http_get_from(k_update_hosts[i], path, sink, user, length, &started)) return 1;
        if (started) return 0;
    }
    return 0;
}

typedef struct { char *data; DWORD used, cap; } Memory;

static int to_memory(const void *data, DWORD size, void *user)
{
    Memory *m = (Memory *)user;
    if (m->used + size > m->cap) return 0;
    memcpy(m->data + m->used, data, size);
    m->used += size;
    return 1;
}

/* ---- trust ------------------------------------------------------------------------------ */

static int unhex(const char *text, unsigned char *out, size_t bytes)
{
    size_t i;
    for (i = 0; i < bytes; i++) {
        unsigned v;
        if (sscanf(text + 2 * i, "%2x", &v) != 1) return 0;
        out[i] = (unsigned char)v;
    }
    return 1;
}

static int sha256(const void *data, ULONG size, unsigned char out[32])
{
    return BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (PUCHAR)data, size, out, 32) == 0;
}

static int signature_ok(const char *manifest, DWORD size, const char *sig_hex)
{
    struct { BCRYPT_ECCKEY_BLOB head; unsigned char xy[64]; } blob;
    unsigned char sig[64], digest[32];
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    int ok = 0;
    if (strlen(sig_hex) < 128 || !unhex(sig_hex, sig, 64) || !unhex(k_public_key, blob.xy, 64)) return 0;
    if (!sha256(manifest, size, digest)) return 0;
    blob.head.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    blob.head.cbKey = 32;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, NULL, 0) == 0 &&
        BCryptImportKeyPair(alg, NULL, BCRYPT_ECCPUBLIC_BLOB, &key, (PUCHAR)&blob, sizeof(blob), 0) == 0)
        ok = BCryptVerifySignature(key, NULL, digest, 32, sig, 64, 0) == 0;
    if (key) BCryptDestroyKey(key);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

/* ---- the manifest ----------------------------------------------------------------------- */

static int json_string(const char *json, const char *key, char *out, size_t size)
{
    char needle[48];
    const char *p;
    size_t o = 0;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (!(p = strstr(json, needle)) || !(p = strchr(p + strlen(needle), ':')) || !(p = strchr(p, '"'))) return 0;
    for (p++; *p && *p != '"' && o + 1 < size; p++) {
        if (*p == '\\' && p[1] == 'u' && isxdigit((unsigned char)p[2]) && isxdigit((unsigned char)p[3])
                && isxdigit((unsigned char)p[4]) && isxdigit((unsigned char)p[5])) {
            /* \uXXXX: Go's encoder writes '>' and '&' this way ("Launcher > Settings" showed as
             * "Launcher u003e Settings"); as UTF-8, surrogates left out */
            char hex[5] = { p[2], p[3], p[4], p[5], 0 };
            unsigned c = (unsigned)strtoul(hex, NULL, 16);
            p += 5;
            if (c < 0x80) {
                out[o++] = (char)c;
            } else if (c < 0x800 && o + 2 < size) {
                out[o++] = (char)(0xC0 | (c >> 6));
                out[o++] = (char)(0x80 | (c & 0x3F));
            } else if (c >= 0x800 && (c < 0xD800 || c > 0xDFFF) && o + 3 < size) {
                out[o++] = (char)(0xE0 | (c >> 12));
                out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
                out[o++] = (char)(0x80 | (c & 0x3F));
            }
        } else if (*p == '\\' && p[1]) {
            p++;
            out[o++] = *p == 'n' ? '\n' : *p;
        } else {
            out[o++] = *p;
        }
    }
    out[o] = 0;
    return 1;
}

static long long json_number(const char *json, const char *key)
{
    char needle[48];
    const char *p;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (!(p = strstr(json, needle)) || !(p = strchr(p + strlen(needle), ':'))) return -1;
    return _strtoi64(p + 1, NULL, 10);
}

static int safe_name(const char *name)
{
    const char *p;
    if (!*name || strlen(name) > 90 || name[0] == '.') return 0;
    for (p = name; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '-' || *p == '_')) return 0;
    return strstr(name, "..") == NULL;
}

static DWORD WINAPI check_thread(LPVOID unused)
{
    static char manifest[MANIFEST_MAX + 1];
    char sig[256];
    Memory m = { manifest, 0, MANIFEST_MAX }, s = { sig, 0, sizeof(sig) - 1 };
    int local = updater_local_build();
    (void)unused;
    if (!http_get(L"/latest.json", to_memory, &m, NULL) || !http_get(L"/latest.json.sig", to_memory, &s, NULL)) {
        say(UPDATER_FAILED, "Could not reach the update server; you can play as you are.");
        return 0;
    }
    manifest[m.used] = 0;
    sig[s.used] = 0;
    if (!signature_ok(manifest, m.used, sig)) {
        say(UPDATER_FAILED, "The update information is not signed by the project; it was ignored.");
        return 0;
    }
    EnterCriticalSection(&g_cs);
    g_build = (int)json_number(manifest, "build");
    g_size = (unsigned long long)json_number(manifest, "size");
    if (!json_string(manifest, "version", g_version, sizeof(g_version))) g_version[0] = 0;
    if (!json_string(manifest, "notes", g_notes, sizeof(g_notes))) g_notes[0] = 0;
    if (!json_string(manifest, "setup", g_setup, sizeof(g_setup))) g_setup[0] = 0;
    if (!json_string(manifest, "sha256", g_sha256, sizeof(g_sha256))) g_sha256[0] = 0;
    LeaveCriticalSection(&g_cs);
    if (g_build <= 0 || !safe_name(g_setup) || strlen(g_sha256) != 64 || g_size == 0 || g_size > SETUP_MAX) {
        say(UPDATER_FAILED, "The update information is incomplete; it was ignored.");
        return 0;
    }
    if (g_build <= local)
        say(UPDATER_CURRENT, "You have the newest version (build %d).", local);
    else
        say(UPDATER_AVAILABLE, "Version %s (build %d) is available; you have build %d.", g_version, g_build, local);
    return 0;
}

void updater_check(void)
{
    HANDLE t;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    if (g_state == UPDATER_CHECKING || g_state == UPDATER_DOWNLOADING || g_state == UPDATER_STARTED) return;
    if (getenv("RECOMP_NO_UPDATE_CHECK")) return;
    InterlockedExchange(&g_state, UPDATER_CHECKING);
    t = CreateThread(NULL, 0, check_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else InterlockedExchange(&g_state, UPDATER_FAILED);
}

/* ---- download and start --------------------------------------------------------------------- */

typedef struct { HANDLE file; BCRYPT_HASH_HANDLE hash; unsigned long long got, total; } Download;

static int to_file(const void *data, DWORD size, void *user)
{
    Download *d = (Download *)user;
    DWORD written;
    if (d->got + size > SETUP_MAX) return 0;
    if (!WriteFile(d->file, data, size, &written, NULL) || written != size) return 0;
    BCryptHashData(d->hash, (PUCHAR)data, size, 0);
    d->got += size;
    if (d->total) InterlockedExchange(&g_permille, (LONG)(d->got * 1000 / d->total));
    return 1;
}

static DWORD WINAPI install_thread(LPVOID unused)
{
    char install[MAX_PATH * 2], data[MAX_PATH * 2], dir[MAX_PATH], path[MAX_PATH * 2], hex[65];
    wchar_t wpath[MAX_PATH * 2], url[160];
    unsigned char digest[32], want[32];
    Download d;
    int i, ok;
    (void)unused;
    if (!find_install(install, sizeof(install), data, sizeof(data))) {
        say(UPDATER_FAILED, "This copy was not installed by setup; update it from its source instead.");
        return 0;
    }
    GetTempPathA(sizeof(dir), dir);
    strncat(dir, "DefJamUpdate", sizeof(dir) - strlen(dir) - 1);
    CreateDirectoryA(dir, NULL);
    {
        /* the setups of earlier updates (about 56 MB each) stayed here for good */
        WIN32_FIND_DATAA found;
        HANDLE h;
        snprintf(path, sizeof(path), "%s\\DefJamSetup-*.exe", dir);
        if ((h = FindFirstFileA(path, &found)) != INVALID_HANDLE_VALUE) {
            do {
                if (_stricmp(found.cFileName, g_setup)) {
                    snprintf(path, sizeof(path), "%s\\%s", dir, found.cFileName);
                    DeleteFileA(path);
                }
            } while (FindNextFileA(h, &found));
            FindClose(h);
        }
    }
    snprintf(path, sizeof(path), "%s\\%s", dir, g_setup);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH * 2);
    {
        wchar_t name[96];
        MultiByteToWideChar(CP_UTF8, 0, g_setup, -1, name, 96);
        _snwprintf(url, sizeof(url) / sizeof(url[0]), L"/%s", name);
    }
    memset(&d, 0, sizeof(d));
    d.total = g_size;
    d.file = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (d.file == INVALID_HANDLE_VALUE || BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &d.hash, NULL, 0, NULL, 0, 0)) {
        if (d.file != INVALID_HANDLE_VALUE) CloseHandle(d.file);
        say(UPDATER_FAILED, "Could not prepare the download folder.");
        return 0;
    }
    InterlockedExchange(&g_permille, 0);
    say(UPDATER_DOWNLOADING, "Downloading the setup for version %s...", g_version);
    ok = http_get(url, to_file, &d, NULL);
    CloseHandle(d.file);
    BCryptFinishHash(d.hash, digest, 32, 0);
    BCryptDestroyHash(d.hash);
    for (i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    if (!ok || d.got != g_size || !unhex(g_sha256, want, 32) || memcmp(digest, want, 32)) {
        DeleteFileW(wpath);
        say(UPDATER_FAILED, ok ? "The downloaded setup did not match the signed update information; it was deleted."
                               : "The download did not finish; try again later.");
        return 0;
    }
    /* The setup runs without its window (--silent) while this launcher stays open: the new
     * version is built beside the running one (setup/engine.py --allow-running; each version
     * has its own executable) and this launcher shows the setup's status and progress files. */
    {
        wchar_t cmd[MAX_PATH * 8], winstall[MAX_PATH * 2], wdata[MAX_PATH * 2], wstatus[MAX_PATH * 2], wlog[MAX_PATH * 2];
        char status[MAX_PATH * 2], text[256], line[MAX_PATH * 2];
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        DWORD code = 1;
        snprintf(status, sizeof(status), "%s\\status.txt", dir);
        DeleteFileA(status);
        snprintf(line, sizeof(line), "%s.progress", status);
        DeleteFileA(line);
        MultiByteToWideChar(CP_UTF8, 0, install, -1, winstall, MAX_PATH * 2);
        MultiByteToWideChar(CP_UTF8, 0, data, -1, wdata, MAX_PATH * 2);
        MultiByteToWideChar(CP_UTF8, 0, status, -1, wstatus, MAX_PATH * 2);
        snprintf(line, sizeof(line), "%s\\update-log.txt", dir);
        MultiByteToWideChar(CP_UTF8, 0, line, -1, wlog, MAX_PATH * 2);
        _snwprintf(cmd, sizeof(cmd) / sizeof(cmd[0]),
                   L"\"%s\" --silent --reuse-extracted --allow-running --install-prerequisites --no-shortcuts"
                   L" --install-dir \"%s\" --data-dir \"%s\" --status-file \"%s\" --log \"%s\"",
                   wpath, winstall, wdata, wstatus, wlog);
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                            NULL, NULL, &si, &pi)) {
            say(UPDATER_FAILED, "The setup could not be started.");
            return 0;
        }
        CloseHandle(pi.hThread);
        InterlockedExchange(&g_permille, 0);
        say(UPDATER_INSTALLING, "Installing version %s...", g_version);
        while (WaitForSingleObject(pi.hProcess, 500) == WAIT_TIMEOUT) {
            FILE *f = fopen(status, "r");
            if (f) {
                if (fgets(text, sizeof(text), f)) {
                    size_t n = strlen(text);
                    while (n && (text[n - 1] == '\n' || text[n - 1] == '\r')) text[--n] = 0;
                    EnterCriticalSection(&g_cs);
                    snprintf(g_message, sizeof(g_message), "%s", text);
                    LeaveCriticalSection(&g_cs);
                }
                fclose(f);
            }
            snprintf(line, sizeof(line), "%s.progress", status);
            if ((f = fopen(line, "r")) != NULL) {
                int permille;
                if (fscanf(f, "%d", &permille) == 1 && permille >= 0 && permille <= 1000)
                    InterlockedExchange(&g_permille, permille);
                fclose(f);
            }
        }
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        if (code == 0) {
            InterlockedExchange(&g_permille, 1000);
            say(UPDATER_INSTALLED, "Version %s is installed. Restart to play it.", g_version);
        } else {
            /* the setup's own reason, its last "Setup stopped: ..." line (players only saw a code) */
            char why[200] = "";
            FILE *f;
            size_t n;
            snprintf(line, sizeof(line), "%s\\update-log.txt", dir);
            if ((f = fopen(line, "r")) != NULL) {
                while (fgets(text, sizeof(text), f))
                    if (!strncmp(text, "Setup stopped: ", 15)) snprintf(why, sizeof(why), "%s", text + 15);
                fclose(f);
            }
            for (n = strlen(why); n && (why[n - 1] == '\n' || why[n - 1] == '\r' || why[n - 1] == '.'); ) why[--n] = 0;
            if (why[0])
                say(UPDATER_FAILED, "The update stopped: %s. Your current version still plays. Details: %s",
                    why, line);
            else
                say(UPDATER_FAILED, "The update stopped (code %lu); your current version still plays. Details: %s",
                    (unsigned long)code, line);
        }
    }
    return 0;
}

/* The new version's launcher can only take play.lock once this game -- and the old launcher
 * waiting for it -- has exited: a hidden helper waits for this process, then starts it. */
void updater_restart(void)
{
    char install[MAX_PATH * 2], data[MAX_PATH * 2], cmd[MAX_PATH * 6];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    if (!find_install(install, sizeof(install), data, sizeof(data))) return;
    snprintf(cmd, sizeof(cmd),
             "powershell.exe -NoProfile -WindowStyle Hidden -Command \"Wait-Process -Id %lu -ErrorAction SilentlyContinue; "
             "Start-Sleep -Seconds 2; Start-Process -FilePath '%s\\DefJamLauncher.exe' -WorkingDirectory '%s'\"",
             (unsigned long)GetCurrentProcessId(), install, install);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

void updater_install(void)
{
    HANDLE t;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    if (g_state != UPDATER_AVAILABLE && g_state != UPDATER_FAILED) return;
    if (!g_build) return;
    InterlockedExchange(&g_state, UPDATER_DOWNLOADING);
    t = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
}

enum UpdaterState updater_state(void) { return (enum UpdaterState)g_state; }

void updater_info(int *build, char *version, int version_size, char *notes, int notes_size)
{
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    if (build) *build = g_build;
    if (version_size > 0) snprintf(version, (size_t)version_size, "%s", g_version);
    if (notes_size > 0) snprintf(notes, (size_t)notes_size, "%s", g_notes);
    LeaveCriticalSection(&g_cs);
}

double updater_progress(void) { return g_permille / 1000.0; }

void updater_message(char *text, int size)
{
    if (size <= 0) return;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    snprintf(text, (size_t)size, "%s", g_message);
    LeaveCriticalSection(&g_cs);
}

#endif
