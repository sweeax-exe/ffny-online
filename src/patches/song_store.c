/*
 * Songs in the profile's jukebox and in the music playlist (docs/research/custom-songs.md).
 *
 * The song store. One per profile manager B, at B + 0x30 (vtable 0x286204, "ItemMgr"):
 *   +4 keys: sorted record handles, (table rank << 24) | the row's name-index rank
 *   +8 count, +0xC states: one byte per key (bits 0-1: 0 never arranged, 1 or 3 on, 2 off;
 *   bits 2-7: jukebox order), +0x18 pointer to an order counter (the next order to give).
 * sub_00013340 builds it from the Song table, with the buffers its caller (sub_000144B0) hands
 * it: fixed ones inside B (states B + 0x1420, keys B + 0x1444, 36 of each; counter B + 0x14D4).
 * A profile's save holds those fixed buffers verbatim (B + 0x4C .. B + 0x14DC), and a load
 * (sub_00014540) copies the saved bytes back over them.
 *
 * With songs added to the database, the store gets buffers of its own here (PC_SONGS_MAX_ROWS:
 * the jukebox screen holds no more, see pc_songs.h), and the fixed buffers become the save's
 * copy only:
 *
 *   - after a load, the store is filled from them (the retail songs) and from the PC-side file
 *     of that profile (the added songs; <data>/song-state/<profile>.txt);
 *   - before the front end saves (any MemCard.* command) and whenever the game rebuilds its
 *     playlist, they are written back in the retail shape: the retail songs only, their orders
 *     renumbered 0.., the other slots empty. A save made here stays a save the original game
 *     reads as its own; the added songs' places live in the PC-side file.
 *
 * The playlist. The game's playlist holds 36 names (0x33E798) and one of the callers of the
 * playlist builders has room for 35 names plus the end marker; the fights put one or two songs
 * of their own in front of the list. So the builders (sub_00020C40 in jukebox order,
 * sub_00020D70 shuffled) hand back at most PLAYLIST_CAP songs: with more songs on, the ordered
 * list is a window that moves on by PLAYLIST_CAP every time it is rebuilt, and the shuffled one
 * is the first PLAYLIST_CAP of the shuffle.
 */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#include "recomp/gen/recomp_types.h"
#include "hooks/guest_call.h"
#include "hooks/pc_songs.h"
#include "song_store.h"

#define CAP            64u              /* store buffers: PC_SONGS_MAX_ROWS rows fit */
/* Songs on at once. The playlist, the jukebox's map of it and PlaySong's list (sub_0004EEC0:
 * the songs on, after the one asked for, which it turns on) hold 36: ticking stops at 35, and
 * playing a song that is off is the one way to the 36th. */
#define ON_LIMIT       35u
#define PLAY_LIMIT     36u
#define FIXED_CAP      36u
#define PLAYLIST_CAP   32u
#define ALLOC_TAG      0x0028997Cu      /* a tag string the playlist builder itself uses */
#define PROFILE_NAME   0x2B8u           /* the profile's name in B (ASCII), part of its save */

extern void sub_00029680(void);         /* the game's allocator: cdecl (size, tag, flags) */
extern void sub_00029740(void);         /* the game's free: cdecl (pointer) */

static int s_active;
static uint32_t s_store, s_keys, s_states, s_counter;            /* the store's own buffers */
static uint32_t s_fixed_keys, s_fixed_states, s_fixed_counter;   /* the save's copy, in B */
static uint32_t s_manager;                                       /* B: the profile manager */
static int s_loaded;                                             /* a profile was loaded */
static char s_profile[40];
static char s_written[16384];                                     /* the state file as last written */
static uint32_t s_rotate;

static int on_count(uint32_t store);

static int trace(void)
{
    static int on = -1;
    if (on < 0) on = getenv("RECOMP_SONGS_TRACE") != NULL;
    return on;
}

static int retail_rank(uint32_t key) { return (key & 0xFFFFFFu) < (uint32_t)pc_songs_retail_rows(); }

/* ---- the store's own buffers -------------------------------------------------------------------- */

