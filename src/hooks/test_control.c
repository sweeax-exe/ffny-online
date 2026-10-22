/* A live control channel for tests (tools/defjam-mcp): RECOMP_CONTROL_PORT=<port> opens a TCP
 * listener on 127.0.0.1 only, one client at a time, one command per line, one JSON line back.
 *
 *   ping                                   {"ok":true}
 *   press <slot> <control>[+<control>...] [ms]   hold on virtual pad <slot> for ms (default 120)
 *   hold <slot> <control>[+...]            hold until release
 *   release <slot> [<control>[+...]|all]
 *   state                                  screen, the last front-end calls, the match (phase,
 *                                          clock, type, fighters with health), the online status
 *
 * Controls are the settings names (a, b, x, y, start, back, dpad_up, left_stick_left, ...). The
 * pads are the virtual ones (RECOMP_INPUT_VIRTUAL_PADS=n with RECOMP_INPUT_NO_PADS=1), pressed
 * through xbox_HostInputScriptVirtual as the harness's scripted pads are. Nothing here runs unless
 * the variable is set, and the listener never binds anything but the loopback address. */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "netplay.h"
#include "automatch.h"
#include "test_observations.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include "input_host.h"
#include "input_map.h"
#pragma comment(lib, "ws2_32.lib")

#define ACTORS      0x003B92E0u
#define STRIDE      0x12E8u
#define G_PHASE     0x003B8F30u
#define G_CLOCK_MIN 0x003B9030u
#define G_CLOCK_SEC 0x003B9034u
#define G_TYPE      0x003B9084u
#define F_SLOT      0x28u
#define F_CHARACTER 0x2Cu
#define F_HEALTH    0x350u
#define F_HEALTH_MAX 0x354u
#define F_CONTROL   0x937u
#define PADS        4
#define CALLS       16

static CRITICAL_SECTION g_cs;
static uint8_t g_held[PADS][INPUT_CONTROL_COUNT];
static ULONGLONG g_until[PADS][INPUT_CONTROL_COUNT];    /* 0: held until release */
static char g_calls[CALLS][160];
static unsigned g_call_next, g_call_total;
static char g_screen[96];
static volatile LONG g_on;

void test_control_note_funccall(const char *text)
{
    if (!g_on || !text || !strcmp(text, "Online.Tick()")) return;     /* every frame on the menus */
    EnterCriticalSection(&g_cs);
    snprintf(g_calls[g_call_next % CALLS], sizeof(g_calls[0]), "%s", text);
    g_call_next++;
    g_call_total++;
    if (!strncmp(text, "Control.GetScreenInfo(", 22)) {
        const char *a = text + 22, *b = strchr(a, ')');
        int n = b ? (int)(b - a) : (int)strlen(a);
        snprintf(g_screen, sizeof(g_screen), "%.*s", n < 95 ? n : 95, a);
    }
    LeaveCriticalSection(&g_cs);
}

static void push_pad(int slot)
{
    xbox_HostInputScriptVirtual(slot, g_held[slot]);
}

/* "a+dpad_up" -> the controls; returns how many were understood. */
static int parse_controls(const char *spec, int *out, int max)
{
    char buf[128], *tok, *ctx = NULL;
    int n = 0, c;
    snprintf(buf, sizeof(buf), "%s", spec);
    for (tok = strtok_s(buf, "+", &ctx); tok && n < max; tok = strtok_s(NULL, "+", &ctx))
        for (c = 0; c < INPUT_CONTROL_COUNT; c++)
            if (!_stricmp(tok, input_control_name(c))) { out[n++] = c; break; }
    return n;
}

static void json_str(char *out, size_t size, size_t *len, const char *s)
{
    size_t n = *len;
    if (n < size) out[n++] = '"';
    for (; *s && n + 7 < size; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') { out[n++] = '\\'; out[n++] = (char)ch; }
        else if (ch < 0x20) n += (size_t)snprintf(out + n, size - n, "\\u%04x", ch);
        else out[n++] = (char)ch;
    }
    if (n < size) out[n++] = '"';
    if (n < size) out[n] = 0;
    *len = n;
}

#define PUT(...) do { if (len < size) len += (size_t)snprintf(out + len, size - len, __VA_ARGS__); } while (0)

