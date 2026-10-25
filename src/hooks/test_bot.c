/* A test bot that fights like a simple player: RECOMP_TEST_BOT=1[,<aggression 0..100>].
 *
 * Scripted pads press fixed or random buttons on the host's clock and rarely finish a fighter,
 * and in this game zero health is not a knockout (test_telemetry.c, the fast knockout), so
 * scripted matches lasted four to eleven minutes or never ended. The bot reads the game state
 * every fight step instead (its own and the opponent's position, facing, health, stun and the
 * state numbers) and presses pad buttons for its fighter: it walks up to the opponent, throws
 * strings of strikes, grabs, blocks now and then, and goes for grabs and heavy hits once the
 * opponent is under the knockout threshold or stunned, until the game decides the knockout.
 *
 * Input path. The bot writes the pad-table entry (0x3B8F48 + slot * 0x20) its fighter is about
 * to read, at the entry of the reader sub_001BAA00, as RECOMP_TEST_INPUT does. Online each copy
 * drives only its own player (netplay_local_slot): netplay.c reads that entry next as the local
 * input of the step and exchanges it with the other PCs like a human's, so the bot cannot part
 * the lockstep. In a step run again by rollback the bot still writes, and netplay ignores the
 * entry (it uses the input recorded the first time). Offline it drives every human slot, or
 * the slots in RECOMP_TEST_BOT_SLOTS (a list such as 0,1).
 *
 * Determinism. Every choice is a function of the step, the slot, the fighters' records and a
 * seed: the match generator's additive term (0x3C1400, set from the match seed at the match
 * start, so the same on every PC of a room) mixed with RECOMP_TEST_BOT_SEED. The one thing the
 * bot keeps between steps is what each direction of the pad moved its fighter by (the camera
 * decides that, and it turns as the fighters circle), learned from its own walking.
 *
 * Buttons (pad-table word, docs/09-testing-harness.md): 0x1 0x2 0x4 0x8 up, down, left, right;
 * BOT_PUNCH, BOT_KICK, BOT_GRAB, BOT_BLOCK below were found by pressing each action bit beside
 * the opponent in a step-input probe and reading the fighter's state afterwards.
 *
 * Tests only: nothing here runs unless RECOMP_TEST_BOT is set. */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "recomp/gen/recomp_types.h"
#include "netplay.h"
#include "test_observations.h"

#define ACTORS        0x003B92E0u
#define STRIDE        0x12E8u
#define SLOTS         4
#define PAD_TABLE     0x003B8F48u
#define G_PHASE       0x003B8F30u
#define G_RNG_ADD     0x003C1400u   /* the match generator's additive term (from the match seed) */

#define F_SLOT        0x28u
#define F_STATE       0x30u
#define F_POS_X       0x44u
#define F_POS_Z       0x4Cu
#define F_FACING      0x54u
#define F_HEALTH      0x350u
#define F_HEALTH_MAX  0x354u
#define F_STUN        0x374u
#define F_STUN_CAP    0x378u
#define F_FLAGS_927   0x927u        /* bit 2 stunned (the meter was full), bit 0x10 under the knockout threshold */
#define F_CONTROL     0x937u        /* bit 0: CPU */

#define DIR_UP        0x1u
#define DIR_DOWN      0x2u
#define DIR_LEFT      0x4u
#define DIR_RIGHT     0x8u
#define BOT_PUNCH     0x80u         /* a strike: state 0x45 */
#define BOT_KICK      0x40u         /* a strike: state 0x49 */
#define BOT_GRAB      0x20u         /* the grab: state 0x1E, then 0x34 holding (the held one 0x35) */
#define BOT_BLOCK     0x2000u       /* held: state 2 */
#define BOT_MOD       0x1000u       /* the modifier held with a strike */

#define WINDOW        12            /* steps one intent lasts (a fifth of a second) */
#define REACH         12.0f         /* world units between root positions: strikes landed from 10 to 15 */

typedef struct {
    float vx[4], vz[4];             /* world motion of up, down, left, right, as learned */
    float weight[4];
    float px, pz;
    uint32_t held_dir;
    long long held_step;
    int intent;
    long long intent_window;
    float ref_dist;                 /* stuck detection: the distance to beat, and since when */
    long long ref_step;
    long long detour_until;         /* walking a detour (a wall or a corner in the way) until then */
    uint32_t detour_dir;
} BotMemory;

static BotMemory s_mem[SLOTS];
static int s_on = -1, s_aggr = 60, s_slots_mask = -1;
static uint32_t s_seed_extra;
static LONG s_lines = 400;