void song_store_before_build(uint32_t store)
{
    /* sub_00013340(this = store; table, states, keys, counter): swap in buffers that hold every
     * song. Nothing to do when no song was added: the game keeps its own. */
    if (pc_songs_rows() <= pc_songs_retail_rows() || pc_songs_rows() > CAP) return;
    if (!s_keys) {
        uint32_t args[3] = { CAP * 4 + CAP + 8, ALLOC_TAG, 0 };
        uint32_t block = guest_call(sub_00029680, 0, 3, args);
        if (!block) {
            fprintf(stderr, "[SONGS] no memory for the song store; added songs stay out of the jukebox\n");
            return;
        }
        s_keys = block;
        s_states = block + CAP * 4;
        s_counter = (s_states + CAP + 3) & ~3u;
    }
    for (uint32_t i = 0; i < CAP; i++) { MEM32(s_keys + i * 4) = 0xFFFFFFFFu; MEM8(s_states + i) = 0; }
    s_fixed_states = MEM32(g_esp + 8);
    s_fixed_keys = MEM32(g_esp + 0xC);
    s_fixed_counter = MEM32(g_esp + 0x10);
    MEM32(s_counter) = s_fixed_counter ? MEM32(s_fixed_counter) : 0;
    MEM32(g_esp + 8) = s_states;
    MEM32(g_esp + 0xC) = s_keys;
    MEM32(g_esp + 0x10) = s_counter;
    s_store = store;
    s_manager = store - 0x30u;
    s_active = 1;
}

/* ---- the PC-side file --------------------------------------------------------------------------- */

static void profile_name(uint32_t manager, char *out, size_t size)
{
    size_t n = 0;
    for (uint32_t i = 0; i < 16 && n + 3 < size; i++) {
        uint8_t c = MEM8(manager + PROFILE_NAME + i);
        if (!c) break;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            out[n++] = (char)c;
        else
            n += (size_t)snprintf(out + n, size - n, "%02X", c);
    }
    out[n] = 0;
    if (!n) snprintf(out, size, "profile");
}

static void state_path(char *out, size_t size)
{
    snprintf(out, size, "%s" HOST_SEP "song-state" HOST_SEP "%s.txt", pc_songs_data_root(), s_profile);
}

typedef struct { char entity[24]; int state; } Saved;

/* entity -> state byte, in file order; *retail gets the checksum line. Returns the count. */
static int s_launcher_edit;     /* the file carries the launcher's choices: follow them over the save */

static int read_state_file(Saved *out, int max, uint32_t *retail, int *have_retail)
{
    char path[1024], line[128];
    int n = 0;
    *have_retail = 0;
    state_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char name[64];
        unsigned v;
        if (line[0] == '#') continue;
        if (sscanf(line, "retail %x", &v) == 1) { *retail = v; *have_retail = 1; continue; }
        if (!strncmp(line, "launcher", 8)) { s_launcher_edit = 1; continue; }
        if (n < max && sscanf(line, "%63s %x", name, &v) == 2 && strlen(name) < sizeof out[n].entity) {
            snprintf(out[n].entity, sizeof out[n].entity, "%s", name);
            out[n++].state = (int)(v & 0xFF);
        }
    }
    fclose(f);
    return n;
}

static uint32_t fixed_checksum(void)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < FIXED_CAP; i++) {
        uint32_t k = MEM32(s_fixed_keys + i * 4);
        h = (h ^ (k & 0xFF)) * 16777619u; h = (h ^ (k >> 8 & 0xFF)) * 16777619u;
        h = (h ^ (k >> 16 & 0xFF)) * 16777619u; h = (h ^ (k >> 24)) * 16777619u;
        h = (h ^ MEM8(s_fixed_states + i)) * 16777619u;
    }
    return h;
}

static void make_dirs(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char c = *p;
        *p = 0;
#if defined(_WIN32)
        _mkdir(tmp);
#else
        mkdir(tmp, 0755);
#endif
        *p = c;
    }
}