static void state(char *out, size_t size)
{
    size_t len = 0;
    unsigned i, first;
    char status[256];
    PUT("{\"ok\":true,\"screen\":");
    EnterCriticalSection(&g_cs);
    json_str(out, size, &len, g_screen);
    PUT(",\"calls_total\":%u,\"calls\":[", g_call_total);
    first = g_call_next > CALLS ? g_call_next - CALLS : 0;
    for (i = first; i < g_call_next; i++) {
        if (i > first) PUT(",");
        json_str(out, size, &len, g_calls[i % CALLS]);
    }
    LeaveCriticalSection(&g_cs);
    PUT("],\"match\":{\"phase\":%u,\"clock\":\"%u:%02u\",\"type\":%u,\"step\":%lld,\"fighters\":[",
        MEM32(G_PHASE), MEM32(G_CLOCK_MIN), MEM32(G_CLOCK_SEC), MEM32(G_TYPE), defjam_test_current_step());
    for (i = 0; i < 4; i++) {
        uint32_t r = ACTORS + i * STRIDE;
        uint32_t ch = MEM32(r + F_CHARACTER);
        if (i) PUT(",");
        if (MEM32(r + F_SLOT) == 0xFFFFFFFFu) { PUT("null"); continue; }
        /* fighter-record-fields.md, wall-stagger.md: state +0x30 (0x11/0x12 stagger, 0x37 wall slam,
         * 0x1B corner pin, 0x14 rope bounce, 0x08 leaning on the wall), position, momentum, combo
         * step, the pinned-hit count, the pad bits held and newly pressed this step. */
        PUT("{\"slot\":%u,\"character\":%u,\"health\":%.1f,\"health_max\":%.1f,\"cpu\":%d,"
            "\"state\":\"0x%02X\",\"pos\":[%.2f,%.2f,%.2f],\"facing\":%.3f,\"momentum\":%.1f,\"momentum_max\":%.1f,"
            "\"combo\":%u,\"pinned_hits\":%u,\"held\":\"0x%05X\",\"pressed\":\"0x%05X\",\"blocked\":\"0x%05X\"}", i, ch,
            (double)MEMF(r + F_HEALTH), (double)MEMF(r + F_HEALTH_MAX), MEM8(r + F_CONTROL) & 1,
            MEM32(r + 0x30u), (double)MEMF(r + 0x44u), (double)MEMF(r + 0x48u), (double)MEMF(r + 0x4Cu),
            (double)MEMF(r + 0x54u), (double)MEMF(r + 0x374u), (double)MEMF(r + 0x378u),
            MEM32(r + 0x40Cu), MEM32(r + 0x384u), MEM32(r + 0x9E0u), MEM32(r + 0x9F0u), MEM32(0x003B8FC8u + i * 4u));
    }
    netplay_status(status, sizeof(status));
    PUT("]},\"online\":{\"state\":%d,\"status\":", (int)netplay_state());
    json_str(out, size, &len, status);
    PUT(",\"lockstep\":");
    if (len < size) len += (size_t)netplay_debug_json(out + len, (int)(size - len));
    PUT("}}");
}

static void release_due(void)
{
    ULONGLONG now = GetTickCount64();
    int s, c, changed;
    EnterCriticalSection(&g_cs);
    for (s = 0; s < PADS; s++) {
        changed = 0;
        for (c = 0; c < INPUT_CONTROL_COUNT; c++)
            if (g_held[s][c] && g_until[s][c] && now >= g_until[s][c]) { g_held[s][c] = 0; g_until[s][c] = 0; changed = 1; }
        if (changed) push_pad(s);
    }
    LeaveCriticalSection(&g_cs);
}

