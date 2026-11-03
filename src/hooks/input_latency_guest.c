/* Guest-consumption adapter and intermediate probes for the verified USA image.
 * Fight action transitions wait for the human fighter's raw pad-table read;
 * other transitions are acknowledged on a successful XDK copy. */
#include "host.h"
#include "input_latency.h"
#include "input_consumption.h"
#include "input_trace.h"
#include "input_host.h"
#include "recomp/gen/recomp_types.h"
#include <stdlib.h>
#include <string.h>

static int consumption;
static CRITICAL_SECTION pending_lock;
static struct { uint32_t packet, actions; uint64_t generation; unsigned char body[18]; int valid; } pending[4];
static RECOMP_TLS uint64_t read_generation[4];
static uint32_t consumed_actions[4];
void input_consumption_start(void)
{
    const char *setting = getenv("RECOMP_INPUT_EDGES");
    consumption = !setting || *setting != '0';
    if (consumption) InitializeCriticalSection(&pending_lock);
    xbox_HostInputSetEdgeThreshold(1); /* sub_001EBBE0 classifies analog buttons with >1. */
    xbox_HostInputEnableConsumedEdges(consumption);
}
int input_consumption_on(void) { return consumption; }

static uint32_t action_bits(const unsigned char *body)
{
    /* sub_000DC4F0 transforms the accessor mask before writing PAD_TABLE.
     * A/B and Black/White differ from Xbox report order. Start/Back remain
     * API-acknowledged so pausing never depends on a simulation step. */
    static const uint32_t analog_bits[8]={0x20u,0x10u,0x40u,0x80u,0x200u,0x100u,0x1000u,0x2000u};
    uint32_t result = body[0]&0xFu;
    unsigned i;
    for (i = 0; i < 8; i++) if (body[2+i] > 1) result |= analog_bits[i];
    return result;
}

static int human_port(uint32_t actor)
{
    uint32_t index;
    if (actor < 0x003B92E0u || actor >= 0x003B92E0u + 4u*0x12E8u ||
        (actor-0x003B92E0u)%0x12E8u || (MEM8(actor+0x937u)&1u)) return -1;
    index=MEM32(actor+0x28u);
    if (index>=8) return -1;
    index=MEM32(0x003B9098u+index*4u);
    return index<4 ? (int)index : -1;
}

static int in_live_fight(int port)
{
    unsigned i;
    if (MEM32(0x003B8F30u)!=1u) return 0;
    for (i=0;i<4;i++) if (human_port(0x003B92E0u+i*0x12E8u)==port) return 1;
    return 0;
}

/* Critical action transitions survive a frame with no simulation step.
 * A fighter's raw pad-table read acknowledges receipt before its own native
 * filters decide whether an attack is allowed. This does not force an action. */
void input_consumption_fighter(uint32_t actor)
{
    int port;
    uint32_t observed;
    if (!consumption || !xbox_HostInputConsumedEdges() || MEM32(0x003B8F30u)!=1u) return;
    port=human_port(actor);
    if (port<0) return;
    observed=MEM32(0x003B8F48u+(uint32_t)port*0x20u)&0x33FFu;
    EnterCriticalSection(&pending_lock);
    if (pending[port].valid && pending[port].generation!=xbox_HostInputEdgeGeneration(port)) {
        pending[port].valid=0;
        xbox_InputTraceEmit("fighter_input_cancelled",port,NULL,0,pending[port].generation,0);
    }
    if (pending[port].valid && observed==pending[port].actions) {
        xbox_HostInputConsumeGeneration(port,pending[port].packet,pending[port].body,18,pending[port].generation);
        consumed_actions[port]=pending[port].actions;
        pending[port].valid=0;
        xbox_InputTraceEmit("fighter_input_ack",port,NULL,0,observed,pending[port].packet);
    }
    LeaveCriticalSection(&pending_lock);
}

static int handle_slot(uint32_t handle)
{
    int slot;
    if (!handle) return -1;
    /* sub_001EBBE0: four handle records, 0x90 apart, in physical port order. */
    for (slot = 0; slot < 4; slot++)
        if (MEM32(0x003C9340u + (uint32_t)slot * 0x90u) == handle) return slot;
    return -1;
}

void input_latency_xdk_begin(uint32_t handle)
{
    int slot=handle_slot(handle);
    if (consumption && slot>=0) read_generation[slot]=xbox_HostInputEdgeGeneration(slot);
    if (input_latency_on())
        xbox_InputTraceEmit("xdk_read_begin", slot, NULL, 0, handle, 0);
}

void input_consumption_xdk_end(uint32_t handle, uint32_t state, uint32_t status)
{
    uint32_t context, length;
    int slot;
    if (!input_latency_on() && !consumption) return;
    slot = handle_slot(handle);
    if (status || !handle || !state) {
        xbox_InputTraceEmit("xdk_read_error", slot, NULL, 0, handle, status);
        return;
    }
    /* XPP 0x27E0B2..D4: DWORD packet followed by a context-sized raw report.
     * Never substitute the Windows XINPUT_STATE layout for this guest layout. */
    context = MEM32(handle + 0xA3u);
    length = context ? MEM8(MEM32(context + 8u)) : 0;
    if (length > 92u) length = 92u;
    if (input_latency_on())
        xbox_InputTraceEmit("xdk_read", slot, (const void *)XBOX_PTR(state), length + 4u, handle, length);
    if (consumption && slot>=0 && length == 18u && xbox_HostInputConsumedEdges()) {
        unsigned char body[18];
        uint32_t actions;
        memcpy(body,(const void *)XBOX_PTR(state+4u),18);
        actions=action_bits(body);
        EnterCriticalSection(&pending_lock);
        if (!read_generation[slot] || read_generation[slot]!=xbox_HostInputEdgeGeneration(slot)) {
            pending[slot].valid=0;
            LeaveCriticalSection(&pending_lock);
            return;
        }
        /* A worker may finish an older API copy after the optional late
         * read. Never replace a still-pending newer report with that copy.
         * Signed subtraction also handles the DWORD packet wrap. */
        if (pending[slot].valid && pending[slot].generation==read_generation[slot] &&
            (int32_t)(MEM32(state)-pending[slot].packet)<0) {
            xbox_InputTraceEmit("xdk_stale_pending",slot,NULL,0,MEM32(state),pending[slot].packet);
            LeaveCriticalSection(&pending_lock);
            return;
        }
        pending[slot].packet=MEM32(state);
        pending[slot].generation=read_generation[slot];
        pending[slot].actions=actions;
        memcpy(pending[slot].body,body,18);
        pending[slot].valid=1;
        if (!in_live_fight(slot) || !(actions|consumed_actions[slot])) {
            xbox_HostInputConsumeGeneration(slot,pending[slot].packet,body,18,pending[slot].generation);
            consumed_actions[slot]=actions;
            pending[slot].valid=0;
        }
        LeaveCriticalSection(&pending_lock);
    }
}

void input_latency_pad_cache(uint32_t base, int after, uint32_t writer)
{
    int slot;
    if (!input_latency_on() || base != 0x003B8F48u) return;
    for (slot = 0; slot < 4; slot++)
        xbox_InputTraceEmit(after ? "pad_cache_after" : "pad_cache_before", slot,
            (const void *)XBOX_PTR(base + (uint32_t)slot * 0x20u), 0x20u, base, writer);
}
