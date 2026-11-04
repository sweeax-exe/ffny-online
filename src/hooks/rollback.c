/* Save states of a fight, for rollback netcode. See rollback.h.
 *
 * What a save holds, and why (measured with the test channel's memdiff and freeze verbs,
 * docs/research/rollback-state.md section 8):
 *  - .data/.bss from 0x2D54E0 to 0x3C92C8, less the clusters other threads own (the mailbox
 *    thread's blocks, the music refiller) and the pages the sound engine, the loader and the
 *    frame producer write. The small-block allocator's free lists (0x3A4B60..) are in it: a load
 *    that restores the heap without them crashes in the next allocation.
 *  - the game's heap block (found at run time, find_heap), page by page, less the sound engine's memory (the
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
#include "recomp_settings.h"
#include "pc_settings.h"

#ifdef _WIN32
#include "host.h"
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"

#define PAGE        4096u
/* The game's heap: the one big block it takes from the contiguous window at start (all that
 * MmQueryStatistics says is free, about 42 MB). It was 0x80003000..0x82A0D000 until the toolkit's
 * low reserve (v0.12 merge) moved it to 0x81000000..0x83A09000: with the old constants the saves
 * rewound the audio arena and GPU blocks below 16 MB and left the heap's top 16 MB out (the sound
 * engine crashed in sub_0011E530 80 steps into every rollback match, 2026-11-02). So it is found at
 * run time, as the largest block in the window (find_heap). */
static uint32_t s_heap_lo = 0x80003000u, s_heap_hi = 0x82A0D000u;
static int s_heap_found;
#define HEAP_LO     s_heap_lo
#define HEAP_HI     s_heap_hi
#define HEAP_PAGES  ((HEAP_HI - HEAP_LO) / PAGE)
#define HEAP_PAGES_MAX (0x04000000u / PAGE)     /* the whole 64 MB window */
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
/* Also left out (2026-10-24): the AEMS layer's globals 0x331C20..0x3323C0 (written only by its code,
 * 0x1C000..0x1E000) and the audio subsystem's 32 bus objects 0x3454A0..0x345968 (reached only through
 * the audio code, e.g. the slow-motion effect sub_000388B0 links a record into 0x345600). Put back
 * under the running mixer, a bus's effect list went back to before a KO's slow-motion effect was
 * linked in and the mixer thread followed a junk pointer (sub_0011E530, a fast-KO rollback test). */
static const Range k_data[] = {
    { 0x002D54E0u, 0x002EB5ACu },
    { 0x002EBF1Cu, 0x00331C20u },
    { 0x003323C0u, 0x0033E778u },
    { 0x0033ECA0u, 0x00340318u },
    { 0x00340D58u, 0x003454A0u },
    { 0x00345968u, 0x003A9324u },
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
    /* The music overlay: its sound handle [0x2DBC40], buffer [0x2DBC44] and name (0x2DBC48, up to the next
     * global at 0x2DBCC8). Rewound to -1 by a load just after an overlay track opened, while the engine kept
     * the overlay player, the next track change left a stale bank instance over the shared MPF buffer, and
     * the director walked the new tune.mpf with the old table (sub_001317C0 off the end of memory, six runs
     * matched to the byte; docs/research/music-director-crash.md). Sound is not rolled back: kept live. */
    { 0x002DBC40u, 0x002DBCC8u },
    /* EAGL's copies of what the D3D device has set, kept so that it calls D3D only on a change
     * (sub_00100BC0): per stage the texture (0x3A5030[4]) and palette (0x3A5050[4]), the LOD bias,
     * the bump-env floats (to 0x3A50A8) and the texture-stage states (seven arrays from 0x2E48B8).
     * The device (object [0x234768], D3D's state from 0x2342C8) is not rewound and renders are not
     * re-run, so its copies must not be either: put back by loads, they named another frame's
     * bindings than the device held (152 of 500 loads in a test with pauses and knock-outs), and
     * the next draw skipped the set it needed. See also rollback_unbind_textures. */
    { 0x002E48B8u, 0x002E4998u },
    { 0x003A5030u, 0x003A50A8u },
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
    LONG at;                    /* s_saves when it was taken: frees held after it are undone by its load */
} Slot;

static uint8_t s_skip[HEAP_PAGES_MAX];      /* 1: left alone */
/* Per page, the words the mixer thread writes (one bit each): a load restores the page around them. A
 * whole page left alone for a few mixer words took the game objects beside them out of the saves (the
 * music's next track, about 2.8 minutes in: three heap pages, 8 bytes each, then a division by zero in
 * sub_00081170; five-match run, 2026-10-25). */
static uint8_t *s_mixmask[HEAP_PAGES_MAX];
static uint8_t s_known[HEAP_PAGES_MAX];     /* 1: in s_change */
static uint32_t s_change[HEAP_PAGES_MAX];   /* heap pages known to change, in the order found */
static unsigned s_nchange, s_cursor;
static uint8_t *s_ref;                  /* the heap at the reference point */
static Range s_dseg[MAX_DSEG];
static int s_ndseg;
static size_t s_dbytes;
static Slot s_slot[ROLLBACK_SLOTS];
static int s_ready;
static uint32_t s_audio_lo, s_audio_hi;
static uint64_t *s_boot_hash;
static uint8_t s_audio_page[HEAP_PAGES_MAX];    /* 1: the audio subsystem's (left alone) */
/* write tracking */
static uint8_t s_guard[HEAP_PAGES_MAX];     /* 1: read-only, not written since the reference */
/* Diagnostics: the saved pages are read-only for the length of a mixer tick, so the first write to each
 * names its thread and address (RECOMP_ROLLBACK_WRITERS=1, with the allocation log on). */
static uint8_t s_mixguard[HEAP_PAGES_MAX];
static volatile LONG s_writers_on = -1;
static DWORD s_mixer_tid;
static void writer_seen(uint32_t addr, DWORD tid);
static volatile LONG s_tracking;
static CRITICAL_SECTION s_track_cs;
static DWORD s_game_tid;
static PVOID s_veh;
static __declspec(thread) int t_in_sound;   /* this thread is inside a sound engine command */
static volatile LONG s_foreign;
extern void (*xbox_guest_write_notify)(void *host, size_t length);
static int s_audio_state;               /* 0 before, 1 during, 2 after the audio start-up */

/* The arena grows in pieces as the sound side asks: it shares the 64 MiB contiguous window with the
 * game's heap and its GPU data, and a fixed 12 MiB piece left the GPU data too little: a player's game
 * froze with 62.9 of 64 MiB in use, the title's contiguous allocation retrying for ever (build 20). The
 * sound side was seen to hold up to 5.6 MiB. */
#define ARENA_FIRST  (3u * 1024u * 1024u)
#define ARENA_FIRST_SMALL (1u * 1024u * 1024u)    /* with ARENA_BIG blocks in pieces of their own: the small ones (0.6-0.8 MB) */
#define ARENA_GROW   (1u * 1024u * 1024u)
#define ARENA_MAX    (12u * 1024u * 1024u)
#define ARENA_CHUNKS 64     /* pieces, among them one per large block (ARENA_BIG); a slot given back is reused */
/* A block this large asked for outside a fight (a venue's or the front end's sample bank, 0.9-1.3 MB) gets a
 * piece of its own, given back to contiguous memory when the block is (arena_trim). In the shared pieces such
 * banks left holes the next venue's slightly larger bank did not fit, so the arena grew at most venue changes
 * and never shrank: 6.6 MB at the first fight, 11.1 MB after 16 matches, and the 17th match's GPU data found
 * no contiguous memory (F9 926777, build 46: "[CONTIG] refused", 64380 of 65408 KB in use). */
#define ARENA_BIG    (256u * 1024u)
#define ARENA_BLOCKS 65536     /* 8192 ran out in a 4.5-minute fight: the blocks a load keeps are many and small */
#define ARENA_ALIGN  128u               /* what sub_00029500 gives at most */
#define SND_ALLOCATOR 0x003ADB58u
/* addr/size: the piece of the arena; user: the block handed out (aligned, 16-byte header before it) */
/* freed: 1 given back, held; 2 taken back by a load (forget_frees_since); fight: handed out while a fight's
 * saves ran; game, at: handed to the game thread outside a sound command, at that save count */
typedef struct { uint32_t addr, size, user; int used, freed, fight, game; LONG at; } Block;
static Block s_blk[ARENA_BLOCKS];
static int s_nblk;
static uint32_t s_arena, s_arena_live, s_arena_peak, s_arena_total;
static uint32_t s_chunk_lo[ARENA_CHUNKS], s_chunk_hi[ARENA_CHUNKS];
static int s_nchunk;

static int in_arena(uint32_t a)
{
    int k;
    for (k = 0; k < s_nchunk; k++)
        if (a >= s_chunk_lo[k] && a < s_chunk_hi[k]) return 1;
    return 0;
}

/* a piece starts here: free blocks are not merged across it, so a piece can become one free block again */
static int piece_start(uint32_t a)
{
    int k;
    for (k = 0; k < s_nchunk; k++)
        if (a == s_chunk_lo[k] && s_chunk_hi[k] != s_chunk_lo[k]) return 1;
    return 0;
}
/* Tests (RECOMP_ROLLBACK_ARENA_WATCH=1): while steps run again the arena's pages are read-only, so a
 * write from the game thread into the sound engine's memory (through a handle the load brought back,
 * to an object the engine may have given to someone else since) is caught with its call chain. The
 * other threads' writes just unprotect the page. Printed when the re-run ends. */
static volatile LONG s_arena_watch_on;
static struct { uint32_t va, chain[4]; } s_aw_hit[64];
static volatile LONG s_aw_n;
static unsigned s_aw_told;
static uint32_t s_aw_seen[256][2];
static unsigned s_aw_nseen;
static void call_chain(uint32_t sp, uint32_t *out, int max);

static int arena_watch_wanted(void)
{
    static int on = -1;
    if (on < 0) on = getenv("RECOMP_ROLLBACK_ARENA_WATCH") != NULL;
    return on;
}

static void arena_watch_set(int on)
{
    int k;
    DWORD old;
    if (!arena_watch_wanted() || !s_nchunk) return;
    /* the flag is up whenever a page may be read-only (another thread can fault at any moment) */
    if (on) InterlockedExchange(&s_arena_watch_on, 1);
    for (k = 0; k < s_nchunk; k++)
        if (s_chunk_hi[k] != s_chunk_lo[k])
            VirtualProtect(XBOX_PTR(s_chunk_lo[k]), s_chunk_hi[k] - s_chunk_lo[k], on ? PAGE_READONLY : PAGE_READWRITE, &old);
    if (on) return;
    InterlockedExchange(&s_arena_watch_on, 0);
    {   /* what the re-run wrote, each call chain once */
        LONG n = s_aw_n < 64 ? s_aw_n : 64, i;
        for (i = 0; i < n; i++) {
            unsigned j;
            for (j = 0; j < s_aw_nseen; j++)
                if (s_aw_seen[j][0] == s_aw_hit[i].chain[0] && s_aw_seen[j][1] == s_aw_hit[i].chain[1]) break;
            if (j < s_aw_nseen) continue;
            if (s_aw_nseen < 256) { s_aw_seen[s_aw_nseen][0] = s_aw_hit[i].chain[0]; s_aw_seen[s_aw_nseen++][1] = s_aw_hit[i].chain[1]; }
            if (s_aw_told++ < 100)
                fprintf(stderr, "[ROLLBACK-ARENA] the game thread wrote the sound engine's %08X in a re-run, from %08X <- %08X "
                        "<- %08X <- %08X\n", s_aw_hit[i].va, s_aw_hit[i].chain[0], s_aw_hit[i].chain[1], s_aw_hit[i].chain[2],
                        s_aw_hit[i].chain[3]);
        }
        InterlockedExchange(&s_aw_n, 0);
    }
}

static int in_arena(uint32_t a);
static int arena_watch_fault(uintptr_t host)
{
    uint32_t va;
    DWORD old;
    if (!s_arena_watch_on) return 0;
    va = (uint32_t)(host - (uintptr_t)XBOX_PTR(0));
    if (!in_arena(va)) return 0;
    VirtualProtect((void *)(host & ~(uintptr_t)(PAGE - 1)), PAGE, PAGE_READWRITE, &old);
    if (GetCurrentThreadId() == s_game_tid && !t_in_sound) {
        LONG n = InterlockedIncrement(&s_aw_n) - 1;
        if (n < 64) {
            s_aw_hit[n].va = va;
            call_chain(g_esp, s_aw_hit[n].chain, 4);
        }
    }
    return 1;
}

/* a file read into the arena while it is watched (the stream buffers): unprotected first */
static void arena_watch_read(uintptr_t host, size_t length)
{
    uintptr_t p;
    DWORD old;
    if (!s_arena_watch_on || !length) return;
    if (!in_arena((uint32_t)(host - (uintptr_t)XBOX_PTR(0)))) return;
    for (p = host & ~(uintptr_t)(PAGE - 1); p < host + length; p += PAGE) VirtualProtect((void *)p, PAGE, PAGE_READWRITE, &old);
}

static CRITICAL_SECTION s_arena_cs;
static int s_arena_state;               /* 0 not yet, 1 on, -1 unavailable */
static void arena_start(void);
static volatile LONG s_mix_on;          /* a mixer tick is being watched (see mixer_begin) */
static void mixer_stats(void);
/* The game thread, to tell a tick it ran during (it does when the mixer blocks) from one it did not. */
static HANDLE s_game_thread;
static ULONG64 s_game_cycles;
/* What the game thread's own rollback work leaves (game_mark: every save and load, so every update of
 * a fight and every re-run): a count, and the CPU it last ran on. mixer_end does not trust a tick in
 * which the count moved, or which ran on another CPU than the game thread (see there). */
static volatile LONG s_game_seq, s_game_cpu = -1;
static LONG s_mix_seq;
static void game_mark(void)
{
    InterlockedIncrement(&s_game_seq);
    InterlockedExchange(&s_game_cpu, (LONG)GetCurrentProcessorNumber());
}

/* Off by default again since build 21: on for everyone in builds 19 and 20, players' games crashed
 * in the sound engine (sub_0012BE20, sub_00126150: engine objects with junk vtables in the audio
 * arena) a few seconds into online fights, and the 12 MiB arena left the GPU data too little room
 * (a freeze with 62.9 of 64 MiB in use). Since build 26 a beta setting ([online] rollback, read once at
 * start because the audio arena is set up with the sound engine); a match uses it only when every
 * player has it on (netplay.c, peer_rb). RECOMP_ROLLBACK=on or off overrides it for tests; off, nothing
 * of it runs: no audio arena, no write tracking, no saves. */
