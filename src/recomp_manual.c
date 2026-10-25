/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "host.h"

/* The generated register model: MEM8/16/32, XBOX_PTR, and the guest registers
 * declared RECOMP_TLS. Declaring them here by hand instead would resolve to
 * the image's TLS template rather than the running thread's copy, so an
 * override would read zeroes for every register. */
#include "recomp/gen/recomp_types.h"
#include "hooks/save_compat.h"
#include "hooks/test_observations.h"
#include "hooks/pc_settings.h"
#include "hooks/netplay.h"
#include "hooks/automatch.h"
#include "hooks/rollback.h"

/* Read-only probes use the lifter's universal entry hooks. They do not alter
 * guest registers, stack, memory or the generated body. Off in normal play. */
void sub_001A4A30_enter(void) { defjam_test_update(); }
void sub_001A5DD0_enter(void)
{
    if (defjam_test_observations_enabled())
        defjam_test_hit(MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_esp));
}
void sub_001A50B0_enter(void)
{
    if (defjam_test_observations_enabled())
        defjam_test_health_begin(MEM32(g_esp + 4), MEM32(g_esp));
}
void sub_001A6870_enter(void)
{
    if (defjam_test_observations_enabled())
        defjam_test_health_notify(MEM32(g_esp + 4), MEM32(g_esp + 8));
}
/* Match telemetry (src/hooks/test_telemetry.c): the fight's simulation step,
 * match set-up, the result recorder and the random number generator's seeding.
 * Read-only, except that RECOMP_TEST_RNG_SEED replaces the seed argument. */
/* Online play (src/hooks/netplay.c) uses the same points: it counts the steps, and in a
 * session it replaces every seed with one both PCs derive from the session. */
void sub_00087700_enter(void) { defjam_test_step(); netplay_on_step(); }
void sub_001A4D90_enter(void) { defjam_test_match_start(); netplay_on_match_start(); }
void sub_001BC690_enter(void) { defjam_test_rng(g_esp); netplay_on_seed(g_esp, 2, 0x1BC690u); }
void sub_001AD220_enter(void) { defjam_test_ai_seed(g_esp); netplay_on_seed(g_esp, 1, 0x1AD220u); }
/* The seeding routine every generator of this kind goes through (thiscall). */
void sub_000AA0D0_enter(void) { defjam_test_generator_seed(g_ecx, g_esp); netplay_on_seed(g_esp, 2, g_ecx); }
/* Every draw (RECOMP_TEST_DRAWS): the C runtime's rand, the match generator, and the two
 * wrappers the object-form generators are drawn through; then the background crowd's update. */
void sub_00200820_enter(void) { defjam_test_draw(1, MEM32(g_esp)); }
void sub_001BC6E0_enter(void) { defjam_test_draw(2, MEM32(g_esp)); }
void sub_000AA300_enter(void) { defjam_test_draw(g_ecx, MEM32(g_esp)); }
void sub_000AA340_enter(void) { defjam_test_draw(g_ecx, MEM32(g_esp)); }
void sub_000DB870_enter(void) { defjam_test_crowd_update(); }
/* The per-fighter pad reader: RECOMP_TEST_INPUT replaces what it is about to read, and so
 * does online play, with the entry both PCs agreed on for this step. */
void sub_001BAA00_enter(void) { defjam_test_input(MEM32(g_esp + 4)); netplay_on_pad_read(MEM32(g_esp + 4)); }
/* Rollback (src/hooks/rollback.c). The main loop's dt read is the first call of every game
 * update, so its entry is the clean point between two updates: save, load and re-run there.
 * The audio subsystem's start-up brackets the memory the sound engine and its mixer thread
 * own, which a load must leave alone (sub_00012CC0, the registry store, follows it). */
void sub_0002C530_enter(void) { netplay_on_update(); }
void sub_0003C6E0_enter(void) { rollback_audio_init_begin(); }
void sub_00012CC0_enter(void) { rollback_audio_init_end(); }
/* EAGL's DynamicLoader::Resolve: a model, weapon or render object is linked in (rollback.c logs it in fights). */
void sub_0010AC20_enter(void) { rollback_note_dynamic_load(MEM32(g_esp), g_ecx); }
/* The music director's stream (re)opens and flushes, and the stream layer's flush (docs/research/
 * stream-table-crash.md 8): whether rollback's re-run steps reach them. Logged with the re-run flag. */
static void music_trace(const char *what)
{
    static unsigned told, rerun;
    int r = netplay_rerunning();
    if (r) rerun++;
    if (told < 60 || (r && rerun <= 20)) {
        told++;
        fprintf(stderr, "[MUSIC] %s (%sfrom %08X)\n", what, r ? "RE-RUN STEP, " : "", MEM32(g_esp));
    }
}
void sub_0003BE50_enter(void) { music_trace("the director rebuilds the stream (sub_0003BE50)"); }
void sub_0001D5B0_enter(void) { music_trace("AEMS stream open (sub_0001D5B0)"); }
void sub_0001D7C0_enter(void) { music_trace("AEMS stream stop (sub_0001D7C0)"); }
void sub_001E8600_enter(void) { music_trace("a stream ring flushed (sub_001E8600)"); }


/* The lifted body below uses the generated code's register names. */
extern void sub_001ECF40(void);     /* fills a new block */
extern void sub_001E9E90(void);     /* the pool allocator */
#define eax g_eax
#define ecx g_ecx
#define edx g_edx
#define esp g_esp
#define ebx g_ebx
#define esi g_esi
#define edi g_edi
/**
 * sub_00029500
 * Original: 0x00029500 - 0x000295A6 (166 bytes, 68 insns)
 * CC: cdecl (tag, size, flags), returns the block
 *
 * The game's allocator, kept as the lifter wrote it, with one test in front: a block the game's
 * audio code asks for (the sound engine's AEMS layer 0x1C000.., the audio subsystem 0x30000..,
 * the engine 0x11C000..) comes from rollback.c's arena outside the heap, so a rollback, which puts
 * the heap back, never touches what the sound side still holds.
 */
static void sub_00029500_body(void)
{
    uint32_t ebp = 0;
    ebp = g_ebp;  /* frameless: caller's frame */
    int _flags = 0, _sf = 0, _of = 0, _pf = 0;
    unsigned _fv = 0; /* dynamic ZF/SF/OF/PF validity */
    uint32_t _fa = 0, _fb = 0;
    int32_t _fas = 0, _fbs = 0;
    (void)_fa; (void)_fb; (void)_fas; (void)_fbs;
    ebp = g_seh_ebp; /* fpo_leaf: inherit caller's frame */

loc_00029500: ;
    PUSH32(esp, ebx);
    ebx = MEM32(esp + 0x10);
    PUSH32(esp, ebp);
    eax = 4;
    _fa = (uint32_t)(LO8(eax)) & 0xFFu; _fb = (uint32_t)(HI8(ebx)) & 0xFFu;
    _fas = (int32_t)(int8_t)(_fa); _fbs = (int32_t)(int8_t)(_fb); /* test LO8(eax), HI8(ebx) (8-bit) */
    { uint32_t _r = (_fa & _fb) & 0xFFu; _flags = (_r == 0); _sf = ((_r & 0x80u) != 0); _of = 0; _pf = RECOMP_PARITY8(_r); _fv = 15; }
    PUSH32(esp, esi);
    PUSH32(esp, edi);
    if (((_fv & 1u) == 1u && (_flags))) goto loc_00029516; /* je: published flags */

loc_00029511: ;
    eax = 0x80;

loc_00029516: ;
    ebp = MEM32(esp + 0x18);
    edx = ebx;
    edx = edx & 0x80000000u;
    _fa = (uint32_t)(edx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* and result */ _flags = (_fa == 0); _sf = (_fas < 0); _of = 0; _pf = RECOMP_PARITY8(_fa); _fv = 15;
    MEM32(esp + 0x1C) = edx;
    if (((_fv & 1u) == 1u && (!_flags))) goto loc_0002956C; /* jne: published flags */

loc_00029528: ;
    ecx = MEM32(0x33DB54);
    _fa = (uint32_t)(ecx) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp ecx, 0 (32-bit) */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_EQ(_fa, _fb)) goto loc_0002956C; /* je: equal / zero */

loc_00029532: ;
    _fa = (uint32_t)(ebp) & 0xFFFFFFFFu; _fb = (uint32_t)(0x400) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp ebp, 0x400 (32-bit) */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_G(_fas, _fbs)) goto loc_0002956C; /* jg: greater (signed >) */

