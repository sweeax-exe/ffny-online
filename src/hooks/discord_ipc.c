/* Discord Rich Presence: the pipe to the Discord app and its thread. See discord_ipc.h. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>

#include "discord_ipc.h"

#if !defined(_WIN32)

int dipc_start(const DipcConfig *c, DipcProvider p, void *u) { (void)c; (void)p; (void)u; return 0; }
void dipc_wake(void) {}
void dipc_stop(void) {}
int dipc_connected(void) { return 0; }

#else

#include <windows.h>

#define IN_MAX 65536

static struct {
    HANDLE thread, wake;
    volatile LONG stop, connected;
    CRITICAL_SECTION cs;            /* the provider */
    int cs_ready;
    DipcProvider provider;
    void *user;
    char client_id[40];
    char prefix[128];
    int interval_ms, retry_ms, tick_ms;
    HANDLE pipe;
    int pipe_index;
    uint8_t in[IN_MAX];
    int in_len;
    int ready;                      /* READY arrived on this connection */
    DrpcActivity sent;
    int have_sent;
    ULONGLONG next_send, next_try;
    unsigned nonce;
    int told_absent, errors_logged;
} D;

static void log_line(const char *fmt, const char *a, const char *b)
{
    fprintf(stderr, fmt, a ? a : "", b ? b : "");
    fflush(stderr);
}

static int write_frame(uint32_t op, const char *payload, int len)
{
    static uint8_t buf[DRPC_HEADER + 8192];
    DWORD done = 0;
    int n = drpc_frame(buf, sizeof(buf), op, payload, len);
    if (!n || D.pipe == INVALID_HANDLE_VALUE || !D.pipe) return 0;
    return WriteFile(D.pipe, buf, (DWORD)n, &done, NULL) && done == (DWORD)n;
}

static void disconnect(const char *why)
{
    if (D.pipe && D.pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(D.pipe);
        if (why) log_line("[DISCORD] disconnected: %s%s\n", why, NULL);
    }
    D.pipe = NULL;
    D.in_len = 0;
    D.ready = 0;
    D.have_sent = 0;
    InterlockedExchange(&D.connected, 0);
}

/* The "message" of a CLOSE or an ERROR answer, for the log. */
static void message_of(const uint8_t *payload, uint32_t len, char *out, int size)
{
    char text[512];
    const char *m;
    int n = len < sizeof(text) - 1 ? (int)len : (int)sizeof(text) - 1;
    memcpy(text, payload, (size_t)n);
    text[n] = 0;
    m = strstr(text, "\"message\"");
    if (m) m = strchr(m + 9, '"');
    drpc_clean(out, size, m ? m + 1 : text, size - 1);
    if (m) {
        char *end = strchr(out, '"');
        if (end) *end = 0;
    }
}

/* 1 when the payload (JSON, not terminated) has "evt": "<evt>", blanks allowed around the colon. */
static int evt_is(const uint8_t *payload, uint32_t len, const char *evt)
{
    static const char key[] = "\"evt\"";
    size_t n = strlen(evt), k = sizeof(key) - 1;
    uint32_t i, j;
    for (i = 0; k <= len && i <= len - k; i++) {
        if (memcmp(payload + i, key, k)) continue;
        j = i + (uint32_t)k;
        while (j < len && (payload[j] == ' ' || payload[j] == ':' || payload[j] == '\t')) j++;
        if (j + n + 2 <= len && payload[j] == '"' && !memcmp(payload + j + 1, evt, n) && payload[j + 1 + n] == '"')
            return 1;
    }
    return 0;
}

/* One frame from Discord. Returns 0 when the connection is to end. */
static int handle(uint32_t op, const uint8_t *payload, uint32_t len)
{
    char text[200];
    switch (op) {
    case DRPC_OP_PING:
        return write_frame(DRPC_OP_PONG, (const char *)payload, (int)len);
    case DRPC_OP_CLOSE:
        message_of(payload, len, text, sizeof(text));
        log_line("[DISCORD] Discord closed the connection: %s%s\n", text, NULL);
        return 0;
    case DRPC_OP_FRAME:
        /* READY carries the user and the config before its "evt", so the whole payload is searched */
        if (evt_is(payload, len, "READY")) D.ready = 1;
        else if (evt_is(payload, len, "ERROR") && D.errors_logged < 5) {
            D.errors_logged++;
            message_of(payload, len, text, sizeof(text));
            log_line("[DISCORD] Discord refused an update: %s%s\n", text, NULL);
        }
        return 1;
    default:
        return 1;
    }
}

/* Reads what Discord sent, without waiting. Returns 0 when the pipe broke or Discord closed it. */
static int pump(void)
{
    DWORD avail = 0;
    uint32_t op, len;
    if (!D.pipe) return 0;
    if (!PeekNamedPipe(D.pipe, NULL, 0, NULL, &avail, NULL)) return 0;
    while (avail > 0) {
        DWORD got = 0, want = (DWORD)(IN_MAX - D.in_len);
        if (want > avail) want = avail;
        if (!want) return 0;                        /* a frame larger than the buffer: start over */
        if (!ReadFile(D.pipe, D.in + D.in_len, want, &got, NULL)) return 0;
        D.in_len += (int)got;
        avail -= got;
        while (drpc_get_header(D.in, D.in_len, &op, &len)) {
            if (len > IN_MAX - DRPC_HEADER) return 0;
            if ((uint32_t)D.in_len < DRPC_HEADER + len) break;
            if (!handle(op, D.in + DRPC_HEADER, len)) return 0;
            D.in_len -= (int)(DRPC_HEADER + len);
            memmove(D.in, D.in + DRPC_HEADER + len, (size_t)D.in_len);
        }
    }
    return 1;
}