static int s_off_for_server;    /* the setting was on at start, but the server's last answer turned it off */
/* Rollback everywhere (build 52): the server's last answer made rollback mandatory in some mode
 * ([online] rollback_modes, kept by rollback_server_modes) and allowed it: the audio arena is set up
 * at start whatever the player's setting says, so the mandatory modes can roll back. The setting then
 * decides only in modes the server leaves to it. RECOMP_ROLLBACK=server (tests): the arena as if the
 * server had made rollback mandatory, but no opt-in of the player's own. */
static int server_mandates(void)
{
    return recomp_settings_get("online", "rollback_server", 0) != 0 && recomp_settings_get("online", "rollback_modes", 0) != 0;
}

int rollback_opted_in(void)
{
    const char *v = getenv("RECOMP_ROLLBACK");
    if (v) return !strcmp(v, "on");
    return recomp_settings_get("online", "rollback", 0) != 0;
}

int rollback_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("RECOMP_ROLLBACK");
        on = v ? !strcmp(v, "on") || !strcmp(v, "server") : recomp_settings_get("online", "rollback", 0) != 0;
        if (!v && !on && server_mandates()) {
            on = 1;
            fprintf(stderr, "[ROLLBACK] the server made rollback mandatory the last time this PC was online: on for "
                    "this session (audio arena)\n");
        }
        /* The beta setting alone started the audio arena in every game, lockstep matches included, while the
         * servers' switch was off: 5.5 to 10 MB more of the 64 MB contiguous window (B2 of
         * rollback-plan-2026-11.md, step 2). Now it also needs the server's yes from the last time this PC
         * was online ([online] rollback_server, kept by rollback_server_allows); [online]
         * rollback_follows_server = 0 goes back to the setting alone. RECOMP_ROLLBACK decides for tests. */
        if (!v && on && recomp_settings_get("online", "rollback_follows_server", 1) &&
            !recomp_settings_get("online", "rollback_server", 0)) {
            on = 0;
            s_off_for_server = 1;
            fprintf(stderr, "[ROLLBACK] the rollback setting is on, but the server did not allow rollback the last time "
                    "this PC was online: off for this session (no audio arena)\n");
        }
    }
    return on;
}

/* netplay.c, at each relay's CAPS answer: whether it allows rollback, for the next start (rollback_enabled). */
void rollback_server_allows(int allowed)
{
    allowed = allowed ? 1 : 0;
    if (recomp_settings_get("online", "rollback_server", 0) == allowed) return;
    if (recomp_settings_set("online", "rollback_server", allowed) > 0) {
        pc_settings_save();
        fprintf(stderr, "[ROLLBACK] the server %s rollback: the rollback setting %s from the next start\n",
                allowed ? "allows" : "no longer allows", allowed ? "applies" : "is not used");
    }
}

/* netplay.c, at each CAPS answer of a relay that sends the mode mask (CAPS bit 9, build 52): the modes in
 * which the server makes rollback mandatory (0: none), kept for the next start (rollback_enabled). */
void rollback_server_modes(unsigned modes)
{
    int v = (int)(modes & 0xFFu);
    if (recomp_settings_get("online", "rollback_modes", 0) == v) return;
    if (recomp_settings_set("online", "rollback_modes", v) > 0) {
        pc_settings_save();
        fprintf(stderr, "[ROLLBACK] the server %s (modes %02X): kept for the next start\n",
                v ? "makes rollback mandatory" : "leaves rollback to the players' setting", (unsigned)v);
    }
}

/* The online menu's notice (build 46): this session started without rollback only because the server
 * did not allow it the last time, and now the setting is still on and the server's answer is yes again
 * (rollback_server_allows kept it); the audio arena is set up at start only, so a restart uses it.
 * 2 (build 52): the server now makes rollback mandatory, and this session started without it. */
int rollback_restart_hint(void)
{
    if (getenv("RECOMP_ROLLBACK") || rollback_enabled()) return 0;
    if (server_mandates()) return 2;
    if (!s_off_for_server) return 0;
    return recomp_settings_get("online", "rollback", 0) != 0 && recomp_settings_get("online", "rollback_server", 0) != 0;
}

/* The rollback setting is on (tests: RECOMP_ROLLBACK=on), but this session runs without rollback
 * (started before the server allowed it, or the setting was turned on since): a restart would use it.
 * Also (build 52) when the server made rollback mandatory since this session started. */
int rollback_setting_idle(void)
{
    const char *v = getenv("RECOMP_ROLLBACK");
    if (rollback_enabled()) return 0;
    if (!v && server_mandates()) return 1;
    return v ? !strcmp(v, "on") : recomp_settings_get("online", "rollback", 0) != 0;
}

/* The game's heap block: the largest block of the contiguous window (the game takes it at start, before
 * the sound engine's start-up). Until it is there the old constants stand. */
extern uint32_t xbox_ContiguousBlockSize(uint32_t va);
static void find_heap(void)
{
    uint32_t va = 0x80000000u + PAGE, best = 0, best_size = 0;
    if (s_heap_found) return;
    while (va < 0x84000000u) {
        uint32_t s = xbox_ContiguousBlockSize(va);
        if (!s) { va += PAGE; continue; }
        if (s > best_size) { best = va; best_size = s; }
        va += (s + PAGE - 1) & ~(PAGE - 1);
    }
    best_size &= ~(PAGE - 1);
    if (best_size < 16u * 1024u * 1024u || best_size / PAGE > HEAP_PAGES_MAX) return;
    s_heap_lo = best;
    s_heap_hi = best + best_size;
    s_heap_found = 1;
    fprintf(stderr, "[ROLLBACK] the game's heap: %08X..%08X (%u KB)\n", s_heap_lo, s_heap_hi, best_size / 1024);
}

