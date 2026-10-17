/*
 * The developer run module (CRunModuleAki), rebuilt from the host.
 *
 * The retail image keeps the module's methods -- Load 0x77C40 (makes the developer mode system,
 * subsystem 9), Update 0x77A40, Render 0x77B20, Unload 0x77DD0 and the constructor body 0x779F0 --
 * but not its virtual table, its constructor or its case in the run-module factory, so nothing
 * ever starts it. Here the table and the object are built in guest memory with the game's own
 * allocator and the module is queued the way the factory queues every module: stored as the run-
 * module manager's pending module (subsystem 0x17, +0x1C) with a switch requested (+0x24 = 1).
 * The manager then loads it on its loader thread, asks the current module whether it can leave,
 * stops and unloads that one, and starts this. docs/research/dev-module-revival.md has the map.
 *
 *   RECOMP_DEV_MODULE=1           queue it once the front end's main menu is up
 *   RECOMP_DEV_MODULE=<anchor>    ... once a front-end call containing <anchor> has run
 *
 * Every method of ours is traced as [DEVMOD]. A test control: nothing changes unless set.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guest_call.h"
#include "recorder.h"
#include "dev_module.h"

#define MANAGER_SLOT    0x002FB1A4u     /* registry 0x2FB148, subsystem 0x17: CRunModuleMan */
#define MGR_CURRENT     0x18
#define MGR_PENDING     0x1C
#define MGR_STATE       0x24            /* 0 running, 1 requested, 2 loading, 3 loaded */
#define TAG_RUNMODULE   0x0029D4B4u     /* "RunModule", the factory's own allocation tag */
#define MODULE_SIZE     0x44            /* fields up to +0x40 (pause menu +0x3C, popup +0x40) */

extern void sub_00029680(void);         /* the game's allocator: cdecl (size, tag, flags) */
extern void sub_000779F0(void);         /* the module's constructor body (thiscall): pause menu, popup */

/* destructor, Load, Start, Update(dt), Render, CanLeave, Stop, Unload */
static const uint32_t k_slots[8] = {
    0x00076D30u,    /* the base class's destructor */
    0x00077C40u,    /* Load: CRunModuleAki::Init */
    0x00076EF0u,    /* Start: nothing (a bare ret) */
    0x00077A40u,    /* Update(dt) */
    0x00077B20u,    /* Render: returns 1 */
    0x00076EE0u,    /* CanLeave: returns 1 */
    0x00076EF0u,    /* Stop: nothing */
    0x00077DD0u,    /* Unload: undoes Init */
};

static int s_mode = -1;                 /* -1 env not read, 0 off, 1 waiting for the anchor, 2 wanted, 3 queued */
static char s_anchor[96];
static uint32_t s_table, s_object;
static unsigned s_updates, s_renders;

static uint32_t guest_alloc(uint32_t size)
{
    uint32_t args[3];
    args[0] = size; args[1] = TAG_RUNMODULE; args[2] = 0;
    return guest_call(sub_00029680, 0, 3, args);
}

static void read_env(void)
{
    const char *v = getenv("RECOMP_DEV_MODULE");
    size_t i;
    s_mode = 0;
    if (!v || !*v || !strcmp(v, "0")) return;
    snprintf(s_anchor, sizeof s_anchor, "%s", strcmp(v, "1") ? v : "getscreeninfo(intmain/mainmenu");
    for (i = 0; s_anchor[i]; i++) s_anchor[i] = (char)tolower((unsigned char)s_anchor[i]);
    s_mode = 1;
    fprintf(stderr, "[DEVMOD] armed: the developer module is queued after a front-end call containing \"%s\"\n",
            s_anchor);
}

void dev_module_request(void) { if (s_mode < 0) read_env(); if (s_mode < 3) s_mode = 2; }
int  dev_module_active(void) { uint32_t m = MEM32(MANAGER_SLOT); return s_object && m && MEM32(m + MGR_CURRENT) == s_object; }

void dev_module_after_call(const char *text)
{
    char low[160];
    size_t i;
    if (s_mode < 0) read_env();
    if (s_mode != 1 || !text) return;
    for (i = 0; text[i] && i + 1 < sizeof low; i++) low[i] = (char)tolower((unsigned char)text[i]);
    low[i] = 0;
    if (strstr(low, s_anchor)) s_mode = 2;
}

/* The run-module manager's tick (thiscall, ecx = the manager), on the game's main thread. */
void dev_module_manager_enter(void)
{
    uint32_t mgr = g_ecx, obj;
    int i;

    if (s_mode < 0) read_env();
    if (s_mode != 2 || !mgr || MEM32(mgr + MGR_STATE) != 0 || MEM32(mgr + MGR_PENDING) != 0) return;
    if (!s_table) {
        s_table = guest_alloc(sizeof k_slots);
        if (!s_table) { fprintf(stderr, "[DEVMOD] no memory for the table\n"); s_mode = 0; return; }
        for (i = 0; i < 8; i++) MEM32(s_table + (uint32_t)i * 4u) = k_slots[i];
    }
    obj = guest_alloc(MODULE_SIZE);
    if (!obj) { fprintf(stderr, "[DEVMOD] no memory for the module\n"); s_mode = 0; return; }
    for (i = 0; i < MODULE_SIZE; i += 4) MEM32(obj + (uint32_t)i) = 0;
    MEM32(obj) = s_table;
    guest_call(sub_000779F0, obj, 0, NULL);
    s_object = obj;
    MEM32(mgr + MGR_PENDING) = obj;
    MEM32(mgr + MGR_STATE) = 1;
    s_mode = 3;
    fprintf(stderr, "[DEVMOD] queued: module %08X, table %08X, pause menu %08X, popup %08X; current module %08X\n",
            obj, s_table, MEM32(obj + 0x3C), MEM32(obj + 0x40), MEM32(mgr + MGR_CURRENT));
    recorder_event_text("devmod", "state", "queued");
}

/* Traces: the methods of ours, entered with ecx = the module. */
void dev_module_trace(int slot)
{
    static const char *names[8] = { "destroy", "Load", "Start", "Update", "Render", "CanLeave", "Stop", "Unload" };
    if (!s_object || g_ecx != s_object) return;
    if (slot == 3 && ++s_updates > 3 && s_updates % 600) return;
    if (slot == 4 && ++s_renders > 3 && s_renders % 600) return;
    if (slot == 3 || slot == 4) {
        fprintf(stderr, "[DEVMOD] %s #%u (mode system %08X, state %u)\n", names[slot], slot == 3 ? s_updates : s_renders,
                MEM32(s_object + 0x14), MEM32(s_object + 0x14) ? MEM32(MEM32(s_object + 0x14) + 0x2A0) : 0xFFFFFFFFu);
    } else {
        fprintf(stderr, "[DEVMOD] %s\n", names[slot]);
        recorder_event_text("devmod", "method", names[slot]);
    }
}
