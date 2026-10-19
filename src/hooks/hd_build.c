/* HD texture packs made from the launcher. See hd_build.h.
 *
 * The working directory is the source tree (pc_settings_init makes the folder that holds
 * game\default.xbe current), so the script and its aliases are found relative to it. The
 * Python is the one setup installed beside the source (versions\<id>\python), which carries
 * Pillow and NumPy; a development checkout without it uses `python` from PATH. The pack
 * names come from the receipt the script writes; the launcher turns them on. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hd_build.h"

#if !defined(_WIN32)

int hd_build_start(void) { return -1; }
void hd_build_cancel(void) {}
enum HdBuildState hd_build_status(int *d, int *t, char *text, int ts, char *p, int ps)
{
    if (d) *d = 0;
    if (t) *t = 0;
    if (ts > 0) snprintf(text, (size_t)ts, "Run the setup again with the HD option to make HD textures.");
    if (ps > 0) p[0] = 0;
    return HD_BUILD_IDLE;
}
double hd_build_free_gib(void) { return -1; }

#else

static CRITICAL_SECTION g_cs;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static volatile LONG g_state = HD_BUILD_IDLE;
static HANDLE g_process, g_reader;
static int g_done, g_total;
static char g_text[256], g_packs[256];
static char g_receipt[MAX_PATH * 2];

static BOOL CALLBACK init_once(PINIT_ONCE o, PVOID p, PVOID *c)
{
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_cs);
    return TRUE;
}

static void set_text(const char *text)
{
    EnterCriticalSection(&g_cs);
    snprintf(g_text, sizeof(g_text), "%s", text);
    LeaveCriticalSection(&g_cs);
}

static int file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* versions\<id>\python\python.exe beside the source tree (the working directory), if setup put one there. */
static int find_python(char *out, size_t size)
{
    char cwd[MAX_PATH];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd)) {
        snprintf(out, size, "%s\\..\\python\\python.exe", cwd);
        if (file_exists(out)) return 1;
    }
    snprintf(out, size, "python");
    return 0;
}

/* The pack names from the receipt: "packs": ["a", "b"]. */
static void read_packs(void)
{
    char buf[8192], *p, *end;
    size_t n;
    FILE *f = fopen(g_receipt, "rb");
    g_packs[0] = 0;
    if (!f) return;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    p = strstr(buf, "\"packs\"");
    if (!p || !(p = strchr(p, '[')) || !(end = strchr(p, ']'))) return;
    *end = 0;
    while ((p = strchr(p, '"')) != NULL) {
        char *close = strchr(p + 1, '"');
        if (!close) break;
        if (g_packs[0]) strncat(g_packs, ";", sizeof(g_packs) - strlen(g_packs) - 1);
        strncat(g_packs, p + 1, (size_t)(close - p - 1) < sizeof(g_packs) - strlen(g_packs) - 1 ?
                (size_t)(close - p - 1) : sizeof(g_packs) - strlen(g_packs) - 1);
        p = close + 1;
    }
}

/* Reads the script's output line by line: PROGRESS a / b, and the final word. */
static DWORD WINAPI reader(LPVOID pipe)
{
    char buf[4096], line[512];
    size_t used = 0;
    DWORD got, code = 1;
    while (ReadFile((HANDLE)pipe, buf, sizeof(buf), &got, NULL) && got) {
        DWORD i;
        for (i = 0; i < got; i++) {
            char c = buf[i];
            if (c != '\n' && used < sizeof(line) - 1) { if (c != '\r') line[used++] = c; continue; }
            line[used] = 0;
            used = 0;
            {
                int a, b;
                if (sscanf(line, "PROGRESS %d / %d", &a, &b) == 2) {
                    char text[96];
                    EnterCriticalSection(&g_cs);
                    g_done = a; g_total = b;
                    LeaveCriticalSection(&g_cs);
                    snprintf(text, sizeof(text), "Upscaling textures: %d of %d", a, b);
                    set_text(text);
                } else if (!strncmp(line, "Verifying", 9)) {
                    set_text("Checking the new textures and putting the packs together...");
                }
            }
        }
    }
    CloseHandle((HANDLE)pipe);
    WaitForSingleObject(g_process, INFINITE);
    GetExitCodeProcess(g_process, &code);
    if (code == 0) {
        read_packs();
        if (g_packs[0]) {
            set_text("HD textures are ready and switched on.");
            InterlockedExchange(&g_state, HD_BUILD_DONE);
        } else {
            set_text("The upscale finished but named no packs; see the log in the data folder.");
            InterlockedExchange(&g_state, HD_BUILD_FAILED);
        }
    } else if (g_state == HD_BUILD_RUNNING) {
        set_text("Making the HD textures stopped. Finished images are kept; start again to continue.");
        InterlockedExchange(&g_state, HD_BUILD_FAILED);
    }
    CloseHandle(g_process);
    g_process = NULL;
    return 0;
}