void rollback_heap_range(uint32_t *lo, uint32_t *hi)
{
    find_heap();
    if (lo) *lo = HEAP_LO;
    if (hi) *hi = HEAP_HI;
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
    find_heap();
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
    if (s_arena && in_arena(base)) {
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
static unsigned s_nrerun;               /* heap blocks allocated in re-runs this fight (s_rerun) */
static void release_held_arena(int all);
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

static int arena_watch_fault(uintptr_t host);

static LONG CALLBACK track_fault(PEXCEPTION_POINTERS e)
{
    const EXCEPTION_RECORD *r = e->ExceptionRecord;
    uintptr_t lo = (uintptr_t)page_host(0), a;
    uint32_t p;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    if (r->ExceptionInformation[0] != 1) return EXCEPTION_CONTINUE_SEARCH;
    a = (uintptr_t)r->ExceptionInformation[1];
    if (arena_watch_fault(a)) return EXCEPTION_CONTINUE_EXECUTION;
    if (a < lo || a >= lo + (uintptr_t)HEAP_PAGES * PAGE) return EXCEPTION_CONTINUE_SEARCH;
    p = (uint32_t)((a - lo) / PAGE);
    if (s_mixguard[p] && InterlockedExchange8((volatile char *)&s_mixguard[p], 0)) {
        DWORD old;
        VirtualProtect(page_host(p), PAGE, PAGE_READWRITE, &old);
        writer_seen(HEAP_LO + (uint32_t)(a - lo), GetCurrentThreadId());
        return EXCEPTION_CONTINUE_EXECUTION;
    }
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
    arena_watch_read(a, length);
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

/* Contiguous frees in a fight (RECOMP_HEAP_RECLAIM, on by default since build 19): a weapon's vertex
 * and index buffers live in contiguous memory, outside the save states. Given back at once, a block
 * could go to something new while a load brings back the weapon that still points at it. So they
 * are held until the fight's tracking ends, when no save that knew them is left. */
extern int (*xbox_contig_free_defer)(uint32_t addr);
extern int xbox_ContiguousFreeNow(uint32_t addr);
/* The list grows (it was 4096 entries; past that the frees ran at once, which a later load could turn
 * into a block used twice: B2 of rollback-plan-2026-11.md). */
static uint32_t *s_held_free;
static LONG *s_held_free_at;            /* s_saves at each held free */
static unsigned s_nheld, s_held_cap, s_held_over;
static CRITICAL_SECTION s_held_cs;
static LONG s_held_cs_ready;

static int hold_contig_free(uint32_t addr)
{
    int kept = 0;
    EnterCriticalSection(&s_held_cs);
    if (s_nheld == s_held_cap) {
        unsigned cap = s_held_cap ? s_held_cap * 2 : 4096;
        uint32_t *a = (uint32_t *)realloc(s_held_free, cap * sizeof(uint32_t));
        LONG *t = a ? (LONG *)realloc(s_held_free_at, cap * sizeof(LONG)) : NULL;
        if (a) s_held_free = a;
        if (t) { s_held_free_at = t; s_held_cap = cap; }
    }
    if (s_nheld < s_held_cap) {
        s_held_free_at[s_nheld] = s_saves;
        s_held_free[s_nheld++] = addr;
        kept = 1;
    }
    else s_held_over++;
    LeaveCriticalSection(&s_held_cs);
    return kept;
}

static void release_contig_frees(void)
{
    unsigned i, n;
    if (!s_held_cs_ready) return;
    xbox_contig_free_defer = NULL;
    EnterCriticalSection(&s_held_cs);
    n = s_nheld;
    for (i = 0; i < n; i++) xbox_ContiguousFreeNow(s_held_free[i]);
    s_nheld = 0;
    LeaveCriticalSection(&s_held_cs);
    if (n || s_held_over)
        fprintf(stderr, "[ROLLBACK] %u contiguous block(s) held through the fight given back (%u given back at once: "
                "too many)\n", n, s_held_over);
    s_held_over = 0;
}

static void hold_contig_frees(void)
{
    if (!InterlockedExchange(&s_held_cs_ready, 1)) InitializeCriticalSection(&s_held_cs);
    xbox_contig_free_defer = hold_contig_free;
}

static void run_late_frees(void);
static void release_held_xapi(int all);

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
    release_contig_frees();
    release_held_arena(1);
    release_held_xapi(1);
    run_late_frees();
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
    s_nrerun = 0;
    xbox_guest_write_notify = track_read;
    InterlockedExchange(&s_tracking, 1);
    hold_contig_frees();
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
int rollback_on_game_thread(void) { return s_game_tid && GetCurrentThreadId() == s_game_tid; }

/* For crash reports: how the save states treat the page of a guest address. */
static int mixmask_keep(void);
const char *rollback_page_state(uint32_t addr)
{
    uint32_t p;
    if (addr < HEAP_LO || addr >= HEAP_HI) return "outside the heap";
    p = (addr - HEAP_LO) / PAGE;
    if (s_skip[p]) return s_audio_page[p] ? "left alone (audio)" : "left alone";
    if (s_mixmask[p]) {
        uint32_t w = ((addr - HEAP_LO) % PAGE) / 4u;
        if (s_mixmask[p][w >> 3] & (1u << (w & 7))) return mixmask_keep() ? "a mixer word (kept live)" : "a mixer word (restored by loads)";
        if (s_known[p]) return "saved (page with mixer words)";
    }
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

/* whether the watched object is still loaded (in the list at 0x3A52A8, next at +0, previous at +4): one the game
 * unloaded gives its memory back, and then a new block there is no error */
static int watched_loaded(void)
{
    uint32_t o = MEM32(0x003A52A8u);
    int k;
    for (k = 0; k < 4096 && o >= HEAP_LO && o < HEAP_HI; k++, o = MEM32(o))
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

/* Tests (RECOMP_ROLLBACK_ALLOCLOG=1): every heap block with the code that asked for it, so a page
 * another thread turns out to write can be traced to its allocations. */
#define ALLOCLOG 131072
static struct { uint32_t block, size, caller, outer, chain[6]; } *s_alog;
static unsigned s_alog_n;
static int alloclog_on(void)
{
    static int on = -1;
    if (on < 0) {
        on = getenv("RECOMP_ROLLBACK_ALLOCLOG") != NULL;
        if (on) s_alog = calloc(ALLOCLOG, sizeof(*s_alog));
        if (!s_alog) on = 0;
    }
    return on;
}

static void alloclog_page(uint32_t page, const char *who)
{
    unsigned i, n = s_alog_n < ALLOCLOG ? s_alog_n : ALLOCLOG, shown = 0;
    if (!alloclog_on()) return;
    for (i = 0; i < n && shown < 6; i++) {
        unsigned k = (s_alog_n - 1 - i) % ALLOCLOG;     /* newest first */
        uint32_t lo = page & (PAGE - 1) ? page : page, hi = page & (PAGE - 1) ? page + 1 : page + PAGE;
        if (s_alog[k].block < hi && s_alog[k].block + s_alog[k].size > lo) {   /* a page, or the block at an address */
            fprintf(stderr, "[ROLLBACK]   %s's page %08X holds block %08X+%X from caller %08X (outer %08X; calls %08X "
                    "%08X %08X %08X %08X %08X)\n", who, page, s_alog[k].block, s_alog[k].size, s_alog[k].caller,
                    s_alog[k].outer, s_alog[k].chain[0], s_alog[k].chain[1], s_alog[k].chain[2], s_alog[k].chain[3],
                    s_alog[k].chain[4], s_alog[k].chain[5]);
            shown++;
        }
    }
}

/* Return addresses on the guest stack above sp that follow a direct call (E8 rel32 into .text): a
 * scan, so a stale word can slip in, but only one a CALL produced. */
static void call_chain(uint32_t sp, uint32_t *out, int max)
{
    uint32_t o;
    int n = 0;
    for (o = 0; o < 0x400u && n < max; o += 4) {
        uint32_t r = MEM32(sp + o), target;
        if (r < 0x00011005u || r >= 0x00210E00u || MEM8(r - 5) != 0xE8) continue;
        target = r + MEM32(r - 4);
        if (target < 0x00011000u || target >= 0x00210E00u) continue;
        out[n++] = r;
    }
    while (n < max) out[n++] = 0;
}

/* Diagnostics (RECOMP_ROLLBACK_ALLOCLOG=1): the heap's live blocks (start -> size, call chain), so a
 * byte the mixer changes can be named as a field of the block it lies in. Open addressing with linear
 * probing; a free shifts the run back. */
#define LIVE_BITS 18
#define LIVE_CAP  (1u << LIVE_BITS)
static struct LiveBlock { uint32_t at, size, chain[3]; } *s_live;
static CRITICAL_SECTION s_live_cs;
static unsigned s_live_n;

static unsigned live_slot(uint32_t a) { return ((a >> 4) * 2654435761u) >> (32 - LIVE_BITS); }

static void live_add(uint32_t a, uint32_t size, const uint32_t *chain)
{
    unsigned i;
    if (!s_live) {
        s_live = (struct LiveBlock *)calloc(LIVE_CAP, sizeof(*s_live));
        InitializeCriticalSection(&s_live_cs);
        if (!s_live) return;
    }
    EnterCriticalSection(&s_live_cs);
    if (s_live_n < LIVE_CAP - LIVE_CAP / 8) {
        for (i = live_slot(a); s_live[i].at && s_live[i].at != a; i = (i + 1) & (LIVE_CAP - 1)) {}
        if (!s_live[i].at) s_live_n++;
        s_live[i].at = a;
        s_live[i].size = size;
        memcpy(s_live[i].chain, chain, sizeof(s_live[i].chain));
    }
    LeaveCriticalSection(&s_live_cs);
}

/* the block starting at a (copied to *out), 1 if live; drop: forget it */
static int live_get(uint32_t a, int drop, struct LiveBlock *out)
{
    unsigned i, j, k;
    int found = 0;
    if (!s_live || !a) return 0;
    EnterCriticalSection(&s_live_cs);
    for (i = live_slot(a); s_live[i].at && s_live[i].at != a; i = (i + 1) & (LIVE_CAP - 1)) {}
    if (s_live[i].at) {
        found = 1;
        if (out) *out = s_live[i];
        if (drop) {
            s_live[i].at = 0;
            s_live_n--;
            for (j = (i + 1) & (LIVE_CAP - 1); s_live[j].at; j = (j + 1) & (LIVE_CAP - 1)) {
                k = live_slot(s_live[j].at);
                if ((j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j))) {
                    s_live[i] = s_live[j];
                    s_live[j].at = 0;
                    i = j;
                }
            }
        }
    }
    LeaveCriticalSection(&s_live_cs);
    return found;
}

/* the live block holding addr, searching 16-byte starts down to 256 KB below */
static int live_find(uint32_t addr, struct LiveBlock *out)
{
    uint32_t a = addr & ~15u, lowest = addr > HEAP_LO + 0x40000u ? addr - 0x40000u : HEAP_LO;
    for (; a >= lowest && a >= 16; a -= 16)
        if (live_get(a, 0, out)) return addr < out->at + out->size;
    return 0;
}

/* Each run of bytes the mixer changed on a page, as a field of its block; each (size, allocator, offset)
 * once, up to 400 lines. */
static void mixer_fields(uint32_t page, const uint8_t *was, const uint8_t *now)
{
    static struct { uint32_t size, chain0, off; } seen[2048];
    static unsigned nseen, told;
    unsigned o = 0, k;
    while (o < PAGE && told < 400) {
        unsigned lo, hi;
        struct LiveBlock b;
        uint32_t off;
        if (was[o] == now[o]) { o++; continue; }
        lo = o;
        while (o < PAGE && was[o] != now[o]) o++;
        hi = o;
        if (!live_find(page + lo, &b)) {
            fprintf(stderr, "[ROLLBACK-MIX] %08X (%u bytes): in no known block\n", page + lo, hi - lo);
            told++;
            continue;
        }
        off = page + lo - b.at;
        for (k = 0; k < nseen; k++)
            if (seen[k].size == b.size && seen[k].chain0 == b.chain[0] && seen[k].off == off) break;
        if (k < nseen) continue;
        if (nseen < 2048) { seen[nseen].size = b.size; seen[nseen].chain0 = b.chain[0]; seen[nseen].off = off; nseen++; }
        told++;
        fprintf(stderr, "[ROLLBACK-MIX] block size %X from %08X <- %08X <- %08X: +%X, %u bytes (%08X)\n", b.size,
                b.chain[0], b.chain[1], b.chain[2], off, hi - lo, b.at);
    }
}

static void fight_alloc_add(uint32_t block);

/* Heap blocks allocated while steps run again (netplay.c's rollback_to), newest last: a crash on one says
 * so (B5 of rollback-plan-2026-11.md; generator-timescale-desync.md 7). */
#define RERUN_ALLOCS 4096
static struct { uint32_t block, size, caller, chain0; LONG saves; } s_rerun[RERUN_ALLOCS];
static volatile int s_resim;
uint32_t rollback_crash_va;

/* The game's own sound globals (the k_data gaps 0x33E778..0x33ECA0 and 0x340318..0x340D58: voice handles,
 * cue state and their counters, used by its audio code on the game thread) are never saved: sound is not
 * rolled back. But a re-run's audio code still changes them, so a pass on a wrong prediction left them
 * otherwise than the pass that is kept would have, and the next re-run read that (the wrong-prediction
 * soak: the second correct re-run of a stretch differed from the first, cue counters at 0x340898 and
 * then a fighter's input history). They are put back as they were before the re-runs.
 * RECOMP_ROLLBACK_SOUND_GLOBALS=live leaves them as the re-runs make them (as before, for comparison). */
static const Range k_sound_globals[] = { { 0x0033E778u, 0x0033ECA0u }, { 0x00340318u, 0x00340D58u } };
static uint8_t s_sound_globals[(0x0033ECA0u - 0x0033E778u) + (0x00340D58u - 0x00340318u)];

static int sound_globals_kept(void)
{
    static int on = -1;
    if (on < 0) on = !(getenv("RECOMP_ROLLBACK_SOUND_GLOBALS") && !strcmp(getenv("RECOMP_ROLLBACK_SOUND_GLOBALS"), "live"));
    return on;
}

/* Tests (the wrong-prediction soak with RECOMP_ROLLBACK_SOAK_DIFF): memory as the load left it before the
 * first correct re-run (phase 1) against the same before the second (phase 3). The saved memory is the
 * same by construction, so every word that differs is state outside the saves that the pass on wrong
 * buttons changed: the carriers. Printed by rollback_soak_carriers when the steps came out otherwise. */
#define SD_DATA_LO 0x002D5000u
#define SD_DATA_HI 0x003CA000u
static int s_lp_phase;
static int s_lp_slot[4];
static uint32_t s_lp_hash[4];
static LONG s_lp_at[4];
static uint8_t *s_lp_data, *s_lp_heap;
static struct { uint32_t lo, hi, now, was; } s_lp_span[64];
static unsigned s_lp_n;
void rollback_soak_phase(int phase) { s_lp_phase = phase; }

static void load_point(void)
{
    uint32_t a, p;
    if (!s_lp_phase || !s_ready) return;
    if (!s_lp_data) s_lp_data = (uint8_t *)malloc(SD_DATA_HI - SD_DATA_LO);
    if (!s_lp_heap) s_lp_heap = (uint8_t *)malloc((size_t)HEAP_PAGES * PAGE);
    if (!s_lp_data || !s_lp_heap) return;
    if (s_lp_phase == 1) {
        memcpy(s_lp_data, XBOX_PTR(SD_DATA_LO), SD_DATA_HI - SD_DATA_LO);
        memcpy(s_lp_heap, XBOX_PTR(HEAP_LO), (size_t)HEAP_PAGES * PAGE);
        return;
    }
    if (s_lp_phase != 3) return;
    s_lp_n = 0;
    for (a = SD_DATA_LO; a < SD_DATA_HI && s_lp_n < 64; a += 4) {
        uint32_t lo;
        if (*(uint32_t *)(s_lp_data + a - SD_DATA_LO) == MEM32(a)) continue;
        lo = a;
        s_lp_span[s_lp_n].now = MEM32(a);
        s_lp_span[s_lp_n].was = *(uint32_t *)(s_lp_data + a - SD_DATA_LO);
        while (a + 4 < SD_DATA_HI && *(uint32_t *)(s_lp_data + a + 4 - SD_DATA_LO) != MEM32(a + 4)) a += 4;
        s_lp_span[s_lp_n].lo = lo;
        s_lp_span[s_lp_n++].hi = a + 4;
    }
    for (p = 0; p < HEAP_PAGES && s_lp_n < 64; p++) {
        const uint32_t *was = (const uint32_t *)(s_lp_heap + (size_t)p * PAGE);
        const uint32_t *now = (const uint32_t *)XBOX_PTR(HEAP_LO + p * PAGE);
        unsigned w;
        if (!memcmp(was, now, PAGE)) continue;
        for (w = 0; w < PAGE / 4 && s_lp_n < 64; w++) {
            unsigned e;
            if (was[w] == now[w]) continue;
            if (s_mixmask[p] && (s_mixmask[p][w >> 3] & (1u << (w & 7)))) continue;
            for (e = w; e + 1 < PAGE / 4 && was[e + 1] != now[e + 1]; e++) {}
            s_lp_span[s_lp_n].lo = HEAP_LO + p * PAGE + w * 4;
            s_lp_span[s_lp_n].hi = HEAP_LO + p * PAGE + e * 4 + 4;
            s_lp_span[s_lp_n].now = now[w];
            s_lp_span[s_lp_n++].was = was[w];
            w = e;
        }
    }
}

static const char *data_kind(uint32_t a);
void rollback_soak_carriers(long long step)
{
    static unsigned told;
    unsigned i;
    if (told < 400)
        fprintf(stderr, "[SOAKDIFF] carriers at step %lld: the loads read save %d (hash %08X, %ld saves in) and %d (hash %08X, %ld)\n",
                step, s_lp_slot[1], s_lp_hash[1], (long)s_lp_at[1], s_lp_slot[3], s_lp_hash[3], (long)s_lp_at[3]);
    for (i = 0; i < s_lp_n && told < 400; i++, told++) {
        uint32_t lo = s_lp_span[i].lo;
        const char *kind = lo < SD_DATA_HI ? data_kind(lo) : rollback_page_state(lo);
        fprintf(stderr, "[SOAKDIFF] carrier at step %lld: %08X..%08X (%s) was %08X before the wrong pass, %08X after\n",
                step, lo, s_lp_span[i].hi, kind, s_lp_span[i].was, s_lp_span[i].now);
        if (lo >= HEAP_LO && lo < HEAP_HI) alloclog_page(lo, "carrier");
    }
}

/* Tests (RECOMP_ROLLBACK_SOAK_WRONG=keep-sound): the soak's pass on wrong buttons keeps what it did to the sound
 * globals, as online, where the pass on a wrong prediction is the normal one (its sound plays, and the re-runs
 * after it put the globals back to what it left). The second correct re-run then starts from them. */
static int s_keep_sound_once;
void rollback_keep_sound_globals_once(void) { s_keep_sound_once = 1; }

void rollback_set_resim(int on)
{
    unsigned k;
    size_t at;
    if (on && !s_resim) load_point();
    if (!on && s_keep_sound_once && s_resim) { s_keep_sound_once = 0; s_resim = 0; arena_watch_set(0); return; }
    if (sound_globals_kept() && on != s_resim)
        for (k = 0, at = 0; k < sizeof(k_sound_globals) / sizeof(k_sound_globals[0]); at += k_sound_globals[k].hi - k_sound_globals[k].lo, k++) {
            if (on) memcpy(s_sound_globals + at, XBOX_PTR(k_sound_globals[k].lo), k_sound_globals[k].hi - k_sound_globals[k].lo);
            else memcpy(XBOX_PTR(k_sound_globals[k].lo), s_sound_globals + at, k_sound_globals[k].hi - k_sound_globals[k].lo);
        }
    s_resim = on;
    arena_watch_set(on);    /* tests: RECOMP_ROLLBACK_ARENA_WATCH */
}

static int rerun_block(uint32_t v, unsigned *k_out)
{
    unsigned i, n = s_nrerun < RERUN_ALLOCS ? s_nrerun : RERUN_ALLOCS;
    for (i = 0; i < n; i++) {
        unsigned k = (s_nrerun - 1 - i) % RERUN_ALLOCS;
        if (v >= s_rerun[k].block && v < s_rerun[k].block + (s_rerun[k].size ? s_rerun[k].size : 1)) { *k_out = k; return 1; }
    }
    return 0;
}

static void crash_word(const char *what, uint32_t v)
{
    unsigned k;
    uint32_t pool = MEM32(POOL_BASE), psize = MEM32(POOL_SIZE);
    char extra[160] = "";
    if (rerun_block(v, &k))
        snprintf(extra, sizeof(extra), "; in block %08X+%X allocated in a re-run (caller %08X <- %08X, %ld saves in, now %ld)",
                 s_rerun[k].block, s_rerun[k].size, s_rerun[k].caller, s_rerun[k].chain0, (long)s_rerun[k].saves, (long)s_saves);
    else if (pool && v >= pool && v < pool + psize)
        snprintf(extra, sizeof(extra), "; in the system pool %08X+%X", pool, psize);
    else if (in_arena(v))
        snprintf(extra, sizeof(extra), "; in the audio arena");
    fprintf(stderr, "[ROLLBACK]   %s = %08X (%s%s)\n", what, v, rollback_page_state(v), extra);
}

/* The crash report: the objects the guest registers name, with the page state of the object, its vtable,
 * +0xC and +0x10 (the container node of the sub_000EDF90 crash: +0xC descriptor, +0x10 child array). */
void rollback_crash_objects(void)
{
    static const char *const names[] = { "ecx", "esi", "edi", "ebx", "eax", "edx" };
    const uint32_t regs[6] = { g_ecx, g_esi, g_edi, g_ebx, g_eax, g_edx };
    int i;
    fprintf(stderr, "[ROLLBACK] at the crash: %s, %u heap blocks allocated in re-runs this fight; fault VA %08X\n",
            s_resim ? "running steps again" : "a normal update", s_nrerun, rollback_crash_va);
    if (rollback_crash_va) crash_word("fault", rollback_crash_va);
    for (i = 0; i < 6; i++) {
        uint32_t v = regs[i];
        char what[24];
        crash_word(names[i], v);
        if (v < HEAP_LO || v >= HEAP_HI - 0x14u) continue;
        snprintf(what, sizeof(what), "  [%s]", names[i]);
        crash_word(what, MEM32(v));
        snprintf(what, sizeof(what), "  [%s+C]", names[i]);
        crash_word(what, MEM32(v + 0xCu));
        snprintf(what, sizeof(what), "  [%s+10]", names[i]);
        crash_word(what, MEM32(v + 0x10u));
    }
}

void rollback_check_alloc(uint32_t caller, uint32_t size, uint32_t block)
{
    uint32_t end = block + size;
    if (s_tracking && block >= HEAP_LO && block < HEAP_HI) fight_alloc_add(block);
    if (s_resim && block >= HEAP_LO && block < HEAP_HI) {
        unsigned k = s_nrerun++ % RERUN_ALLOCS;
        uint32_t chain[1];
        call_chain(g_esp, chain, 1);
        s_rerun[k].block = block;
        s_rerun[k].size = size;
        s_rerun[k].caller = caller;
        s_rerun[k].chain0 = chain[0];
        s_rerun[k].saves = s_saves;
    }
    if (block && alloclog_on()) {
        unsigned k = s_alog_n++ % ALLOCLOG;
        s_alog[k].block = block;
        s_alog[k].size = size;
        s_alog[k].caller = caller;
        /* sub_00029680 / 000296B0 / 00029710 call with five words pushed: their caller is 20 bytes up
         * from here (the allocator has returned) */
        s_alog[k].outer = (caller == 0x00029696u || caller == 0x000296C6u || caller == 0x00029723u) ? MEM32(g_esp + 20) : 0;
        call_chain(g_esp, s_alog[k].chain, 6);
        live_add(block, size, s_alog[k].chain);
    }
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

static volatile LONG s_dynloads;
unsigned rollback_dynamic_loads(void) { return (unsigned)s_dynloads; }

void rollback_note_dynamic_load(uint32_t caller, uint32_t handle)
{
    static unsigned told;
    InterlockedIncrement(&s_dynloads);
    if (s_tracking && told++ < 200)
        fprintf(stderr, "[ROLLBACK] a dynamic load in the fight (caller %08X, %ld updates in)\n", caller, (long)s_saves);
    if (check_on() && s_tracking && handle >= 0x10000u) {
        uint32_t obj = MEM32(handle);
        rollback_check_tables("before the next load");
        if (obj >= HEAP_LO && obj < HEAP_HI) {
            s_wobj = obj;
            s_wcount = MEM32(obj + 0x10u);
            s_wchain = MEM32(obj + 0x420u);
            s_wflags = MEM32(obj + 0x424u);
            s_wcaller = caller;
            rollback_check_tables("as the load starts");
        }
    }
}

static void release_kept_arena(void);
static int s_kept_rescan;   /* the fight's end left kept blocks named: one more scan when the next load needs room */
void rollback_reset(void)
{
    int fought = s_tracking;
    track_stop();
    /* The blocks a load kept go back as soon as the fight is over, not only when the next fight's saves
     * start: a rematch chain loads its next venue in between, and its banks found the arena scattered
     * with the last fight's kept blocks (F9 e7d57d, build 54: "1813 blocks used (817 freed and held or
     * kept)" as the next venue loaded, the arena grown twice). Those still named here (the results
     * screen holds the fight's objects) get one more try when a load finds no room (arena_alloc). */
    if (fought && s_arena) {
        release_kept_arena();
        s_kept_rescan = 1;
    }
    InterlockedExchange(&s_mix_on, 0);
    mixer_stats();
    s_ready = 0;
    s_nchange = s_cursor = 0;
    memset(s_known, 0, sizeof(s_known));
    {
        unsigned p;
        for (p = 0; p < HEAP_PAGES; p++) { free(s_mixmask[p]); s_mixmask[p] = NULL; }
    }
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

/* The next fight starts: the blocks a load kept after the engine gave them back (used and freed) can go,
 * but only those nothing points into any more. Freed blindly (10900 after one match), some were still in
 * the engine's stream table and the mixer crashed in sub_001E8340 in the second match (2026-10-25); kept
 * for ever they fill the arena (1-2 MB a fight in a 64 MB window that is 61 MB full). So guest memory is
 * scanned for words that point into a kept block, the way a conservative collector marks: the image's
 * data, the thread stacks and XAPI's heap up to 0x04000000, and the heap and contiguous window above. */
typedef struct { uint32_t lo, hi; int ref; } Kept;
static Kept *s_kept;
static unsigned s_nkept;

static void mark_words(const uint32_t *w, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        uint32_t v = w[i];
        unsigned lo = 0, hi = s_nkept;
        if (v < s_kept[0].lo || v >= s_kept[s_nkept - 1].hi) continue;
        while (lo < hi) {           /* the kept blocks are in address order */
            unsigned mid = (lo + hi) / 2;
            if (v < s_kept[mid].lo) hi = mid;
            else if (v >= s_kept[mid].hi) lo = mid + 1;
            else { s_kept[mid].ref = 1; break; }
        }
    }
}

static void mark_range(uint32_t lo, uint32_t hi)
{
    uint32_t a = lo;
    while (a < hi) {
        MEMORY_BASIC_INFORMATION mbi;
        uint32_t end;
        if (!VirtualQuery((void *)XBOX_PTR(a), &mbi, sizeof(mbi))) break;
        end = a + (uint32_t)((uintptr_t)mbi.BaseAddress + mbi.RegionSize - XBOX_PTR(a));
        if (end > hi || end <= a) end = hi;
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            mark_words((const uint32_t *)XBOX_PTR(a), (end - a) / 4);
        a = end;
    }
}

static void release_kept_arena(void)
{
    int i;
    unsigned k, n = 0, freed = 0, bytes = 0;
    DWORD t0 = GetTickCount();
    if (!s_arena) return;
    if (!s_kept && !(s_kept = (Kept *)malloc(ARENA_BLOCKS * sizeof(Kept)))) return;
    EnterCriticalSection(&s_arena_cs);
    for (i = 0; i < s_nblk; i++)
        if (s_blk[i].used && s_blk[i].freed) {
            s_kept[n].lo = s_blk[i].user - 16u;
            s_kept[n].hi = s_blk[i].addr + s_blk[i].size;
            s_kept[n].ref = 0;
            n++;
        }
    s_nkept = n;
    if (n) {
        uint32_t at = HEAP_LO;
        /* below the window: the image, the stacks and XAPI's heap, which runs to 0x04000000 since the
         * toolkit's v0.12 layout ("Heap: 48 MB at 0x00F80000-0x04000000"; it ended at 0x01600000 before) */
        mark_range(0x00010000u, 0x04000000u);
        /* the heap and the whole contiguous window above it (the arena, DirectSound's voice memory, the GPU
         * data), less the kept blocks themselves (one that only a kept block names is no less dead) and less
         * the arena's free blocks: their bytes are what the last owner left, and old handles in them kept
         * blocks "named" for ever (F9 e7d57d, build 54: 241, then 295 of them carried from match to match
         * through a rematch chain, scattered between the live ones, until the next venue's 116 and 201 KB
         * blocks found no room among 516 free blocks of at most 21 KB) */
        for (i = 0; i < s_nblk; i++) {
            if (s_blk[i].used && !s_blk[i].freed) continue;
            if (s_blk[i].addr > at) mark_range(at, s_blk[i].addr);
            if (s_blk[i].addr + s_blk[i].size > at) at = s_blk[i].addr + s_blk[i].size;
        }
        mark_range(at, 0x84000000u);
    }
    LeaveCriticalSection(&s_arena_cs);
    for (k = 0; k < n; k++)
        if (!s_kept[k].ref) { arena_free_now(s_kept[k].lo + 16u); freed++; bytes += s_kept[k].hi - s_kept[k].lo; }
    if (n)
        fprintf(stderr, "[ROLLBACK] %u arena blocks the engine gave back in the last fight: %u (%u KB) freed, %u still "
                "named somewhere (%lu ms)\n", n, freed, bytes / 1024, n - freed, (unsigned long)(GetTickCount() - t0));
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
    if (!s_heap_found) {
        find_heap();
        if (!s_heap_found) {
            fprintf(stderr, "[ROLLBACK] off for this match: the game's heap block was not found\n");
            return -1;
        }
    }
    release_kept_arena();
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
        {   /* which: the heap pages no save or load touches */
            char line[300];
            int n = 0, shown = 0;
            for (p = 0; p < HEAP_PAGES && shown < 24; p++)
                if (s_skip[p]) {
                    n += snprintf(line + n, sizeof(line) - (size_t)n, " %08X%s", HEAP_LO + p * PAGE,
                                  s_audio_page[p] ? "(audio)" : "");
                    shown++;
                    alloclog_page(HEAP_LO + p * PAGE, "left alone");
                }
            if (shown) fprintf(stderr, "[ROLLBACK] heap pages left alone:%s\n", line);
        }
    }
    return 0;
}

int rollback_reference(void)
{
    if (!s_game_thread)     /* called on the game thread */
        s_game_thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
    if (!s_ref || !s_ndseg) {
        fprintf(stderr, "[ROLLBACK] off for this match: no reference copy (%s)\n",
                !s_ref ? "no memory for the 42 MB copy" : "no .data segments were built");
        return -1;
    }
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
    while (s_foreign_told < 12 && s_foreign_told < s_foreign && s_foreign_page[s_foreign_told]) {
        fprintf(stderr, "[ROLLBACK] another thread writes %08X: left alone from now on\n", s_foreign_page[s_foreign_told]);
        alloclog_page(s_foreign_page[s_foreign_told++], "another thread");
    }
    while (s_reads_told < 8 && s_reads_told < (uint32_t)s_reads && s_read_at[s_reads_told]) {
        fprintf(stderr, "[ROLLBACK] a file read into the heap in the fight: %08X, %u bytes, %s thread, %u updates in\n",
                s_read_at[s_reads_told], s_read_len[s_reads_told], s_read_game[s_reads_told] ? "the game" : "another",
                s_read_save[s_reads_told]);
        s_reads_told++;
    }
    game_mark();
    InterlockedIncrement(&s_saves);
    release_held_arena(0);
    release_held_xapi(0);
    s = &s_slot[slot];
    s->at = s_saves;
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

static void forget_frees_since(LONG at);

/* Whether the words a mixer tick was seen to change stay live through loads (s_mixmask). Off since build 55:
 * in every local run since 506b39f a clean tick changed no word of the game's heap (the sound side lives in
 * the arena), so what the mask held on players' PCs was other writers' words, the game's own objects left
 * half restored by every later load. All three build-54 rollback crashes sit on them: F9 e7d57d crashed in
 * sub_00117410 with edi = 8172D208, the first of the 7 words "the mixer writes 8172D000 (bytes 8172D208..)"
 * kept live in that fight (ecx = BF800000, the float -1.0 of a08559 and 99b1d9); 2df2cd's draw command and
 * loaded object, ba0793's particle pages lay on "page with mixer words" before the billboard writer
 * (sub_00115CB0) ran to 84000000. The ticks are still found and logged; a load now puts those words back
 * with the rest. RECOMP_ROLLBACK_MIXMASK=keep: the rule before build 55. */
static int mixmask_keep(void)
{
    static int keep = -1;
    if (keep < 0) keep = getenv("RECOMP_ROLLBACK_MIXMASK") && !strcmp(getenv("RECOMP_ROLLBACK_MIXMASK"), "keep");
    return keep;
}

/* A heap page back as it was (with RECOMP_ROLLBACK_MIXMASK=keep: but for the words the mixer writes). */
static void load_page(uint32_t p, const uint8_t *saved)
{
    uint32_t *now = (uint32_t *)XBOX_PTR(HEAP_LO + p * PAGE), live[PAGE / 4];
    const uint8_t *m = s_mixmask[p];
    unsigned w;
    if (!m || !mixmask_keep()) { memcpy(now, saved, PAGE); return; }
    memcpy(live, now, PAGE);
    memcpy(now, saved, PAGE);
    for (w = 0; w < PAGE / 4; w++)
        if (m[w >> 3] & (1u << (w & 7))) now[w] = live[w];
}

int rollback_load(int slot)
{
    Slot *s;
    unsigned i;
    size_t at;
    int k;
    if (!s_ready || slot < 0 || slot >= ROLLBACK_SLOTS || !s_slot[slot].valid) return -1;
    s = &s_slot[slot];
    if (s_saves - s->at > ROLLBACK_HOLD_SAVES) {   /* frees held since it may have been given back */
        static unsigned told;
        if (told++ < 10) fprintf(stderr, "[ROLLBACK] save %d is %ld saves old: not loaded\n", slot, (long)(s_saves - s->at));
        return -1;
    }
    if (s_lp_phase) {   /* tests: which save the carrier comparison loads, and whether it is still the same */
        uint32_t h = 2166136261u;
        size_t b;
        for (b = 0; b < s_dbytes; b += 64) h = (h ^ s->data[b]) * 16777619u;
        s_lp_slot[s_lp_phase] = slot;
        s_lp_hash[s_lp_phase] = h ^ s->npages;
        s_lp_at[s_lp_phase] = s->at;
    }
    game_mark();
    forget_frees_since(s->at);
    for (k = 0, at = 0; k < s_ndseg; at += s_dseg[k].hi - s_dseg[k].lo, k++)
        memcpy(XBOX_PTR(s_dseg[k].lo), s->data + at, s_dseg[k].hi - s_dseg[k].lo);
    for (i = 0; i < s->npages; i++)
        if (!s_skip[s_change[i]])
            load_page(s_change[i], s->heap + (size_t)i * PAGE);
    for (; i < s_nchange; i++)      /* found to change after this save: as it was at the reference */
        if (!s_skip[s_change[i]])
            load_page(s_change[i], s_ref + (size_t)s_change[i] * PAGE);
    game_mark();
    return 0;
}

/* Tests (RECOMP_ROLLBACK_SOAK_DIFF=1, with RECOMP_ROLLBACK_SOAK): the whole of .data and the heap before
 * the soak goes back, compared with what the re-run made of them. A re-run on the same PC must come out
 * exactly as the first run did; a word that differs in memory the saves keep is state a load did not put
 * back, or a step that read something outside the saves (B5 of rollback-plan-2026-11.md). Memory the
 * saves leave alone (other threads', the sound engine's) is listed too, marked as such. */
#define SD_DATA_LO 0x002D5000u
#define SD_DATA_HI 0x003CA000u
static uint8_t *s_sd_data, *s_sd_heap;
static unsigned s_sd_soaks, s_sd_bad, s_sd_told;
static struct { uint32_t key; unsigned n; } s_sd_seen[4096];
static unsigned s_sd_nseen;

int rollback_soak_diff_on(void)
{
    static int on = -1;
    if (on < 0) on = getenv("RECOMP_ROLLBACK_SOAK_DIFF") != NULL;
    return on;
}

void rollback_soak_before(void)
{
    if (!s_ready) return;
    if (!s_sd_data) s_sd_data = (uint8_t *)malloc(SD_DATA_HI - SD_DATA_LO);
    if (!s_sd_heap) s_sd_heap = (uint8_t *)malloc((size_t)HEAP_PAGES * PAGE);
    if (!s_sd_data || !s_sd_heap) return;
    memcpy(s_sd_data, XBOX_PTR(SD_DATA_LO), SD_DATA_HI - SD_DATA_LO);
    memcpy(s_sd_heap, XBOX_PTR(HEAP_LO), (size_t)HEAP_PAGES * PAGE);
}

/* how the saves treat a .data address */
static const char *data_kind(uint32_t a)
{
    unsigned j;
    int k;
    for (j = 0; j < N_DATA_HOLE; j++) if (a >= k_data_hole[j].lo && a < k_data_hole[j].hi) return "hole (kept live)";
    for (k = 0; k < s_ndseg; k++) if (a >= s_dseg[k].lo && a < s_dseg[k].hi) return "saved";
    for (j = 0; j < N_DATA_SKIP; j++) if (a >= k_data_skip[j].lo && a < k_data_skip[j].hi) return "skipped page (sound)";
    for (j = 0; j < N_DATA; j++) if (a >= k_data[j].lo && a < k_data[j].hi) return "page other threads wrote";
    return "gap (never saved)";
}

/* 1 the first time this key is seen */
static int sd_new(uint32_t key)
{
    unsigned i;
    for (i = 0; i < s_sd_nseen; i++) if (s_sd_seen[i].key == key) { s_sd_seen[i].n++; return 0; }
    if (s_sd_nseen < sizeof(s_sd_seen) / sizeof(s_sd_seen[0])) { s_sd_seen[s_sd_nseen].key = key; s_sd_seen[s_sd_nseen++].n = 1; }
    return 1;
}

void rollback_soak_after(int depth, long long step, int verbose)
{
    static unsigned vtold;
    uint32_t a, p;
    unsigned saved_words = 0, other_words = 0, saved_pages = 0, other_pages = 0;
    if (!s_ready || !s_sd_data || !s_sd_heap) return;
    s_sd_soaks++;
    for (a = SD_DATA_LO; a < SD_DATA_HI; a += 4) {
        uint32_t lo, was, now;
        const char *kind;
        if (*(uint32_t *)(s_sd_data + a - SD_DATA_LO) == MEM32(a)) continue;
        lo = a;
        was = *(uint32_t *)(s_sd_data + a - SD_DATA_LO);
        now = MEM32(a);
        while (a + 4 < SD_DATA_HI && *(uint32_t *)(s_sd_data + a + 4 - SD_DATA_LO) != MEM32(a + 4)) a += 4;
        kind = data_kind(lo);
        if (kind[0] == 's' && kind[1] == 'a') saved_words += (a + 4 - lo) / 4; else other_words += (a + 4 - lo) / 4;
        if ((sd_new(lo) && s_sd_told++ < 300) || (verbose && kind[0] == 's' && kind[1] == 'a' && vtold++ < 800)) {
            fprintf(stderr, "[SOAKDIFF] soak %u (back %d, step %lld): .data %08X..%08X differs after the re-run (%s): "
                    "%08X, was %08X%s\n", s_sd_soaks, depth, step, lo, a + 4, kind, now, was, verbose ? " [steps differ]" : "");
        }
    }
    for (p = 0; p < HEAP_PAGES; p++) {
        const uint32_t *was = (const uint32_t *)(s_sd_heap + (size_t)p * PAGE);
        const uint32_t *now = (const uint32_t *)XBOX_PTR(HEAP_LO + p * PAGE);
        unsigned w, first = PAGE, last = 0, n = 0;
        const char *kind;
        if (!memcmp(was, now, PAGE)) continue;
        for (w = 0; w < PAGE / 4; w++)
            if (was[w] != now[w]) { if (first == PAGE) first = w * 4; last = w * 4; n++; }
        kind = rollback_page_state(HEAP_LO + p * PAGE);
        if (s_mixmask[p]) {     /* only the mixer's words? */
            unsigned m = 0;
            for (w = 0; w < PAGE / 4; w++) if (was[w] != now[w] && !(s_mixmask[p][w >> 3] & (1u << (w & 7)))) m++;
            if (!m) continue;
        }
        if (!s_skip[p]) saved_pages++; else other_pages++;
        if ((sd_new(HEAP_LO + p * PAGE) && s_sd_told++ < 300) || (verbose && !s_skip[p] && vtold++ < 800)) {
            fprintf(stderr, "[SOAKDIFF] soak %u (back %d, step %lld): heap page %08X differs after the re-run (%s): %u words, "
                    "%08X..%08X, first %08X was %08X%s\n", s_sd_soaks, depth, step, HEAP_LO + p * PAGE, kind, n,
                    HEAP_LO + p * PAGE + first, HEAP_LO + p * PAGE + last + 4, now[first / 4], was[first / 4],
                    verbose ? " [steps differ]" : "");
            if (!s_skip[p]) alloclog_page(HEAP_LO + p * PAGE + (first & ~0xFu), "soak diff");
        }
    }
    if (saved_words || saved_pages) s_sd_bad++;
    if (s_sd_soaks % 100 == 0)
        fprintf(stderr, "[SOAKDIFF] %u soaks, %u with saved memory that came out otherwise (this one: %u .data words, %u heap "
                "pages saved; %u words, %u pages left alone)\n", s_sd_soaks, s_sd_bad, saved_words, saved_pages,
                other_words, other_pages);
}

/* The D3D device holds a reference on each texture and palette it has bound (SetTexture sub_00215F60,
 * SetPalette sub_00216140: the old one's Common word -0x80000, the new one's +0x80000, bits 19-22
 * the internal count). The device's bindings ([0x234768] +0xF88 textures, +0xF98 palettes) are not
 * rewound by a load, the objects' Common words in the heap are: a load back to a save taken while
 * the device held another palette left the one it holds now one reference short, and the next
 * SetPalette took that count below zero (a two-copy test: 628 device bindings without their reference
 * in 500 loads, a palette at Common FFFB0001). Common then reads 0xFFF8xxxx, and SetPalette sends
 * Data | Common >> 28 to the palette register, so its low bits became "32 entries" (and context 3):
 * the vignette (interior index 125, drawn DST_COLOR x ZERO) was expanded through 32 entries and
 * multiplied the whole fight scene by 0 (F9 reports of build 26, rollback on; their palette registers
 * end in F only in rollback matches; docs/research/stuck-screen-fade.md 9). Textures go the same way
 * (a count that reaches zero early frees one still in use, sub_0021DEE0). So at every update of a
 * rollback match nothing stays bound: every save and every load then sees the device without references, the counts agree with any
 * state a load puts back, and the next draw binds again (EAGL's copies of the bindings,
 * 0x3A5030[4] and 0x3A5050[4], are cleared with them so that it does). */
extern void sub_00215F60(void);     /* D3DDevice_SetTexture(stage, texture), stdcall */
extern void sub_00216140(void);     /* D3DDevice_SetPalette(stage, palette), stdcall */
#define D3D_DEVICE      0x00234768u
#define EAGL_TEXTURES   0x003A5030u
#define EAGL_PALETTES   0x003A5050u

static void d3d_call2(void (*fn)(void), uint32_t a, uint32_t b)
{
    uint32_t sp = g_esp;
    g_esp = (sp - 0x80u) & ~3u;         /* below the hook's frame */
    PUSH32(g_esp, b);
    PUSH32(g_esp, a);
    PUSH32(g_esp, 0u);                  /* a return address the lifted ret only pops */
    fn();
    g_esp = sp;
}

/* How many textures and palettes the device holds now (a save should see none). */
int rollback_device_bindings(void)
{
    uint32_t dev = MEM32(D3D_DEVICE), s;
    int n = 0;
    if (!dev) return 0;
    for (s = 0; s < 4; s++) n += (MEM32(dev + 0xF88u + 4u * s) != 0) + (MEM32(dev + 0xF98u + 4u * s) != 0);
    return n;
}

void rollback_unbind_textures(void)
{
    uint32_t dev = MEM32(D3D_DEVICE), s;
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, ebx = g_ebx, esi = g_esi, edi = g_edi, ebp = g_ebp;
    if (!dev) return;
    for (s = 0; s < 4; s++) {   /* a bound object whose internal count is 0: one reference short, about to wrap */
        uint32_t o[2], k;
        o[0] = MEM32(dev + 0xF88u + 4u * s);
        o[1] = MEM32(dev + 0xF98u + 4u * s);
        for (k = 0; k < 2; k++)
            if (o[k] && !((MEM32(o[k]) >> 19) & 0xFu)) {
                static unsigned told;
                if (told++ < 8)
                    fprintf(stderr, "[ROLLBACK] the device holds %s %08X on stage %u without its reference (Common %08X, "
                            "%ld saves in): its count wraps at this release\n", k ? "palette" : "texture", o[k], s,
                            MEM32(o[k]), (long)s_saves);
            }
    }
    for (s = 0; s < 4; s++) {
        if (MEM32(dev + 0xF88u + 4u * s)) d3d_call2(sub_00215F60, s, 0);
        if (MEM32(dev + 0xF98u + 4u * s)) d3d_call2(sub_00216140, s, 0);
        MEM32(EAGL_TEXTURES + 4u * s) = 0;
        MEM32(EAGL_PALETTES + 4u * s) = 0;
    }
    g_eax = eax; g_ecx = ecx; g_edx = edx; g_ebx = ebx; g_esi = esi; g_edi = edi; g_ebp = ebp;
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

extern int xbox_ContiguousFreeNow(uint32_t va);
/* RECOMP_ROLLBACK_ARENA_TRIM=off (tests): large blocks share the pieces and no piece is given back, as before */
static int arena_trim_on(void)
{
    static int on = -1;
    if (on < 0) on = !(getenv("RECOMP_ROLLBACK_ARENA_TRIM") && !strcmp(getenv("RECOMP_ROLLBACK_ARENA_TRIM"), "off"));
    return on;
}

/* A new piece of contiguous memory for the arena, as a free block in address order (the lock held); own: exactly
 * the size asked, for one large block (ARENA_BIG). */
extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
static uint32_t arena_grow(uint32_t need, int own)
{
    uint32_t size = own ? (need + 0xFFFu) & ~0xFFFu : need > ARENA_GROW ? (need + 0xFFFFu) & ~0xFFFFu : ARENA_GROW, base;
    int i, k;
    if (s_nchunk && !own) {     /* why it grows: the request against the free blocks it has (fragmentation, B2) */
        uint32_t nfree = 0, free_kb = 0, largest = 0, nused = 0, zombies = 0;
        for (i = 0; i < s_nblk; i++) {
            if (!s_blk[i].used) { nfree++; free_kb += s_blk[i].size; if (s_blk[i].size > largest) largest = s_blk[i].size; }
            else { nused++; if (s_blk[i].freed) zombies++; }
        }
        fprintf(stderr, "[ROLLBACK] the arena has no free block of %u KB (%s): %u KB free in %u blocks, the largest %u KB; "
                "%u blocks used (%u freed and held or kept)\n", (need + 1023u) / 1024u, s_tracking ? "in a fight" : "outside a fight",
                free_kb / 1024u, nfree, largest / 1024u, nused, zombies);
    }
    for (k = 1; k < s_nchunk && s_chunk_hi[k] != s_chunk_lo[k]; k++) {}     /* a slot given back, or a new one */
    if (!s_nchunk) k = 0;
    if (k >= ARENA_CHUNKS || s_nblk >= ARENA_BLOCKS || s_arena_total + size > ARENA_MAX) {
        fprintf(stderr, "[ROLLBACK] the audio arena cannot grow: %d pieces, %d blocks, %u KB of %u\n", s_nchunk,
                s_nblk, s_arena_total / 1024, ARENA_MAX / 1024);
        return 0;
    }
    base = xbox_ContiguousAlloc(size, 4096);
    if (!base) {
        fprintf(stderr, "[ROLLBACK] the audio arena cannot grow: no %u KB of contiguous memory\n", size / 1024);
        return 0;
    }
    s_chunk_lo[k] = base;
    s_chunk_hi[k] = base + size;
    if (k == s_nchunk) s_nchunk++;
    s_arena_total += size;
    for (i = 0; i < s_nblk && s_blk[i].addr < base; i++) {}
    memmove(&s_blk[i + 1], &s_blk[i], (size_t)(s_nblk - i) * sizeof(Block));
    s_blk[i].addr = base;
    s_blk[i].size = size;
    s_blk[i].user = 0;
    s_blk[i].used = 0;
    s_nblk++;
    if (own) {
        static unsigned told;
        if (told++ < 40)
            fprintf(stderr, "[ROLLBACK] the audio arena takes %u KB at %08X for one block (%u KB in all)\n", size / 1024,
                    base, s_arena_total / 1024);
    } else {
        fprintf(stderr, "[ROLLBACK] the audio arena grows by %u KB at %08X (%u KB in all)\n", size / 1024, base,
                s_arena_total / 1024);
    }
    return base;
}

/* Gives a piece back to contiguous memory once it is one free block, outside a fight only (in a fight a free is
 * held until no save can reach it, and the contiguous allocator's frees are held too). The first piece stays.
 * The lock held; i: the free block. Its slot stays, empty (lo == hi), so in_arena never shifts under a reader. */
static unsigned s_trims;
static void arena_trim(int i)
{
    int k;
    uint32_t base = s_blk[i].addr, size = s_blk[i].size;
    if (s_tracking || s_blk[i].used || !arena_trim_on()) return;
    for (k = 1; k < s_nchunk; k++)
        if (s_chunk_lo[k] == base && s_chunk_hi[k] == base + size) break;
    if (k >= s_nchunk) return;
    memmove(&s_blk[i], &s_blk[i + 1], (size_t)(s_nblk - i - 1) * sizeof(Block));
    s_nblk--;
    s_chunk_hi[k] = s_chunk_lo[k];
    s_arena_total -= size;
    xbox_ContiguousFreeNow(base);
    if (s_trims++ < 40)
        fprintf(stderr, "[ROLLBACK] the audio arena gives %u KB at %08X back (%u KB in all)\n", size / 1024, base,
                s_arena_total / 1024);
}

/* Every arena allocation and free with its thread and call chain, so a crash on an arena block can say
 * who gave it back (the file layer's crash at the second match's start, 2026-10-24: a stream handle in
 * the arena read junk). A alloc, F given back, H held, R a held one given back, D a held one kept. */
#define ARENA_EVENTS 65536
typedef struct { uint32_t user, size, chain[3]; LONG saves; DWORD tid; char op, tracking; } ArenaEvent;
static ArenaEvent *s_aev;
static volatile LONG s_aev_n;
static void arena_event(char op, uint32_t user, uint32_t size)
{
    ArenaEvent *e;
    if (!s_aev && !(s_aev = (ArenaEvent *)calloc(ARENA_EVENTS, sizeof(ArenaEvent)))) return;
    e = &s_aev[(unsigned)(InterlockedIncrement(&s_aev_n) - 1) % ARENA_EVENTS];
    e->user = user;
    e->size = size;
    e->op = op;
    e->tracking = (char)s_tracking;
    e->saves = s_saves;
    e->tid = GetCurrentThreadId();
    call_chain(g_esp, e->chain, 3);
}

static uint32_t arena_block_size(uint32_t user)
{
    int i;
    for (i = 0; i < s_nblk; i++)
        if (s_blk[i].user == user) return s_blk[i].size;
    return 0;
}

/* A sound command (cmd, n, n words) naming an engine object the engine has already given back: a
 * load brought back game state that still holds the object's handle, while the engine (never saved
 * or loaded) had freed it. Run, such commands made the engine walk its own freed objects (a copy's
 * game thread looping in sub_0012BE20 at step 1430, five-match run 2026-10-25); they are silenced. */
int rollback_sound_command_dead(uint32_t sp)
{
    static unsigned told;
    uint32_t cmd = MEM32(sp + 4u), n = MEM32(sp + 8u), k;
    int dead = 0;
    if (!s_tracking || !s_arena || n > 16u) return 0;
    EnterCriticalSection(&s_arena_cs);
    for (k = 0; k < n && !dead; k++) {
        uint32_t v = MEM32(sp + 12u + k * 4u);
        int lo = 0, hi = s_nblk - 1;
        if (!in_arena(v)) continue;
        while (lo <= hi) {          /* the blocks are in address order */
            int mid = (lo + hi) / 2;
            if (v < s_blk[mid].addr) hi = mid - 1;
            else if (v >= s_blk[mid].addr + s_blk[mid].size) lo = mid + 1;
            else { dead = s_blk[mid].used && s_blk[mid].freed == 1; break; }   /* not one a load took back */
        }
    }
    LeaveCriticalSection(&s_arena_cs);
    if (dead && told++ < 40)
        fprintf(stderr, "[ROLLBACK] a sound command (%08X, %u words) names an engine object already given back: "
                "silenced (%ld saves in)\n", cmd, n, (long)s_saves);
    return dead;
}

/* The crash report: the arena blocks the guest registers (and the words they point at) fall in, with
 * their last events. */
/* The arena block v lies in and its last events; 0 if no event names one. */
static uint32_t arena_block_history(uint32_t v)
{
    uint32_t user = 0;
    LONG n = s_aev_n, k;
    int shown = 0;
    if (!s_aev || !in_arena(v)) return 0;
    for (k = n - 1; k >= 0 && k >= n - ARENA_EVENTS && !user; k--) {
        const ArenaEvent *e = &s_aev[(unsigned)k % ARENA_EVENTS];
        if (v >= e->user - 16u && v < e->user + e->size) user = e->user;
    }
    if (!user) return 0;
    fprintf(stderr, "[ROLLBACK] %08X is in arena block %08X (%s), its last events:\n", v, user,
            arena_block_size(user) ? "allocated" : "free");
    for (k = n - 1; k >= 0 && k >= n - ARENA_EVENTS && shown < 10; k--) {
        const ArenaEvent *e = &s_aev[(unsigned)k % ARENA_EVENTS];
        if (e->user != user) continue;
        fprintf(stderr, "[ROLLBACK]   %c size %X, thread %lu (%s), %s, save %ld, from %08X <- %08X <- %08X (event %ld of %ld)\n",
                e->op, e->size, (unsigned long)e->tid,
                e->tid == s_game_tid ? "game" : e->tid == s_mixer_tid ? "mixer" : "other",
                e->tracking ? "in a fight" : "outside fights", (long)e->saves, e->chain[0], e->chain[1], e->chain[2],
                (long)k, (long)n);
        shown++;
    }
    return user;
}

void rollback_arena_history(void)
{
    uint32_t cand[8 * 9 + 4], done[16];
    unsigned nc = 0, nd = 0, i, j;
    /* and the sound engine's bank table [0x3AD928]: sub_001317C0 crashed three times reading a bank's cue
     * table (+0x34 data, +0x4C index) through junk (2026-10-25) */
    const uint32_t regs[8] = { g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_esp, MEM32(0x003AD928u) };
    if (!s_aev || !s_arena) return;
    {
        uint32_t bank = MEM32(0x003AD928u);
        if (in_arena(bank) || (bank >= HEAP_LO && bank < HEAP_HI)) {
            fprintf(stderr, "[ROLLBACK] the sound bank table %08X: +34 %08X, +4C %08X\n", bank, MEM32(bank + 0x34u),
                    MEM32(bank + 0x4Cu));
            cand[nc++] = MEM32(bank + 0x34u);
            cand[nc++] = MEM32(bank + 0x4Cu);
        }
    }
    for (i = 0; i < 8; i++) {
        uint32_t r = regs[i];
        cand[nc++] = r;
        if (in_arena(r) || (r >= HEAP_LO && r < HEAP_HI) || (r >= 0x002D5000u && r < 0x003CA000u))
            for (j = 0; j < 8; j++) cand[nc++] = MEM32((r & ~3u) + j * 4u);
    }
    for (i = 0; i < nc && nd < 16; i++) {
        uint32_t user;
        for (j = 0; j < nd && !(cand[i] >= done[j] - 16u && cand[i] < done[j] + arena_block_size(done[j])); j++) {}
        if (j < nd) continue;
        if ((user = arena_block_history(cand[i])) != 0) done[nd++] = user;
    }
}

static uint32_t arena_alloc(uint32_t want)
{
    int i, grown = 0;
    uint32_t only = 0, a = 0, size = (want + ARENA_ALIGN + 16u + 15u) & ~15u;
    if (s_arena_state != 1 || want > ARENA_MAX / 2) return 0;
    EnterCriticalSection(&s_arena_cs);
    if (want >= ARENA_BIG && !s_tracking && arena_trim_on() && (only = arena_grow(size, 1)) != 0) grown = 1;
again:
    for (i = 0; i < s_nblk; i++) {
        if (s_blk[i].used || s_blk[i].size < size) continue;
        if (only && s_blk[i].addr != only) continue;    /* the piece of its own, all of it */
        if (only) size = s_blk[i].size;
        if (s_blk[i].size > size && s_nblk < ARENA_BLOCKS) {    /* split: the rest stays free */
            memmove(&s_blk[i + 2], &s_blk[i + 1], (size_t)(s_nblk - i - 1) * sizeof(Block));
            s_blk[i + 1].addr = s_blk[i].addr + size;
            s_blk[i + 1].size = s_blk[i].size - size;
            s_blk[i + 1].used = 0;
            s_blk[i].size = size;
            s_nblk++;
        }
        s_blk[i].used = 1;
        s_blk[i].freed = 0;
        s_blk[i].fight = s_tracking ? 1 : 0;
        s_blk[i].game = s_tracking && GetCurrentThreadId() == s_game_tid && !t_in_sound;
        s_blk[i].at = s_saves;
        a = (s_blk[i].addr + 16u + ARENA_ALIGN - 1u) & ~(ARENA_ALIGN - 1u);
        s_blk[i].user = a;
        arena_event('A', a, want);
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
    if (!a && !grown && !s_tracking && s_kept_rescan) {     /* the last fight's kept blocks first (rollback_reset) */
        s_kept_rescan = 0;
        release_kept_arena();
        goto again;
    }
    if (!a && !grown && arena_grow(size, 0)) { grown = 1; goto again; }
    LeaveCriticalSection(&s_arena_cs);
    return a;
}

static int arena_free_now(uint32_t a)
{
    int i;
    if (!s_arena || !in_arena(a)) return 0;
    EnterCriticalSection(&s_arena_cs);
    for (i = 0; i < s_nblk; i++) {
        if (s_blk[i].user != a || !s_blk[i].used) continue;
        arena_event('F', a, s_blk[i].size);
        s_blk[i].used = 0;
        s_arena_live -= s_blk[i].size;
        /* Merged only with a neighbour that touches it: the arena is pieces of contiguous memory with
         * other owners between them. Merged across a gap, a free block took in what lay between two
         * pieces, DirectSound's voice memory among it, and the engine's next object went over it:
         * the voice list read junk handles (0xEFFD) and the game crashed in DirectSound
         * (sub_0025F509) and the host's voice processor (two-copy rollback tests, 2026-10-24; the
         * players' sound crashes in builds 19 and 20 had the same shape). */
        if (i + 1 < s_nblk && !s_blk[i + 1].used &&
            s_blk[i].addr + s_blk[i].size == s_blk[i + 1].addr && !piece_start(s_blk[i + 1].addr)) {   /* merge with the next */
            s_blk[i].size += s_blk[i + 1].size;
            memmove(&s_blk[i + 1], &s_blk[i + 2], (size_t)(s_nblk - i - 2) * sizeof(Block));
            s_nblk--;
        }
        if (i > 0 && !s_blk[i - 1].used &&
            s_blk[i - 1].addr + s_blk[i - 1].size == s_blk[i].addr && !piece_start(s_blk[i].addr)) {   /* and with the previous */
            s_blk[i - 1].size += s_blk[i].size;
            memmove(&s_blk[i], &s_blk[i + 1], (size_t)(s_nblk - i - 1) * sizeof(Block));
            s_nblk--;
            i--;
        }
        arena_trim(i);      /* a piece now wholly free goes back to contiguous memory (outside a fight) */
        break;
    }
    LeaveCriticalSection(&s_arena_cs);
    return 1;
}

/* Arena frees in a fight are held, as the contiguous ones are: the arena is not part of the save
 * states, so a block given back after a save and handed to the sound engine again was still the
 * game's after a load back to that save, and the game wrote into the engine's new object (a
 * player's crash in sub_0012BE20, build 19: engine objects with junk vtables, 124 rollbacks in).
 * A held block stays allocated until no save older than its free is left (ROLLBACK_SLOTS saves
 * later); a block freed again meanwhile (the game frees it once more after a load) just moves
 * its time on. */
typedef struct { uint32_t addr; LONG at; int game; } HeldArena;    /* game: freed on the game thread */
static HeldArena *s_held_arena;         /* grows as needed (was 4096; past that, freed at once) */
static unsigned s_nheld_arena, s_held_arena_cap;

static int arena_free(uint32_t a)
{
    unsigned k;
    int i, live = 0;
    if (!s_arena || !in_arena(a)) return 0;
    if (!s_tracking) return arena_free_now(a);
    EnterCriticalSection(&s_arena_cs);
    for (i = 0; i < s_nblk && !live; i++)
        if (s_blk[i].user == a && s_blk[i].used) { live = 1; s_blk[i].freed = 1; }
    if (live) {
        for (k = 0; k < s_nheld_arena && s_held_arena[k].addr != a; k++) {}
        if (k == s_nheld_arena && s_nheld_arena == s_held_arena_cap) {
            unsigned cap = s_held_arena_cap ? s_held_arena_cap * 2 : 4096;
            HeldArena *h = (HeldArena *)realloc(s_held_arena, cap * sizeof(HeldArena));
            if (h) { s_held_arena = h; s_held_arena_cap = cap; }
        }
        if (k == s_nheld_arena && s_nheld_arena < s_held_arena_cap) s_nheld_arena++;
        if (k < s_nheld_arena) {
            s_held_arena[k].addr = a;
            s_held_arena[k].at = s_saves;
            s_held_arena[k].game = GetCurrentThreadId() == s_game_tid;
            arena_event('H', a, 0);
        }
        else live = 0;      /* the list is full: give it back now */
    }
    LeaveCriticalSection(&s_arena_cs);
    return live ? 1 : arena_free_now(a);
}

/* give back the held blocks no save can reach any more (all = 1: every one, the fight is over) */
static void release_held_arena(int all)
{
    unsigned k = 0;
    if (!s_arena) return;
    EnterCriticalSection(&s_arena_cs);
    if (all) {      /* the fight is over: what the arena holds, and how much of it the engine gave back */
        int i;
        unsigned zombies = 0, zbytes = 0, nfree = 0, fbytes = 0, largest = 0, nfight = 0, fightb = 0, nused = 0;
        int pieces = 0;
        for (i = 0; i < s_nchunk; i++) pieces += s_chunk_hi[i] != s_chunk_lo[i];
        for (i = 0; i < s_nblk; i++) {
            if (s_blk[i].used && s_blk[i].freed) { zombies++; zbytes += s_blk[i].size; }
            if (s_blk[i].used) nused++;
            if (s_blk[i].used && !s_blk[i].freed && s_blk[i].fight) { nfight++; fightb += s_blk[i].size; }
            if (!s_blk[i].used) { nfree++; fbytes += s_blk[i].size; if (s_blk[i].size > largest) largest = s_blk[i].size; }
        }
        fprintf(stderr, "[ROLLBACK] the audio arena at the fight's end: %u KB live, %u held frees, %u blocks (%u KB) "
                "freed before a load and kept\n", s_arena_live / 1024, s_nheld_arena, zombies - s_nheld_arena, zbytes / 1024);
        /* the numbers a long rematch chain must keep flat */
        fprintf(stderr, "[ROLLBACK] arena: %u KB in %d pieces, %u blocks used (%u allocated in this fight still live, "
                "%u KB), %u KB free in %u blocks, the largest %u KB\n", s_arena_total / 1024, pieces, nused, nfight,
                fightb / 1024, fbytes / 1024, nfree, largest / 1024);
    }
    while (k < s_nheld_arena) {
        if (all || s_saves - s_held_arena[k].at > ROLLBACK_HOLD_SAVES) {
            uint32_t a = s_held_arena[k].addr;
            s_held_arena[k] = s_held_arena[--s_nheld_arena];
            arena_event('R', a, 0);
            LeaveCriticalSection(&s_arena_cs);
            arena_free_now(a);
            EnterCriticalSection(&s_arena_cs);
        } else {
            k++;
        }
    }
    LeaveCriticalSection(&s_arena_cs);
}

/* XAPI heap frees on the game thread in a fight (src/recomp_manual.c, sub_001FBE1E): held like the arena's,
 * forgotten by a load that goes back before them, given back on the game thread once no save older than
 * the free is left, and all at the fight's end. */
extern void defjam_xapi_free_now(uint32_t ptr);
typedef struct { uint32_t ptr; LONG at; } HeldXapi;
static HeldXapi *s_held_xapi;           /* grows as needed (was 4096; past that, freed at once) */
static unsigned s_nheld_xapi, s_held_xapi_cap;

int rollback_hold_xapi_free(uint32_t ptr)
{
    unsigned k;
    if (!s_tracking || !ptr || GetCurrentThreadId() != s_game_tid) return 0;
    for (k = 0; k < s_nheld_xapi && s_held_xapi[k].ptr != ptr; k++) {}
    if (k == s_nheld_xapi) {
        if (s_nheld_xapi == s_held_xapi_cap) {
            unsigned cap = s_held_xapi_cap ? s_held_xapi_cap * 2 : 4096;
            HeldXapi *h = (HeldXapi *)realloc(s_held_xapi, cap * sizeof(HeldXapi));
            if (!h) return 0;
            s_held_xapi = h;
            s_held_xapi_cap = cap;
        }
        s_nheld_xapi++;
    }
    s_held_xapi[k].ptr = ptr;
    s_held_xapi[k].at = s_saves;
    return 1;
}

static void release_held_xapi(int all)
{
    unsigned k = 0, n = 0;
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx;
    if (GetCurrentThreadId() != s_game_tid) return;
    while (k < s_nheld_xapi) {
        if (all || s_saves - s_held_xapi[k].at > ROLLBACK_HOLD_SAVES) {
            uint32_t ptr = s_held_xapi[k].ptr;
            s_held_xapi[k] = s_held_xapi[--s_nheld_xapi];
            defjam_xapi_free_now(ptr);
            n++;
        } else {
            k++;
        }
    }
    g_eax = eax; g_ecx = ecx; g_edx = edx;
    if (all && n)
        fprintf(stderr, "[ROLLBACK] %u XAPI heap frees held through the fight given back\n", n);
}

/* A load goes back to before the frees held since that save: the state it brings back still uses those
 * blocks. They stay allocated (the re-run frees them again if it does), instead of being handed out at the
 * fight's end while the game holds them (the crash at the first KO of a five-match run). */
static void forget_frees_since(LONG at)
{
    static unsigned told;
    unsigned i = 0, contig = 0, arena = 0, taken = 0;
    static unsigned taken_told;
    if (s_held_cs_ready) {
        EnterCriticalSection(&s_held_cs);
        while (i < s_nheld) {
            if (s_held_free_at[i] >= at) {
                s_nheld--;
                s_held_free[i] = s_held_free[s_nheld];
                s_held_free_at[i] = s_held_free_at[s_nheld];
                contig++;
            } else {
                i++;
            }
        }
        LeaveCriticalSection(&s_held_cs);
    }
    if (s_arena) {
        EnterCriticalSection(&s_arena_cs);
        i = 0;
        while (i < s_nheld_arena) {
            /* the mixer's frees too: game state a load brings back can still hold a handle to the
             * object (a sound that ended after the save). Given back in the fight, such a block went to a
             * new owner and the game's next command for the old handle broke it (sub_001317C0 as the
             * second match loaded, 2026-10-25). Kept, they are given back when the next fight starts. */
            if (s_held_arena[i].at >= at) {
                /* freed on the game thread: the state the load brings back uses the block, so it is alive
                 * again (the music stream's handle, used on into the next match: freeing such blocks at the
                 * next fight crashed the mixer in sub_001E8340, 2026-10-25); the mixer's: the engine is done
                 * with it, only a stale handle in the game can name it */
                if (s_held_arena[i].game) {
                    int b;
                    for (b = 0; b < s_nblk; b++)
                        if (s_blk[b].user == s_held_arena[i].addr) { s_blk[b].freed = 0; break; }
                }
                arena_event('D', s_held_arena[i].addr, 0);
                s_held_arena[i] = s_held_arena[--s_nheld_arena];
                arena++;
            } else {
                i++;
            }
        }
        /* The game thread's blocks handed out after that save (outside sound commands, where the engine,
         * which no load touches, keeps what it makes): the state the load brings back never had them, and
         * the re-run asks for new ones. Left as they were, every rollback leaked them; in long two-copy
         * fights at 100+10 ms the fight's live blocks went 187 -> 547 -> 550 -> 735 over four fights and the
         * arena's free room broke into 141-219 blocks of at most 43-62 KB (build 55 tests). They are not
         * given back here: a page no load restores, or the engine, may still name one. Marked, they go
         * with the kept blocks to the conservative scan at the fight's end (release_kept_arena). */
        {
            int b;
            for (b = 0; b < s_nblk; b++)
                if (s_blk[b].used && !s_blk[b].freed && s_blk[b].game && s_blk[b].at >= at && s_blk[b].size < ARENA_BIG) {
                    s_blk[b].freed = 2;
                    taken++;
                }
        }
        LeaveCriticalSection(&s_arena_cs);
    }
    {
        unsigned xapi = 0;
        i = 0;
        while (i < s_nheld_xapi) {
            if (s_held_xapi[i].at >= at) { s_held_xapi[i] = s_held_xapi[--s_nheld_xapi]; xapi++; }
            else i++;
        }
        contig += xapi;     /* counted with the contiguous ones in the line below */
    }
    if ((contig || arena) && told++ < 20)
        fprintf(stderr, "[ROLLBACK] a load went back before %u contiguous and %u audio frees: they stay allocated\n",
                contig, arena);
    if (taken && taken_told++ < 20)
        fprintf(stderr, "[ROLLBACK] a load went back before %u audio blocks the game thread was given: kept for the "
                "fight's-end scan\n", taken);
}

static void arena_start(void)
{
    if (s_arena_state) return;
    if (!rollback_enabled()) { s_arena_state = -1; return; }
    InitializeCriticalSection(&s_arena_cs);
    s_nblk = 0;
    if (!arena_grow(arena_trim_on() ? ARENA_FIRST_SMALL : ARENA_FIRST, 0)) { s_arena_state = -1; fprintf(stderr, "[ROLLBACK] no room for the sound engine's arena\n"); return; }
    s_arena = s_chunk_lo[0];
    s_arena_state = 1;
    fprintf(stderr, "[ROLLBACK] the audio code allocates from its own arena at %08X (%u KB to start, up to %u KB; "
            "pool slot 63 %08X)\n", s_arena, (arena_trim_on() ? ARENA_FIRST_SMALL : ARENA_FIRST) / 1024, ARENA_MAX / 1024, MEM32(0x003C9158u + 63u * 4u));
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
    /* Another thread than the game's (the mixer), or the game thread inside a sound command: the
     * block is the sound engine's whoever called. Through the heap such blocks put the heap's own
     * bookkeeping on pages the mixer writes (0x80003104 in a hub run), which then left the save. */
    /* The file layer's stream buffers go to the arena (below). In the heap (RECOMP_ROLLBACK_STREAMS_IN_HEAP=1)
     * the mixer writes them between the game's objects: whole pages left the saves and the game stalled in
     * match 1 (2026-10-25, morning); with only the mixer's words kept live, the mixer's lists broke in match 1
     * (sub_00139D80 from sub_0011E530, step 2860). Routed, first matches run clean for 2-7 minutes. */
    {
        static int heap = -1;
        if (heap < 0) heap = getenv("RECOMP_ROLLBACK_STREAMS_IN_HEAP") != NULL;
        if (ret == 0x001E3F20u && heap) return 0;
    }
    if (!audio_code(req) && !t_in_sound && !(s_tracking && GetCurrentThreadId() != s_game_tid)) {
        /* The file layer's buffers (allocated at return 0x1E3F20) for a stream the audio code opens:
         * the music stream's buffer is the one block outside the arena the mixer writes (two-copy runs
         * with each copy on one CPU, 2026-10-24). Its caller is generic, so the call chain decides. */
        uint32_t chain[4];
        int k, audio = 0;
        if (ret != 0x001E3F20u) return 0;
        call_chain(g_esp, chain, 4);
        for (k = 0; k < 4; k++) audio = audio || audio_code(chain[k]);
        if (!audio) return 0;
    }
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

/* Live memory for state that follows the device rather than the fight (netplay.c: EAGL's dynamic
 * vertex buffer objects): from the arena, which no load touches. 0 when there is none. */
uint32_t rollback_live_alloc(uint32_t size)
{
    uint32_t a = s_arena_state == 1 ? arena_alloc(size) : 0;
    if (a) memset((void *)XBOX_PTR(a), 0, size);
    return a;
}

int rollback_live_free(uint32_t a)
{
    return s_arena_state == 1 && arena_free(a);
}

extern void sub_0002A180(void);

/* A heap block given back on another thread than the game's in a fight (the mixer, when the music
 * moves to its next track about 2.7 minutes in) writes the heap's own free lists (0x800031A4,
 * 0x80007130..0x80007220): those pages left the save from then on, and the copy stopped drawing
 * a moment later (five-match run, 2026-10-25). Such frees wait for the fight's end and then run
 * on the game thread. */
#define LATE_FREES 2048
static struct { uint32_t self, block; int sound; } s_late[LATE_FREES];
static unsigned s_nlate, s_late_twice;
static volatile LONG s_late_lock;

/* Heap blocks allocated in this fight. A late free of one is dropped, not run: a load back to before
 * its allocation gave its memory back to the heap, which may have handed it out again, and freeing
 * it at the fight's end corrupted the heap's free list (sub_00029DE0, a junk link right after 50 late
 * frees; five-match run, 2026-10-25). Dropped, it stays allocated: a small leak. */
#define FIGHT_ALLOCS 65536u
static uint32_t *s_fight_alloc;
static unsigned s_nfight_alloc;

static void fight_alloc_add(uint32_t block)
{
    unsigned h;
    if (!s_fight_alloc && !(s_fight_alloc = (uint32_t *)calloc(FIGHT_ALLOCS, sizeof(uint32_t)))) return;
    if (s_nfight_alloc >= FIGHT_ALLOCS / 2) return;
    for (h = (block >> 3) * 2654435761u % FIGHT_ALLOCS; s_fight_alloc[h] && s_fight_alloc[h] != block; h = (h + 1) % FIGHT_ALLOCS) {}
    if (!s_fight_alloc[h]) { s_fight_alloc[h] = block; s_nfight_alloc++; }
}

static int fight_alloc_has(uint32_t block)
{
    unsigned h;
    if (!s_fight_alloc) return 0;
    if (s_nfight_alloc >= FIGHT_ALLOCS / 2) return 1;      /* the set overflowed: every block may be one */
    for (h = (block >> 3) * 2654435761u % FIGHT_ALLOCS; s_fight_alloc[h]; h = (h + 1) % FIGHT_ALLOCS)
        if (s_fight_alloc[h] == block) return 1;
    return 0;
}

static int late_free(uint32_t self, uint32_t block, int sound)
{
    int kept = 0;
    if (!s_tracking || GetCurrentThreadId() == s_game_tid || block < HEAP_LO || block >= HEAP_HI) return 0;
    while (InterlockedExchange(&s_late_lock, 1)) Sleep(0);
    {
        unsigned k;
        for (k = 0; k < s_nlate && s_late[k].block != block; k++) {}
        if (k < s_nlate) {      /* given back twice: the second is not run either */
            s_late_twice++;
            InterlockedExchange(&s_late_lock, 0);
            return 1;
        }
    }
    if (s_nlate < LATE_FREES) {
        s_late[s_nlate].self = self;
        s_late[s_nlate].block = block;
        s_late[s_nlate].sound = sound;
        s_nlate++;
        kept = 1;
    }
    InterlockedExchange(&s_late_lock, 0);
    return kept;
}

/* the fight is over: the held frees, on the game thread (thiscall (block), ret 4) */
static void run_late_frees(void)
{
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx;
    unsigned i, n, dropped = 0, twice;
    if (GetCurrentThreadId() != s_game_tid) return;
    while (InterlockedExchange(&s_late_lock, 1)) Sleep(0);
    n = s_nlate;
    twice = s_late_twice;
    s_nlate = s_late_twice = 0;
    InterlockedExchange(&s_late_lock, 0);
    for (i = 0; i < n; i++) dropped += fight_alloc_has(s_late[i].block);
    if (n)
        fprintf(stderr, "[ROLLBACK] %u heap frees from other threads in the fight: %u given back now, %u kept (allocated "
                "in the fight), %u more were repeats\n", n, n - dropped, dropped, twice);
    for (i = 0; i < n; i++) {
        if (fight_alloc_has(s_late[i].block)) continue;
        g_ecx = s_late[i].self;
        g_esp -= 4; MEM32(g_esp) = s_late[i].block;
        g_esp -= 4; MEM32(g_esp) = 0;
        if (s_late[i].sound) sub_0001DC20(); else sub_0002A180();
    }
    g_eax = eax; g_ecx = ecx; g_edx = edx;
    if (s_fight_alloc) memset(s_fight_alloc, 0, FIGHT_ALLOCS * sizeof(uint32_t));
    s_nfight_alloc = 0;
}

/* thiscall (block), ret 4 */
void rollback_heap_free(void)
{
    if (s_arena_state == 1 && arena_free(MEM32(g_esp + 4))) { g_eax = 1; g_esp += 8; return; }
    if (late_free(g_ecx, MEM32(g_esp + 4), 0)) { g_eax = 1; g_esp += 8; return; }
    if (s_live) live_get(MEM32(g_esp + 4), 1, NULL);
    sub_0002A180();
}

/* thiscall (size, alignment, flags), ret 12 */
void rollback_sound_alloc(void)
{
    if (s_arena_state == 1 && g_ecx == MEM32(SND_ALLOCATOR)) {
        uint32_t a = arena_alloc(MEM32(g_esp + 4));
        /* sub_0013A380 adds a node to the list at esi: a head that names the block just handed out
         * is the self-linked node the game thread looped on (five-match run, 2026-10-25) */
        if (a && MEM32(g_esp) == 0x0013A3CDu && g_esi >= 0x002D5000u && g_esi < 0x003CA000u && MEM32(g_esi) == a) {
            static unsigned told;
            if (told++ < 8) {
                fprintf(stderr, "[ROLLBACK] the list at %08X still names arena block %08X, handed out again (%ld saves in)\n",
                        g_esi, a, (long)s_saves);
                arena_block_history(a);
            }
        }
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
    if (late_free(g_ecx, MEM32(g_esp + 4), 1)) { g_esp += 8; return; }
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
#define MIX_TICK_PAGES_MAX 8            /* more pages with new words in one tick: not the mixer's alone (mixer_end) */
#define MIX_TICK_WORDS_MAX 128          /* or more new words: F9 70d296 kept 8 pages, 2128 words (1024 on one page) */
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

/* Did the mixer keep its CPU for the whole tick? Every guest thread shares that one CPU, so while the
 * mixer has it nobody else's guest code runs; when it blocks inside the tick (the file layer's slot lock
 * in sub_001E8080, the engine lock), whoever runs then writes too. The game thread's cycle count catches
 * only the game thread, and only where thread cycles are counted exactly; a file worker filling a heap
 * buffer, or any other guest thread, passed as the mixer. Player logs (F9 8e97cd, 70d296, 0b7e16, build
 * 47-51) show such ticks: whole heap pages "written by the mixer" (1024 words on one page), the words kept
 * live through every later load, and then a particle emitter on those pages half restored, its billboard
 * writer (sub_00115CB0) running through the contiguous window to 84000000 over the file layer's slots at
 * [0x3C8F30], where the mixer read them first (sub_001E55D0). So a tick counts only when the mixer's own
 * cycles cover the tick's time on the clock (the two are the same counter, the TSC); a tick it spent
 * off its CPU, or one the counters cannot vouch for, is left out like a tick the game thread ran in. */
static ULONG64 s_mix_tsc, s_mix_own;
static int s_mix_offcpu = -1;           /* RECOMP_ROLLBACK_MIX_OFFCPU=off: the old rule (A/B tests) */
static DWORD s_mix_yield = (DWORD)-1;   /* RECOMP_ROLLBACK_TEST_MIX_YIELD=<ms>: the mixer sleeps in its tick (tests) */
static struct { unsigned watched, clean, words, game, seq, cpu, off, pages; ULONG64 own, clock; } s_mixst;

static ULONG64 mixer_own_cycles(void)
{
    ULONG64 c = 0;
    QueryThreadCycleTime(GetCurrentThread(), &c);
    return c;
}

/* 1 when the mixer ran for (nearly) the whole of the tick: interrupts on its CPU are not charged to it,
 * so up to an eighth of the tick plus 40000 cycles (10-20 us) may be missing. */
static int mixer_kept_cpu(ULONG64 clock, ULONG64 own)
{
    ULONG64 slack = clock / 8 + 40000u;
    s_mixst.own += own;
    s_mixst.clock += clock;
    return own + slack >= clock && own <= clock + slack;
}

/* The fight's mixer ticks, once at its end (rollback_reset). */
static void mixer_stats(void)
{
    if (!s_mixst.watched) return;
    fprintf(stderr, "[ROLLBACK] mixer ticks this fight: %u watched, %u clean (%u words %s); left out: "
            "game thread ran %u, saved or loaded %u, other CPU %u, the mixer off its CPU %u, too many pages or words %u; "
            "the mixer had its CPU for %.1f%% of the watched time\n", s_mixst.watched, s_mixst.clean, s_mixst.words,
            mixmask_keep() ? "kept live through loads" : "seen, restored by loads", s_mixst.game, s_mixst.seq, s_mixst.cpu, s_mixst.off, s_mixst.pages,
            s_mixst.clock ? 100.0 * (double)s_mixst.own / (double)s_mixst.clock : 0.0);
    memset(&s_mixst, 0, sizeof(s_mixst));
    s_mix_found = 0;
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
    /* before the copy: a game thread that ran during it (this thread preempted) wrote pages the copy
     * already had, and taken after it, its cycles would not tell */
    s_game_cycles = game_cycles();
    s_mix_seq = s_game_seq;
    s_mix_own = mixer_own_cycles();
    s_mix_tsc = __rdtsc();
    for (i = 0; i < n; i++)
        memcpy(s_mix + (size_t)i * PAGE, XBOX_PTR(HEAP_LO + s_change[i] * PAGE), PAGE);
    s_mix_n = n;
    if (s_writers_on < 0) s_writers_on = getenv("RECOMP_ROLLBACK_WRITERS") && s_live ? 1 : 0;
    s_mixer_tid = GetCurrentThreadId();
    if (s_writers_on == 1) {
        DWORD old;
        for (i = 0; i < n; i++) {
            uint32_t q = s_change[i];
            if (s_guard[q]) continue;           /* still guarded by the tracking itself */
            s_mixguard[q] = 1;
            if (!VirtualProtect(page_host(q), PAGE, PAGE_READONLY, &old)) s_mixguard[q] = 0;
        }
    }
    InterlockedExchange(&s_mix_on, 1);
    if (s_mix_yield == (DWORD)-1) {
        const char *y = getenv("RECOMP_ROLLBACK_TEST_MIX_YIELD");
        s_mix_yield = y ? (DWORD)atoi(y) : 0;
        if (s_mix_yield) fprintf(stderr, "[ROLLBACK] tests: the mixer sleeps %lu ms in each tick\n", (unsigned long)s_mix_yield);
    }
    if (s_mix_yield) Sleep(s_mix_yield);
}

/* The first write to a guarded page in a tick: which thread, which block and field. Each (thread kind,
 * block size, allocator, offset) once, up to 500 lines. */
static void writer_seen(uint32_t addr, DWORD tid)
{
    static struct { uint32_t size, chain0, off; int kind; } seen[4096];
    static unsigned nseen, told;
    static LONG lock;
    struct LiveBlock b;
    int kind = tid == s_mixer_tid ? 0 : tid == s_game_tid ? 1 : 2;
    static const char *const kinds[] = { "mixer", "GAME", "other" };
    unsigned k;
    uint32_t off = 0;
    int known;
    if (kind == 1) return;      /* the game thread: mixer_end leaves such a tick out anyway */
    while (InterlockedExchange(&lock, 1)) Sleep(0);
    known = live_find(addr, &b);
    if (known) off = addr - b.at;
    for (k = 0; k < nseen; k++)
        if (seen[k].kind == kind && seen[k].size == (known ? b.size : 0) && seen[k].chain0 == (known ? b.chain[0] : addr) &&
            seen[k].off == off) break;
    if (k == nseen && told < 500) {
        if (nseen < 4096) {
            seen[nseen].kind = kind; seen[nseen].size = known ? b.size : 0; seen[nseen].chain0 = known ? b.chain[0] : addr;
            seen[nseen].off = off; nseen++;
        }
        told++;
        if (known)
            fprintf(stderr, "[ROLLBACK-WRITER] %s thread %lu: block size %X from %08X <- %08X <- %08X, +%X (%08X)\n",
                    kinds[kind], (unsigned long)tid, b.size, b.chain[0], b.chain[1], b.chain[2], off, addr);
        else
            fprintf(stderr, "[ROLLBACK-WRITER] %s thread %lu: %08X, in no known block\n", kinds[kind], (unsigned long)tid, addr);
    }
    InterlockedExchange(&lock, 0);
}

static void mixer_end(void)
{
    unsigned i;
    ULONG64 clock = __rdtsc() - s_mix_tsc, own = mixer_own_cycles() - s_mix_own;
    if (!InterlockedExchange(&s_mix_on, 0)) return;
    s_mixst.watched++;
    if (s_mix_offcpu < 0) {
        const char *v = getenv("RECOMP_ROLLBACK_MIX_OFFCPU");
        s_mix_offcpu = !(v && !strcmp(v, "off"));
        if (!s_mix_offcpu) fprintf(stderr, "[ROLLBACK] tests: a mixer tick counts without its own cycles (the rule before build 52)\n");
    }
    if (s_writers_on == 1) {
        DWORD old;
        for (i = 0; i < s_mix_n; i++) {
            uint32_t q = s_change[i];
            if (s_mixguard[q] && InterlockedExchange8((volatile char *)&s_mixguard[q], 0))
                VirtualProtect(page_host(q), PAGE, PAGE_READWRITE, &old);
        }
    }
    if (game_cycles() != s_game_cycles) { s_mixst.game++; return; }    /* the game thread ran too: no telling who wrote */
    if (s_game_seq != s_mix_seq) { s_mixst.seq++; return; }            /* it saved or loaded meanwhile, whatever its cycles say */
    if (!mixer_kept_cpu(clock, own)) {          /* another thread ran (above); with the test switch only counted */
        s_mixst.off++;
        if (s_mix_offcpu) return;
    }
    /* The cycle count tells only while every guest thread shares one CPU (xbox_PinToGuestCore): a game
     * thread running beside the mixer on another CPU is counted at the clock's interrupts, so a short tick
     * can see its count stand still while it writes (rollback-sound-crashes.md, "one CPU per copy"). */
    {
        LONG gcpu = s_game_cpu, mcpu = (LONG)GetCurrentProcessorNumber();
        if (gcpu >= 0 && gcpu != mcpu) {
            static LONG told;
            if (InterlockedIncrement(&told) <= 4)
                fprintf(stderr, "[ROLLBACK] the mixer ran on CPU %ld and the game thread last on CPU %ld: guest threads "
                        "are not on one CPU here, so the mixer's pages are not told apart (tick left out)\n", mcpu, gcpu);
            s_mixst.cpu++;
            return;
        }
    }
    /* A real tick writes a few words in a few blocks (the routed sound side writes none of the game's heap
     * in test runs). Dozens of pages and thousands of words at once are a game update or a load the checks
     * above did not see; kept live through every later load, they left the game's own objects half
     * restored: F9 cff81c (build 47, 64 pages / 2333 words in one tick, then a crash in a re-run writing
     * through a particle scheduler's half-restored free list, sub_001156E0) and b379eb (55 pages / 2869
     * words, 56 lines before a desync, then a junk small-block free list, sub_000E5F40). Such a tick is left
     * out: the mixer's own words change at every tick and are taken from the next clean one. */
    {
        unsigned pages = 0, words = 0;
        for (i = 0; i < s_mix_n; i++) {
            uint32_t p = s_change[i];
            const uint32_t *was = (const uint32_t *)(s_mix + (size_t)i * PAGE);
            const uint32_t *now = (const uint32_t *)XBOX_PTR(HEAP_LO + p * PAGE);
            unsigned w, here = 0;
            if (s_skip[p] || !memcmp(was, now, PAGE)) continue;
            for (w = 0; w < PAGE / 4; w++)
                if (was[w] != now[w] && !(s_mixmask[p] && (s_mixmask[p][w >> 3] & (1u << (w & 7))))) here++;
            if (here) { pages++; words += here; }
        }
        if (pages > MIX_TICK_PAGES_MAX || words > MIX_TICK_WORDS_MAX) {
            static LONG told;
            if (InterlockedIncrement(&told) <= 8)
                fprintf(stderr, "[ROLLBACK] a mixer tick changed %u pages (%u words) of the game's heap: more than the mixer "
                        "writes, left out\n", pages, words);
            s_mixst.pages++;
            return;
        }
    }
    s_mixst.clean++;
    for (i = 0; i < s_mix_n; i++) {
        uint32_t p = s_change[i];
        if (!memcmp(s_mix + (size_t)i * PAGE, XBOX_PTR(HEAP_LO + p * PAGE), PAGE)) continue;
        if (s_live) mixer_fields(HEAP_LO + p * PAGE, s_mix + (size_t)i * PAGE, (const uint8_t *)XBOX_PTR(HEAP_LO + p * PAGE));
        if (s_skip[p]) continue;
        {
            const uint32_t *was = (const uint32_t *)(s_mix + (size_t)i * PAGE);
            const uint32_t *now = (const uint32_t *)XBOX_PTR(HEAP_LO + p * PAGE);
            unsigned w, lo = PAGE, hi = 0, added = 0;
            if (!s_mixmask[p] && !(s_mixmask[p] = (uint8_t *)calloc(PAGE / 32, 1))) { s_skip[p] = 1; continue; }
            for (w = 0; w < PAGE / 4; w++) {
                if (was[w] == now[w] || (s_mixmask[p][w >> 3] & (1u << (w & 7)))) continue;
                s_mixmask[p][w >> 3] |= (uint8_t)(1u << (w & 7));
                added++;
                if (w * 4 < lo) lo = w * 4;
                hi = w * 4 + 4;
            }
            s_mixst.words += added;
            if (added && s_mix_found++ < 32) {     /* per fight (mixer_stats): a chain's later fights showed none */
                fprintf(stderr, "[ROLLBACK] the mixer writes %08X (bytes %08X..%08X, %u words): %s\n",
                        HEAP_LO + p * PAGE, HEAP_LO + p * PAGE + lo, HEAP_LO + p * PAGE + hi, added,
                        mixmask_keep() ? "they stay as they are through loads" : "seen, a load restores them");
                alloclog_page(HEAP_LO + p * PAGE + (lo & ~0xFu), "the mixer");
            }
        }
    }
}

void rollback_mixer_lock_a(void) { uint32_t ret = MEM32(g_esp); sub_0011FCC0(); if (ret == MIX_LOCKED) mixer_begin(); }
void rollback_mixer_lock_b(void) { uint32_t ret = MEM32(g_esp); sub_00121C80(); if (ret == MIX_LOCKED) mixer_begin(); }
void rollback_mixer_unlock_a(void) { if (MEM32(g_esp) == MIX_UNLOCKS) mixer_end(); sub_0011FCD0(); }
void rollback_mixer_unlock_b(void) { if (MEM32(g_esp) == MIX_UNLOCKS) mixer_end(); sub_00121C90(); }

#else
int rollback_enabled(void) { return 0; }
int rollback_opted_in(void) { return 0; }
void rollback_server_allows(int allowed) { (void)allowed; }
void rollback_server_modes(unsigned modes) { (void)modes; }
int rollback_setting_idle(void) { return 0; }
int rollback_restart_hint(void) { return 0; }
void rollback_heap_range(uint32_t *lo, uint32_t *hi) { if (lo) *lo = 0x80003000u; if (hi) *hi = 0x82A0D000u; }
int rollback_soak_diff_on(void) { return 0; }
void rollback_soak_phase(int phase) { (void)phase; }
void rollback_soak_carriers(long long step) { (void)step; }
void rollback_soak_before(void) {}
void rollback_soak_after(int depth, long long step, int verbose) { (void)depth; (void)step; (void)verbose; }
uint32_t rollback_crash_va;
void rollback_crash_objects(void) {}
void rollback_set_resim(int on) { (void)on; }
const char *rollback_page_state(uint32_t addr) { (void)addr; return "-"; }
void rollback_arena_history(void) {}
int rollback_sound_command_dead(uint32_t sp) { (void)sp; return 0; }
int rollback_hold_xapi_free(uint32_t ptr) { (void)ptr; return 0; }
void rollback_note_dynamic_load(uint32_t caller, uint32_t handle) { (void)caller; (void)handle; }
unsigned rollback_dynamic_loads(void) { return 0; }
void rollback_check_alloc(uint32_t caller, uint32_t size, uint32_t block) { (void)caller; (void)size; (void)block; }
void rollback_check_tables(const char *where) { (void)where; }
void rollback_sound_context(int on) { (void)on; }
int rollback_on_game_thread(void) { return 0; }
void rollback_mixer_lock_a(void) {}
void rollback_mixer_lock_b(void) {}
void rollback_mixer_unlock_a(void) {}
void rollback_mixer_unlock_b(void) {}
void rollback_sound_alloc(void) {}
void rollback_sound_free(void) {}
int rollback_route_alloc(void) { return 0; }
uint32_t rollback_live_alloc(uint32_t s) { (void)s; return 0; }
int rollback_live_free(uint32_t a) { (void)a; return 0; }
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
void rollback_unbind_textures(void) {}
int rollback_device_bindings(void) { return 0; }
void rollback_stats(unsigned *a, unsigned *b, unsigned *c) { if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; }
#endif