loc_0002953A: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0x80) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0x80 (32-bit) */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_G(_fas, _fbs)) goto loc_0002956C; /* jg: greater (signed >) */

loc_00029541: ;
    _fa = (uint32_t)(ebp) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp ebp, eax (32-bit) */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    edi = ebp;
    if (((_fv & 6u) == 6u && (_sf == _of))) goto loc_00029549; /* jge: published flags */

loc_00029547: ;
    edi = eax;

loc_00029549: ;
    eax = MEM32(ecx);
    { uint32_t _icall_esp = g_esp;
    PUSH32(esp, edi);
    { uint32_t _icall_target = MEM32(eax + 4); PUSH32(esp, 0x0002954Fu); RECOMP_ICALL_SAFE_AT(_icall_target, _icall_esp, 0x0002954Cu); } /* indirect call */
    }

loc_0002954F: ;
    esi = eax;
    _fa = (uint32_t)(esi) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp esi, 0 (32-bit) */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_EQ(_fa, _fb)) goto loc_00029568; /* je: equal / zero */

loc_00029555: ;
    PUSH32(esp, edi);
    PUSH32(esp, 0);
    PUSH32(esp, esi);
    PUSH32(esp, 0x0002955Eu); RECOMP_ABI_CALL(0x001ECF40u, sub_001ECF40); /* call 0x001ECF40 */

loc_0002955E: ;
    _fb = (uint32_t)(0xC) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    esp = esp + 0xC;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }
    POP32(esp, edi);
    eax = esi;
    POP32(esp, esi);
    POP32(esp, ebp);
    POP32(esp, ebx);
    esp += 4; return; /* ret */

loc_00029568: ;
    edx = MEM32(esp + 0x1C);

loc_0002956C: ;
    _fa = (uint32_t)(edx) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp edx, 0 (32-bit) */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_NE(_fa, _fb)) goto loc_00029578; /* jne: not equal / not zero */

loc_00029570: ;
    ebx = ebx | MEM32(0x3C9138);
    _fa = (uint32_t)(ebx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* or result */ _flags = (_fa == 0); _sf = (_fas < 0); _of = 0; _pf = RECOMP_PARITY8(_fa); _fv = 15;
    goto loc_0002957E;

loc_00029578: ;
    ebx = ebx ^ 0x80000000u;
    _fa = (uint32_t)(ebx) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* xor result */ _flags = (_fa == 0); _sf = (_fas < 0); _of = 0; _pf = RECOMP_PARITY8(_fa); _fv = 15;

loc_0002957E: ;
    ecx = MEM32(esp + 0x14);
    PUSH32(esp, ebx);
    PUSH32(esp, ebp);
    PUSH32(esp, ecx);
    PUSH32(esp, 0x0002958Au); RECOMP_ABI_CALL(0x001E9E90u, sub_001E9E90); /* call 0x001E9E90 */

loc_0002958A: ;
    esi = eax;
    _fb = (uint32_t)(0xC) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    esp = esp + 0xC;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }
    _fa = (uint32_t)(esi) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp esi, 0 (32-bit) */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_EQ(_fa, _fb)) goto loc_0002959F; /* je: equal / zero */

loc_00029593: ;
    PUSH32(esp, ebp);
    PUSH32(esp, 0);
    PUSH32(esp, esi);
    PUSH32(esp, 0x0002959Cu); RECOMP_ABI_CALL(0x001ECF40u, sub_001ECF40); /* call 0x001ECF40 */

loc_0002959C: ;
    _fb = (uint32_t)(0xC) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    esp = esp + 0xC;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }

loc_0002959F: ;
    POP32(esp, edi);
    eax = esi;
    POP32(esp, esi);
    POP32(esp, ebp);
    POP32(esp, ebx);
    esp += 4; return; /* ret */

}

void sub_00029500(void)
{
    uint32_t caller = MEM32(g_esp), size = MEM32(g_esp + 8u);
    if (rollback_route_alloc()) return;
    sub_00029500_body();
    rollback_check_alloc(caller, size, g_eax);     /* tests: a block over a live loader's tables */
}
/**
 * sub_0011F0D0
 * Original: 0x0011F0D0 (cdecl (channel), the sound engine's stream channel stop: its ring flushed by sub_001E8600)
 *
 * Game objects call it directly (sub_000C0480, sub_000C04E0, sub_000BF8F0, ...), so rollback's re-run
 * steps flushed the music's ring about 20 times a match while the engine's player went on with it, and
 * the music director later crashed on a chunk already gone (sub_001317C0, five-match run 2026-10-25;
 * docs/research/stream-table-crash.md). In re-run steps on the game thread it does nothing: the sound
 * side is never rolled back, as with the engine's commands (netplay_sound_command). Kept as lifted below.
 */
extern void sub_0011FCE0(void);
extern void sub_0011FD00(void);
extern void sub_00122ED0(void);
extern void sub_00126370(void);
extern void sub_001263E0(void);
extern void sub_00126440(void);
extern void sub_00126970(void);
extern void sub_001E8600(void);
static void sub_0011F0D0_body(void)
{
    uint32_t ebp = 0;
    ebp = g_ebp;  /* frameless: caller's frame */
    int _flags = 0, _sf = 0, _of = 0, _pf = 0;
    unsigned _fv = 0; /* dynamic ZF/SF/OF/PF validity */
    uint32_t _fa = 0, _fb = 0;
    int32_t _fas = 0, _fbs = 0;
    (void)_fa; (void)_fb; (void)_fas; (void)_fbs;
    int _cf = 0; /* carry flag */
    ebp = g_seh_ebp; /* fpo_leaf: inherit caller's frame */

loc_0011F0D0: ;
    PUSH32(esp, esi);
    PUSH32(esp, 0x0011F0D6u); RECOMP_ABI_CALL(0x0011FCE0u, sub_0011FCE0); /* call 0x0011FCE0 */

loc_0011F0D6: ;
    ecx = ZX8(MEM8(0x3A9386));
    eax = MEM32(esp + 8);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(ecx) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, ecx (32-bit) */
    _cf = (int)(_fa < _fb);
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_GE(_fas, _fbs)) goto loc_0011F0F4; /* jge: greater or equal (signed >=) */

loc_0011F0E5: ;
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0 (32-bit) */
    _cf = 0; /* nothing borrows from zero */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_L(_fas, _fbs)) goto loc_0011F0F4; /* jl: less (signed <) */

loc_0011F0E9: ;
    esi = MEM32(eax * 4 + 0x3A92A0);
    _fa = (uint32_t)(esi) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp esi, 0 (32-bit) */
    _cf = 0; /* nothing borrows from zero */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_NE(_fa, _fb)) goto loc_0011F100; /* jne: not equal / not zero */

loc_0011F0F4: ;
    PUSH32(esp, 0x0011F0F9u); RECOMP_ABI_CALL(0x0011FD00u, sub_0011FD00); /* call 0x0011FD00 */

loc_0011F0F9: ;
    eax = 0xFFFFFFF8u;
    POP32(esp, esi);
    esp += 4; return; /* ret */

loc_0011F100: ;
    eax = MEM32(esi + 4);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0 (32-bit) */
    _cf = 0; /* nothing borrows from zero */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_L(_fas, _fbs)) goto loc_0011F113; /* jl: less (signed <) */

loc_0011F107: ;
    edx = MEM32(esi + 8);
    PUSH32(esp, edx);
    PUSH32(esp, 0x0011F110u); RECOMP_ABI_CALL(0x00126370u, sub_00126370); /* call 0x00126370 */

loc_0011F110: ;
    _fb = (uint32_t)(4) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    _cf = (int)((((uint64_t)(esp) + (uint64_t)(4)) >> 32) & 1);
    esp = esp + 4;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }

