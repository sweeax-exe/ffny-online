/* Created (Story mode) fighters in Battle setups: step 1 of fighting online with one's own
 * created fighter. See docs/research/created-fighter-online.md.
 *
 * Battle mode already builds a created fighter for a slot whose ControllerSetup fighter byte is
 * 99: StartGame (sub_00020270, the branch at 0x2096D) finds the profile through the slot's pad
 * (GS+0x1AAB+5*slot -> GS+0x1ABD[pad]), takes the name, the archetype (P+0x143), the move keys
 * and the stats from it, and the match load (sub_0008B730) builds the body and the clothes from
 * the profile's recipe into the pad's CP-Fighter holder. This file only proves that a record
 * copied into a spare profile slot is enough for that; nothing here runs unless the test switch
 * below is set.
 *
 * RECOMP_TEST_CP="slot=1,from=0,to=15[,file=<profile dump>][,mod=<n>:<i>:<hex>][,keep=<n>][,nounlink=1]"
 *   at each automatch sequence (RECOMP_AUTOMATCH or the control channel's `match`): copy the
 *   record subset of profile `from` (or of a raw 0x5950-byte profile dump, `file`, as a record
 *   from the network would arrive) into profile `to`, point the pad of `slot` at `to`, clear the
 *   pad holder's "built" byte (so the match load rebuilds the look) and set the slot's fighter to
 *   99. `mod`: from the n-th injection on write recipe dword i of the copy (a different colour);
 *   `keep`: on the n-th injection leave the built byte alone (the setup calls before it have
 *   cleared it already, so the look is rebuilt anyway); `nounlink`: keep the pad linked to the
 *   copy through the summary's reward step, which then saves the copy to disk (see
 *   created_fighter_before_funccall). The slot needs a pad (>= 0): the holder and the profile are
 *   found through it. */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp/gen/recomp_types.h"
#include "created_fighter.h"

#define GS_PTR          (0x2FB148u + 4u * 0x18u)    /* game-flow singleton, component 0x18 */
#define GS_PROFILES     4u                          /* MEM32(GS+4) + 0x177C: 16 profiles */
#define PROFILE_BASE    0x177Cu
#define PROFILE_SIZE    0x5950u
#define PROFILE_COUNT   16
#define GS_PAD_PROFILE  0x1ABDu                     /* s8 per pad: profile index, -1 none */
#define GS_HOLDERS      0x1A9Cu                     /* 4 x 0x1C per pad; +1 = look built */

/* The record subset Battle reads from a profile (offsets from the profile's start). */
static const struct { unsigned off, len; } k_record[] = {
    { 0x00C, CP_NAME_BYTES },   /* name, NUL-terminated */
    { 0x0FC, 0x18 },            /* six base stats (u32) */
    { 0x114, 0x18 },            /* six bonus stats (s32) */
    { 0x12C, 0x10 },            /* four move keys */
    { 0x13C, 0x04 },            /* voice */
    { 0x140, 0x04 },            /* style bytes 0x140..0x142, archetype 0x143 (fighter.xml id) */
    { 0x4D8A, 0x01 },           /* created-fighter flag */
    { 0x5564, 0x98 },           /* recipe: 38 dwords (table<<24 | colour<<16 | row, -1 default) */
    { 0x55FC, 0x26 },           /* options: 38 bytes */
};

static int g_parsed, g_slot = -1, g_from, g_to = 15, g_mod_n, g_mod_i, g_keep_n, g_count;
static unsigned g_mod_v;
static char g_file[260];
static int g_linked_pad = -1, g_nounlink;      /* see created_fighter_before_funccall */

static void parse(void)
{
    const char *e = getenv("RECOMP_TEST_CP");
    g_parsed = 1;
    if (!e || !*e) return;
    while (*e) {
        int n = 0;
        if (!strncmp(e, "file=", 5)) {
            size_t k = strcspn(e + 5, ",");
            snprintf(g_file, sizeof(g_file), "%.*s", (int)(k < sizeof(g_file) ? k : sizeof(g_file) - 1), e + 5);
            e += 5 + k;
        } else if (sscanf(e, "slot=%d%n", &g_slot, &n) == 1 || sscanf(e, "from=%d%n", &g_from, &n) == 1 ||
            sscanf(e, "to=%d%n", &g_to, &n) == 1 || sscanf(e, "keep=%d%n", &g_keep_n, &n) == 1 ||
            sscanf(e, "nounlink=%d%n", &g_nounlink, &n) == 1 ||
            sscanf(e, "mod=%d:%d:%x%n", &g_mod_n, &g_mod_i, &g_mod_v, &n) == 3) {
            e += n;
        } else e++;
        while (*e == ',' || *e == ' ') e++;
    }
    if (g_from < 0 || g_from >= PROFILE_COUNT || g_to < 0 || g_to >= PROFILE_COUNT || g_from == g_to) g_slot = -1;
    fprintf(stderr, "[CP-TEST] slot %d: profile %d%s%s -> %d, mod #%d recipe[%d]=%08X, keep #%d\n",
            g_slot, g_from, g_file[0] ? " / file " : "", g_file, g_to, g_mod_n, g_mod_i, g_mod_v, g_keep_n);
}