static int try_connect(void)
{
    char name[160], hello[128];
    int i, n;
    for (i = 0; i < 10; i++) {
        snprintf(name, sizeof(name), "%s%d", D.prefix, i);
        D.pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (D.pipe != INVALID_HANDLE_VALUE) break;
        D.pipe = NULL;
    }
    if (!D.pipe) {
        if (!D.told_absent) {
            D.told_absent = 1;
            fprintf(stderr, "[DISCORD] Discord is not running; trying again every %d s\n", D.retry_ms / 1000);
            fflush(stderr);
        }
        return 0;
    }
    D.pipe_index = i;
    D.in_len = 0;
    D.ready = 0;
    n = drpc_handshake_json(hello, sizeof(hello), D.client_id);
    if (!write_frame(DRPC_OP_HANDSHAKE, hello, n)) { disconnect("the handshake could not be sent"); return 0; }
    {   /* READY, at most 3 s */
        ULONGLONG until = GetTickCount64() + 3000;
        while (!D.ready && !D.stop && GetTickCount64() < until) {
            if (!pump()) { disconnect("no answer to the handshake"); return 0; }
            if (!D.ready) Sleep(20);
        }
    }
    if (!D.ready) { disconnect("no answer to the handshake"); return 0; }
    D.told_absent = 0;
    InterlockedExchange(&D.connected, 1);
    fprintf(stderr, "[DISCORD] connected to Discord (pipe %d)\n", D.pipe_index);
    fflush(stderr);
    return 1;
}

static int send_activity(const DrpcActivity *a)
{
    static char json[8192];
    int n = drpc_set_activity_json(json, sizeof(json), GetCurrentProcessId(), a, ++D.nonce);
    if (!n) return 0;
    if (!write_frame(DRPC_OP_FRAME, json, n)) return 0;
    if (a) {
        fprintf(stderr, "[DISCORD] presence: %s | %s\n", a->details, a->state);
        fflush(stderr);
    }
    return 1;
}

static void clear_and_close(const char *why)
{
    if (!D.pipe) return;
    if (send_activity(NULL)) {
        ULONGLONG until = GetTickCount64() + 300;       /* let Discord read it before the pipe goes */
        while (GetTickCount64() < until && pump()) Sleep(20);
    }
    disconnect(why);
}

static DWORD WINAPI presence_thread(LPVOID unused)
{
    (void)unused;
    while (!D.stop) {
        DrpcActivity want;
        int on;
        ULONGLONG now;
        memset(&want, 0, sizeof(want));
        EnterCriticalSection(&D.cs);
        on = D.provider ? D.provider(&want, D.user) : 0;
        LeaveCriticalSection(&D.cs);
        now = GetTickCount64();
        if (!on) {
            if (D.pipe) clear_and_close("presence switched off (cleared)");
            D.next_try = 0;
        } else {
            if (!D.pipe && now >= D.next_try) {
                if (try_connect()) D.next_send = now;
                else D.next_try = now + (ULONGLONG)D.retry_ms;
            }
            if (D.pipe) {
                if (!pump()) {
                    disconnect("the pipe broke");
                    D.next_try = now;           /* once more at the next tick, then every retry_ms */
                } else if ((!D.have_sent || !drpc_same(&want, &D.sent)) && now >= D.next_send) {
                    if (send_activity(&want)) {
                        D.sent = want;
                        D.have_sent = 1;
                        D.next_send = now + (ULONGLONG)D.interval_ms;
                    } else {
                        disconnect("an update could not be sent");
                        D.next_try = now;
                    }
                }
            }
        }
        WaitForSingleObject(D.wake, (DWORD)D.tick_ms);
    }
    clear_and_close("the program ends (cleared)");
    return 0;
}

int dipc_start(const DipcConfig *config, DipcProvider provider, void *user)
{
    if (!D.cs_ready) {
        InitializeCriticalSection(&D.cs);
        D.cs_ready = 1;
    }
    EnterCriticalSection(&D.cs);
    D.provider = provider;
    D.user = user;
    LeaveCriticalSection(&D.cs);
    if (D.thread) {
        dipc_wake();
        return 1;
    }
    snprintf(D.client_id, sizeof(D.client_id), "%s", config && config->client_id ? config->client_id : DRPC_CLIENT_ID);
    snprintf(D.prefix, sizeof(D.prefix), "%s", config && config->pipe_prefix ? config->pipe_prefix : "\\\\.\\pipe\\discord-ipc-");
    D.interval_ms = config && config->interval_ms > 0 ? config->interval_ms : 15000;
    D.retry_ms = config && config->retry_ms > 0 ? config->retry_ms : 15000;
    D.tick_ms = config && config->tick_ms > 0 ? config->tick_ms : 250;
    D.stop = 0;
    D.next_try = D.next_send = 0;
    D.have_sent = 0;
    D.pipe = NULL;
    D.wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    D.thread = CreateThread(NULL, 0, presence_thread, NULL, 0, NULL);
    if (!D.thread) {
        if (D.wake) CloseHandle(D.wake);
        D.wake = NULL;
        return 0;
    }
    return 1;
}

void dipc_wake(void)
{
    if (D.wake) SetEvent(D.wake);
}

void dipc_stop(void)
{
    if (!D.thread) return;
    InterlockedExchange(&D.stop, 1);
    dipc_wake();
    if (WaitForSingleObject(D.thread, 1500) != WAIT_OBJECT_0) {
        fprintf(stderr, "[DISCORD] the presence thread did not end in time\n");
        fflush(stderr);
        return;                 /* left running; the process ends soon and Discord clears it then */
    }
    CloseHandle(D.thread);
    D.thread = NULL;
    if (D.wake) CloseHandle(D.wake);
    D.wake = NULL;
}

int dipc_connected(void) { return (int)D.connected; }

#endif
