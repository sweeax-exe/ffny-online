/* Issue #161 / PR #164: the retail renderer substitutes 1/30 s when an SC04
 * shot has less than 1/120 s pending. A rendered frame can have zero simulation
 * steps on the host; the substitution advances video without advancing audio.
 *
 * Suppress only the zero-delta case. The generated body still performs its
 * normal shot cleanup, selector dispatch and drawing preparation. Its threshold
 * has exactly one reader in the verified image, this cinema-render function.
 * Temporarily lowering it for this call lets the existing zero-delta guard run,
 * then restores it. Positive deltas retain the retail behavior, including the
 * positive sub-1/120 fallback; other scenes never change the threshold.
 *
 * Cleanup before the zero-delta guard can invoke callbacks. Serialize access
 * with a recursive lock and save/restore the threshold per invocation: a nested
 * positive-delta render must see the retail threshold even inside a guarded
 * zero-delta render. No other subsystem reads this constant. Never leave a
 * global threshold-zero patch installed at startup.
 * RECOMP_SC04_ZERO_DT_FIX=0 restores the original behavior for A/B diagnostics.
 * RECOMP_SC04_TIMING_TRACE=1 reports a bounded running suppression count.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp/gen/recomp_types.h"
#include "cinema_timing.h"

#ifdef _WIN32
#include <windows.h>
static INIT_ONCE s_lock_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_lock;
static BOOL CALLBACK initialize_lock(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once; (void)parameter; (void)context;
    InitializeCriticalSection(&s_lock);
    return TRUE;
}
static void lock_render(void)
{
    InitOnceExecuteOnce(&s_lock_once, initialize_lock, NULL, NULL);
    EnterCriticalSection(&s_lock);
}
static void unlock_render(void) { LeaveCriticalSection(&s_lock); }
#else
#include <pthread.h>
static pthread_once_t s_lock_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t s_lock;
static void initialize_lock(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_lock, &attr);
    pthread_mutexattr_destroy(&attr);
}
static void lock_render(void)
{
    pthread_once(&s_lock_once, initialize_lock);
    pthread_mutex_lock(&s_lock);
}
static void unlock_render(void) { pthread_mutex_unlock(&s_lock); }
#endif

#define CINEMA_MIN_DELTA 0x0029D24Cu
#define RETAIL_MIN_BITS  0x3C088889u
#define SHOT_STRIDE      0xCCu
static unsigned s_managed_depth; /* protected by the recursive renderer lock */

static int enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *value = getenv("RECOMP_SC04_ZERO_DT_FIX");
        on = !value || *value != '0';
    }
    return on;
}

static int sc04_zero_delta(uint32_t player)
{
    uint32_t index, count, shots;
    if (!player || !(MEM8(player) & 1) || MEM8(player + 0x984))
        return 0;
    /* Both signs of zero are zero; preserve every nonzero delta bit-for-bit. */
    if ((MEM32(player + 0x764) & 0x7FFFFFFFu) != 0)
        return 0;
    shots = MEM32(player + 0xC4);
    index = MEM32(player + 0xCC);
    count = MEM32(player + 0xC8);
    return shots && index < count &&
           MEM32(shots + index * SHOT_STRIDE) == 0x34306373u; /* inline "sc04" */
}

void defjam_cinema_render(void (*original)(void))
{
    uint32_t saved;
    int known, suppress;
    lock_render();
    saved = MEM32(CINEMA_MIN_DELTA);
    known = saved == RETAIL_MIN_BITS || (s_managed_depth && saved == 0);
    suppress = enabled() && known && sc04_zero_delta(g_ecx);
    if (known) {
        MEM32(CINEMA_MIN_DELTA) = suppress ? 0 : RETAIL_MIN_BITS;
        s_managed_depth++;
    }
    original();
    if (known) {
        s_managed_depth--;
        MEM32(CINEMA_MIN_DELTA) = saved;
    }
    if (suppress) {
        static unsigned suppressed;
        static int trace = -1;
        if (trace < 0) {
            const char *value = getenv("RECOMP_SC04_TIMING_TRACE");
            trace = value && *value == '1';
        }
        if (trace && (++suppressed <= 4 || (suppressed & (suppressed - 1)) == 0))
            fprintf(stderr, "[CINEMA-TIME] SC04 zero-delta fallback suppressed (%u so far)\n", suppressed);
    }
    unlock_render();
}
