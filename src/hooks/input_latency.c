/* RECOMP_INPUT_TRACE=<csv path> (or 1 for logs/input-trace-<pid>.csv).
 * A bounded, nonblocking producer queue; only the worker formats/writes IO.
 * It observes input without changing polling, state, hold times or scheduling. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include "input_latency.h"
#include "input_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define CAPACITY 8192
#define PAYLOAD 96
/* Leave room for the consumer's next cell generation in signed LONG math. */
#define MAX_LIMIT (0x7fffffffu - CAPACITY)
typedef struct Row {
    uint64_t ns, aux0, aux1;
    unsigned seq, tid, bytes;
    int slot;
    const char *stage;
    unsigned char data[PAYLOAD];
} Row;
typedef struct Cell { volatile LONG generation; Row row; } Cell;
static Cell rows[CAPACITY];
static HANDLE worker;
static FILE *output;
static LARGE_INTEGER origin, frequency;
static char filename[1024];
static volatile LONG active, stop, accepted, written;
static volatile LONG drop_lock, drop_full, bad_payload, io_errors, max_enqueue_ns;
static volatile LONG64 attempted, drop_budget;
static volatile LONG producers;
static volatile LONG64 step, frame;
static volatile LONG head;
static LONG tail;
static unsigned limit = 1000000;

static uint64_t now_ns(void)
{
    LARGE_INTEGER q;
    uint64_t ticks, freq = (uint64_t)frequency.QuadPart;
    QueryPerformanceCounter(&q);
    ticks = (uint64_t)(q.QuadPart - origin.QuadPart);
    return ticks / freq * 1000000000ull + ticks % freq * 1000000000ull / freq;
}

static void sink(const char *stage_name, int slot, const void *data,
                 unsigned bytes, uint64_t aux0, uint64_t aux1)
{
    Row *r;
    Cell *cell;
    LONG position;
    LONG ns, previous;
    LONG64 seq;
    uint64_t begin, elapsed;
    if (!InterlockedCompareExchange(&active, 0, 0)) return;
    InterlockedIncrement(&producers);
    if (!InterlockedCompareExchange(&active, 0, 0)) { InterlockedDecrement(&producers); return; }
    begin = now_ns();
    seq = InterlockedIncrement64(&attempted);
    if ((uint64_t)seq > limit) { InterlockedIncrement64(&drop_budget); goto finished; }
    if (bytes > PAYLOAD || (bytes && !data)) { InterlockedIncrement(&bad_payload); goto finished; }
    /* Bounded MPSC sequence queue. A preempted producer owns only its cell;
     * others can publish without waiting on that thread or the disk writer. */
    for (;;) {
        LONG generation, difference;
        position = InterlockedCompareExchange(&head, 0, 0);
        cell = &rows[(unsigned)position % CAPACITY];
        generation = InterlockedCompareExchange(&cell->generation, 0, 0);
        difference = generation - position;
        if (!difference) {
            if (InterlockedCompareExchange(&head, position + 1, position) == position) break;
        } else if (difference < 0) {
            InterlockedIncrement(&drop_full);
            goto finished;
        }
    }
    r = &cell->row;
    r->ns = begin; r->seq = (unsigned)seq; r->tid = GetCurrentThreadId();
    r->stage = stage_name; r->slot = slot; r->bytes = bytes; r->aux0 = aux0; r->aux1 = aux1;
    if (bytes) memcpy(r->data, data, bytes);
    InterlockedExchange(&cell->generation, position + 1);
    InterlockedIncrement(&accepted);
    elapsed = now_ns() - begin;
    ns = (LONG)(elapsed > 2000000000ull ? 2000000000ull : elapsed);
    previous = max_enqueue_ns;
    while (ns > previous) {
        LONG old = InterlockedCompareExchange(&max_enqueue_ns, ns, previous);
        if (old == previous) break;
        previous = old;
    }
finished:
    InterlockedDecrement(&producers);
}

static void metadata(int closed)
{
    char path[1100], temporary[1100];
    FILE *f;
    snprintf(path, sizeof path, "%s.json", filename);
    snprintf(temporary, sizeof temporary, "%s.tmp", filename);
    f = fopen(temporary, "wb");
    if (!f) { InterlockedIncrement(&io_errors); return; }
    fprintf(f, "{\"schema\":1,\"clock\":\"QPC relative nanoseconds\",\"frequency\":%llu,"
        "\"origin\":%llu,\"last_ns\":%llu,\"closed\":%d,\"limit\":%u,\"capacity\":%u,"
        "\"attempted\":%lld,\"accepted\":%ld,\"written\":%ld,\"drop_lock\":%ld,"
        "\"drop_full\":%ld,\"drop_budget\":%lld,\"bad_payload\":%ld,\"io_errors\":%ld,"
        "\"max_enqueue_ns\":%ld}\n", (unsigned long long)frequency.QuadPart,
        (unsigned long long)origin.QuadPart, (unsigned long long)now_ns(), closed, limit, CAPACITY,
        (long long)InterlockedCompareExchange64(&attempted, 0, 0), (long)accepted, (long)written,
        (long)drop_lock, (long)drop_full, (long long)InterlockedCompareExchange64(&drop_budget, 0, 0),
        (long)bad_payload, (long)io_errors, (long)max_enqueue_ns);
    if (fclose(f)) InterlockedIncrement(&io_errors);
#if defined(_WIN32)
    if (!MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING)) InterlockedIncrement(&io_errors);