static void command(char *line, char *out, size_t size)
{
    char verb[16] = "", a1[128] = "", a2[32] = "";
    int slot, n, i, ctl[8];
    sscanf(line, "%15s %127s %31s", verb, a1, a2);
    if (!strcmp(verb, "ping")) { snprintf(out, size, "{\"ok\":true}"); return; }
    if (!strcmp(verb, "state")) { state(out, size); return; }
    if (!strcmp(verb, "press") || !strcmp(verb, "hold") || !strcmp(verb, "release")) {
        char spec[128] = "";
        int ms = 120;
        if (sscanf(line, "%*s %d %127s %d", &slot, spec, &ms) < 1 || slot < 0 || slot >= PADS) {
            snprintf(out, size, "{\"ok\":false,\"error\":\"slot 0..3\"}");
            return;
        }
        EnterCriticalSection(&g_cs);
        if (!strcmp(verb, "release") && (!spec[0] || !_stricmp(spec, "all"))) {
            memset(g_held[slot], 0, sizeof(g_held[slot]));
            memset(g_until[slot], 0, sizeof(g_until[slot]));
            n = 1;
        } else {
            n = parse_controls(spec, ctl, 8);
            for (i = 0; i < n; i++) {
                g_held[slot][ctl[i]] = !strcmp(verb, "release") ? 0 : 1;
                g_until[slot][ctl[i]] = !strcmp(verb, "press") ? GetTickCount64() + (ULONGLONG)(ms > 0 ? ms : 120) : 0;
            }
        }
        if (n) push_pad(slot);
        LeaveCriticalSection(&g_cs);
        if (n) snprintf(out, size, "{\"ok\":true}");
        else snprintf(out, size, "{\"ok\":false,\"error\":\"unknown control: %s\"}", spec);
        return;
    }
    if (!strcmp(verb, "match")) {          /* match type=0,venue=1,f0=55,p0=0,f1=22,p1=-1 */
        int ok = automatch_request_text(line + 6);
        snprintf(out, size, ok ? "{\"ok\":true,\"note\":\"armed: starts at the main menu\"}" : "{\"ok\":false}");
        return;
    }
    if (!strcmp(verb, "fe")) {             /* fe Game.GetMatchInfo(): run on the game thread */
        int ok = automatch_queue_fe(line + 3);
        snprintf(out, size, ok ? "{\"ok\":true,\"note\":\"runs at the front end's next call; result in the log\"}"
                               : "{\"ok\":false,\"error\":\"queue full\"}");
        return;
    }
    if (!strcmp(verb, "inject")) {         /* inject <slot> <from +steps> <steps> <hex pad bits> | inject clear */
        int slot2, rel, n2;
        unsigned bits;
        long long now = defjam_test_current_step();
        if (!strcmp(a1, "clear")) { defjam_test_input_clear(); snprintf(out, size, "{\"ok\":true}"); return; }
        if (sscanf(line, "%*s %d %d %d %x", &slot2, &rel, &n2, &bits) != 4 || now < 0 || n2 < 1) {
            snprintf(out, size, "{\"ok\":false,\"error\":\"inject <slot> <from +steps> <steps> <hex>, during a fight\"}");
            return;
        }
        if (!defjam_test_input_add(slot2, now + rel, now + rel + n2 - 1, bits)) {
            snprintf(out, size, "{\"ok\":false,\"error\":\"not taken\"}");
            return;
        }
        snprintf(out, size, "{\"ok\":true,\"first\":%lld,\"last\":%lld}", now + rel, now + rel + n2 - 1);
        return;
    }
    if (!strcmp(verb, "save") || !strcmp(verb, "load")) {   /* at the start of the next fight step */
        if (!strcmp(verb, "save")) defjam_test_request_save(); else defjam_test_request_load();
        snprintf(out, size, "{\"ok\":true,\"saved_step\":%lld}", defjam_test_snap_step());
        return;
    }
    if (!strcmp(verb, "hashes")) {         /* hashes <from step> <to step>: the state hash at each step */
        long long from = atoll(a1), to = atoll(a2), k;
        size_t len = (size_t)snprintf(out, size, "{\"ok\":true,\"from\":%lld,\"hashes\":[", from);
        if (to - from > 600) to = from + 600;
        for (k = from; k <= to && len + 24 < size; k++)
            len += (size_t)snprintf(out + len, size - len, "%s\"%016llX\"", k > from ? "," : "", defjam_test_step_hash(k));
        if (len + 3 < size) { out[len++] = ']'; out[len++] = '}'; out[len] = 0; }
        return;
    }
    if (!strcmp(verb, "memdiff")) {        /* memdiff begin | memdiff end: which 4 KB pages of guest RAM changed */
        enum { PAGES = 0x04000000 / 4096 };
        static uint64_t *base;
        const uint64_t *ram = (const uint64_t *)XBOX_PTR(0);
        unsigned p, changed = 0, ranges = 0, run = 0;
        size_t len;
        if (!base) base = (uint64_t *)calloc(PAGES, sizeof(uint64_t));
        if (!base) { snprintf(out, size, "{\"ok\":false,\"error\":\"no memory\"}"); return; }
        if (!strcmp(a1, "begin")) {
            for (p = 0; p < PAGES; p++) {
                const uint64_t *w = ram + (size_t)p * 512;
                uint64_t h = 0x9E3779B97F4A7C15ull;
                int k;
                for (k = 0; k < 512; k++) h = (h ^ w[k]) * 0x100000001B3ull;
                base[p] = h;
            }
            snprintf(out, size, "{\"ok\":true,\"pages\":%u}", (unsigned)PAGES);
            return;
        }
        len = (size_t)snprintf(out, size, "{\"ok\":true,\"ranges\":[");
        for (p = 0; p <= PAGES; p++) {
            int dirty = 0;
            if (p < PAGES) {
                const uint64_t *w = ram + (size_t)p * 512;
                uint64_t h = 0x9E3779B97F4A7C15ull;
                int k;
                for (k = 0; k < 512; k++) h = (h ^ w[k]) * 0x100000001B3ull;
                dirty = h != base[p];
                base[p] = h;
            }
            if (dirty) { changed++; run++; continue; }
            if (run) {
                if (ranges < 300 && len + 48 < size)
                    len += (size_t)snprintf(out + len, size - len, "%s[\"0x%08X\",%u]", ranges ? "," : "",
                                            (p - run) * 4096u, run * 4u);
                ranges++;
                run = 0;
            }
        }
        if (len + 64 < size)
            snprintf(out + len, size - len, "],\"range_count\":%u,\"changed_pages\":%u,\"changed_kb\":%u}",
                     ranges, changed, changed * 4u);
        return;
    }
    if (!strcmp(verb, "peek")) {           /* peek <guest address> [bytes<=512] */
        unsigned addr = (unsigned)strtoul(a1, NULL, 0), count = a2[0] ? (unsigned)strtoul(a2, NULL, 0) : 64, k;
        size_t len = 0;
        if (count > 512) count = 512;
        if (addr < 0x10000u || addr + count > 0x04000000u) {
            snprintf(out, size, "{\"ok\":false,\"error\":\"guest RAM is 0x10000..0x4000000\"}");
            return;
        }
        len = (size_t)snprintf(out, size, "{\"ok\":true,\"addr\":\"0x%08X\",\"hex\":\"", addr);
        for (k = 0; k < count && len + 4 < size; k++) len += (size_t)snprintf(out + len, size - len, "%02X", MEM8(addr + k));
        if (len + 3 < size) { out[len++] = '"'; out[len++] = '}'; out[len] = 0; }
        return;
    }
    if (!strcmp(verb, "poke")) {           /* poke <guest address> <32-bit value>: tests only */
        unsigned addr = (unsigned)strtoul(a1, NULL, 0), value = (unsigned)strtoul(a2, NULL, 0);
        if (addr < 0x10000u || addr + 4 > 0x04000000u || (addr & 3)) {
            snprintf(out, size, "{\"ok\":false,\"error\":\"aligned guest RAM address\"}");
            return;
        }
        MEM32(addr) = value;
        snprintf(out, size, "{\"ok\":true}");
        return;
    }
    snprintf(out, size, "{\"ok\":false,\"error\":\"commands: ping, state, press, hold, release, inject, match, fe, peek, poke\"}");
}

