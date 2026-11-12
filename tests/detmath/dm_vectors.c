/* SPDX-License-Identifier: MIT
 *
 * The deterministic math layer's cross-architecture check (tests/detmath/check.py runs it).
 *
 * Runs every dm_* function over fixed pseudo-random input sets (and, with a file argument, over
 * the operands a game run recorded with RECOMP_DETMATH_TRACE) and prints one FNV-1a 64 hash of
 * the result bits per function and set. Two builds agree bit for bit exactly when every line
 * matches. Builds:
 *   hosted (Windows MSVC / clang-cl, or any libc): main(); with --host it also compares each
 *     result with the host libm's and prints how many differ and by how many ulps;
 *   DM_FREESTANDING (Linux x86-64 / arm64 static ELF, no libc): its own _start and syscalls, so
 *     a cross-compiled binary runs under qemu-user with nothing else installed.
 */
#include <stdint.h>
#include "../../src/detmath/detmath.h"

#ifndef DM_N
#define DM_N 1000000            /* inputs per function and set */
#endif

/* ---- output and files: libc or raw syscalls -------------------------------------------------- */

#ifdef DM_FREESTANDING
static long sys3(long n, long a, long b, long c)
{
    long ret;
#if defined(__aarch64__)
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory", "cc");
    ret = x0;
#elif defined(__x86_64__)
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory", "cc");
#else
#error "freestanding build: aarch64 or x86-64 Linux only"
#endif
    return ret;
}
#if defined(__aarch64__)
#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_OPENAT 56
#define SYS_EXIT 94
#else
#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPENAT 257
#define SYS_EXIT 60
#endif
void *memset(void *d, int c, unsigned long n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
static void out(const char *s) { long n = 0; while (s[n]) n++; sys3(SYS_WRITE, 1, (long)s, n); }
static long file_open(const char *path) { return sys3(SYS_OPENAT, -100 /* AT_FDCWD */, (long)path, 0); }
static long file_read(long fd, void *buf, long n) { return sys3(SYS_READ, fd, (long)buf, n); }
#else
#include <stdio.h>
#include <string.h>
#include <math.h>
static void out(const char *s) { fputs(s, stdout); fflush(stdout); }
static FILE *g_files[4];
static long file_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    g_files[0] = f;
    return 0;
}
static long file_read(long fd, void *buf, long n) { return (long)fread(buf, 1, (size_t)n, g_files[fd]); }
#endif

static void out_u64(uint64_t v, int hex)
{
    char b[24];
    int i = 23;
    b[i] = 0;
    if (hex) { int k; for (k = 0; k < 16; k++) { b[--i] = "0123456789abcdef"[v & 15]; v >>= 4; } }
    else { do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); }
    out(b + i);
}

/* ---- inputs ------------------------------------------------------------------------------------ */

typedef union { double d; uint64_t u; } bits;
static double asd(uint64_t u) { bits b; b.u = u; return b.d; }
static uint64_t asu(double d) { bits b; b.d = d; return b.u; }

static uint64_t g_rng;
static uint64_t rnd(void)                       /* xorshift64* */
{
    g_rng ^= g_rng >> 12; g_rng ^= g_rng << 25; g_rng ^= g_rng >> 27;
    return g_rng * 0x2545F4914F6CDD1Dull;
}
/* uniform in [0, 1) with 53 bits, from integers only */
static double unit(void) { return (double)(rnd() >> 11) * (1.0 / 9007199254740992.0); }

enum { SET_BITS, SET_TURN, SET_GAME, SET_WIDE, SET_HUGE, SET_NEAR, SET_COUNT };
static const char *set_name[SET_COUNT] = {"any-bits", "|x|<2pi", "|x|<1e3", "|x|<1e9", "|x|<2^70", "near-k*pi/2"};