#else
    if (rename(temporary, path)) InterlockedIncrement(&io_errors);
#endif
}

static DWORD WINAPI write_rows(LPVOID unused)
{
    Row batch[256];
    uint64_t checkpoint = 0;
    (void)unused;
    for (;;) {
        unsigned count = 0, i, j;
        while (count < 256) {
            Cell *cell = &rows[(unsigned)tail % CAPACITY];
            if (InterlockedCompareExchange(&cell->generation,0,0) != tail + 1) break;
            batch[count++] = cell->row;
            InterlockedExchange(&cell->generation, tail + CAPACITY);
            tail++;
        }
        for (i = 0; i < count; i++) {
            Row *r = &batch[i];
            char hex[PAYLOAD * 2 + 1];
            static const char digits[] = "0123456789abcdef";
            for (j = 0; j < r->bytes; j++) { hex[j*2] = digits[r->data[j] >> 4]; hex[j*2+1] = digits[r->data[j] & 15]; }
            hex[r->bytes*2] = 0;
            if (fprintf(output, "%llu,%u,%u,%s,%d,%llu,%llu,%u,%s\n",
                (unsigned long long)r->ns, r->seq, r->tid, r->stage, r->slot,
                (unsigned long long)r->aux0, (unsigned long long)r->aux1, r->bytes, hex) < 0)
                InterlockedIncrement(&io_errors);
            else InterlockedIncrement(&written);
        }
        if (count && fflush(output)) InterlockedIncrement(&io_errors);
        if (now_ns() - checkpoint >= 1000000000ull) { metadata(0); checkpoint = now_ns(); }
        if (!count && InterlockedCompareExchange(&stop, 0, 0)) break;
        if (!count) Sleep(10);
    }
    metadata(1);
    fclose(output);
    return 0;
}

static void shutdown_trace(void)
{
    InterlockedExchange(&active, 0);
    while (InterlockedCompareExchange(&producers, 0, 0)) Sleep(1);
    InterlockedExchange(&stop, 1);
    if (worker) { WaitForSingleObject(worker, INFINITE); CloseHandle(worker); worker = NULL; }
}

void input_latency_start(void)
{
    unsigned i;
    const char *path = getenv("RECOMP_INPUT_TRACE"), *budget = getenv("RECOMP_INPUT_TRACE_LIMIT");
    if (!path || !*path || !strcmp(path, "0") || worker) return;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) return;
    QueryPerformanceCounter(&origin);
    if (!strcmp(path, "1")) {
#if defined(_WIN32)
        _mkdir("logs");
#else
        mkdir("logs", 0755);
#endif
        snprintf(filename, sizeof filename, "logs/input-trace-%lu.csv", (unsigned long)GetCurrentProcessId());
    } else snprintf(filename, sizeof filename, "%s", path);
    if (budget && *budget != '-') {
        char *end;
        unsigned long long value = strtoull(budget, &end, 10);
        if (end != budget && !*end && value)
            limit = (unsigned)(value > MAX_LIMIT ? MAX_LIMIT : value);
    }
    output = fopen(filename, "wb");
    if (!output) { fprintf(stderr, "[INPUT-TRACE] cannot open %s\n", filename); return; }
    setvbuf(output, NULL, _IOFBF, 1 << 20);
    if (fputs("t_ns,seq,tid,stage,slot,aux0,aux1,n,payload\n", output) < 0 || fflush(output)) {
        fclose(output);
        fprintf(stderr, "[INPUT-TRACE] cannot write header to %s\n", filename);
        return;
    }
    for (i = 0; i < CAPACITY; i++) rows[i].generation = (LONG)i;
    /* Replace any old sidecar before producers start, even if killed in <1s. */
    metadata(0);
    worker = CreateThread(NULL, 0, write_rows, NULL, 0, NULL);
    if (!worker) { fclose(output); fprintf(stderr, "[INPUT-TRACE] writer could not start\n"); return; }
    InterlockedExchange(&active, 1);
    xbox_InputTraceSetSink(sink); /* All state is ready before input/guest threads start. */
    atexit(shutdown_trace);
    fprintf(stderr, "[INPUT-TRACE] %s; QPC %llu Hz; limit %u; queue %u\n", filename,
            (unsigned long long)frequency.QuadPart, limit, CAPACITY);
}
int input_latency_on(void) { return InterlockedCompareExchange(&active, 0, 0) != 0; }
void input_latency_step(void)
{
    if (input_latency_on()) xbox_InputTraceEmit("game_step", -1, NULL, 0, InterlockedIncrement64(&step), 0);
}
void input_latency_present(int before)
{
    if (input_latency_on()) xbox_InputTraceEmit(before ? "present_begin" : "present_end", -1, NULL, 0,
        before ? InterlockedIncrement64(&frame) : (uint64_t)InterlockedCompareExchange64(&frame,0,0),
        (uint64_t)InterlockedCompareExchange64(&step,0,0));
}
void input_latency_guest(int slot, const uint32_t words[12], int after)
{
    if (input_latency_on()) xbox_InputTraceEmit(after ? "fighter_after" : "guest_pad_read", slot,
        words, 12 * sizeof(uint32_t), (uint64_t)InterlockedCompareExchange64(&step,0,0), words[11]);
}