static void write_state_file(void)
{
    static char text[sizeof s_written];
    char path[1024], part[1040];
    size_t n = 0;
    uint32_t count = MEM32(s_store + 8);
    n += (size_t)snprintf(text + n, sizeof text - n,
                          "# The Beatbox of profile %s: every song, its state (bits 0-1: 0 never arranged, 1 or 3 on,\n"
                          "# 2 off; bits 2-7: its place) and name. Written by the game; the launcher's Songs page may\n"
                          "# change states and places and add a \"launcher\" line, which the next load follows over the\n"
                          "# save. docs/research/custom-songs.md.\n"
                          "retail %08X\n", s_profile, fixed_checksum());
    for (uint32_t i = 0; i < count && n < sizeof text - 200; i++) {
        uint32_t rank = MEM32(s_keys + i * 4) & 0xFFFFFFu;
        const char *e = pc_songs_id_for_rank(rank), *label = pc_songs_label_for_rank(rank);
        if (e && *e) n += (size_t)snprintf(text + n, sizeof text - n, "%s %02X %s\n", e, MEM8(s_states + i), label ? label : "");
    }
    if (!strcmp(text, s_written)) return;
    state_path(path, sizeof path);
    snprintf(part, sizeof part, "%s.part", path);
    make_dirs(path);
    FILE *f = fopen(part, "w");
    int ok = f && fputs(text, f) >= 0;
    if (f && fclose(f)) ok = 0;
    remove(path);
    if (!ok || rename(part, path)) {
        remove(part);
        fprintf(stderr, "[SONGS] cannot write %s\n", path);
        return;
    }
    snprintf(s_written, sizeof s_written, "%s", text);
}

/* ---- the save's copy ---------------------------------------------------------------------------- */

/* The fixed buffers in the retail shape: retail songs only, orders 0.. in the store's order. */
static void sync_fixed(void)
{
    uint32_t count = MEM32(s_store + 8), retail = 0, arranged = 0;
    uint32_t order_of[CAP];
    if (!s_active || !s_manager) return;
    for (uint32_t i = 0; i < count; i++) if (retail_rank(MEM32(s_keys + i * 4))) retail++;
    if (retail > FIXED_CAP) return;
    /* Renumber the arranged retail songs by their order in the store. */
    for (uint32_t i = 0; i < count; i++) order_of[i] = 0xFFFFFFFFu;
    for (uint32_t pos = 0; pos < 64; pos++)
        for (uint32_t i = 0; i < count; i++) {
            uint8_t s = MEM8(s_states + i);
            if (retail_rank(MEM32(s_keys + i * 4)) && (s & 3) && (uint32_t)(s >> 2) == pos)
                order_of[i] = arranged++;
        }
    uint32_t slot = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t k = MEM32(s_keys + i * 4);
        if (!retail_rank(k)) continue;
        uint8_t s = MEM8(s_states + i);
        MEM32(s_fixed_keys + slot * 4) = k;
        MEM8(s_fixed_states + slot) = (s & 3) ? (uint8_t)((order_of[i] << 2) | (s & 3)) : 0;
        slot++;
    }
    for (; slot < FIXED_CAP; slot++) { MEM32(s_fixed_keys + slot * 4) = 0xFFFFFFFFu; MEM8(s_fixed_states + slot) = 0; }
    if (s_fixed_counter) MEM32(s_fixed_counter) = arranged;
    /* The PC-side file belongs to a profile that was loaded (a new one gets it from its first load). */
    if (s_loaded) {
        profile_name(s_manager, s_profile, sizeof s_profile);
        write_state_file();
    }
}

void song_store_after_build(void)
{
    /* The game no longer writes the save's copy itself: give it the retail keys at once. */
    sync_fixed();
}

void song_store_on_funccall(const char *text)
{
    if (s_active && !strncmp(text, "MemCard.", 8)) sync_fixed();
}

/* ---- a profile load ----------------------------------------------------------------------------- */

void song_store_before_load(uint32_t manager) { (void)manager; }

typedef struct { uint32_t index; uint32_t rank; } Order;

static int order_cmp(const void *a, const void *b)
{
    const Order *x = (const Order *)a, *y = (const Order *)b;
    return x->rank < y->rank ? -1 : x->rank > y->rank;
}