loc_0011F113: ;
    MEM32(esi + 4) = 0xFFFFFFFFu;
    SET_LO8(eax, MEM8(esi + 0x11));
    _fa = (uint32_t)(LO8(eax)) & 0xFFu; _fb = (uint32_t)(0) & 0xFFu;
    _fas = (int32_t)(int8_t)(_fa); _fbs = (int32_t)(int8_t)(_fb); /* cmp LO8(eax), 0 (8-bit) */
    _cf = 0; /* nothing borrows from zero */
    { uint32_t _r = (_fa - _fb) & 0xFFu; _flags = (_r == 0); _sf = ((_r & 0x80u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_NE(_fa, _fb)) goto loc_0011F12C; /* jne: not equal / not zero */

loc_0011F121: ;
    eax = MEM32(esi);
    PUSH32(esp, eax);
    PUSH32(esp, 0x0011F129u); RECOMP_ABI_CALL(0x001E8600u, sub_001E8600); /* call 0x001E8600 */

loc_0011F129: ;
    _fb = (uint32_t)(4) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    _cf = (int)((((uint64_t)(esp) + (uint64_t)(4)) >> 32) & 1);
    esp = esp + 4;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }

loc_0011F12C: ;
    eax = MEM32(esi + 0x9C);
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0 (32-bit) */
    _cf = 0; /* nothing borrows from zero */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    PUSH32(esp, ebp);
    ebp = esi + 0x9C;
    PUSH32(esp, edi);
    if (((_fv & 1u) == 1u && (_flags))) goto loc_0011F15F; /* je: published flags */

loc_0011F13E: ;
    SET_LO8(eax, MEM8(esi + 0x1A));
    _cf = 0; /* xor clears CF */
    edi = 0; /* xor self */
    _fa = (uint32_t)(edi) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* xor result */ _flags = (_fa == 0); _sf = (_fas < 0); _of = 0; _pf = RECOMP_PARITY8(_fa); _fv = 15;
    _fa = (uint32_t)(LO8(eax)) & 0xFFu; _fb = (uint32_t)(0) & 0xFFu;
    _fas = (int32_t)(int8_t)(_fa); _fbs = (int32_t)(int8_t)(_fb); /* cmp LO8(eax), 0 (8-bit) */
    _cf = 0; /* nothing borrows from zero */
    { uint32_t _r = (_fa - _fb) & 0xFFu; _flags = (_r == 0); _sf = ((_r & 0x80u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_BE(_fa, _fb)) goto loc_0011F15F; /* jbe: below or equal (unsigned <=) */

loc_0011F147: ;
    ecx = MEM32(ebp);
    PUSH32(esp, ecx);
    PUSH32(esp, 0x0011F150u); RECOMP_ABI_CALL(0x00122ED0u, sub_00122ED0); /* call 0x00122ED0 */

loc_0011F150: ;
    edx = ZX8(MEM8(esi + 0x1A));
    _fb = (uint32_t)(4) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    _cf = (int)((((uint64_t)(esp) + (uint64_t)(4)) >> 32) & 1);
    esp = esp + 4;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }
    edi++;
    _fa = (uint32_t)(edi) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fb = (_fa == 0x80000000u); /* inc result/SF/OF; CF unchanged */
    _flags = (_fa == 0); _sf = (_fas < 0); _of = (_fb != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15;
    _fb = (uint32_t)(4) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    _cf = (int)((((uint64_t)(ebp) + (uint64_t)(4)) >> 32) & 1);
    ebp = ebp + 4;
    _fa = (uint32_t)(ebp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }
    _fa = (uint32_t)(edi) & 0xFFFFFFFFu; _fb = (uint32_t)(edx) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp edi, edx (32-bit) */
    _cf = (int)(_fa < _fb);
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_L(_fas, _fbs)) goto loc_0011F147; /* jl: less (signed <) */

loc_0011F15F: ;
    edi = esi + 0x11C;

loc_0011F165: ;
    PUSH32(esp, edi);
    PUSH32(esp, 0x0011F16Bu); RECOMP_ABI_CALL(0x00126440u, sub_00126440); /* call 0x00126440 */

loc_0011F16B: ;
    _fb = (uint32_t)(4) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    _cf = (int)((((uint64_t)(esp) + (uint64_t)(4)) >> 32) & 1);
    esp = esp + 4;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fb = (uint32_t)(0) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp eax, 0 (32-bit) */
    _cf = 0; /* nothing borrows from zero */
    { uint32_t _r = (_fa - _fb) & 0xFFFFFFFFu; _flags = (_r == 0); _sf = ((_r & 0x80000000u) != 0); _of = (((_fa ^ _fb) & (_fa ^ _r) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_r); _fv = 15; }
    if (CMP_EQ(_fa, _fb)) goto loc_0011F184; /* je: equal / zero */

loc_0011F172: ;
    PUSH32(esp, eax);
    eax = esi + 0x128;
    PUSH32(esp, eax);
    PUSH32(esp, 0x0011F17Fu); RECOMP_ABI_CALL(0x001263E0u, sub_001263E0); /* call 0x001263E0 */

loc_0011F17F: ;
    _fb = (uint32_t)(8) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    _cf = (int)((((uint64_t)(esp) + (uint64_t)(8)) >> 32) & 1);
    esp = esp + 8;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }
    goto loc_0011F165;

loc_0011F184: ;
    ecx = esi + 0x14;
    PUSH32(esp, 4);
    PUSH32(esp, ecx);
    MEM32(esi + 0x134) = 0;
    MEM8(esi + 0x10) = 0;
    PUSH32(esp, 0x0011F19Du); RECOMP_ABI_CALL(0x00126970u, sub_00126970); /* call 0x00126970 */

loc_0011F19D: ;
    edx = esi + 0x18;
    PUSH32(esp, 4);
    PUSH32(esp, edx);
    PUSH32(esp, 0x0011F1A8u); RECOMP_ABI_CALL(0x00126970u, sub_00126970); /* call 0x00126970 */

loc_0011F1A8: ;
    eax = esi + 0x1C;
    PUSH32(esp, 0x68);
    PUSH32(esp, eax);
    PUSH32(esp, 0x0011F1B3u); RECOMP_ABI_CALL(0x00126970u, sub_00126970); /* call 0x00126970 */

loc_0011F1B3: ;
    PUSH32(esp, 0x68);
    _fb = (uint32_t)(0x84) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    _cf = (int)((((uint64_t)(esi) + (uint64_t)(0x84)) >> 32) & 1);
    esi = esi + 0x84;
    _fa = (uint32_t)(esi) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }
    PUSH32(esp, esi);
    PUSH32(esp, 0x0011F1C1u); RECOMP_ABI_CALL(0x00126970u, sub_00126970); /* call 0x00126970 */

loc_0011F1C1: ;
    _fb = (uint32_t)(0x20) & 0xFFFFFFFFu; _fbs = (int32_t)(int32_t)(_fb); /* add source, before the write */
    _cf = (int)((((uint64_t)(esp) + (uint64_t)(0x20)) >> 32) & 1);
    esp = esp + 0x20;
    _fa = (uint32_t)(esp) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* add result */ _flags = (_fa == 0); _sf = (_fas < 0); { uint32_t _a = (_fa - _fb) & 0xFFFFFFFFu; _of = ((~(_a ^ _fb) & (_a ^ _fa) & 0x80000000u) != 0); _pf = RECOMP_PARITY8(_fa); _fv = 15; }
    PUSH32(esp, 0x0011F1C9u); RECOMP_ABI_CALL(0x0011FD00u, sub_0011FD00); /* call 0x0011FD00 */

loc_0011F1C9: ;
    POP32(esp, edi);
    POP32(esp, ebp);
    _cf = 0; /* xor clears CF */
    eax = 0; /* xor self */
    _fa = (uint32_t)(eax) & 0xFFFFFFFFu; _fas = (int32_t)(int32_t)(_fa); /* xor result */ _flags = (_fa == 0); _sf = (_fas < 0); _of = 0; _pf = RECOMP_PARITY8(_fa); _fv = 15;
    POP32(esp, esi);
    esp += 4; return; /* ret */

}

void sub_0011F0D0(void)
{
    if (netplay_rerunning() && rollback_on_game_thread()) {
        static unsigned told;
        if (told++ < 5) fprintf(stderr, "[ROLLBACK] a stream stop in a re-run step: skipped\n");
        eax = 0;
        esp += 4;       /* ret */
        return;
    }
    sub_0011F0D0_body();
}

#undef eax
#undef ecx
#undef edx
#undef esp
#undef ebx
#undef esi
#undef edi