uint32_t created_fighter_profile(int index)
{
    uint32_t gs = MEM32(GS_PTR), base;
    if (!gs || index < 0 || index >= PROFILE_COUNT) return 0;
    base = MEM32(gs + GS_PROFILES);
    return base ? base + PROFILE_BASE + (uint32_t)index * PROFILE_SIZE : 0;
}

void created_fighter_write_record(uint32_t dst, const uint8_t *profile)
{
    size_t i;
    for (i = 0; i < sizeof(k_record) / sizeof(k_record[0]); i++)
        memcpy((void *)XBOX_PTR(dst + k_record[i].off), profile + k_record[i].off, k_record[i].len);
    MEM8(dst + 0xC + CP_NAME_BYTES - 1) = 0;
}

void created_fighter_copy_record(uint32_t dst, uint32_t src)
{
    created_fighter_write_record(dst, (const uint8_t *)XBOX_PTR(src));
}

/* The test's profile dump: a raw 0x5950-byte profile, as the network would deliver a record. */
static int load_file(uint8_t *buf)
{
    FILE *f = fopen(g_file, "rb");
    size_t got;
    if (!f) return 0;
    got = fread(buf, 1, PROFILE_SIZE, f);
    fclose(f);
    return got == PROFILE_SIZE;
}

/* Battle's stat formula (sub_0001EE80): round(base/5 * 1.275 + bonus * 0.45) per stat. */
static int stat(uint32_t base, int32_t bonus)
{
    double v = (double)(base / 5u) * (double)1.275f + (double)bonus * (double)0.45f + 0.5;
    return (int)v;
}

/* The pad an injected profile is linked to, -1 when none. After the match the summary's reward
 * step saves the profile behind each pad (Game.UpdateMatchRewardsPoints, then MemCard.Save):
 * linked, the copied record went to disk as a user ID named after the copied fighter, replacing a
 * local user of that name (measured, section 4 of the report). So the link is undone before the
 * reward step, once the match load has built the look and the summary has shown the name.
 * `nounlink` in the test switch keeps the link, to show the save. */

int created_fighter_before_funccall(const char *text)
{
    uint32_t gs;
    if (g_linked_pad < 0 || g_nounlink) return 0;
    if (strncmp(text, "Game.UpdateMatchRewardsPoints", 29) && strncmp(text, "MemCard.", 8) &&
        strncmp(text, "Game.Quit", 9) && strncmp(text, "Game.StartBattleMode", 20))
        return 0;
    gs = MEM32(GS_PTR);
    if (gs) MEM8(gs + GS_PAD_PROFILE + (uint32_t)g_linked_pad) = 0xFF;
    fprintf(stderr, "[CP-TEST] pad %d unlinked from the copied profile before %s\n", g_linked_pad, text);
    g_linked_pad = -1;
    return 0;
}

int created_fighter_test_slot(int slot, int pad)
{
    uint32_t gs, src, dst, holder;
    int k;
    if (!g_parsed) parse();
    if (slot != g_slot) return -1;
    if (pad < 0 || pad > 3) {
        fprintf(stderr, "[CP-TEST] slot %d has no pad; a created fighter needs one\n", slot);
        return -1;
    }
    gs = MEM32(GS_PTR);
    src = created_fighter_profile(g_from);
    dst = created_fighter_profile(g_to);
    if (!gs || !src || !dst) return -1;
    g_count++;
    if (g_file[0]) {
        static uint8_t buf[PROFILE_SIZE];
        if (!load_file(buf)) {
            fprintf(stderr, "[CP-TEST] cannot read the profile dump %s\n", g_file);
            return -1;
        }
        created_fighter_write_record(dst, buf);
    } else
        created_fighter_copy_record(dst, src);
    if (g_mod_n && g_count >= g_mod_n) MEM32(dst + 0x5564 + 4u * (uint32_t)g_mod_i) = g_mod_v;
    MEM8(gs + GS_PAD_PROFILE + (uint32_t)pad) = (uint8_t)g_to;
    g_linked_pad = pad;
    holder = MEM32(gs + GS_HOLDERS);
    if (holder && g_keep_n != g_count) MEM8(holder + (uint32_t)pad * 0x1Cu + 1) = 0;
    fprintf(stderr, "[CP-TEST] #%d slot %d pad %d: profile %d (%08X) -> %d (%08X) \"%s\" archetype %d, holder %08X built=%d\n",
            g_count, slot, pad, g_from, src, g_to, dst, (const char *)XBOX_PTR(dst + 0xC), MEM8(dst + 0x143),
            holder ? holder + (uint32_t)pad * 0x1Cu : 0, holder ? MEM8(holder + (uint32_t)pad * 0x1Cu + 1) : -1);
    fprintf(stderr, "[CP-TEST] expected stats:");
    for (k = 0; k < 6; k++) fprintf(stderr, " %d", stat(MEM32(dst + 0xFC + 4u * k), (int32_t)MEM32(dst + 0x114 + 4u * k)));
    fprintf(stderr, "\n");
    return CP_FIGHTER_ID;
}