void song_store_after_load(uint32_t manager)
{
    Saved saved[CAP];
    uint32_t count, file_retail = 0;
    int nsaved, have_retail;
    if (!s_active) return;
    count = MEM32(s_store + 8);
    if (count > CAP) return;
    s_manager = manager;
    s_loaded = 1;
    profile_name(manager, s_profile, sizeof s_profile);
    s_written[0] = 0;
    s_launcher_edit = 0;
    nsaved = read_state_file(saved, CAP, &file_retail, &have_retail);
    /* The file describes this save when the save's song block is the one it was written with;
     * otherwise (a new profile, or one the original game changed) the save decides for the
     * retail songs and the file only for the added ones. */
    int file_whole = (have_retail && file_retail == fixed_checksum()) || (s_launcher_edit && nsaved);
    if (s_launcher_edit) fprintf(stderr, "[SONGS] profile %s: the launcher's Beatbox choices\n", s_profile);

    /* Wanted position of every song: (state, sort key). Sort keys: file songs in file order
     * (their own order) when the file is whole; else retail by their saved order, then the
     * added songs in file order, then the rest. */
    Order want[CAP];
    uint8_t state[CAP];
    uint32_t nwant = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t key = MEM32(s_keys + i * 4);
        const char *entity = pc_songs_id_for_rank(key & 0xFFFFFFu);
        int from_file = -1;
        for (int j = 0; j < nsaved && entity; j++) if (!strcmp(saved[j].entity, entity)) from_file = j;
        uint8_t fixed_state = 0;
        int in_fixed = 0;
        for (uint32_t j = 0; j < FIXED_CAP; j++)
            if (MEM32(s_fixed_keys + j * 4) == key) { fixed_state = MEM8(s_fixed_states + j); in_fixed = 1; }
        uint32_t rank;
        if (retail_rank(key) && !file_whole) {
            state[i] = in_fixed ? fixed_state : 0;
            rank = (state[i] & 3) ? (uint32_t)(state[i] >> 2) : 0x800u + i;     /* never arranged: after the arranged, before new songs */
        } else if (from_file >= 0) {
            state[i] = (uint8_t)saved[from_file].state;
            rank = (state[i] & 3) ? (file_whole ? (uint32_t)(state[i] >> 2) : 0x100u + (uint32_t)from_file)
                                  : 0x800u + i;
        } else {
            state[i] = 3;                                   /* new: on, after every song the profile knows (off past ON_LIMIT) */
            rank = 0x1000u + i;
        }
        /* A song the game never arranged it would arrange itself right after the load (on, after
         * the others, from its order counter): do it here, so it counts toward ON_LIMIT. */
        if (!(state[i] & 3)) state[i] = 1;
        want[nwant].index = i;
        want[nwant++].rank = rank;
    }
    qsort(want, nwant, sizeof *want, order_cmp);
    {
        /* At most ON_LIMIT on: the ones after that keep their places, turned off. */
        uint32_t on = 0;
        for (uint32_t o = 0; o < nwant; o++) {
            uint8_t s = state[want[o].index] & 3;
            if (s != 2) { if (on >= ON_LIMIT) s = 2; else on++; }
            MEM8(s_states + want[o].index) = (uint8_t)(((o < 62 ? o : 62) << 2) | s);
        }
    }
    MEM32(s_counter) = nwant;

    song_store_test_only();
    if (trace()) {
        fprintf(stderr, "[SONGS] profile %s loaded (%s): ", s_profile,
                file_whole ? "state file matches its save" : nsaved ? "save + state file" : "save only");
        for (uint32_t i = 0; i < count; i++) fprintf(stderr, " %X=%02X", MEM32(s_keys + i * 4), MEM8(s_states + i));
        fprintf(stderr, "\n");
    }
    sync_fixed();
    fprintf(stderr, "[SONGS] profile %s: %u song(s) in its jukebox, %u on\n", s_profile, count, (unsigned)on_count(s_store));
}

/*
 * Test aid: RECOMP_TEST_SONGS_ONLY=<rank> turns every other song off after a profile loads, so
 * the next playlist holds that song alone and the main menu's song banner announces it at
 * start-up. The rank is the record handle's low bits (0-31 retail; the added songs follow).
 */
void song_store_test_only(void)
{
    const char *only = getenv("RECOMP_TEST_SONGS_ONLY");
    if (!only || !*only || !s_active) return;
    uint32_t want = (uint32_t)strtoul(only, NULL, 0), count = MEM32(s_store + 8);
    if (only[0] < '0' || only[0] > '9')               /* a folder name instead of a rank */
        for (uint32_t r = 0; r < (uint32_t)pc_songs_rows(); r++)
            if (pc_songs_entity_for_rank(r) && !strcmp(pc_songs_entity_for_rank(r), only)) want = r;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t s = MEM8(s_states + i);
        if ((MEM32(s_keys + i * 4) & 0xFFFFFFu) == want) MEM8(s_states + i) = (uint8_t)((s & 0xFC) | 3);
        else MEM8(s_states + i) = (uint8_t)((s & 0xFC) | 2);
    }
    fprintf(stderr, "[SONGS] test: only song %u on\n", want);
}