static DWORD WINAPI control_thread(LPVOID param)
{
    SOCKET ls = (SOCKET)(UINT_PTR)param;
    static char out[16384];
    for (;;) {
        SOCKET c = accept(ls, NULL, NULL);
        char buf[512];
        int have = 0;
        if (c == INVALID_SOCKET) { Sleep(100); continue; }
        for (;;) {
            fd_set rd;
            struct timeval tv = { 0, 20000 };
            char *nl;
            int got;
            release_due();
            FD_ZERO(&rd);
            FD_SET(c, &rd);
            if (select(0, &rd, NULL, NULL, &tv) <= 0) continue;
            got = recv(c, buf + have, (int)sizeof(buf) - 1 - have, 0);
            if (got <= 0) break;
            have += got;
            buf[have] = 0;
            while ((nl = strchr(buf, '\n')) != NULL) {
                size_t olen;
                *nl = 0;
                if (nl > buf && nl[-1] == '\r') nl[-1] = 0;
                command(buf, out, sizeof(out) - 2);
                olen = strlen(out);
                out[olen++] = '\n';
                send(c, out, (int)olen, 0);
                have -= (int)(nl + 1 - buf);
                memmove(buf, nl + 1, (size_t)have + 1);
            }
            if (have >= (int)sizeof(buf) - 1) have = 0;
        }
        closesocket(c);
    }
}

void test_control_start(void)
{
    const char *port = getenv("RECOMP_CONTROL_PORT");
    WSADATA wsa;
    SOCKET ls;
    struct sockaddr_in addr;
    HANDLE h;
    if (!port || !*port || g_on) return;
    InitializeCriticalSection(&g_cs);
    WSAStartup(MAKEWORD(2, 2), &wsa);
    ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)atoi(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (ls == INVALID_SOCKET || bind(ls, (struct sockaddr *)&addr, sizeof(addr)) || listen(ls, 1)) {
        fprintf(stderr, "[CONTROL] could not listen on 127.0.0.1:%s\n", port);
        return;
    }
    InterlockedExchange(&g_on, 1);
    h = CreateThread(NULL, 0, control_thread, (LPVOID)(UINT_PTR)ls, 0, NULL);
    if (h) CloseHandle(h);
    fprintf(stderr, "[CONTROL] listening on 127.0.0.1:%s\n", port);
    fflush(stderr);
}

#else
void test_control_start(void) {}
void test_control_note_funccall(const char *text) { (void)text; }
#endif