static uint32_t rec(int slot) { return ACTORS + (uint32_t)slot * STRIDE; }
static float f32(uint32_t a) { float v = MEMF(a); return isfinite(v) ? v : 0.0f; }
static int active(int slot) { return MEM32(rec(slot) + F_SLOT) != 0xFFFFFFFFu; }
static int byte_at(uint32_t a) { return (int)((MEM32(a & ~3u) >> (8u * (a & 3u))) & 0xFFu); }

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}

/* A number 0..99 for this step's window, slot and purpose: the same for the same state. */
static int roll(int slot, long long window, uint32_t salt)
{
    uint32_t h = mix(MEM32(G_RNG_ADD) ^ s_seed_extra ^ 0xB0710000u);
    h = mix(h ^ (uint32_t)window ^ ((uint32_t)(window >> 32) * 0x9E3779B9u));
    h = mix(h ^ ((uint32_t)slot * 0x85EBCA6Bu) ^ salt);
    return (int)(h % 100u);
}

static void configure(void)
{
    const char *v = getenv("RECOMP_TEST_BOT"), *slots = getenv("RECOMP_TEST_BOT_SLOTS"), *seed;
    s_on = v && *v && *v != '0';
    if (!s_on) return;
    if (strchr(v, ',')) {
        s_aggr = atoi(strchr(v, ',') + 1);
        if (s_aggr < 0) s_aggr = 0;
        if (s_aggr > 100) s_aggr = 100;
    }
    if (slots && *slots) {
        s_slots_mask = 0;
        for (; *slots; slots++)
            if (*slots >= '0' && *slots < '0' + SLOTS) s_slots_mask |= 1 << (*slots - '0');
    }
    seed = getenv("RECOMP_TEST_BOT_SEED");
    s_seed_extra = seed ? (uint32_t)strtoul(seed, NULL, 0) : 0;
    fprintf(stderr, "[TEST-BOT] on: aggression %d, slots %s\n", s_aggr,
            s_slots_mask < 0 ? "the human ones (online: this PC's)" : getenv("RECOMP_TEST_BOT_SLOTS"));
}

int defjam_test_bot_enabled(void)
{
    if (s_on < 0) configure();
    return s_on;
}

void defjam_test_bot_match_start(void)
{
    int i, d;
    memset(s_mem, 0, sizeof(s_mem));
    /* a first guess before anything is learned, from the camera at the start (it looks along +z,
     * player 1 on the left): right is +x (the scripted routes walk player 1 to player 2 with
     * "right"), left -x, up +z, down -z. Walking corrects it as the camera turns. */
    for (i = 0; i < SLOTS; i++) {
        for (d = 0; d < 4; d++) s_mem[i].weight[d] = 0.2f;
        s_mem[i].vz[0] = 1.0f;
        s_mem[i].vz[1] = -1.0f;
        s_mem[i].vx[2] = -1.0f;
        s_mem[i].vx[3] = 1.0f;
        s_mem[i].held_step = -10;
    }
}

static int dir_index(uint32_t d)
{
    return d == DIR_UP ? 0 : d == DIR_DOWN ? 1 : d == DIR_LEFT ? 2 : d == DIR_RIGHT ? 3 : -1;
}

/* Learn what the direction held last step moved the fighter by. */
static void learn(BotMemory *m, int slot, long long step)
{
    float x = f32(rec(slot) + F_POS_X), z = f32(rec(slot) + F_POS_Z);
    int d = dir_index(m->held_dir);
    if (d >= 0 && m->held_step == step - 1 && !netplay_rerunning()) {
        float dx = x - m->px, dz = z - m->pz, len = sqrtf(dx * dx + dz * dz);
        if (len > 0.02f && len < 1.0f) {         /* a walk, not a throw or a placement */
            float a = 0.15f;
            m->vx[d] = m->vx[d] * (1.0f - a) + dx / len * a;
            m->vz[d] = m->vz[d] * (1.0f - a) + dz / len * a;
            if (m->weight[d] < 1.0f) m->weight[d] += 0.05f;
        } else if (len <= 0.02f && MEM32(rec(slot) + F_STATE) == 1u && m->weight[d] > 0.05f) {
            m->weight[d] -= 0.01f;              /* standing while it is held: a wall, or a wrong guess */
        }
    }
    m->px = x;
    m->pz = z;
}