int hd_build_start(void)
{
    char python[MAX_PATH * 2], cmd[MAX_PATH * 8], log[MAX_PATH * 2];
    const char *data = getenv("DEFJAM_DATA");
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE out_r = NULL, out_w = NULL, nul;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    if (g_state == HD_BUILD_RUNNING) return 0;
    if (!data || !*data) { set_text("The data folder is not known; start the game from its shortcut."); InterlockedExchange(&g_state, HD_BUILD_FAILED); return -1; }
    if (!file_exists("scripts\\build-hd-pack.py")) { set_text("This installation has no texture tools; run the setup again with the HD option."); InterlockedExchange(&g_state, HD_BUILD_FAILED); return -1; }
    if (hd_build_free_gib() >= 0 && hd_build_free_gib() < 15.0) { set_text("Making HD textures needs about 15 GB free on the data drive."); InterlockedExchange(&g_state, HD_BUILD_FAILED); return -1; }
    find_python(python, sizeof(python));
    snprintf(g_receipt, sizeof(g_receipt), "%s\\hd-pack-build.json", data);
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -B scripts\\build-hd-pack.py --root \"%s\\extracted\" --work \"%s\\hd-work\\setup-lanczos\""
             " --mods \"%s\\mods\" --receipt \"%s\" --aliases config\\hd-runtime-aliases.json",
             python, data, data, data, g_receipt);
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) return -1;
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    snprintf(log, sizeof(log), "NUL");
    nul = CreateFileA(log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = out_w;
    si.hStdError = out_w;
    EnterCriticalSection(&g_cs);
    g_done = g_total = 0;
    g_packs[0] = 0;
    LeaveCriticalSection(&g_cs);
    set_text("Preparing: reading the textures from your game files...");
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi)) {
        CloseHandle(out_r); CloseHandle(out_w);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        set_text("Python could not be started for the texture tools.");
        InterlockedExchange(&g_state, HD_BUILD_FAILED);
        return -1;
    }
    CloseHandle(out_w);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    CloseHandle(pi.hThread);
    g_process = pi.hProcess;
    InterlockedExchange(&g_state, HD_BUILD_RUNNING);
    g_reader = CreateThread(NULL, 0, reader, out_r, 0, NULL);
    fprintf(stderr, "[HD] making texture packs: %s\n", cmd);
    return 0;
}

void hd_build_cancel(void)
{
    if (g_state != HD_BUILD_RUNNING || !g_process) return;
    InterlockedExchange(&g_state, HD_BUILD_FAILED);
    set_text("Stopped. Finished images are kept; start again to continue.");
    /* The script runs worker processes of its own: end the whole tree. */
    {
        char cmd[64];
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        snprintf(cmd, sizeof(cmd), "taskkill /PID %lu /T /F", (unsigned long)GetProcessId(g_process));
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
}

enum HdBuildState hd_build_status(int *done, int *total, char *text, int text_size, char *packs, int packs_size)
{
    InitOnceExecuteOnce(&g_once, init_once, NULL, NULL);
    EnterCriticalSection(&g_cs);
    if (done) *done = g_done;
    if (total) *total = g_total;
    if (text_size > 0) snprintf(text, (size_t)text_size, "%s", g_text);
    if (packs_size > 0) snprintf(packs, (size_t)packs_size, "%s", g_packs);
    LeaveCriticalSection(&g_cs);
    return (enum HdBuildState)g_state;
}

double hd_build_free_gib(void)
{
    ULARGE_INTEGER free_bytes;
    const char *data = getenv("DEFJAM_DATA");
    if (!data || !GetDiskFreeSpaceExA(data, &free_bytes, NULL, NULL)) return -1;
    return (double)free_bytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
}

#endif
