/* Save states of a fight, for rollback netcode. See rollback.h.
 *
 * What a save holds, and why (measured with the test channel's memdiff and freeze verbs,
 * docs/research/rollback-state.md section 8):
 *  - .data/.bss from 0x2D54E0 to 0x3C92C8, less the clusters other threads own (the mailbox
 *    thread's blocks, the music refiller) and the pages the sound engine, the loader and the
 *    frame producer write. The small-block allocator's free lists (0x3A4B60..) are in it: a load
 *    that restores the heap without them crashes in the next allocation.
 *  - the heap block 0x80003000..0x82A0D000, page by page, less the sound engine's memory (the
 *    pages the audio subsystem's start-up writes, from AudioManagedRAM on), the frame-record
 *    queue the 60 Hz producer fills, and the pages the other threads write while the game
 *    thread is held at the first step. Only 2-3 MB of the 42 MB change in a fight.
 * Which pages change is tracked exactly: after the reference copy every heap page kept is made
 * read-only, and the first write to one raises an access violation that the handler below turns
 * into "this page changes from now on" (the game thread) or "this page belongs to another thread"
 * (any other), makes it writable and lets the write go on. A save copies the pages that changed so
 * far; a load puts those back and gives every page that first changed after the save its
 * reference content, which it still had then. Host file reads write guest memory from kernel
 * mode, where no exception comes: the toolkit calls xbox_guest_write_notify before each one. */
#define _CRT_SECURE_NO_WARNINGS
#include "rollback.h"

#ifdef _WIN32
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"

#define PAGE        4096u
#define HEAP_LO     0x80003000u
#define HEAP_HI     0x82A0D000u
#define HEAP_PAGES  ((HEAP_HI - HEAP_LO) / PAGE)
#define AUDIO_RAM   0x0033E9E4u     /* AudioManagedRAM, the sound engine's first block */
#define FRAME_QUEUE 0x003A017Cu     /* the frame-record queue object (registry 0x27) */
#define QUEUE_BYTES 0x1AA0u
#define FILE_REQUESTS 0x003C8F30u   /* the file layer's slot array and request objects */
#define FILE_REQUEST_BYTES 0x2400u
#define HOLD_MS     150

typedef struct { uint32_t lo, hi; } Range;

/* .data/.bss kept; the gaps are the mailbox thread's blocks, the game's own sound globals
 * (0x33E778.., 0x340318..: voice handles and cue state, used only by its audio code 0x30000..0x3D000;
 * sound is not rolled back, so they stay in step with the engine) and the music refiller. */
static const Range k_data[] = {
    { 0x002D54E0u, 0x002EB5ACu },
    { 0x002EBF1Cu, 0x0033E778u },
    { 0x0033ECA0u, 0x00340318u },
    { 0x00340D58u, 0x003A9324u },
    { 0x003A9684u, 0x003B8070u },
    { 0x003B83BCu, 0x003C92C8u },
};
/* .data pages of the sound engine (only its code, 0x11C000..0x142000, uses them, or it and the
 * game's cue wrappers: voice lists, the music ring, command sequence numbers; restoring them under
 * the running mixer thread made it walk stale voices) and the producer and loader words. */
static const Range k_data_skip[] = {
    { 0x002E6000u, 0x002E8000u }, { 0x003A9000u, 0x003B1000u }, { 0x003C8000u, 0x003CA000u },
};
/* Bytes another thread writes on a .data page that must be kept: they stay out of the save, the
 * page around them goes in. 0x3A56B8..0x3A56C0 changes under the held game thread (a host-timed
 * counter); left out as a whole page, it took with it EAGL's list of loaded objects (0x3A52A8), its
 * symbol pool and registries, so after a load back to before a weapon's model was made the list
 * still began with the model, whose memory was the older one: the next lookup read junk and crashed
 * in sub_0010A7D0 (stress runs, 2026-10-22/23; the 20-second soak shows it too). */
static const Range k_data_hole[] = {
    { 0x003A56B8u, 0x003A56C0u },
};
#define N_DATA_HOLE (sizeof(k_data_hole) / sizeof(k_data_hole[0]))

/* Kept on purpose: 0x3A4000..0x3A7000 holds the small-block pool's free lists (0x3A4B60.., its
 * block [0x3A4FDC], sub_000E5F40), which the fight allocates from (animation mirrors); the lists
 * and the block are restored together, or the next allocation crashes. */
#define POOL_BASE 0x003A4FDCu
#define POOL_SIZE 0x003A4FD8u
#define N_DATA      (sizeof(k_data) / sizeof(k_data[0]))
#define N_DATA_SKIP (sizeof(k_data_skip) / sizeof(k_data_skip[0]))
#define MAX_DSEG    64

typedef struct {
    int valid;
    uint8_t *data;              /* the .data segments, in order */
    uint8_t *heap;              /* pages s_change[0 .. npages) */
    size_t heap_cap;
    unsigned npages;
} Slot;

static uint8_t s_skip[HEAP_PAGES];      /* 1: left alone */
static uint8_t s_known[HEAP_PAGES];     /* 1: in s_change */
static uint32_t s_change[HEAP_PAGES];   /* heap pages known to change, in the order found */
static unsigned s_nchange, s_cursor;
static uint8_t *s_ref;                  /* the heap at the reference point */
static Range s_dseg[MAX_DSEG];
static int s_ndseg;
static size_t s_dbytes;
static Slot s_slot[ROLLBACK_SLOTS];
static int s_ready;
static uint32_t s_audio_lo, s_audio_hi;
static uint64_t *s_boot_hash;
static uint8_t s_audio_page[HEAP_PAGES];    /* 1: the audio subsystem's (left alone) */
/* write tracking */
static uint8_t s_guard[HEAP_PAGES];     /* 1: read-only, not written since the reference */
static volatile LONG s_tracking;
static CRITICAL_SECTION s_track_cs;
static DWORD s_game_tid;
static PVOID s_veh;
static __declspec(thread) int t_in_sound;   /* this thread is inside a sound engine command */
static volatile LONG s_foreign;
extern void (*xbox_guest_write_notify)(void *host, size_t length);
static int s_audio_state;               /* 0 before, 1 during, 2 after the audio start-up */