/* The pad direction (one or two of the four bits) whose learned motion points most along (wx, wz). */
static uint32_t direction_toward(const BotMemory *m, float wx, float wz)
{
    static const uint32_t combos[8] = { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT,
                                        DIR_UP | DIR_LEFT, DIR_UP | DIR_RIGHT, DIR_DOWN | DIR_LEFT, DIR_DOWN | DIR_RIGHT };
    float len = sqrtf(wx * wx + wz * wz), best = -2.0f;
    uint32_t pick = 0;
    int i, d;
    if (len < 1e-3f) return 0;
    wx /= len; wz /= len;
    for (i = 0; i < 8; i++) {
        float x = 0, z = 0, l, score, sure = 1.0f;
        for (d = 0; d < 4; d++)
            if (combos[i] & (1u << d)) {      /* the bit values are 1 << index: up 1, down 2, left 4, right 8 */
                x += m->vx[d] * m->weight[d];
                z += m->vz[d] * m->weight[d];
                if (m->weight[d] < sure) sure = m->weight[d];
            }
        l = sqrtf(x * x + z * z);
        if (l < 1e-3f) continue;
        score = (x * wx + z * wz) / l * (0.5f + 0.5f * sure);     /* the aim, and how sure it is */
        if (score > best) { best = score; pick = combos[i]; }
    }
    return pick;
}

enum { I_APPROACH, I_STRIKE, I_HEAVY, I_GRAB, I_BLOCK, I_CIRCLE, I_FINISH };

static int nearest_opponent(int slot, float *dist)
{
    int i, best = -1;
    float x = f32(rec(slot) + F_POS_X), z = f32(rec(slot) + F_POS_Z), bd = 1e9f;
    for (i = 0; i < SLOTS; i++) {
        float dx, dz, d;
        if (i == slot || !active(i)) continue;
        dx = f32(rec(i) + F_POS_X) - x;
        dz = f32(rec(i) + F_POS_Z) - z;
        d = sqrtf(dx * dx + dz * dz);
        if (d < bd) { bd = d; best = i; }
    }
    *dist = bd;
    return best;
}