/* The screen camera's projection shape, written out by hand so it can answer
 * 16:9 (docs/research/widescreen.md). thiscall, ret 20:
 *   (mode, float *fov, float *aspect, float *top, float *height)
 * mode 0 is the console's 4:3 over the whole viewport and is the only one this
 * title asks for (one camera, at 0x80017200, built at start-up); mode 1 is a
 * 16:9 letterbox inside it (height less a quarter, moved down an eighth);
 * mode 2 takes the shape from the camera's own rectangle. With [display]
 * widescreen on, mode 0 answers 16:9 instead: the engine builds a wider
 * projection (and culls to it) into the same 640x480 target, and the present
 * pass shows that target at 16:9 -- anamorphic, as console widescreen was.
 * Everything else is what the lifted body did. Read once: a restart setting. */
void sub_0001A4E0(void)
{
    uint32_t cam = g_ecx, mode = MEM32(g_esp + 4);
    uint32_t p_fov = MEM32(g_esp + 8), p_aspect = MEM32(g_esp + 0xC);
    uint32_t p_top = MEM32(g_esp + 0x10), p_height = MEM32(g_esp + 0x14);
    int widescreen = pc_widescreen_latch();

    switch (mode) {
    case 0:
        MEM32(p_fov) = MEM32(cam + 0xC);
        MEM32(p_aspect) = widescreen ? 0x3FE38E39u /* 16/9 */ : 0x3FAAAAABu /* 4/3 */;
        MEM32(p_top) = MEM32(cam + 0x18);
        MEM32(p_height) = MEM32(cam + 0x20);
        break;
    case 1: {
        float h = MEMF(cam + 0x20), cut = h * MEMF(0x286D30);   /* 0.25: a quarter of the height */
        MEM32(p_fov) = MEM32(cam + 0xC);
        MEM32(p_aspect) = 0x3FE38E39u;
        MEMF(p_top) = MEMF(0x286D2C) * cut + MEMF(cam + 0x18);  /* 0.5: half of it above */
        MEMF(p_height) = h - cut;
        break;
    }
    case 2:
        MEM32(p_fov) = MEM32(cam + 0xC);
        MEMF(p_aspect) = MEMF(cam + 0x1C) / MEMF(cam + 0x20);
        MEM32(p_top) = MEM32(cam + 0x18);
        MEM32(p_height) = MEM32(cam + 0x20);
        break;
    default:
        break;
    }
    g_esp += 24;    /* ret 20 */
}

/* The music ring's refill (docs/research/music-ring-mixer.md). The menu music
 * plays from a 50 ms looping DirectSound ring (2,400 frames, [0x3A965C]); a
 * time-critical thread wakes 100 times a second, reads the write cursor and
 * fills to 20 ms past it ([0x3A9660] = 960 frames). On the console the voice
 * processor reads 32 frames at a time, so those 20 ms are all slack. Here it
 * reads a 256-frame period at once plus a resampler's look-ahead, and the
 * refill waits on the engine lock first; the slack left was 16 to 21 ms, a
 * late refill about once a second let playback lap the writer and play a
 * stretch of the previous lap -- the crackling in the music. Each refill makes
 * sure the lead is 35 ms. The fill still stops far short of a lap
 * (1,680 + 32 < 2,400), so nothing unplayed is overwritten; the cost is 15 ms
 * more latency on the music alone. RECOMP_MUSIC_LEAD_MS (20..40) overrides it;
 * 20 is the title's own value. */
void sub_001241A0_enter(void)
{
    static uint32_t lead;
    if (!lead) {
        const char *e = getenv("RECOMP_MUSIC_LEAD_MS");
        int ms = e && *e ? atoi(e) : 35;
        if (ms < 20) ms = 20;
        if (ms > 40) ms = 40;
        lead = (uint32_t)(48 * ms);
    }
    if (MEM32(0x3A965C) == 2400 && MEM32(0x3A9660) == 960)
        MEM32(0x3A9660) = lead;
}
void sub_001A6A80_enter(void)
{
    if (defjam_test_observations_enabled())
        defjam_test_result(MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_esp + 12));
}

/* â”€â”€ ICALL trace ring buffer â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* Upstream's lifter reports reached instructions it cannot translate. Keep
 * this visible in automated validation, with its optional first-hit trap. */
void recomp_unimpl(const char *text, uint32_t va)
{
    static volatile LONG printed;
    static volatile LONG known_printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';
    /* These instructions were already omitted in the accepted baseline
     * (TODO lines in its generated C). Keep them visible and separate from new
     * omissions; repeated IRQ/cache instructions must not exhaust that budget. */
    int known = !strcmp(text, "cli") || !strcmp(text, "sti") || !strcmp(text, "wbinvd");
    /* Exact PUSHFD/POPFD sites from the preserved baseline's lift summary and
     * generated TODOs. Other sites remain failures. Implementing their flags
     * and interrupt effects is a separate runtime project, not this migration. */
    if (!strcmp(text, "pushfd"))
        known = va == 0x00088500u || va == 0x0009F2C0u || va == 0x00130B46u
             || va == 0x002470B8u || va == 0x002470C4u;
    else if (!strcmp(text, "popfd"))
        known = va == 0x000330A4u || va == 0x000C3FE4u || va == 0x00130B50u
             || va == 0x002470C3u;
    LONG n = InterlockedIncrement(known ? &known_printed : &printed);
    if (n <= (known ? 10 : 50) || stop) {
        fprintf(stderr, "%s untranslated instruction REACHED: `%s` at 0x%08X\n",
                known ? "[UNIMPL-KNOWN]" : "[UNIMPL]", text, va);
        fflush(stderr);
    }
    if (stop) abort();
}

/* Keep the corrected name hash, with a read-only fallback for saves created
 * before upstream fixed SHRD at count zero. A complete replacement ensures
 * direct and indirect callers both use the compatibility check. */
void sub_001F8360(void)
{
    uint32_t name = MEM32(g_esp + 4);
    uint32_t destination = MEM32(g_esp + 8);
    uint32_t capacity = MEM32(g_esp + 12);
    char folder[13];
    int legacy = defjam_save_directory_name((const uint16_t *)XBOX_PTR(name), folder);
    if (capacity >= sizeof(folder)) {
        memcpy((void *)XBOX_PTR(destination), folder, sizeof(folder));
        g_eax = (unsigned char)folder[0];
    } else {
        g_eax = 0;
    }
    if (legacy) {
        static volatile LONG printed;
        if (InterlockedIncrement(&printed) <= 8)
            fprintf(stderr, "[SAVE-COMPAT] using verified existing folder %s\n", folder);
    }
    g_esp += 16; /* stdcall: return address and three arguments */
}

/* â”€â”€ Manual overrides for this title â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * memmove/memcpy, XDK 5849 CRT, guest 0x002016B0 (19 direct callers).
 *
 * The lifted version is wrong. MSVC's copy routine dispatches its
 * alignment and tail cases through jump tables embedded in the body
 * (`jmp dword ptr [edx*4 + 0x2017FC]` and friends). Function detection
 * ends the body at 0x00201952, but those tables' arms live at
 * 0x0020194C-0x002019D4, past that end -- so the lifter could not prove
 * they were intra-function, emitted an indirect call instead of a goto,
 * and at runtime the dispatch found no such function:
 *
 *   [ICALL] Failed to resolve VA 0x00201770 (total calls: 2485)
 *   [ICALL] Failed to resolve VA 0x00201954
 *
 * `rep movsd` still copied the aligned bulk, so most calls looked fine
 * and only the head/tail bytes were dropped. A pointer copied by one of
 * those calls came out half-written, which is where the null indirect
 * calls that followed came from.
 *
 * Reimplementing it natively is both the fix and a large speed-up; the
 * original is cdecl, returns the destination in eax, and handles
 * overlap, which is C memmove exactly. Callers must already treat
 * eax/ecx/edx as clobbered, so leaving ecx/edx alone is safe.
 */
void sub_002016B0(void)
{
    uint32_t dst = MEM32(g_esp + 4);
    uint32_t src = MEM32(g_esp + 8);
    uint32_t n   = MEM32(g_esp + 12);

    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), (size_t)n);

    g_eax = dst;
    g_esp += 4;          /* cdecl: consume the return address, caller pops args */
}