/* ---- the playlist ------------------------------------------------------------------------------- */

/* sub_00020C40 / sub_00020D70 (this = store; out names[], out count), ret 8. The builder runs
 * into a list of our own on the guest stack below the caller's frame; then at most
 * PLAYLIST_CAP of its names go to the caller and the rest are freed. */
void song_store_build_playlist(void (*builder)(void), int shuffled)
{
    uint32_t esp0 = g_esp, ret = MEM32(esp0), out = MEM32(esp0 + 4), count_at = MEM32(esp0 + 8);
    uint32_t frame = esp0 - 0x240, list = frame + 0x20, n, names[CAP + 1], take, start;
    if (!s_active) { builder(); return; }
    MEM32(frame) = ret;
    MEM32(frame + 4) = list;
    MEM32(frame + 8) = frame + 0x10;
    MEM32(frame + 0x10) = 0;
    g_esp = frame;
    builder();
    n = MEM32(frame + 0x10);
    if (n > CAP) n = CAP;
    for (uint32_t i = 0; i < n; i++) names[i] = MEM32(list + i * 4);
    g_esp = esp0 + 12;                       /* as the builder's own ret 8 leaves it */
    if (shuffled)                            /* the game's shuffle, on the host (sub_00020D70 keeps 36) */
        for (uint32_t i = n; i > 1; i--) {
            uint32_t j = (uint32_t)rand() % i, x = names[i - 1];
            names[i - 1] = names[j];
            names[j] = x;
        }
    take = n < PLAYLIST_CAP ? n : PLAYLIST_CAP;
    start = (!shuffled && n > take) ? s_rotate % n : 0;
    if (!shuffled && n > take) s_rotate = (s_rotate + take) % n;
    for (uint32_t i = 0; i < take; i++) MEM32(out + i * 4) = names[(start + i) % n];
    MEM32(out + take * 4) = 0;
    MEM32(count_at) = take;
    for (uint32_t i = take; i < n; i++) {
        uint32_t p = names[(start + i) % n];
        guest_call(sub_00029740, 0, 1, &p);
    }
    if (trace() && n > take) fprintf(stderr, "[SONGS] playlist: %u of %u songs on this time (%s)\n", take, n,
                                      shuffled ? "shuffled" : "jukebox order, moving window");
}

/* In place of sub_0003CFF0 (SetMusicPlaylist: cdecl, a NULL-terminated list of names, a start
 * index, a flag). The game copies the names into 36 slots and keeps no bound, so a longer list
 * (the jukebox hands its whole list over when it closes) goes in as a window of 36 that moves on
 * every time; the caller's own list is not touched (it frees its names itself). */
void song_store_set_playlist(void (*set)(void))
{
    uint32_t list = MEM32(g_esp + 4), n = 0;
    while (n < 256 && MEM32(list + n * 4)) n++;
    if (n <= FIXED_CAP) { set(); return; }
    uint32_t esp0 = g_esp, copy = esp0 - 0x200, frame = copy - 0x20, start = s_rotate % n;
    for (uint32_t i = 0; i < FIXED_CAP; i++) MEM32(copy + i * 4) = MEM32(list + ((start + i) % n) * 4);
    MEM32(copy + FIXED_CAP * 4) = 0;
    s_rotate = (s_rotate + FIXED_CAP) % n;
    /* The call again, below our copy: return address, then the same arguments with the copy. */
    MEM32(frame) = MEM32(esp0);
    MEM32(frame + 4) = copy;
    MEM32(frame + 8) = MEM32(esp0 + 8) < FIXED_CAP ? MEM32(esp0 + 8) : 0;   /* start index */
    MEM32(frame + 12) = MEM32(esp0 + 12);
    g_esp = frame;
    set();
    g_esp = esp0;                            /* cdecl: the caller pops its own arguments */
    if (trace()) fprintf(stderr, "[SONGS] playlist: %u of %u songs on this time (window)\n", FIXED_CAP, n);
}