/* x for set s; y (atan2's x, and the second operand where one exists) from the same set */
static double gen(int s)
{
    double sign = (rnd() & 1) ? -1.0 : 1.0;
    switch (s) {
    case SET_BITS: return asd(rnd());           /* every encoding: NaN, inf, subnormal, all exponents */
    case SET_TURN: return sign * unit() * 6.283185307179586;
    case SET_GAME: return sign * unit() * 1000.0;
    case SET_WIDE: return sign * unit() * 1e9;
    case SET_HUGE: { int e = (int)(rnd() % 70); return sign * (1.0 + unit()) * asd((uint64_t)(1023 + e) << 52); }
    default: {      /* within a few ulps of k * pi/2 (the reduction's hard cases) */
        double k = (double)(rnd() % 2000000) + 1.0;
        double x = k * 1.5707963267948966;
        return sign * asd(asu(x) + (rnd() % 9) - 4);
    }
    }
}

/* ---- the functions ------------------------------------------------------------------------------ */

enum { F_SIN, F_COS, F_TAN, F_ATAN, F_ATAN2, F_LOG, F_LOG2, F_LOG1P, F_EXPM1, F_COUNT };
static const char *fn_name[F_COUNT] = {"sin", "cos", "tan", "atan", "atan2", "log", "log2", "log1p", "expm1"};

static double run_dm(int f, double a, double b)
{
    switch (f) {
    case F_SIN: return dm_sin(a);
    case F_COS: return dm_cos(a);
    case F_TAN: return dm_tan(a);
    case F_ATAN: return dm_atan(a);
    case F_ATAN2: return dm_atan2(a, b);
    case F_LOG: return dm_log(a);
    case F_LOG2: return dm_log2(a);
    case F_LOG1P: return dm_log1p(a);
    default: return dm_expm1(a);
    }
}

/* the domain each function's sets are mapped into (log wants x > 0 mostly, expm1 |x| < 710) */
static double fit(int f, int s, double a)
{
    if (s == SET_BITS) return a;
    if (f == F_LOG || f == F_LOG2) return a < 0 ? -a : a;
    if (f == F_LOG1P) return (a < 0 ? -a : a) - (s == SET_TURN ? 0.999 : 0.0);
    if (f == F_EXPM1) { while (a > 709.0 || a < -745.0) a *= 0.5; return a; }
    return a;
}

#ifndef DM_FREESTANDING
static double run_host(int f, double a, double b)
{
    switch (f) {
    case F_SIN: return sin(a);
    case F_COS: return cos(a);
    case F_TAN: return tan(a);
    case F_ATAN: return atan(a);
    case F_ATAN2: return atan2(a, b);
    case F_LOG: return log(a);
    case F_LOG2: return log2(a);
    case F_LOG1P: return log1p(a);
    default: return expm1(a);
    }
}
static uint64_t ulps(double a, double b)        /* distance in representable steps; NaN pairs 0 */
{
    int64_t x = (int64_t)asu(a), y = (int64_t)asu(b);
    if (a != a && b != b) return 0;
    if (a != a || b != b) return UINT64_MAX;
    if (x < 0) x = INT64_MIN - x;
    if (y < 0) y = INT64_MIN - y;
    return x > y ? (uint64_t)(x - y) : (uint64_t)(y - x);
}
static int g_host;
#endif

static uint64_t fnv(uint64_t h, uint64_t v)
{
    int k;
    for (k = 0; k < 8; k++) { h ^= (v >> (8 * k)) & 0xFF; h *= 0x100000001B3ull; }
    return h;
}

static uint64_t g_all = 0xCBF29CE484222325ull;

static void line(const char *fn, const char *set, uint64_t n, uint64_t h)
{
    out(fn); out(" "); out(set); out(" n="); out_u64(n, 0); out(" hash="); out_u64(h, 1); out("\n");
    g_all = fnv(g_all, h);
}