/* DirectSound's idle-voice lookup, XDK DSOUND section, guest 0x0025FB7C.
 *
 * When the audio chip traps to say a hardware voice has gone idle, the
 * interrupt routine calls this with the voice's id (thiscall: ecx = the voice
 * manager, one stack argument, `ret 4`). It looks the voice's client up in the
 * manager's 256-entry table at +0x88 and, if the client still owns that voice
 * and is on a list, has sub_002626B5 take it off the hardware voice list.
 *
 * The original does not test the table entry for null: on a console a trapped
 * voice always has a client, because the trap is serviced within microseconds.
 * Here the chip model's frame thread raises the trap and the routine runs about
 * a millisecond later, and the model lets a later method overwrite the trapped
 * voice id, so the entry can be empty by then. With a null client and voice id
 * 0 every test below passes on zero-page reads and sub_002626B5 is called with
 * this == NULL: the read fault at 0xFFFFFFBE seen once in three Debug runs of
 * the four-fighter Terrordome match (docs/research/dsound-voice-list-crash.md).
 *
 * This is the original's logic with that one test added. An empty entry means
 * there is no client to take off any list, so returning is what the original
 * does for every other "nothing to do" case. The chip model's trap handling is
 * the real fault and is still to be fixed; the count below says how often the
 * guard is what saved a run. */
extern void sub_002626B5(void);
void sub_0025FB7C(void)
{
    uint32_t voice = MEM32(g_esp + 4);
    uint32_t manager = g_ecx;

    g_eax = voice;
    if (voice < 0x100u && MEM32(manager + 0x84) == 0) {
        uint32_t voices = MEM32(0x27CCB0);
        g_edx = voice << 7;
        if (!(MEM32(g_edx + voices + 4) & 0x800000u)) {
            uint32_t client = MEM32(manager + voice * 4 + 0x88);
            g_ecx = client;
            if (!client) {
                static volatile LONG empty;
                LONG n = InterlockedIncrement(&empty);
                if (n <= 8 || (n & (n - 1)) == 0) {
                    fprintf(stderr, "[DSOUND] idle trap for voice %u with no client; skipped (%ld so far)\n",
                            voice, (long)n);
                    fflush(stderr);
                }
            } else {
                g_edx = MEM8(client + 0x64);
                g_edx = MEM16(client + g_edx * 2 + 0xA);
                if (voice == g_edx) {
                    g_eax = client + 0x4C;
                    if (MEM32(g_eax) != g_eax) {
                        PUSH32(g_esp, 1);
                        PUSH32(g_esp, 0x0025FBC9u);
                        sub_002626B5();
                    }
                }
            }
        }
    }
    g_esp += 8;          /* ret 4: the return address and the argument */
}

/* DirectSound's GP DSP mailbox, XDK DSOUND section, guest 0x0025F530.
 *
 * DirectSound talks to the APU's DSP through a command block in shared
 * memory, [[[this+8]+0x10]]+0x800: it spins until the status word at +0x10
 * is 0 (the DSP took the last command), writes the next one and sets the
 * status to 2. Nothing here runs a DSP, so the first command posted stays
 * at 2 and the second spins for ever. Def Jam posts on tearing a fight's
 * audio down: "No" to a rematch called Game.Quit() and the main thread hung
 * in this loop with the status word (0x831F8810 in that run) at 2
 * (PROGRESS Â§6 2026-09-28).
 *
 * The same writes, minus the wait, with the command marked taken: with no
 * DSP there is nothing it would have done. thiscall, two stack arguments:
 * a length or offset (used when the second is non-zero) and a count; with
 * the second 0 the pending pair in [this+0x18]/[this+0x1C] is posted,
 * unless there is none (0x8000 / 0).
 *
 * sub_0025F394 posts command 3 to the same block and waits the same way; it
 * has not been seen to run, so it is left alone. */
/* Direct3D's timer callback (0x00216410, D3DDevice_SetTimerCallback's body:
 * time in TSC ticks, callback, context), with one change: a time already
 * past is armed at once instead of refused.
 *
 * Def Jam's frame clock is a chain of these: the callback (0x000D7EA0, run in
 * Direct3D's PTIMER DPC) asks for the next frame at now + period - lateness
 * (0x000D7DE0). Delivered more than a frame late, that time has passed, the
 * original returns 0x8876082A without arming anything, nothing retries, and
 * the title's tick never runs again -- the screen keeps drawing while its
 * timeline stands still. On the console the DPC is never that late; here a
 * host stall during start-up's loading made it so in about one start in four
 * (the legal screen), once the PTIMER count stopped running away (0074).
 * Armed at once, the callback runs, the title's own lateness correction
 * catches up, and the chain continues. Otherwise identical: raise to
 * DISPATCH_LEVEL, hand the delay to 0x00224082 on the device's timer object,
 * lower, return S_OK. */
extern void sub_00224082(void);
extern uint64_t xbox_ReadTimeStampCounter(void);
extern unsigned char __stdcall xbox_KeRaiseIrqlToDpcLevel(void);
extern void __fastcall xbox_KfLowerIrql(unsigned char NewIrql);

void sub_00216410(void)
{
    uint32_t t_lo = MEM32(g_esp + 4), t_hi = MEM32(g_esp + 8);
    uint32_t cb = MEM32(g_esp + 12), ctx = MEM32(g_esp + 16);
    uint64_t delta = 0;
    unsigned char old = xbox_KeRaiseIrqlToDpcLevel();

    if (cb) {
        uint64_t target = ((uint64_t)t_hi << 32) | t_lo;
        uint64_t now = xbox_ReadTimeStampCounter();
        if (target <= now + 0x28) {
            static unsigned late;
            if (late++ < 8 || (late & (late - 1)) == 0)
                fprintf(stderr, "[TIMER] callback 0x%08X asked for a time %llu us past;"
                                " armed at once (#%u)\n", cb,
                        (unsigned long long)((now - target) / 733), late);
            target = now + 0x28;
        }
        delta = target - now;
    }
    PUSH32(g_esp, ctx);
    PUSH32(g_esp, cb);
    PUSH32(g_esp, (uint32_t)(delta >> 32));
    PUSH32(g_esp, (uint32_t)delta);
    g_ecx = MEM32(0x00234768u) + 0x1C28u;
    PUSH32(g_esp, 0x00216478u);
    sub_00224082();                 /* thiscall, pops its return and 0x10 */
    xbox_KfLowerIrql(old);
    g_eax = 0;
    g_esp += 4 + 0x10;              /* ret 0x10 */
}

void sub_0025F530(void)
{
    uint32_t self = g_ecx;
    uint32_t a1 = MEM32(g_esp + 4), a2 = MEM32(g_esp + 8);
    uint32_t len, count, box;

    if (a2 == 0) {
        len = MEM32(self + 0x18);
        count = MEM32(self + 0x1C);
        if (len == 0x8000 || count == 0)
            goto done;
    } else {
        len = a1;
        count = a2;
    }
    box = MEM32(MEM32(MEM32(self + 8) + 0x10)) + 0x800;
    MEM32(box) = (len >> 2) - MEM32(box + 4) - 0x206;
    MEM32(box + 8) = len;
    MEM32(box + 0xC) = count >> 2;
    MEM32(box + 0x10) = 0;          /* 2 on hardware: posted, until the DSP takes it */
    MEM32(self + 0x1C) = 0;
    MEM32(self + 0x18) = 0x8000;
    g_eax = box;
    g_edx = count >> 2;
done:
    g_esp += 12;         /* thiscall, ret 8 */
}

/* â”€â”€ Manual function overrides â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */


/* ---- A wrapper that does not wrap -------------------------------------
 *
 * Two attempts lived here: `_gen` wrappers on 0x00197650 and 0x0019B0C0, to
 * find out whether the front-end constructor ran and what its allocation
 * returned. Neither ever printed anything, and the reason is worth keeping.
 *
 * Declaring `extern void sub_X_gen(void);` does emit the real body under that
 * name -- and it also rewrites every direct call site to call `sub_X_gen`
 * instead. So a direct caller goes straight to the original body and the
 * wrapper is never on the path. It only intercepts calls that arrive through
 * recomp_lookup_manual(), which sees indirect calls only. The sleep trace
 * below works for exactly that reason: its target is reached through a
 * function pointer.
 *
 * To trace a function with direct callers, either replace it outright (define
 * it here without the `_gen` declaration and implement the behaviour), or put
 * a hardware watchpoint on something it writes -- src/hooks/watchpoint.c, and
 * scripts/guest-stack.py to read the call chain once it fires.
 */