/* The buttons for `slot` at fight step `step`. */
static uint32_t decide(int slot, long long step)
{
    BotMemory *m = &s_mem[slot];
    long long window = step / WINDOW;
    int phase_in = (int)(step % WINDOW), opp, r;
    float dist, ox, oz, x, z, ratio, opp_ratio;
    uint32_t me = rec(slot), them, buttons = 0, toward;
    int opp_low, opp_stunned;

    learn(m, slot, step);
    if (MEM32(G_PHASE) != 1u) return 0;
    opp = nearest_opponent(slot, &dist);
    if (opp < 0) return 0;
    them = rec(opp);
    x = f32(me + F_POS_X); z = f32(me + F_POS_Z);
    ox = f32(them + F_POS_X); oz = f32(them + F_POS_Z);
    toward = direction_toward(m, ox - x, oz - z);
    ratio = f32(me + F_HEALTH) / (f32(me + F_HEALTH_MAX) > 0 ? f32(me + F_HEALTH_MAX) : 1.0f);
    opp_ratio = f32(them + F_HEALTH) / (f32(them + F_HEALTH_MAX) > 0 ? f32(them + F_HEALTH_MAX) : 1.0f);
    opp_low = (byte_at(them + F_FLAGS_927) & 0x10) || opp_ratio < 0.1f;
    opp_stunned = (byte_at(them + F_FLAGS_927) & 0x04) != 0;
    /* the opponent winds up a strike within reach (states 0x45..0x4B): block it, now and then */
    {
        uint32_t st = MEM32(them + F_STATE);
        if (st >= 0x45u && st <= 0x4Bu && dist < REACH * 1.3f && m->intent != I_BLOCK &&
            roll(slot, window, 8) < 15 + (100 - s_aggr) / 3 && phase_in == 0)
            m->intent_window = -1;      /* choose again below, as a block */
    }

    /* Not getting closer for a while (a wall, a corner, a wrong idea of the camera): walk a
     * detour in a direction picked by the roll, which also teaches what that direction does. */
    if (dist > REACH) {
        if (!m->ref_step || dist < m->ref_dist - 1.0f) {
            m->ref_dist = dist;
            m->ref_step = step;
        } else if (step - m->ref_step > 45 && step >= m->detour_until) {
            static const uint32_t dirs[4] = { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
            m->detour_dir = dirs[roll(slot, step, 9) % 4];
            m->detour_until = step + 40;
            m->ref_dist = dist;
            m->ref_step = step + 40;
        }
    } else {
        m->ref_step = 0;
    }
    if (window != m->intent_window) {
        int reblock = m->intent_window == -1;
        m->intent_window = window;
        r = roll(slot, window, 1);
        if (reblock) {
            m->intent = I_BLOCK;
        } else if (dist > REACH) {
            m->intent = (r < 15 + (100 - s_aggr) / 5) ? I_CIRCLE : I_APPROACH;
        } else if (opp_low || opp_stunned) {
            m->intent = r < 55 ? I_FINISH : r < 80 ? I_GRAB : I_HEAVY;
        } else {
            int block = 8 + (100 - s_aggr) / 4 + (ratio < 0.3f ? 10 : 0);
            int grab = 22;
            if (r < block) m->intent = I_BLOCK;
            else if (r < block + grab) m->intent = I_GRAB;
            else if (r < block + grab + 15) m->intent = I_HEAVY;
            else if (r < block + grab + 15 + 6) m->intent = I_CIRCLE;
            else m->intent = I_STRIKE;
        }
    }
    switch (m->intent) {
    case I_APPROACH:
        buttons = toward;
        break;
    case I_CIRCLE:      /* sideways to the opponent: the perpendicular of the line between them */
        buttons = roll(slot, window, 2) < 50 ? direction_toward(m, -(oz - z), ox - x)
                                             : direction_toward(m, oz - z, -(ox - x));
        break;
    case I_BLOCK:
        buttons = BOT_BLOCK;
        break;
    case I_STRIKE:      /* a string: a press every four steps, punch or kick by the roll */
        if (phase_in % 4 == 0) buttons = roll(slot, step / 4, 3) < 55 ? BOT_PUNCH : BOT_KICK;
        break;
    case I_HEAVY:       /* a strong strike: held modifier with a press */
        buttons = BOT_MOD;
        if (phase_in % 6 == 1) buttons |= roll(slot, step / 6, 4) < 50 ? BOT_PUNCH : BOT_KICK;
        break;
    case I_GRAB:        /* grab, then the grapple's follow-ups (strikes and directions) */
        if (phase_in == 0) buttons = BOT_GRAB;
        else if (phase_in % 4 == 2) buttons = (roll(slot, step / 4, 5) < 50 ? BOT_PUNCH : BOT_KICK) |
                                              (roll(slot, step / 4, 6) < 50 ? toward : 0);
        break;
    case I_FINISH:      /* the opponent can be knocked out: grab and heavy strikes toward them */
        if (dist > REACH * 0.7f) buttons = toward;
        else if (phase_in % 6 == 0) buttons = BOT_GRAB;
        else if (phase_in % 6 == 3) buttons = BOT_MOD | (roll(slot, step / 6, 7) < 50 ? BOT_PUNCH : BOT_KICK);
        else if (phase_in % 6 == 4) buttons = BOT_MOD;
        break;
    }
    if (dist > REACH && m->intent != I_APPROACH && m->intent != I_CIRCLE && m->intent != I_FINISH)
        buttons |= toward;      /* never stand off out of reach */
    if (step < m->detour_until && dist > REACH) buttons = m->detour_dir;
    m->held_dir = buttons & 0xFu;
    m->held_step = step;
    if (s_lines > 0 && step % 300 == 7) {
        InterlockedDecrement(&s_lines);
        fprintf(stderr, "[TEST-BOT] step %lld slot %d: opponent %d at %.1f, intent %d, buttons %04X, health %.2f/%.2f%s%s\n",
                step, slot, opp, (double)dist, m->intent, buttons, (double)ratio, (double)opp_ratio,
                opp_low ? ", opponent knockable" : "", opp_stunned ? ", opponent stunned" : "");
    }
    return buttons;
}

/* sub_001BAA00's entry (through defjam_test_input): 1 when the bot wrote this fighter's pad. */
int defjam_test_bot_input(uint32_t actor, int slot, long long step)
{
    uint32_t base, buttons;
    int local;
    (void)actor;
    if (!defjam_test_bot_enabled() || slot < 0 || slot >= SLOTS || step < 1) return 0;
    local = netplay_local_slot();
    if (local >= 0) {
        if (slot != local) return 0;
    } else if (s_slots_mask >= 0) {
        if (!(s_slots_mask & (1 << slot))) return 0;
    } else if (byte_at(rec(slot) + F_CONTROL) & 1) {
        return 0;               /* a CPU fighter: the game's own AI */
    }
    buttons = decide(slot, step);
    base = PAD_TABLE + (uint32_t)slot * 0x20u;
    MEM32(base) = buttons;
    MEM32(base + 4) = 0;
    MEM32(base + 8) = MEM32(base + 12) = MEM32(base + 16) = MEM32(base + 20) = 0;   /* sticks at rest */
    MEM32(base + 24) = 0;
    MEM32(base + 28) = 0;       /* a fresh poll: netplay.c takes it as this PC's input */
    return 1;
}