/* After the game builds its music playlist (sub_0003CFF0). With RECOMP_SONGS_TRACE, list it
 * (36 names of 16 bytes at 0x33E798, the count at 0x33E9DC) and the store. */
void song_store_playlist_built(void)
{
    if (!trace()) return;
    uint32_t count = MEM32(0x33E9DC);
    fprintf(stderr, "[SONGS] playlist built: %u song(s):", count);
    for (uint32_t i = 0; i < count && i < FIXED_CAP; i++) {
        char name[17];
        for (int c = 0; c < 16; c++) name[c] = (char)MEM8(0x33E798 + i * 16 + c);
        name[16] = 0;
        fprintf(stderr, " %s", name);
    }
    fprintf(stderr, "\n");
    if (s_active) {
        uint32_t n = MEM32(s_store + 8);
        fprintf(stderr, "[SONGS] store:");
        for (uint32_t i = 0; i < n && i < CAP; i++) fprintf(stderr, " %X=%02X", MEM32(s_keys + i * 4), MEM8(s_states + i));
        fprintf(stderr, "\n");
    }
}

int song_store_active(void) { return s_active; }

/* ---- the jukebox screen ------------------------------------------------------------------------- */

static int on_count(uint32_t store)
{
    uint32_t n = MEM32(store + 8), states = MEM32(store + 0xC);
    int on = 0;
    for (uint32_t i = 0; i < n; i++) { uint8_t s = MEM8(states + i) & 3; on += s != 0 && s != 2; }
    return on;
}

static int find_key(uint32_t store, uint32_t h)
{
    uint32_t n = MEM32(store + 8), keys = MEM32(store + 4);
    for (uint32_t i = 0; i < n; i++) if (MEM32(keys + i * 4) == h) return (int)i;
    return -1;
}

/*
 * In place of sub_0004FD80 (this = the jukebox object J of ChooseShopList(6); ret 4): the list of
 * songs the Beatbox shows, at 0x3A07B0 (128 handles), built the way the game builds it but with
 * arrays sized for every song (the game's are 36, 36 and 32 entries on its stack):
 *   arranged songs by jukebox order; then the unplaced ones (order bits all set), each given its
 *   list position as order and turned on; then the never-arranged (state 0); -1 after them.
 *   J+4 the list's length, J+0x3C arranged + unplaced, J+0x4C[J+0x38++] the list index of each
 *   song that is on (the jukebox's map of the playlist: 37 slots, the flag at J+0xE0 after them),
 *   J+0x18 = 0. With music playing, the song now playing is selected: PlaySong(index, 1) after
 *   J+0xE0 = 1. Song state is read from the store directly (every Song row is in it).
 */