/* ---- Who is sleeping? ------------------------------------------------
 *
 * The title reaches its loading screen and stays there, and the indirect-call
 * ring (scripts/icall-window.py) says the busiest thing it dispatches is
 * guest 0x000D1270, a two-instruction shim onto XAPI's Sleep. So game code is
 * running and it is waiting for something in a poll-and-sleep loop.
 *
 * Which code is the question this answers. The shim is reached through a
 * function pointer, so recomp_lookup_manual() can intercept it without the
 * lifter being involved at all: on entry the guest stack top is the caller's
 * return address, which names the call site exactly. Then the real body runs,
 * so behaviour is unchanged.
 *
 * Set RECOMP_TRACE_SLEEP to turn it on. Off by default: the interception costs
 * nothing, but the histogram is only meaningful while this question is open.
 */
extern void sub_000D1270(void);

static void trace_sleep_caller(void)
{
    enum { SLOTS = 24 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    uint32_t ret = MEM32(g_esp);
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == ret)
            break;
    if (i == count) {
        if (count < SLOTS) {
            seen[count] = ret;
            hits[count] = 0;
            count++;
        } else {
            sub_000D1270();
            return;
        }
    }
    hits[i]++;
    /* Report at 1, 10, 100, ... so a rare caller shows up once and a loop
     * shows up as a progression, without a line per sleep. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n == 1)
            fprintf(stderr, "[SLEEP] called from 0x%08X -- %llu time(s)\n",
                    ret, (unsigned long long)hits[i]);
        fflush(stderr);
    }
    sub_000D1270();
}

/* ---- The front end's calls into the game --------------------------------
 *
 * Each screen's ActionScript reaches the game through FuncCall, which hands a
 * string such as "Controller.ControllerBind(0)" or
 * "Control.GetScreenInfo(intMain/mainMenu.swf)" to GetURL, and the game's
 * handler, guest 0x00068BA0, dispatches it by module and method
 * (sub_000457D0). That handler is reached through a function pointer only, so
 * it can be wrapped here. Each call is printed as [FUNCCALL] -- the front
 * end's own account of what it is doing, a few a second at most -- and
 * offered to the script anchors, so a pad script can wait for a screen:
 * "@getscreeninfo(intmain/mainmenu" (matched without case).
 */
extern void sub_00068BA0(void);
extern void xbox_NoteAnchorEvent(const char *text);

static void funccall_hook(void)
{
    static long printed;
    char text[192];
    uint32_t p = MEM32(g_esp + 4);
    int i;

    for (i = 0; p && i < (int)sizeof text - 1; i++) {
        char c = (char)MEM8(p + (uint32_t)i);
        if (!c)
            break;
        text[i] = c;
    }
    text[i] = 0;
    /* Online.Tick() comes every frame from the patched main menu (scripts/apt-patch.py). */
    /* the menu-list calls repeat a dozen times a screen; 4000 in all ran out early in a long online
     * session, and the end of a session is where a report needs them */
    if (strcmp(text, "Online.Tick()") && strcmp(text, "Game.GetNextMatchType()") && printed++ < 20000)
        fprintf(stderr, "[FUNCCALL] %s\n", text);
    xbox_NoteAnchorEvent(text);
    if (automatch_hold_funccall(text)) {    /* online: a rematch the others have not chosen yet */
        g_eax = 0;
        g_esp += 4;                         /* cdecl: only the return address is popped */
        return;
    }
    sub_00068BA0();
    automatch_on_funccall(text);    /* a requested match starts here (src/hooks/automatch.c) */
}

/* The fight's "first time help" cards (BLAZIN', weapon, KO): the message-bus handler
 * sub_000D6670(msg, data) takes the request that later pauses the fight and shows a card
 * (docs/research/fight-tips.md). Whether a card comes depends on the player's save, so in an
 * online match one PC would pause and the other not, and the lockstep would stop: while
 * netplay is active the request is dropped (cdecl: only the return address is popped). */
extern void sub_000D6670(void);

static void first_time_help_hook(void)
{
    if (netplay_active()) {
        static int told;
        if (!told++) { fprintf(stderr, "[NETPLAY] a first-time help card was skipped (online match)\n"); fflush(stderr); }
        g_esp += 4;
        return;
    }
    sub_000D6670();
}

/* One front-end command, as the Flash side would send it, on the game thread (the FuncCall
 * hook above is the only caller, through automatch). cdecl: the argument and a dummy return
 * address are pushed below the current stack, the string above the callee's frame. Returns the
 * guest address of the result string, or 0. */
unsigned int defjam_fe_call(const char *command)
{
    uint32_t sp0 = g_esp, n = (uint32_t)strlen(command) + 1, s, i;
    s = (sp0 - 0x40u - n) & ~3u;
    for (i = 0; i < n; i++)
        MEM8(s + i) = (uint8_t)command[i];
    g_esp = s;
    PUSH32(g_esp, s);
    PUSH32(g_esp, 0x00000000u);
    sub_00068BA0();
    g_esp = sp0;
    return g_eax;
}

/* ---- Trace chosen functions, by guest address --------------------------
 *
 * The generalisation of the sleep trace below. Set

 *     RECOMP_TRACE_ICALL=0x000D7060,0x0019AB60
 *
 * and every call to one of those addresses is reported -- the first one, then
 * at ten, a hundred, a thousand, so a function called once and a function
 * called in a loop read differently without a line per call. The caller is
 * reported too: on entry the guest stack top is the return address.
 *
 * This only sees calls that arrive indirectly, through a vtable or a function
 * pointer, because that is all recomp_lookup_manual() is asked about. That
 * covers more than it sounds like: a title's per-frame work and all of its
 * state machines go through vtables, and a direct call compiles to an ordinary
 * C call that nothing can intercept without re-lifting.
 *
 * Answering "does this ever run at all" is what it is for. That question came
 * up for the intro sequence at 0x000D7060, which plays movies/eagames,
 * movies/thx and movies/intro from a six-state switch -- and none of those
 * files is ever opened.
 */

#define TRACE_SLOTS 8

static uint32_t s_trace_va[TRACE_SLOTS];
static uint64_t s_trace_hits[TRACE_SLOTS];
static int      s_trace_count = -1;

static void trace_parse(void)
{
    const char *list = getenv("RECOMP_TRACE_ICALL");
    s_trace_count = 0;
    if (!list || !*list)
        return;
    while (*list && s_trace_count < TRACE_SLOTS) {
        char *end;
        unsigned long v = strtoul(list, &end, 0);
        if (end == list)
            break;
        s_trace_va[s_trace_count++] = (uint32_t)v;
        list = (*end == ',' || *end == ' ') ? end + 1 : end;
    }
    fprintf(stderr, "[TRACE] watching %d indirect target(s)\n", s_trace_count);
    fflush(stderr);
}

static void trace_report(int slot)
{
    uint64_t n = ++s_trace_hits[slot];
    uint64_t m = n;

    while (m >= 10 && m % 10 == 0)
        m /= 10;
    if (m != 1)
        return;
    fprintf(stderr, "[TRACE] 0x%08X called from 0x%08X -- %llu time(s)\n",
            s_trace_va[slot], MEM32(g_esp), (unsigned long long)n);
    fflush(stderr);
}

/* One trampoline per slot, because a C function pointer carries no state and
 * the dispatcher hands the target nothing to identify itself with. Eight is
 * more than any one question has needed. */
extern recomp_func_t recomp_lookup(uint32_t xbox_va);

/* recomp_lookup() is the generated table and does not consult this file, so
 * calling it here dispatches the real body rather than recursing. */
static void trace_forward(int slot)
{
    recomp_func_t fn = recomp_lookup(s_trace_va[slot]);
    if (fn)
        fn();
}

static void trace_slot_0(void) { trace_report(0); trace_forward(0); }
static void trace_slot_1(void) { trace_report(1); trace_forward(1); }
static void trace_slot_2(void) { trace_report(2); trace_forward(2); }
static void trace_slot_3(void) { trace_report(3); trace_forward(3); }
static void trace_slot_4(void) { trace_report(4); trace_forward(4); }
static void trace_slot_5(void) { trace_report(5); trace_forward(5); }
static void trace_slot_6(void) { trace_report(6); trace_forward(6); }
static void trace_slot_7(void) { trace_report(7); trace_forward(7); }

static const recomp_func_t s_trace_slots[TRACE_SLOTS] = {
    trace_slot_0, trace_slot_1, trace_slot_2, trace_slot_3,
    trace_slot_4, trace_slot_5, trace_slot_6, trace_slot_7,
};

/* RECOMP_TRACE_VOICE=1: EA's software voice render (0x0012C310, reached
 * through the voice object's vtable) -- its history flag, pitch and saved
 * history on entry, what it returned and the first samples it wrote. Written
 * to find why every 512-sample block of the six-channel mix began with four
 * zero samples. */
extern recomp_func_t recomp_lookup(uint32_t xbox_va);
static void trace_voice_render(void)
{
    static unsigned shown;
    static uint64_t first;
    uint32_t st = MEM32(g_esp + 4), n = MEM32(g_esp + 8);
    uint32_t dec = MEM32(g_esp + 12), out = MEM32(g_esp + 16);
    uint16_t flag = MEM16(st + 0x28), pad = MEM16(st + 0x2A);
    uint32_t pitch = MEM32(st + 0x1C);
    float h[4];
    uint64_t now = GetTickCount64();
    int show;
    recomp_func_t fn = recomp_lookup(0x0012C310u);
    memcpy(h, (const void *)XBOX_PTR(st + 0x2C), sizeof h);
    if (!first)
        first = now;
    show = now - first > 30000 && shown < 60;
    if (fn)
        fn();
    if (show) {
        float o[6];
        memcpy(o, (const void *)XBOX_PTR(out), sizeof o);
        shown++;
        fprintf(stderr, "[VOICE] st %08X n %u pitch %08X flag %u pad %u hist %g %g %g %g"
                        " -> %u; out %g %g %g %g %g %g (dec %08X out %08X) resampler %08X pos %08X\n",
                st, n, pitch, flag, pad, h[0], h[1], h[2], h[3], g_eax,
                o[0], o[1], o[2], o[3], o[4], o[5], dec, out, MEM32(st + 0x24), MEM32(st + 0x20));
    }
}

/* The mixer's "packet retired" callback (type 1 in the deferred queue 0x3A9680, run by sub_00126300):
 * chunk = [param - 4], channel = [chunk] & 0xFF, then sub_001E8340 on 0x3A92A0[channel]. A chunk released
 * already ([chunk] = 0xFFFFFFFE) gave channel 0xFE, whose table slot is the queue itself, and the stream layer
 * read audio samples as an object: the mixer's crash in sub_001E8340 at track changes in rollback tests
 * (docs/research/stream-table-crash.md). Such an event, or one for a channel that is not there, is dropped,
 * as sub_001E8340 itself drops a released chunk. cdecl. */
extern void sub_0011EC80(void);
static void stream_retire_guard(void)
{
    uint32_t p = MEM32(g_esp + 4u);
    if (p >= 0x80000004u && p < 0x84000000u) {
        uint32_t chunk = MEM32(p - 4u);
        if (chunk >= 0x80000000u && chunk < 0x84000000u) {
            uint32_t head = MEM32(chunk), id = head & 0xFFu;
            if (head == 0xFFFFFFFEu || id >= MEM8(0x003A9386u) || !MEM32(0x003A92A0u + id * 4u)) {
                static unsigned told;
                if (told++ < 20)
                    fprintf(stderr, "[AUDIO] a retired stream packet names chunk %08X = %08X: dropped (rollback re-run %d)\n",
                            chunk, head, netplay_rerunning());
                g_esp += 4;     /* ret */
                return;
            }
        }
    }
    sub_0011EC80();
}

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    if (xbox_va == 0x0012C310u && getenv("RECOMP_TRACE_VOICE"))
        return trace_voice_render;
    /* This hook sees indirect calls only, so it suits a trace wrapper or a
     * conditional override. A function with direct callers is replaced
     * instead by defining it in this file: the recompiler reads this file and
     * stops generating any body it finds here. */
    int i;

    if (xbox_va == 0x000D1270u && getenv("RECOMP_TRACE_SLEEP"))
        return trace_sleep_caller;
    if (xbox_va == 0x00068BA0u)
        return funccall_hook;
    if (xbox_va == 0x000D6670u)
        return first_time_help_hook;
    if (xbox_va == 0x00081170u)         /* the crowd's update: its own generator copy online */
        return netplay_crowd_update;
    if (xbox_va == 0x0011EC80u)         /* the mixer's stream packet callback: released chunks */
        return stream_retire_guard;
    if (xbox_va == 0x00137010u)         /* the sound engine's commands: silent while steps re-run */
        return netplay_sound_command;
    if (netplay_sound_handler(xbox_va))  /* and the audio subsystem's bus handlers */
        return (recomp_func_t)netplay_sound_handler(xbox_va);
    if (xbox_va == 0x0001DC10u)         /* the sound engine's allocator: its own arena (rollback.c) */
        return rollback_sound_alloc;
    if (xbox_va == 0x0001DC20u)
        return rollback_sound_free;
    if (xbox_va == 0x0002A180u)         /* the game heap's free (vtable 0x28BB14 +8): arena blocks */
        return rollback_heap_free;
    if (xbox_va == 0x0011FCC0u)         /* the mixer's tick: the pages it writes (rollback.c) */
        return rollback_mixer_lock_a;
    if (xbox_va == 0x00121C80u)
        return rollback_mixer_lock_b;
    if (xbox_va == 0x0011FCD0u)
        return rollback_mixer_unlock_a;
    if (xbox_va == 0x00121C90u)
        return rollback_mixer_unlock_b;

    if (s_trace_count < 0)
        trace_parse();
    for (i = 0; i < s_trace_count; i++)
        if (s_trace_va[i] == xbox_va)
            return s_trace_slots[i];

    return (recomp_func_t)0;
}