static void run_sets(uint64_t n)
{
    int f, s;
    for (f = 0; f < F_COUNT; f++)
        for (s = 0; s < SET_COUNT; s++) {
            uint64_t h = 0xCBF29CE484222325ull, i;
#ifndef DM_FREESTANDING
            uint64_t differ = 0, worst = 0, nan_differ = 0;
#endif
            g_rng = 0x9E3779B97F4A7C15ull ^ ((uint64_t)(f + 1) << 32) ^ (uint64_t)(s + 1);
            for (i = 0; i < n; i++) {
                double a = fit(f, s, gen(s)), b = gen(s), r = run_dm(f, a, b);
                h = fnv(h, asu(r));
#ifndef DM_FREESTANDING
                if (g_host) {
                    double q = run_host(f, a, b);
                    if (asu(q) != asu(r)) {
                        uint64_t d = ulps(q, r);
                        if (d == 0) nan_differ++;
                        else { differ++; if (d > worst) worst = d; }
                    }
                }
#endif
            }
            line(fn_name[f], set_name[s], n, h);
#ifndef DM_FREESTANDING
            if (g_host)
                printf("    vs host libm: %llu differ (max %llu ulp), %llu only in the NaN bits\n",
                       (unsigned long long)differ, (unsigned long long)worst, (unsigned long long)nan_differ);
#endif
        }
}

/* RECOMP_DETMATH_TRACE records: u32 function (detmath_select.c's numbering), u32 0, u64 a, u64 b */
static void run_trace(const char *path)
{
    static unsigned char buf[24 * 4096];
    uint64_t h[F_COUNT], n[F_COUNT];
    long fd = file_open(path), got, k;
    int f;
    if (fd < 0) { out("cannot open the trace\n"); return; }
    for (f = 0; f < F_COUNT; f++) { h[f] = 0xCBF29CE484222325ull; n[f] = 0; }
    while ((got = file_read(fd, buf, (long)sizeof(buf))) > 0) {
        for (k = 0; k + 24 <= got; k += 24) {
            uint32_t id = (uint32_t)buf[k] | (uint32_t)buf[k + 1] << 8 | (uint32_t)buf[k + 2] << 16 | (uint32_t)buf[k + 3] << 24;
            uint64_t a = 0, b = 0;
            int j;
            for (j = 7; j >= 0; j--) { a = (a << 8) | buf[k + 8 + j]; b = (b << 8) | buf[k + 16 + j]; }
            if (id >= F_COUNT) continue;
            h[id] = fnv(h[id], asu(run_dm((int)id, asd(a), asd(b))));
            n[id]++;
        }
        if (got % 24) break;                    /* (the writer only writes whole records) */
    }
    for (f = 0; f < F_COUNT; f++)
        if (n[f]) line(fn_name[f], "trace", n[f], h[f]);
}

static int dm_main(int argc, char **argv)
{
    int i;
    uint64_t n = DM_N;
    const char *trace = 0;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
#ifndef DM_FREESTANDING
        if (!strcmp(a, "--host")) { g_host = 1; continue; }
#if defined(_MSC_VER) && defined(_M_X64)
        /* UCRT picks an FMA3 code path for its transcendentals on CPUs that have FMA3; this
         * takes the SSE2 one, as on an older CPU (shows whether two PCs' libms differ). */
        if (!strcmp(a, "--no-fma3")) { _set_FMA3_enable(0); continue; }
#endif
#endif
        if (a[0] == '-' && a[1] == 'n' && a[2] == '=') { n = 0; a += 3; while (*a >= '0' && *a <= '9') n = n * 10 + (uint64_t)(*a++ - '0'); continue; }
        trace = a;
    }
#if defined(__aarch64__) || defined(_M_ARM64)
    out("arch arm64\n");
#elif defined(__x86_64__) || defined(_M_X64)
    out("arch x86-64\n");
#else
    out("arch other\n");
#endif
    if (trace) run_trace(trace);
    else run_sets(n);
    out("all hash="); out_u64(g_all, 1); out("\n");
    return 0;
}

#ifdef DM_FREESTANDING
__attribute__((used)) void dm_cstart(long *sp)
{
    sys3(SYS_EXIT, dm_main((int)sp[0], (char **)(sp + 1)), 0, 0);
    for (;;) {}
}
#if defined(__aarch64__)
__asm__(".global _start\n_start:\n mov x0, sp\n and sp, x0, #-16\n bl dm_cstart\n");
#else
__asm__(".global _start\n_start:\n mov %rsp, %rdi\n and $-16, %rsp\n call dm_cstart\n");
#endif
#else
int main(int argc, char **argv) { return dm_main(argc, argv); }
#endif
