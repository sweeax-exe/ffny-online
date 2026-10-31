/* Optional experiment: refresh the report, before the title advances its
 * controller history once in DD930. No extra EBBE0 poll, FIFO acknowledgement,
 * device lifecycle, pad-table write, or simulation step is performed here. */
#include "host.h"
#include "input_late.h"
#include "input_consumption.h"
#include "input_host.h"
#include "input_trace.h"
#include "recomp/gen/recomp_types.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern int xbox_DispatchExclusionTryEnter(void);
extern void xbox_DispatchExclusionLeave(void);
extern void sub_0027E075(void);
extern void sub_000D7BF0_gen(void);
static int mode; /* 0 off; 1 apply; 2 shadow comparison only */
static CRITICAL_SECTION lifecycle;
static RECOMP_TLS uint32_t private_raw;
static RECOMP_TLS unsigned char captured[4][22];
static RECOMP_TLS unsigned captured_valid;

void input_late_start(void)
{
    const char *e = getenv("RECOMP_INPUT_LATE_REFRESH");
    mode = e && *e == '1' ? 1 : e && *e == 's' ? 2 : 0;
    if (mode) InitializeCriticalSection(&lifecycle);
    fprintf(stderr, "[INPUT] late report refresh: %s\n",
        mode == 1 ? "experimental" : mode == 2 ? "shadow" : "off");
}
int input_late_on(void) { return mode; }
void input_late_lock(void) { if (mode) EnterCriticalSection(&lifecycle); }
void input_late_unlock(void) { if (mode) LeaveCriticalSection(&lifecycle); }
uint32_t input_late_raw(void) { return private_raw; }

void input_late_capture(uint32_t handle, uint32_t state, uint32_t status)
{
    unsigned port;
    if (!mode || status || !state) return;
    for (port = 0; port < 4; ++port)
        if (handle && MEM32(0x3C9340u + port*0x90u) == handle) {
            uint32_t context=MEM32(handle+0xA3u);
            if (!context || MEM8(MEM32(context+8u))!=18) return;
            memcpy(captured[port], XBOX_PTR(state), 22);
            captured_valid |= 1u << port;
            break;
        }
}

/* The fields D7BF0 reads from EBBE0's record, using the title's constants
 * and x87 precision model. Its one-poll edge latches are deliberately absent:
 * D7BF0 consumes held values; DD930/DCE00 own the game-facing edge history. */
static void raw_report(unsigned char out[0x90], const unsigned char state[22])
{
    unsigned i;
    memset(out, 0, 0x90);
    for (i = 0; i < 4; ++i) {
        int16_t axis;
        double value;
        float stored;
        memcpy(&axis, state + 14 + i*2, 2);
        value = RECOMP_FP_PC((double)axis + MEMF(0x286D2Cu));
        value = RECOMP_FP_PC(value * MEMF(0x2B5D1Cu));
        stored = fabs(value) < MEMD(0x2D0538u) ? 0.0f : (float)value;
        memcpy(out + i*4, &stored, 4);
    }
    memcpy(out + 0x10, state + 4, 10); /* held digital + eight analog buttons */
}

/* Runs on the normal poller's own thread immediately after D7BF0. This
 * checks the small conversion above against the original guest record. */
void input_late_validate(unsigned port)
{
    unsigned char raw[0x90];
    unsigned i, differences = 0;
    if (!mode || port >= 4 || !(captured_valid & (1u << port))) return;
    raw_report(raw, captured[port]);
    for (i = 0; i < 26; ++i)
        if (raw[i] != MEM8(0x3C92B8u + port*0x90u + i)) differences |= 1u << i;
    xbox_InputTraceEmit("late_shadow", port, captured[port], 22, differences, 0);
}

typedef struct guest_context {
    uint32_t ax, cx, dx, sp, bx, si, di, bp, seh, flags;
    int df, top, cmp;
    uint16_t control, cc;
    double fp[8];
} guest_context;
static guest_context save_context(void)
{
    guest_context c = {g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,
        g_seh_ebp,g_eflags,g_df,g_fp_top,g_fp_cmp,g_fp_control_word,g_fp_cc,{0}};
    memcpy(c.fp,g_fp_stack,sizeof c.fp);
    return c;
}
static void restore_context(const guest_context *c)
{
    g_eax=c->ax;g_ecx=c->cx;g_edx=c->dx;g_esp=c->sp;g_ebx=c->bx;
    g_esi=c->si;g_edi=c->di;g_ebp=c->bp;g_seh_ebp=c->seh;
    g_eflags=c->flags;g_df=c->df;g_fp_top=c->top;g_fp_cmp=c->cmp;
    g_fp_control_word=c->control;g_fp_cc=c->cc;
    memcpy(g_fp_stack,c->fp,sizeof c->fp);
}

void input_late_refresh(uint32_t device, uint32_t report)
{
    guest_context c;
    uint32_t port, handle, state, raw, call_stack, status;
    if (mode != 1 || !input_consumption_on() || !xbox_HostInputConsumedEdges() ||
        MEM32(0x3B8F30u) != 1 || !device || !report || MEM32(device+0xCu) != 2) return;
    port = MEM32(device+8u);
    if (port >= 4) return;
    xbox_InputTraceEmit("late_refresh_begin",port,NULL,0,device,0);
    /* Both acquisitions are opportunistic. The XDK API's own raise is
     * recursive inside the acquired dispatch exclusion. Never park this
     * update waiting for a worker or DPC; retain the normal queued report. */
    if (!xbox_DispatchExclusionTryEnter()) {
        xbox_InputTraceEmit("late_skip_dispatch",port,NULL,0,device,0);
        xbox_InputTraceEmit("late_refresh_end",port,NULL,0,device,0);
        return;
    }
    if (!TryEnterCriticalSection(&lifecycle)) {
        xbox_DispatchExclusionLeave();
        xbox_InputTraceEmit("late_skip_busy",port,NULL,0,device,0);
        xbox_InputTraceEmit("late_refresh_end",port,NULL,0,device,0);
        return;
    }
    handle = MEM32(0x3C9340u + port*0x90u);
    if (!handle || !MEM32(handle+0xA3u) ||
        MEM8(MEM32(MEM32(handle+0xA3u)+8u)) != 18) goto done;
    c=save_context();
    /* Scratch is below the returned callback's stack. The report destination
     * belongs to DD930 and lies above it. Neither global poller records nor
     * its queued clock records are modified. Both callees use integer/x87
     * registers only; preserve all of those around the injected calls. */
    state=c.sp-0x80u;raw=c.sp-0x120u;call_stack=c.sp-0x180u;
    g_esp=call_stack;
    PUSH32(g_esp,state);PUSH32(g_esp,handle);PUSH32(g_esp,0x000DC9B1u);
    sub_0027E075();
    status=g_eax;
    if (!status) {
        raw_report((unsigned char *)XBOX_PTR(raw), (const unsigned char *)XBOX_PTR(state));
        private_raw=raw;
        g_esp=call_stack;
        PUSH32(g_esp,port);PUSH32(g_esp,report);PUSH32(g_esp,0x000DC9B1u);
        sub_000D7BF0_gen(); /* original title conversion and button ordering */
        private_raw=0;
        xbox_InputTraceEmit("late_report",port,XBOX_PTR(state),22,device,MEM32(state));
    } else xbox_InputTraceEmit("late_read_error",port,NULL,0,handle,status);
    restore_context(&c);
done:
    LeaveCriticalSection(&lifecycle);
    xbox_DispatchExclusionLeave();
    xbox_InputTraceEmit("late_refresh_end",port,NULL,0,device,0);
}