/* â”€â”€ ICALL failure logging â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
extern ptrdiff_t g_xbox_mem_offset;
#if defined(_MSC_VER)
extern __declspec(thread) uint32_t g_esp;
extern __declspec(thread) uint32_t g_ecx;
#else
extern __thread uint32_t g_esp;
extern __thread uint32_t g_ecx;
#endif

/* Set by RECOMP_ITAIL_AT around a tail jump, zero otherwise. */
#ifndef RECOMP_ITAIL_RUNTIME_OWNED
uint32_t g_itail_site;
#endif

void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    if (g_itail_site) {
        /* A tail jump: no return address was pushed, so the stack says
         * nothing. The lifter recorded the jump site instead, and for a
         * switch that is the dispatch whose table was read out of
         * range. */
        fprintf(stderr, "  tail jump at guest 0x%08X (a switch dispatch; "
                        "its jump table read out of range)\n",
                g_itail_site);
        fflush(stderr);
        return;
    }

    /* Where the call was made from, which the target alone never says.
     *
     * The lifter pushes the guest return address immediately before an
     * indirect call, so it is the word at the top of the guest stack when
     * this runs. A target of 0x000000FF is plainly not an address, and the
     * only useful question about it is which call site read a byte where a
     * function pointer belonged. Printing the return address answers that
     * directly: it symbolizes straight back to the caller through the
     * lifted source. */
    if (g_esp) {
        uint32_t ret = *(volatile uint32_t *)((uintptr_t)g_esp
                                            + (uintptr_t)g_xbox_mem_offset);
        fprintf(stderr, "  called from guest 0x%08X (esp 0x%08X)\n", ret, g_esp);
    }

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);

    /* And where from. RECOMP_ICALL_SAFE calls this before restoring the
     * stack, so the guest return address the lifter pushed for the call is
     * still the top word -- which turns "something called a null pointer"
     * into a line of lifted C. */
    if (g_esp) {
        uint32_t ret = *(volatile uint32_t *)((uintptr_t)g_esp
                                            + (uintptr_t)g_xbox_mem_offset);
        fprintf(stderr, "  called from guest 0x%08X\n", ret);
        /* And the object, since most of these are virtual calls: ecx is
         * the this pointer and the first word of it is the vtable. A null
         * slot in a real vtable and a null vtable on a half-built object
         * look identical from the call site and want different fixes. */
        fprintf(stderr, "  this 0x%08X\n", g_ecx);
        /* The stack too. A vtable thunk has no direct callers to grep for,
         * so the only way to learn who invoked it with a null object is to
         * read the frames above it while they are still there. A scan, so
         * some entries are stale -- scripts/guest-stack.py filters the same
         * shape against the real call sites. */
        {
            int k, shown = 0;
            for (k = 0; k < 256 && shown < 16; k++) {
                uint32_t slot = g_esp + (uint32_t)(k * 4);
                uint32_t v;
                if (slot < 0x1000u || slot >= 0x04000000u) break;
                v = *(volatile uint32_t *)((uintptr_t)slot
                                         + (uintptr_t)g_xbox_mem_offset);
                if (v >= 0x00010000u && v < 0x00400000u) {
                    fprintf(stderr, "    [esp+%-3d] 0x%08X\n", k * 4, v);
                    shown++;
                }
            }
        }
        if (g_ecx >= 0x1000u && g_ecx < 0x84000000u) {
            uint32_t vt = *(volatile uint32_t *)((uintptr_t)g_ecx
                                               + (uintptr_t)g_xbox_mem_offset);
            fprintf(stderr, "  this 0x%08X vtable 0x%08X\n", g_ecx, vt);
        }
    }
    fflush(stderr);
}