#define ARENA_BYTES  (12u * 1024u * 1024u)
#define ARENA_BLOCKS 8192
#define ARENA_ALIGN  128u               /* what sub_00029500 gives at most */
#define SND_ALLOCATOR 0x003ADB58u
/* addr/size: the piece of the arena; user: the block handed out (aligned, 16-byte header before it) */
typedef struct { uint32_t addr, size, user; int used; } Block;
static Block s_blk[ARENA_BLOCKS];
static int s_nblk;
static uint32_t s_arena, s_arena_live, s_arena_peak;
static CRITICAL_SECTION s_arena_cs;
static int s_arena_state;               /* 0 not yet, 1 on, -1 unavailable */
static void arena_start(void);
static volatile LONG s_mix_on;          /* a mixer tick is being watched (see mixer_begin) */
/* The game thread, to tell a tick it ran during (it does when the mixer blocks) from one it did not. */
static HANDLE s_game_thread;
static ULONG64 s_game_cycles;

/* RECOMP_ROLLBACK=on (debug-env.txt) until rollback ships for everyone; off, nothing of it runs:
 * no audio arena, no write tracking, no saves. */
int rollback_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("RECOMP_ROLLBACK");
        on = v && !strcmp(v, "on");
    }
    return on;
}

static uint64_t page_hash(uint32_t addr)
{
    const uint64_t *w = (const uint64_t *)XBOX_PTR(addr);
    uint64_t h = 0x9E3779B97F4A7C15ull;
    int k;
    for (k = 0; k < (int)(PAGE / 8); k++) h = (h ^ w[k]) * 0x100000001B3ull;
    return h;
}

void rollback_audio_init_begin(void)
{
    unsigned p;
    if (s_audio_state) return;
    if (!rollback_enabled()) { s_audio_state = 2; return; }
    s_boot_hash = (uint64_t *)malloc(HEAP_PAGES * sizeof(uint64_t));
    if (!s_boot_hash) { s_audio_state = 2; return; }
    for (p = 0; p < HEAP_PAGES; p++) s_boot_hash[p] = page_hash(HEAP_LO + p * PAGE);
    s_audio_state = 1;
    arena_start();      /* every block the engine asks for from here on is outside the heap */
}

void rollback_audio_init_end(void)
{
    unsigned p;
    uint32_t base, top;
    /* only the registry store that follows the audio start-up in sub_00011E40 (its return address);
     * the start-up makes its own registry stores */
    if (s_audio_state != 1 || MEM32(g_esp) != 0x0001284Bu) return;
    s_audio_state = 2;
    base = MEM32(AUDIO_RAM);
    if (s_arena && base >= s_arena && base < s_arena + ARENA_BYTES) {
        free(s_boot_hash);
        s_boot_hash = NULL;
        s_audio_lo = base;
        fprintf(stderr, "[ROLLBACK] the audio subsystem's memory is in its arena (AudioManagedRAM %08X)\n", base);
        return;
    }
    if (base < HEAP_LO || base >= HEAP_HI) {
        fprintf(stderr, "[ROLLBACK] the sound engine's memory was not found (AudioManagedRAM %08X)\n", base);
        free(s_boot_hash);
        s_boot_hash = NULL;
        return;
    }
    /* the audio subsystem's two blocks (sub_0003C6E0: 0x96000 and 0x9000 bytes) and every page
     * its start-up wrote from the first of them on; below it the small-block pages are shared */
    for (top = base; top < base + 0x96000u; top += PAGE) s_audio_page[(top - HEAP_LO) / PAGE] = 1;
    top = MEM32(0x0033EC2Cu);
    if (top >= HEAP_LO && top + 0x9000u <= HEAP_HI)
        for (; top < MEM32(0x0033EC2Cu) + 0x9000u; top += PAGE) s_audio_page[(top - HEAP_LO) / PAGE] = 1;
    for (p = (base - HEAP_LO) / PAGE; p < HEAP_PAGES; p++)
        if (page_hash(HEAP_LO + p * PAGE) != s_boot_hash[p]) s_audio_page[p] = 1;
    free(s_boot_hash);
    s_boot_hash = NULL;
    for (p = 0, top = 0; p < HEAP_PAGES; p++) top += s_audio_page[p];
    s_audio_lo = base;
    fprintf(stderr, "[ROLLBACK] the audio subsystem's memory: %u KB from %08X on\n", top * 4, base);
}

static void skip_heap(uint32_t lo, uint32_t hi)
{
    uint32_t a;
    if (lo < HEAP_LO) lo = HEAP_LO;
    if (hi > HEAP_HI) hi = HEAP_HI;
    for (a = lo & ~(PAGE - 1); a < hi; a += PAGE) s_skip[(a - HEAP_LO) / PAGE] = 1;
}

static void slots_free(void)
{
    int i;
    for (i = 0; i < ROLLBACK_SLOTS; i++) {
        free(s_slot[i].data);
        free(s_slot[i].heap);
        memset(&s_slot[i], 0, sizeof(s_slot[i]));
    }
}

static uint8_t *page_host(uint32_t p) { return (uint8_t *)XBOX_PTR(HEAP_LO + p * PAGE); }

/* The first write since the reference to page p, by the game thread or another. */
static uint32_t s_foreign_page[12], s_foreign_told;
static volatile LONG s_reads;
static uint32_t s_read_at[8], s_read_len[8], s_reads_told;
static int s_read_game[8];
static uint32_t s_read_save[8];
static volatile LONG s_saves;           /* saves since the reference: a clock for the log */
static uint8_t *s_data_before;          /* rollback_prepare: .data before the hold, to name what changed */