extern void sub_0004EEC0(void);
void song_jukebox_build(void (*original)(void))
{
    uint32_t j = g_ecx, store = MEM32(j + 0x34);
    uint32_t n, keys, states, O[CAP], U[CAP], T[CAP], no = 0, nu = 0, nt = 0, c = 0;
    uint8_t taken[CAP];
    if (!s_active || store != s_store) { original(); return; }
    n = MEM32(store + 8); keys = MEM32(store + 4); states = MEM32(store + 0xC);
    if (n > CAP) n = CAP;
    MEM8(j + 0xE0) = 0;
    MEM32(j + 4) = 0;
    MEM32(j + 0x18) = 0;
    memset(taken, 0, sizeof taken);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t s = MEM8(states + i);
        if (!(s & 3)) T[nt++] = MEM32(keys + i * 4);
        else if ((s & 0xFC) == 0xFC) U[nu++] = MEM32(keys + i * 4);
    }
    for (uint32_t p = 0; p < 63 && no < n - nu - nt; p++)
        for (uint32_t i = 0; i < n; i++) {
            uint8_t s = MEM8(states + i);
            if ((s & 3) && (s & 0xFC) != 0xFC && (uint32_t)(s >> 2) == p && !taken[i]) {
                O[no++] = MEM32(keys + i * 4);
                taken[i] = 1;
                break;
            }
        }
    /* Two songs at one position (the game's own search would never finish): the rest after. */
    for (uint32_t i = 0; i < n && no < n - nu - nt; i++) {
        uint8_t s = MEM8(states + i);
        if ((s & 3) && (s & 0xFC) != 0xFC && !taken[i]) O[no++] = MEM32(keys + i * 4);
    }
    for (uint32_t i = 0; i < no; i++) MEM32(0x3A07B0 + c++ * 4) = O[i];
    for (uint32_t i = 0; i < nu; i++) {
        int k = find_key(store, U[i]);
        MEM32(0x3A07B0 + c * 4) = U[i];
        if (k >= 0) MEM8(states + k) = (uint8_t)((c << 2) | 3);
        c++;
    }
    for (uint32_t i = 0; i < nt; i++) MEM32(0x3A07B0 + c++ * 4) = T[i];
    MEM32(j + 4) = c;
    MEM32(j + 0x3C) = no + nu;
    for (uint32_t i = c; i < 0x80; i++) MEM32(0x3A07B0 + i * 4) = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < no + nu; i++) {
        int k = find_key(store, MEM32(0x3A07B0 + i * 4));
        uint8_t s = k >= 0 ? MEM8(states + k) & 3 : 0;
        if (s == 2 || s == 0) continue;
        if (MEM32(j + 0x38) >= 37) break;            /* its map has 37 slots, then the flag */
        MEM32(j + 0x4C + MEM32(j + 0x38) * 4) = i;
        MEM32(j + 0x38) = MEM32(j + 0x38) + 1;
    }
    if (MEM8(MEM32(j + 0x44))) {
        char now[17];
        uint32_t e;
        for (int k = 0; k < 16; k++) now[k] = (char)MEM8(0x33E798 + MEM32(0x33EC94) * 16 + k);
        now[16] = 0;
        for (e = 0; e < no + nu; e++) {
            uint32_t h = MEM32(0x3A07B0 + e * 4);
            int k = find_key(store, h);
            uint8_t s = k >= 0 ? MEM8(states + k) & 3 : 0;
            const char *entity = pc_songs_entity_for_rank(h & 0xFFFFFFu);
            if (s == 2 || s == 0 || !entity) continue;
            if (!strcmp(entity, now)) break;
        }
        uint32_t args[2] = { e, 1 };
        MEM8(j + 0xE0) = 1;
        guest_call(sub_0004EEC0, j, 2, args);
    }
    g_eax = 0;
    g_esp += 8;                                      /* ret 4 */
    if (trace())
        fprintf(stderr, "[SONGS] jukebox list: %u songs (%u in order, %u placed now, %u never arranged), %u on\n",
                c, no, nu, nt, MEM32(j + 0x38));
}

/* In place of sub_00050E00 (Shops.EnableSong(i): this = J, the list index on the stack; ret 4;
 * returns the reply string 0x294A28). With ON_LIMIT songs on, a further one stays off. */
void song_jukebox_enable(void (*original)(void))
{
    uint32_t j = g_ecx, store = MEM32(j + 0x34), idx = MEM32(g_esp + 4);
    if (s_active && store == s_store && idx < 0x80) {
        int k = find_key(store, MEM32(0x3A07B0 + idx * 4));
        uint8_t s = k >= 0 ? MEM8(MEM32(store + 0xC) + k) & 3 : 0;
        if ((s == 0 || s == 2) && on_count(store) >= (int)ON_LIMIT) {
            fprintf(stderr, "[SONGS] jukebox: %u songs on already; song %u stays off\n", ON_LIMIT, idx);
            g_eax = 0x294A28u;
            g_esp += 8;
            return;
        }
    }
    original();
}

/* In place of sub_0004EEC0 (PlaySong: this = J, the list index and a flag on the stack; ret 8).
 * Playing a song that is off turns it on: with PLAY_LIMIT on already, it is not played. */
void song_jukebox_play(void (*original)(void))
{
    uint32_t j = g_ecx, store = MEM32(j + 0x34), idx = MEM32(g_esp + 4);
    if (s_active && store == s_store && idx < 0x80) {
        int k = find_key(store, MEM32(0x3A07B0 + idx * 4));
        uint8_t s = k >= 0 ? MEM8(MEM32(store + 0xC) + k) & 3 : 0;
        if ((s == 0 || s == 2) && on_count(store) >= (int)PLAY_LIMIT) {
            fprintf(stderr, "[SONGS] jukebox: %u songs on; song %u (off) not played\n", PLAY_LIMIT, idx);
            g_esp += 12;
            return;
        }
    }
    original();
}