/*
 * Direct3D's fence insert (sub_0021EB90), entered: hold the calling thread
 * while the push-buffer executor is far behind.
 *
 * Direct3D numbers a fence every time it kicks the push buffer (the counter
 * at device+0x2C, in steps of two) and the GPU reports the last one it
 * reached through the semaphore the device's +0x30 points at. Its free-space
 * arithmetic finds the GPU's position from that fence in a small table, which
 * holds on a console because the GPU is never more than a few fences behind.
 * The optimised build submits faster than the executor draws: at 24 to 40
 * fence numbers of lag Direct3D judged space free that was not, wrote the next
 * lap over commands still to be run ("[PB] D3D overran the executor"), the
 * executor read the new data as commands, and the title died in its own
 * interrupt-time fixup a moment later. A debug build idles at a lag of 2 and
 * peaks around 12.
 *
 * So this waits, yielding, until the lag is back under 16 or 100 ms have
 * passed (the executor may itself be waiting on this thread).
 * RECOMP_FENCE_HOLD=0 turns it off.
 */
void sub_0021EB90_enter(void)
{
    static int on = -1;
    static LARGE_INTEGER freq;
    static unsigned holds, timeouts;
    uint32_t dev, sem;
    LARGE_INTEGER t0, t1;

    if (on < 0) {
        const char *e = getenv("RECOMP_FENCE_HOLD");
        on = !(e && *e == '0');
        QueryPerformanceFrequency(&freq);
    }
    if (!on)
        return;
    dev = MEM32(0x234768);
    if (dev < 0x10000u)
        return;
    sem = MEM32(dev + 0x30);
    if (sem < 0x10000u || (uint32_t)(MEM32(dev + 0x2C) - MEM32(sem)) < 16u)
        return;
    QueryPerformanceCounter(&t0);
    for (;;) {
        SwitchToThread();
        if ((uint32_t)(MEM32(dev + 0x2C) - MEM32(sem)) < 16u)
            break;
        QueryPerformanceCounter(&t1);
        if ((t1.QuadPart - t0.QuadPart) * 10 > freq.QuadPart) {
            timeouts++;
            break;
        }
    }
    holds++;
    if (holds <= 5 || (holds & (holds - 1)) == 0)
        fprintf(stderr, "[FENCE] held the title for the executor (#%u, %u timed out)\n",
                holds, timeouts);
}

/*
 * The screen manager (registry slot 0x17, docs/research/screen-swap-freeze.md). Its loader thread
 * (sub_000771E0) loads the pending screen (+0x1C) and sets the state (+0x24) to 3; the main loop's
 * sub_00077040 then swaps it in: the pending screen becomes current (+0x18) and the old one is
 * deleted. When a second request woke the loader once more, it set 3 again with nothing pending,
 * and the swap deleted the live screen and stored 0: nothing was drawn any more and the next match
 * never loaded (two-copy tests and players: "a freeze after a few matches, the opponent stops
 * answering"). A state of 3 with nothing pending is put back to 0, as after a finished swap.
 */
void sub_00077040_enter(void)
{
    static unsigned fixed;
    uint32_t m = g_ecx;
    if (m < 0x10000u || MEM32(m + 0x24) != 3 || MEM32(m + 0x1C) != 0)
        return;
    MEM32(m + 0x24) = 0;
    if (fixed++ < 20)
        fprintf(stderr, "[SCREEN] a swap with no screen loaded was skipped (current %08X, #%u)\n",
                MEM32(m + 0x18), fixed);
}

/* sub_000773A0(kind): a new screen requested. Logged when one arrives while another is still loading
 * (state not 0): the second requester the stray swap above follows. */
void sub_000773A0_enter(void)
{
    static unsigned told;
    uint32_t m = g_ecx;
    if (m < 0x10000u || MEM32(m + 0x24) == 0 || told >= 40)
        return;
    told++;
    fprintf(stderr, "[SCREEN] screen %d requested from %08X while state %u (pending %08X, current %08X)\n",
            (int)MEM32(g_esp + 4), MEM32(g_esp), MEM32(m + 0x24), MEM32(m + 0x1C), MEM32(m + 0x18));
}

/*
 * The display's gamma ramp.
 *
 * Direct3D's SetGammaRamp ends in sub_002203C6(this, ramp): 256 bytes each of
 * red, green and blue, written one at a time to the RAMDAC's palette port
 * (0xFD6813C9). That page is plain memory here, so the ramp went nowhere. The
 * title sets one at start-up and it is not the identity: an S curve (32 -> 18,
 * 64 -> 52, 128 -> 125, 192 -> 198, 224 -> 233) that deepens the shadows,
 * which is why everything looked lighter than the console. The runtime's
 * Direct3D layer already applies a ramp at presentation (d3d8_gamma.c); this
 * hands it the title's. g_recomp_gamma is the same ramp for captures, which
 * are read before presentation. RECOMP_GAMMA=0 leaves the picture as drawn.
 */
uint8_t g_recomp_gamma[3][256];
int g_recomp_gamma_on;

void sub_002203C6_enter(void)
{
    struct { uint16_t c[3][256]; } ramp;
    extern void d3d8_gamma_set(const void *ramp);
    static int on = -1;
    static unsigned sets;
    uint32_t src = MEM32(g_esp + 4);
    int c, i;

    if (on < 0) {
        const char *e = getenv("RECOMP_GAMMA");
        on = !(e && *e == '0');
    }
    if (!on || src < 0x10000u)
        return;
    for (c = 0; c < 3; c++)
        for (i = 0; i < 256; i++) {
            uint8_t v = MEM8(src + c * 256 + i);
            g_recomp_gamma[c][i] = v;
            ramp.c[c][i] = (uint16_t)(v * 257);
        }
    g_recomp_gamma_on = 1;
    d3d8_gamma_set(&ramp);
    if (sets++ < 4)
        fprintf(stderr, "[GAMMA] ramp set: 32 -> %u, 64 -> %u, 128 -> %u, 192 -> %u, 224 -> %u\n",
                g_recomp_gamma[0][32], g_recomp_gamma[0][64], g_recomp_gamma[0][128],
                g_recomp_gamma[0][192], g_recomp_gamma[0][224]);
}

/* XAPI's heap free (ptr), stdcall ret 4: RtlFreeHeap(the process heap at [0x3CD1D0], 0, ptr); returns 0 when
 * freed, else ptr. Hand-written so a rollback fight can hold the game thread's frees: that heap (Direct3D's
 * resource headers among it) is outside the save states, so a load brought back game state that still held a
 * texture or buffer it had released, the game released it again, and the heap's lists broke (the game thread
 * looping in RtlAllocateHeap, sub_001FE621, early in the second match of two five-match runs, 2026-10-25). */
extern void sub_001FF616(void);
void defjam_xapi_free_now(uint32_t ptr)
{
    g_esp -= 4; MEM32(g_esp) = ptr;
    g_esp -= 4; MEM32(g_esp) = 0;
    g_esp -= 4; MEM32(g_esp) = MEM32(0x003CD1D0u);
    g_esp -= 4; MEM32(g_esp) = 0x001FBE2Fu;          /* the return address the original pushes */
    sub_001FF616();                                     /* stdcall: pops it and the three arguments */
    g_eax = (g_eax & 0xFFu) ? 0 : ptr;
}

void sub_001FBE1E(void)
{
    uint32_t ptr = MEM32(g_esp + 4);
    if (rollback_hold_xapi_free(ptr)) g_eax = 0;
    else defjam_xapi_free_now(ptr);
    g_esp += 8;                                         /* ret 4 */
}