/* The first write since the reference to page p, by the game thread or another. Lock-free: it runs
 * in the exception handler of whichever thread wrote, and two threads may meet on one page (both
 * make it writable; the one that cleared the flag decides). Printing waits for the game thread. */
static void first_write(uint32_t p, int game)
{
    DWORD old;
    char was = InterlockedExchange8((volatile char *)&s_guard[p], 0);
    VirtualProtect(page_host(p), PAGE, PAGE_READWRITE, &old);
    if (!was) return;
    if (!game) {
        LONG n;
        s_skip[p] = 1;
        n = InterlockedIncrement((volatile LONG *)&s_foreign);
        if (n <= 12) s_foreign_page[n - 1] = HEAP_LO + p * PAGE;
    } else if (!s_known[p]) {
        s_known[p] = 1;
        s_change[s_nchange++] = p;
    }
}

/* src/main.c's crash report calls it: the rollback state at the crash. */
void (*rollback_crash_cb)(void);

static LONG CALLBACK track_fault(PEXCEPTION_POINTERS e)
{
    const EXCEPTION_RECORD *r = e->ExceptionRecord;
    uintptr_t lo = (uintptr_t)page_host(0), a;
    uint32_t p;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    if (r->ExceptionInformation[0] != 1) return EXCEPTION_CONTINUE_SEARCH;
    a = (uintptr_t)r->ExceptionInformation[1];
    if (a < lo || a >= lo + (uintptr_t)HEAP_PAGES * PAGE) return EXCEPTION_CONTINUE_SEARCH;
    p = (uint32_t)((a - lo) / PAGE);
    /* a page still guarded counts even while tracking starts or stops (the flag and the
     * protection change one after the other while other threads run) */
    if (!s_tracking && !s_guard[p]) return EXCEPTION_CONTINUE_SEARCH;
    /* the game thread inside a sound command writes the engine's memory, not the fight's */
    first_write(p, GetCurrentThreadId() == s_game_tid && !t_in_sound);
    {   /* writable now (by this thread or another a moment ago): write again */
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((void *)a, &mbi, sizeof(mbi)) && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
            return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void track_read(void *host, size_t length)
{
    uintptr_t lo = (uintptr_t)page_host(0), a = (uintptr_t)host, end = a + length;
    /* the file layer is synchronous for the game: a worker thread reads while the game thread waits
     * for it (sub_001E5420), into the game's buffers; audio streams read into the arena, not here */
    int game = 1;
    if (!s_tracking || end <= lo || a >= lo + (uintptr_t)HEAP_PAGES * PAGE) return;
    {   /* how often the fight reads files into the heap, and on which thread (printed at saves) */
        LONG n = InterlockedIncrement(&s_reads);
        if (n <= 8) {
            s_read_at[n - 1] = HEAP_LO + (uint32_t)(a - lo);
            s_read_len[n - 1] = (uint32_t)length;
            s_read_game[n - 1] = game;
            s_read_save[n - 1] = (uint32_t)s_saves;
        }
    }
    if (a < lo) a = lo;
    if (end > lo + (uintptr_t)HEAP_PAGES * PAGE) end = lo + (uintptr_t)HEAP_PAGES * PAGE;
    for (a = (a - lo) / PAGE; a < (end - lo + PAGE - 1) / PAGE; a++)
        if (s_guard[a]) first_write((uint32_t)a, game && !t_in_sound);
}

static void track_stop(void)
{
    uint32_t p, q;
    DWORD old;
    if (!InterlockedExchange(&s_tracking, 0)) return;
    xbox_guest_write_notify = NULL;
    EnterCriticalSection(&s_track_cs);
    for (p = 0; p < HEAP_PAGES; p = q) {
        for (q = p; q < HEAP_PAGES && s_guard[q] == s_guard[p]; q++) {}
        if (s_guard[p]) VirtualProtect(page_host(p), (size_t)(q - p) * PAGE, PAGE_READWRITE, &old);
    }
    memset(s_guard, 0, sizeof(s_guard));
    LeaveCriticalSection(&s_track_cs);
}

static int track_start(void)
{
    uint32_t p, q, guarded = 0;
    DWORD old;
    static int init;
    if (!init) {
        InitializeCriticalSection(&s_track_cs);
        s_veh = AddVectoredExceptionHandler(1, track_fault);
        init = 1;
    }
    if (!s_veh) return -1;
    s_game_tid = GetCurrentThreadId();
    s_foreign = s_foreign_told = 0;
    s_reads = 0;
    s_saves = 0;
    s_reads_told = 0;
    xbox_guest_write_notify = track_read;
    InterlockedExchange(&s_tracking, 1);
    EnterCriticalSection(&s_track_cs);
    for (p = 0; p < HEAP_PAGES; p = q) {
        for (q = p; q < HEAP_PAGES && !s_skip[q] == !s_skip[p]; q++) {}
        if (s_skip[p]) continue;
        memset(s_guard + p, 1, q - p);          /* before the protection: a fault finds the flag set */
        if (VirtualProtect(page_host(p), (size_t)(q - p) * PAGE, PAGE_READONLY, &old)) guarded += q - p;
        else memset(s_guard + p, 0, q - p);
    }
    LeaveCriticalSection(&s_track_cs);
    fprintf(stderr, "[ROLLBACK] write tracking on %u heap pages\n", guarded);
    return 0;
}

void rollback_sound_context(int on) { t_in_sound = on; }

/* For crash reports: how the save states treat the page of a guest address. */
const char *rollback_page_state(uint32_t addr)
{
    uint32_t p;
    if (addr < HEAP_LO || addr >= HEAP_HI) return "outside the heap";
    p = (addr - HEAP_LO) / PAGE;
    if (s_skip[p]) return s_audio_page[p] ? "left alone (audio)" : "left alone";
    if (s_known[p]) return "saved";
    if (s_guard[p]) return "unchanged since the reference";
    return "not tracked";
}

/* Tests (RECOMP_ROLLBACK_CHECK=1): the tables of the object loaded last, to catch what overwrites
 * them. A weapon's model, loaded again in a re-run, crashed in its own "Model" lookup a moment after
 * its hash chain was built (sub_0010A7D0 reading a chain entry that was not one): something wrote
 * over the chain between the build and the lookup, an allocation handed out twice or a stale
 * pointer. EAGL DynamicLoader object (docs/research/rollback-async-loads.md 1.1): +0x10 symbol
 * count, +0x20..+0x41F 256 buckets, +0x420 chain (count words), +0x424 defined flags (count bytes). */
static int s_check = -1;
static uint32_t s_wobj, s_wchain, s_wflags, s_wcount, s_wcaller;
static unsigned s_wtold;

static int check_on(void)
{
    if (s_check < 0) s_check = getenv("RECOMP_ROLLBACK_CHECK") && *getenv("RECOMP_ROLLBACK_CHECK") == '1';
    return s_check;
}

/* the first entry that is not -1 or a symbol index; -1 when all hold */
static int loader_bad(uint32_t *at, uint32_t *value)
{
    uint32_t i, v;
    for (i = 0; i < 256; i++) {
        v = MEM32(s_wobj + 0x20u + i * 4u);
        if (v != 0xFFFFFFFFu && v >= s_wcount) { *at = s_wobj + 0x20u + i * 4u; *value = v; return (int)i; }
    }
    for (i = 0; i < s_wcount; i++) {
        v = MEM32(s_wchain + i * 4u);
        if (v != 0xFFFFFFFFu && v >= s_wcount) { *at = s_wchain + i * 4u; *value = v; return 256 + (int)i; }
    }
    return -1;
}

/* whether the watched object is still loaded (in the list at 0x3A52A8, next at +4): one the game
 * unloaded gives its memory back, and then a new block there is no error */
static int watched_loaded(void)
{
    uint32_t o = MEM32(0x003A52A8u);
    int k;
    for (k = 0; k < 4096 && o >= 0x80003000u && o < 0x82A0D000u; k++, o = MEM32(o + 4u))
        if (o == s_wobj) return 1;
    return 0;
}

void rollback_check_tables(const char *where)
{
    uint32_t at, v;
    if (!check_on() || !s_wobj) return;
    if (!s_tracking || !watched_loaded()) { s_wobj = 0; return; }
    if (MEM32(s_wobj + 0x420u) != s_wchain || MEM32(s_wobj + 0x10u) != s_wcount) {
        if (s_wtold++ < 40)
            fprintf(stderr, "[ROLLBACK-CHECK] %s: loader %08X no longer itself (count %08X chain %08X, were %08X %08X; "
                    "its page %s, list head %08X, %ld saves in); watch off\n", where, s_wobj, MEM32(s_wobj + 0x10u),
                    MEM32(s_wobj + 0x420u), s_wcount, s_wchain, rollback_page_state(s_wobj), MEM32(0x003A52A8u),
                    (long)s_saves);
        s_wobj = 0;
        return;
    }
    if (loader_bad(&at, &v) >= 0) {
        if (s_wtold++ < 40)
            fprintf(stderr, "[ROLLBACK-CHECK] %s: loader %08X (loaded from %08X) has a bad table word at %08X = %08X "
                    "(%s, %ld saves in)\n", where, s_wobj, s_wcaller, at, v, rollback_page_state(at), (long)s_saves);
        s_wobj = 0;
    }
}

void rollback_check_alloc(uint32_t caller, uint32_t size, uint32_t block)
{
    uint32_t end = block + size;
    if (!check_on() || !s_wobj || !block) return;
    if (!s_tracking || !watched_loaded()) { s_wobj = 0; return; }
    if ((block < s_wobj + 0x430u && end > s_wobj) || (block < s_wchain + s_wcount * 4u && end > s_wchain) ||
        (block < s_wflags + s_wcount && end > s_wflags)) {
        if (s_wtold++ < 40)
            fprintf(stderr, "[ROLLBACK-CHECK] allocation %08X..%08X (size %X, caller %08X) lies over loader %08X "
                    "(chain %08X, flags %08X; %ld saves in)\n", block, end, size, caller, s_wobj,
                    s_wchain, s_wflags, (long)s_saves);
        s_wobj = 0;
        return;
    }
    rollback_check_tables("after an allocation");
}

void rollback_note_dynamic_load(uint32_t caller, uint32_t handle)
{
    static unsigned told;
    if (s_tracking && told++ < 200)
        fprintf(stderr, "[ROLLBACK] a dynamic load in the fight (caller %08X, %ld updates in)\n", caller, (long)s_saves);
    if (check_on() && s_tracking && handle >= 0x10000u) {
        uint32_t obj = MEM32(handle);
        rollback_check_tables("before the next load");
        if (obj >= 0x80003000u && obj < 0x82A0D000u) {
            s_wobj = obj;
            s_wcount = MEM32(obj + 0x10u);
            s_wchain = MEM32(obj + 0x420u);
            s_wflags = MEM32(obj + 0x424u);
            s_wcaller = caller;
            rollback_check_tables("as the load starts");
        }
    }
}

void rollback_reset(void)
{
    track_stop();
    InterlockedExchange(&s_mix_on, 0);
    s_ready = 0;
    s_nchange = s_cursor = 0;
    memset(s_known, 0, sizeof(s_known));
    slots_free();
}

/* .data kept less the skipped pages, as segments. */
/* whether every word of the page that changed during the hold lies in one of k_data_hole */
static int only_holes_changed(uint32_t page)
{
    uint32_t o, j;
    if (!s_data_before) return 0;
    for (o = 0; o < PAGE; o += 4) {
        int in_hole = 0;
        if (MEM32(page + o) == *(uint32_t *)(s_data_before + page + o)) continue;
        for (j = 0; j < N_DATA_HOLE; j++)
            if (page + o >= k_data_hole[j].lo && page + o < k_data_hole[j].hi) in_hole = 1;
        if (!in_hole) return 0;
    }
    return 1;
}

static void build_data(const uint8_t *data_bg, unsigned data_pages)
{
    unsigned k, j;
    s_ndseg = 0;
    s_dbytes = 0;
    for (k = 0; k < N_DATA; k++) {
        uint32_t a = k_data[k].lo;
        while (a < k_data[k].hi) {
            uint32_t page = a & ~(PAGE - 1), next = page + PAGE, end = next < k_data[k].hi ? next : k_data[k].hi;
            int skip = 0;
            for (j = 0; j < N_DATA_SKIP; j++)
                if (page >= k_data_skip[j].lo && page < k_data_skip[j].hi) skip = 1;
            if (page / PAGE < data_pages && data_bg[page / PAGE]) skip = 1;
            if (!skip) {
                uint32_t lo = a;
                while (lo < end) {      /* the piece [lo, hi) up to the next hole, then past it */
                    uint32_t hi = end, after = end;
                    for (j = 0; j < N_DATA_HOLE; j++)
                        if (k_data_hole[j].hi > lo && k_data_hole[j].lo < hi) {
                            hi = k_data_hole[j].lo > lo ? k_data_hole[j].lo : lo;
                            after = k_data_hole[j].hi < end ? k_data_hole[j].hi : end;
                        }
                    if (hi > lo) {
                        if (s_ndseg && s_dseg[s_ndseg - 1].hi == lo) s_dseg[s_ndseg - 1].hi = hi;
                        else if (s_ndseg < MAX_DSEG) { s_dseg[s_ndseg].lo = lo; s_dseg[s_ndseg].hi = hi; s_ndseg++; }
                        else break;
                        s_dbytes += hi - lo;
                    }
                    lo = after > hi ? after : end;
                }
            }
            a = end;
        }
    }
}

int rollback_prepare(void)
{
    enum { DATA_PAGES = 0x003CA000u / PAGE };
    static uint64_t *heap_h;
    static uint64_t data_h[DATA_PAGES];
    static uint8_t data_bg[DATA_PAGES];
    unsigned p, held_heap = 0, held_data = 0;
    uint32_t queue = MEM32(FRAME_QUEUE);
    rollback_reset();
    if (!s_audio_lo) {
        fprintf(stderr, "[ROLLBACK] off for this match: the sound engine's memory is unknown\n");
        return -1;
    }
    if (!heap_h) heap_h = (uint64_t *)malloc(HEAP_PAGES * sizeof(uint64_t));
    if (!s_ref) s_ref = (uint8_t *)malloc(HEAP_PAGES * PAGE);
    if (!heap_h || !s_ref) return -1;
    memset(s_skip, 0, sizeof(s_skip));
    memset(data_bg, 0, sizeof(data_bg));
    for (p = 0; p < HEAP_PAGES; p++) if (s_audio_page[p]) s_skip[p] = 1;
    if (queue >= HEAP_LO && queue < HEAP_HI) skip_heap(queue, queue + QUEUE_BYTES);
    {   /* the file layer's worker slots and request objects (one heap block, [0x3C8F30]); its free
         * list head (0x3C8F34) is in .data the other threads own, so the block goes with it
         * (docs/research/rollback-async-loads.md 5, 6) */
        uint32_t io = MEM32(FILE_REQUESTS);
        if (io >= HEAP_LO && io < HEAP_HI) skip_heap(io, io + FILE_REQUEST_BYTES);
    }
    {
        uint32_t pool = MEM32(POOL_BASE), size = MEM32(POOL_SIZE);
        fprintf(stderr, "[ROLLBACK] the system pool: %08X, %u KB\n", pool, size / 1024);
    }
    /* hold the game thread: what changes meanwhile belongs to another thread */
    for (p = 0x002D5000u / PAGE; p < DATA_PAGES; p++) data_h[p] = page_hash(p * PAGE);
    if (!s_data_before) s_data_before = (uint8_t *)malloc(DATA_PAGES * PAGE);
    if (s_data_before) memcpy(s_data_before + 0x002D5000u, XBOX_PTR(0x002D5000u), DATA_PAGES * PAGE - 0x002D5000u);
    for (p = 0; p < HEAP_PAGES; p++) heap_h[p] = page_hash(HEAP_LO + p * PAGE);
    Sleep(HOLD_MS);
    for (p = 0x002D5000u / PAGE; p < DATA_PAGES; p++)
        if (page_hash(p * PAGE) != data_h[p] && !only_holes_changed(p * PAGE)) {
            data_bg[p] = 1;
            if (held_data++ < 16) {
                /* which bytes: the words that differ, as up to six spans */
                char spans[200] = "";
                size_t used = 0;
                uint32_t o = 0, n = 0;
                while (o < PAGE && n < 6 && s_data_before) {
                    uint32_t lo, hi;
                    if (MEM32(p * PAGE + o) == *(uint32_t *)(s_data_before + p * PAGE + o)) { o += 4; continue; }
                    lo = o;
                    while (o < PAGE && MEM32(p * PAGE + o) != *(uint32_t *)(s_data_before + p * PAGE + o)) o += 4;
                    hi = o;
                    used += (size_t)snprintf(spans + used, sizeof(spans) - used, " %08X..%08X", p * PAGE + lo, p * PAGE + hi);
                    n++;
                }
                fprintf(stderr, "[ROLLBACK] .data page %08X changed while the game was held: left out (bytes%s)\n",
                        p * PAGE, spans);
            }
        }
    for (p = 0; p < HEAP_PAGES; p++)
        if (!s_skip[p] && page_hash(HEAP_LO + p * PAGE) != heap_h[p]) { s_skip[p] = 1; held_heap++; }
    build_data(data_bg, DATA_PAGES);
    {
        unsigned skipped = 0;
        for (p = 0; p < HEAP_PAGES; p++) skipped += s_skip[p];
        fprintf(stderr, "[ROLLBACK] prepared: .data %zu KB in %d pieces (%u pages other threads wrote), heap %u pages "
                "left alone (%u written by other threads)\n", s_dbytes / 1024, s_ndseg, held_data, skipped, held_heap);
    }
    return 0;
}

int rollback_reference(void)
{
    if (!s_game_thread)     /* called on the game thread */
        s_game_thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
    if (!s_ref || !s_ndseg) return -1;
    memcpy(s_ref, XBOX_PTR(HEAP_LO), (size_t)HEAP_PAGES * PAGE);
    s_nchange = s_cursor = 0;
    memset(s_known, 0, sizeof(s_known));
    if (getenv("RECOMP_ROLLBACK_FULL")) {   /* tests: every page in every save (about 4 ms a save) */
        unsigned p;
        for (p = 0; p < HEAP_PAGES; p++)
            if (!s_skip[p]) { s_known[p] = 1; s_change[s_nchange++] = p; }
    } else if (track_start()) {
        fprintf(stderr, "[ROLLBACK] off for this match: no write tracking\n");
        return -1;
    }
    s_ready = 1;
    return 0;
}

int rollback_ready(void) { return s_ready; }

void rollback_discover(int fraction)
{
    unsigned n, p;
    if (!s_ready || fraction < 1 || s_tracking) return;     /* tracking knows already */
    n = (HEAP_PAGES + (unsigned)fraction - 1) / (unsigned)fraction;
    while (n--) {
        p = s_cursor;
        s_cursor = (s_cursor + 1) % HEAP_PAGES;
        if (s_skip[p] || s_known[p]) continue;
        if (memcmp(XBOX_PTR(HEAP_LO + p * PAGE), s_ref + (size_t)p * PAGE, PAGE)) {
            s_known[p] = 1;
            s_change[s_nchange++] = p;
        }
    }
}

int rollback_save(int slot)
{
    Slot *s;
    unsigned i;
    size_t at;
    int k;
    if (!s_ready || slot < 0 || slot >= ROLLBACK_SLOTS) return -1;
    while (s_foreign_told < 12 && s_foreign_told < s_foreign && s_foreign_page[s_foreign_told])
        fprintf(stderr, "[ROLLBACK] another thread writes %08X: left alone from now on\n", s_foreign_page[s_foreign_told++]);
    while (s_reads_told < 8 && s_reads_told < (uint32_t)s_reads && s_read_at[s_reads_told]) {
        fprintf(stderr, "[ROLLBACK] a file read into the heap in the fight: %08X, %u bytes, %s thread, %u updates in\n",
                s_read_at[s_reads_told], s_read_len[s_reads_told], s_read_game[s_reads_told] ? "the game" : "another",
                s_read_save[s_reads_told]);
        s_reads_told++;
    }
    InterlockedIncrement(&s_saves);
    s = &s_slot[slot];
    if (!s->data && !(s->data = (uint8_t *)malloc(s_dbytes))) return -1;
    if (s->heap_cap < (size_t)s_nchange * PAGE) {
        size_t cap = ((size_t)s_nchange + 256) * PAGE;
        uint8_t *h = (uint8_t *)realloc(s->heap, cap);
        if (!h) return -1;
        s->heap = h;
        s->heap_cap = cap;
    }
    for (k = 0, at = 0; k < s_ndseg; at += s_dseg[k].hi - s_dseg[k].lo, k++)
        memcpy(s->data + at, XBOX_PTR(s_dseg[k].lo), s_dseg[k].hi - s_dseg[k].lo);
    for (i = 0; i < s_nchange; i++)
        memcpy(s->heap + (size_t)i * PAGE, XBOX_PTR(HEAP_LO + s_change[i] * PAGE), PAGE);
    s->npages = s_nchange;
    s->valid = 1;
    return 0;
}

int rollback_load(int slot)
{
    Slot *s;
    unsigned i;
    size_t at;
    int k;
    if (!s_ready || slot < 0 || slot >= ROLLBACK_SLOTS || !s_slot[slot].valid) return -1;
    s = &s_slot[slot];
    for (k = 0, at = 0; k < s_ndseg; at += s_dseg[k].hi - s_dseg[k].lo, k++)
        memcpy(XBOX_PTR(s_dseg[k].lo), s->data + at, s_dseg[k].hi - s_dseg[k].lo);
    for (i = 0; i < s->npages; i++)
        if (!s_skip[s_change[i]])
            memcpy(XBOX_PTR(HEAP_LO + s_change[i] * PAGE), s->heap + (size_t)i * PAGE, PAGE);
    for (; i < s_nchange; i++)      /* found to change after this save: as it was at the reference */
        if (!s_skip[s_change[i]])
            memcpy(XBOX_PTR(HEAP_LO + s_change[i] * PAGE), s_ref + (size_t)s_change[i] * PAGE, PAGE);
    return 0;
}

void rollback_stats(unsigned *save_kb, unsigned *changing, unsigned *skipped)
{
    unsigned p, n = 0;
    for (p = 0; p < HEAP_PAGES; p++) n += s_skip[p];
    if (save_kb) *save_kb = (unsigned)((s_dbytes + (size_t)s_nchange * PAGE) / 1024);
    if (changing) *changing = s_nchange;
    if (skipped) *skipped = n;
}

/* ---- the sound engine's own arena ----------------------------------------------------------
 *
 * The sound engine allocates through an allocator object (MEM32(0x3ADB58), vtable 0x289480:
 * +0 sub_0001DC10 alloc, +4 sub_0001DC20 free), both reached only indirectly. Its blocks would sit
 * between the fight's objects in the game heap, where a load puts back what the engine has since
 * changed and the mixer thread then walks stale voice lists. After its start-up every block it asks
 * for comes from this arena instead: contiguous memory outside the heap, never saved or loaded. */
extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
extern void sub_0001DC10(void);
extern void sub_0001DC20(void);

static uint32_t arena_alloc(uint32_t want)
{
    int i;
    uint32_t a = 0, size = (want + ARENA_ALIGN + 16u + 15u) & ~15u;
    if (s_arena_state != 1 || want > ARENA_BYTES / 2) return 0;
    EnterCriticalSection(&s_arena_cs);
    for (i = 0; i < s_nblk; i++) {
        if (s_blk[i].used || s_blk[i].size < size) continue;
        if (s_blk[i].size > size && s_nblk < ARENA_BLOCKS) {    /* split: the rest stays free */
            memmove(&s_blk[i + 2], &s_blk[i + 1], (size_t)(s_nblk - i - 1) * sizeof(Block));
            s_blk[i + 1].addr = s_blk[i].addr + size;
            s_blk[i + 1].size = s_blk[i].size - size;
            s_blk[i + 1].used = 0;
            s_blk[i].size = size;
            s_nblk++;
        }
        s_blk[i].used = 1;
        a = (s_blk[i].addr + 16u + ARENA_ALIGN - 1u) & ~(ARENA_ALIGN - 1u);
        s_blk[i].user = a;
        memset(XBOX_PTR(a - 16u), 0, (size_t)want + 16u);
        /* the pool free the game also calls directly (sub_001E9F90) reads the pool index at -14:
         * 0x3F names an empty pool slot, so it leaves an arena block alone */
        MEM8(a - 14u) = 0x3Fu;
        s_arena_live += s_blk[i].size;
        if (s_arena_live > s_arena_peak) {
            if (s_arena_live / (512u * 1024u) != s_arena_peak / (512u * 1024u))
                fprintf(stderr, "[ROLLBACK] the sound engine's arena holds %u KB\n", s_arena_live / 1024);
            s_arena_peak = s_arena_live;
        }
        break;
    }
    LeaveCriticalSection(&s_arena_cs);
    return a;
}

static int arena_free(uint32_t a)
{
    int i;
    if (!s_arena || a < s_arena || a >= s_arena + ARENA_BYTES) return 0;
    EnterCriticalSection(&s_arena_cs);
    for (i = 0; i < s_nblk; i++) {
        if (s_blk[i].user != a || !s_blk[i].used) continue;
        s_blk[i].used = 0;
        s_arena_live -= s_blk[i].size;
        if (i + 1 < s_nblk && !s_blk[i + 1].used) {             /* merge with the next */
            s_blk[i].size += s_blk[i + 1].size;
            memmove(&s_blk[i + 1], &s_blk[i + 2], (size_t)(s_nblk - i - 2) * sizeof(Block));
            s_nblk--;
        }
        if (i > 0 && !s_blk[i - 1].used) {                      /* and with the previous */
            s_blk[i - 1].size += s_blk[i].size;
            memmove(&s_blk[i], &s_blk[i + 1], (size_t)(s_nblk - i - 1) * sizeof(Block));
            s_nblk--;
        }
        break;
    }
    LeaveCriticalSection(&s_arena_cs);
    return 1;
}

static void arena_start(void)
{
    if (s_arena_state) return;
    if (!rollback_enabled()) { s_arena_state = -1; return; }
    InitializeCriticalSection(&s_arena_cs);
    s_arena = xbox_ContiguousAlloc(ARENA_BYTES, 4096);
    if (!s_arena) { s_arena_state = -1; fprintf(stderr, "[ROLLBACK] no room for the sound engine's arena\n"); return; }
    s_blk[0].addr = s_arena;
    s_blk[0].size = ARENA_BYTES;
    s_blk[0].used = 0;
    s_nblk = 1;
    s_arena_state = 1;
    fprintf(stderr, "[ROLLBACK] the audio code allocates from its own arena at %08X (%u KB; pool slot 63 %08X)\n",
            s_arena, ARENA_BYTES / 1024, MEM32(0x003C9158u + 63u * 4u));
}

static int audio_code(uint32_t ret)
{
    return (ret >= 0x0001C000u && ret < 0x0001E000u) ||     /* AEMS: streams, banks, the engine allocator */
           (ret >= 0x00030000u && ret < 0x0003E200u) ||     /* the audio subsystem, cues, moment sounds */
           (ret >= 0x0011C000u && ret < 0x00142000u);       /* the sound engine */
}

int rollback_route_alloc(void)
{
    uint32_t ret = MEM32(g_esp), req = ret, a;
    if (s_arena_state == 0 && audio_code(ret == 0x00029696u || ret == 0x000296C6u || ret == 0x00029723u
                                         ? MEM32(g_esp + 24) : ret))
        arena_start();      /* the first block the audio code asks for */
    if (s_arena_state != 1) return 0;
    /* sub_00029680 / 000296B0 / 00029710 call it with five words pushed: their caller is 24 bytes up */
    if (ret == 0x00029696u || ret == 0x000296C6u || ret == 0x00029723u) req = MEM32(g_esp + 24);
    if (!audio_code(req)) return 0;
    a = arena_alloc(MEM32(g_esp + 8));
    if (!a) {
        static int told;
        if (!told++) fprintf(stderr, "[ROLLBACK] the audio arena is full (%u KB live); the heap serves it\n", s_arena_live / 1024);
        return 0;
    }
    g_eax = a;
    g_esp += 4;                     /* ret */
    return 1;
}

extern void sub_0002A180(void);
/* thiscall (block), ret 4 */
void rollback_heap_free(void)
{
    if (s_arena_state == 1 && arena_free(MEM32(g_esp + 4))) { g_eax = 1; g_esp += 8; return; }
    sub_0002A180();
}

/* thiscall (size, alignment, flags), ret 12 */
void rollback_sound_alloc(void)
{
    if (s_arena_state == 1 && g_ecx == MEM32(SND_ALLOCATOR)) {
        uint32_t a = arena_alloc(MEM32(g_esp + 4));
        if (a) { g_eax = a; g_esp += 16; return; }
        {
            static int told;
            if (!told++) fprintf(stderr, "[ROLLBACK] the sound engine's arena is full (%u KB live); the heap serves it\n",
                                 s_arena_live / 1024);
        }
    }
    sub_0001DC10();
}

/* thiscall (block), ret 4 */
void rollback_sound_free(void)
{
    if (s_arena_state == 1 && arena_free(MEM32(g_esp + 4))) { g_esp += 8; return; }
    sub_0001DC20();
}

/* ---- pages the mixer thread writes ---------------------------------------------------------
 *
 * The mixer (sub_001242D0, 100 Hz) holds the sound engine's lock for its whole tick, taken and
 * given back through two function pointers (0x2E61C0 / 0x2E61C4). It also writes blocks in the
 * game heap (stream buffers the game's audio code allocated); a load that put those back made it
 * write through stale offsets. Every guest thread runs on one host core, so what changes in the
 * saved pages between the tick's lock and unlock is the mixer's: those pages are left alone from
 * then on. */
#define MIX_LOCKED   0x001242E6u      /* return addresses in sub_001242D0 */
#define MIX_UNLOCKS  0x0012435Au
static uint8_t *s_mix;
static size_t s_mix_cap;
static unsigned s_mix_n, s_mix_found;
extern void sub_0011FCC0(void);
extern void sub_0011FCD0(void);
extern void sub_00121C80(void);
extern void sub_00121C90(void);


static ULONG64 game_cycles(void)
{
    ULONG64 c = 0;
    if (s_game_thread) QueryThreadCycleTime(s_game_thread, &c);
    return c;
}

static void mixer_begin(void)
{
    unsigned i, n = s_nchange;
    s_mix_n = 0;
    if (!s_ready || !n || !s_game_thread) return;
    if (s_mix_cap < (size_t)n * PAGE) {
        size_t cap = ((size_t)n + 512) * PAGE;
        uint8_t *m = (uint8_t *)realloc(s_mix, cap);
        if (!m) return;
        s_mix = m;
        s_mix_cap = cap;
    }
    for (i = 0; i < n; i++)
        memcpy(s_mix + (size_t)i * PAGE, XBOX_PTR(HEAP_LO + s_change[i] * PAGE), PAGE);
    s_mix_n = n;
    s_game_cycles = game_cycles();
    InterlockedExchange(&s_mix_on, 1);
}

static void mixer_end(void)
{
    unsigned i;
    if (!InterlockedExchange(&s_mix_on, 0)) return;
    if (game_cycles() != s_game_cycles) return;     /* the game thread ran too: no telling who wrote */
    for (i = 0; i < s_mix_n; i++) {
        uint32_t p = s_change[i];
        if (s_skip[p] || !memcmp(s_mix + (size_t)i * PAGE, XBOX_PTR(HEAP_LO + p * PAGE), PAGE)) continue;
        s_skip[p] = 1;
        if (s_mix_found++ < 12)
            fprintf(stderr, "[ROLLBACK] the mixer writes %08X: left alone from now on\n", HEAP_LO + p * PAGE);
    }
}

void rollback_mixer_lock_a(void) { uint32_t ret = MEM32(g_esp); sub_0011FCC0(); if (ret == MIX_LOCKED) mixer_begin(); }
void rollback_mixer_lock_b(void) { uint32_t ret = MEM32(g_esp); sub_00121C80(); if (ret == MIX_LOCKED) mixer_begin(); }
void rollback_mixer_unlock_a(void) { if (MEM32(g_esp) == MIX_UNLOCKS) mixer_end(); sub_0011FCD0(); }
void rollback_mixer_unlock_b(void) { if (MEM32(g_esp) == MIX_UNLOCKS) mixer_end(); sub_00121C90(); }

#else
int rollback_enabled(void) { return 0; }
const char *rollback_page_state(uint32_t addr) { (void)addr; return "-"; }
void rollback_note_dynamic_load(uint32_t caller, uint32_t handle) { (void)caller; (void)handle; }
void rollback_check_alloc(uint32_t caller, uint32_t size, uint32_t block) { (void)caller; (void)size; (void)block; }
void rollback_check_tables(const char *where) { (void)where; }
void rollback_sound_context(int on) { (void)on; }
void rollback_mixer_lock_a(void) {}
void rollback_mixer_lock_b(void) {}
void rollback_mixer_unlock_a(void) {}
void rollback_mixer_unlock_b(void) {}
void rollback_sound_alloc(void) {}
void rollback_sound_free(void) {}
int rollback_route_alloc(void) { return 0; }
extern void sub_0002A180(void);
void rollback_heap_free(void) { sub_0002A180(); }
void rollback_audio_init_begin(void) {}
void rollback_audio_init_end(void) {}
int rollback_prepare(void) { return -1; }
int rollback_reference(void) { return -1; }
void rollback_discover(int fraction) { (void)fraction; }
int rollback_ready(void) { return 0; }
int rollback_save(int slot) { (void)slot; return -1; }
int rollback_load(int slot) { (void)slot; return -1; }
void rollback_reset(void) {}
void rollback_stats(unsigned *a, unsigned *b, unsigned *c) { if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; }
#endif
