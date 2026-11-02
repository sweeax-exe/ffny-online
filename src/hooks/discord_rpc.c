/* Discord Rich Presence: frames, JSON, text rules and the composer. See discord_rpc.h. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>

#include "discord_rpc.h"

#define DOT " \xC2\xB7 "    /* " · " */

void drpc_put_header(uint8_t *out, uint32_t op, uint32_t len)
{
    int i;
    for (i = 0; i < 4; i++) {
        out[i] = (uint8_t)(op >> (8 * i));
        out[4 + i] = (uint8_t)(len >> (8 * i));
    }
}

int drpc_get_header(const uint8_t *in, int have, uint32_t *op, uint32_t *len)
{
    if (have < DRPC_HEADER) return 0;
    *op = (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    *len = (uint32_t)in[4] | ((uint32_t)in[5] << 8) | ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 24);
    return 1;
}

int drpc_frame(uint8_t *out, int cap, uint32_t op, const char *payload, int len)
{
    if (len < 0 || cap < DRPC_HEADER || len > cap - DRPC_HEADER) return 0;
    drpc_put_header(out, op, (uint32_t)len);
    if (len) memcpy(out + DRPC_HEADER, payload, (size_t)len);
    return DRPC_HEADER + len;
}

/* The length of the UTF-8 sequence at s (1..4), or 0 when it is not a valid one. */
static int utf8_len(const unsigned char *s)
{
    int n, i;
    unsigned c = s[0];
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) n = 2;
    else if (c >= 0xE0 && c <= 0xEF) n = 3;
    else if (c >= 0xF0 && c <= 0xF4) n = 4;
    else return 0;
    for (i = 1; i < n; i++)
        if ((s[i] & 0xC0) != 0x80) return 0;
    if (n == 3 && c == 0xE0 && s[1] < 0xA0) return 0;   /* overlong */
    if (n == 3 && c == 0xED && s[1] > 0x9F) return 0;   /* surrogates */
    if (n == 4 && c == 0xF0 && s[1] < 0x90) return 0;
    if (n == 4 && c == 0xF4 && s[1] > 0x8F) return 0;
    return n;
}

int drpc_clean(char *out, int size, const char *in, int max)
{
    const unsigned char *s = (const unsigned char *)(in ? in : "");
    int n = 0, blank = 1;   /* blank: the last character written was a space (or nothing yet) */
    if (size <= 0) return 0;
    if (max > size - 1) max = size - 1;
    while (*s) {
        int k = utf8_len(s);
        if (k == 1 && (*s < 0x20 || *s == 0x7F || *s == ' ')) {   /* control characters read as blanks */
            if (!blank && n < max) { out[n++] = ' '; blank = 1; }
            s++;
            continue;
        }
        if (k == 0) {                   /* broken UTF-8 */
            if (n + 1 > max) break;
            out[n++] = '?';
            blank = 0;
            s++;
            continue;
        }
        if (n + k > max) break;         /* cut at a character boundary */
        memcpy(out + n, s, (size_t)k);
        n += k;
        s += k;
        blank = 0;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n;
}

int drpc_json_escape(char *out, int size, int *pos, const char *in)
{
    const unsigned char *s = (const unsigned char *)in;
    int p = *pos;
    for (; *s; s++) {
        char esc[8];
        int k = 0;
        if (*s == '"' || *s == '\\') { esc[0] = '\\'; esc[1] = (char)*s; k = 2; }
        else if (*s == '\n') { esc[0] = '\\'; esc[1] = 'n'; k = 2; }
        else if (*s == '\r') { esc[0] = '\\'; esc[1] = 'r'; k = 2; }
        else if (*s == '\t') { esc[0] = '\\'; esc[1] = 't'; k = 2; }
        else if (*s < 0x20 || *s == 0x7F) k = snprintf(esc, sizeof(esc), "\\u%04x", *s);
        else { esc[0] = (char)*s; k = 1; }
        if (p + k >= size) return 0;
        memcpy(out + p, esc, (size_t)k);
        p += k;
    }
    out[p] = 0;
    *pos = p;
    return 1;
}

/* Appends printf-style text; 0 once out is full. */
static int put(char *out, int size, int *pos, const char *text)
{
    int k = (int)strlen(text);
    if (*pos + k >= size) return 0;
    memcpy(out + *pos, text, (size_t)k + 1);
    *pos += k;
    return 1;
}

/* ,"key":"text" for a text Discord accepts (cleaned; left out when shorter than the minimum). */
static int put_text(char *out, int size, int *pos, int *first, const char *key, const char *text, int max)
{
    char clean[DRPC_TEXT_MAX * 4 + 1];
    if (drpc_clean(clean, sizeof(clean), text, max) < DRPC_TEXT_MIN) return 1;
    if (!*first && !put(out, size, pos, ",")) return 0;
    *first = 0;
    return put(out, size, pos, "\"") && put(out, size, pos, key) && put(out, size, pos, "\":\"") &&
           drpc_json_escape(out, size, pos, clean) && put(out, size, pos, "\"");
}

int drpc_handshake_json(char *out, int size, const char *client_id)
{
    int pos = 0;
    if (size > 0) out[0] = 0;
    if (!put(out, size, &pos, "{\"v\":1,\"client_id\":\"") || !drpc_json_escape(out, size, &pos, client_id) ||
        !put(out, size, &pos, "\"}"))
        return 0;
    return pos;
}

int drpc_set_activity_json(char *out, int size, unsigned long pid, const DrpcActivity *a, unsigned nonce)
{
    char num[48];
    int pos = 0, first;
    if (size > 0) out[0] = 0;
    snprintf(num, sizeof(num), "%lu", pid);
    if (!put(out, size, &pos, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":") || !put(out, size, &pos, num))
        return 0;
    if (a) {
        if (!put(out, size, &pos, ",\"activity\":{")) return 0;
        first = 1;
        if (!put_text(out, size, &pos, &first, "details", a->details, DRPC_TEXT_MAX) ||
            !put_text(out, size, &pos, &first, "state", a->state, DRPC_TEXT_MAX))
            return 0;
        if (a->start > 0) {
            snprintf(num, sizeof(num), "%s\"timestamps\":{\"start\":%lld}", first ? "" : ",", a->start);
            if (!put(out, size, &pos, num)) return 0;
            first = 0;
        }
        if (a->large_image[0] || a->small_image[0]) {
            int afirst = 1;
            if (!put(out, size, &pos, first ? "\"assets\":{" : ",\"assets\":{")) return 0;
            first = 0;
            if (a->large_image[0] &&
                (!put_text(out, size, &pos, &afirst, "large_image", a->large_image, DRPC_KEY_MAX) ||
                 !put_text(out, size, &pos, &afirst, "large_text", a->large_text, DRPC_TEXT_MAX)))
                return 0;
            if (a->small_image[0] &&
                (!put_text(out, size, &pos, &afirst, "small_image", a->small_image, DRPC_KEY_MAX) ||
                 !put_text(out, size, &pos, &afirst, "small_text", a->small_text, DRPC_TEXT_MAX)))
                return 0;
            if (!put(out, size, &pos, "}")) return 0;
        }
        if (a->buttons) {
            if (!put(out, size, &pos, first ? "\"buttons\":[" : ",\"buttons\":[") ||
                !put(out, size, &pos, "{\"label\":\"" DRPC_BUTTON1_LABEL "\",\"url\":\"" DRPC_BUTTON1_URL "\"},") ||
                !put(out, size, &pos, "{\"label\":\"" DRPC_BUTTON2_LABEL "\",\"url\":\"" DRPC_BUTTON2_URL "\"}]"))
                return 0;
            first = 0;
        }
        if (!put(out, size, &pos, "}")) return 0;
    }
    snprintf(num, sizeof(num), "},\"nonce\":\"%u\"}", nonce);
    if (!put(out, size, &pos, num)) return 0;
    return pos;
}

int drpc_same(const DrpcActivity *a, const DrpcActivity *b)
{
    return !strcmp(a->details, b->details) && !strcmp(a->state, b->state) && a->start == b->start &&
           !strcmp(a->large_image, b->large_image) && !strcmp(a->large_text, b->large_text) &&
           !strcmp(a->small_image, b->small_image) && !strcmp(a->small_text, b->small_text) &&
           a->buttons == b->buttons;
}

/* ---- the composer ------------------------------------------------------------------------------ */

static void set(char *field, const char *text)
{
    drpc_clean(field, DRPC_TEXT_MAX + 1, text, DRPC_TEXT_MAX);
}

/* A player as the state line names them: their name, or CPU. */
static const char *who(const DrpcGameState *g, int pos, char *buf, int size)
{
    if (pos < 0 || pos >= g->count || pos >= 4) return "CPU";
    drpc_clean(buf, size, g->names[pos], DRPC_NAME - 1);
    return buf[0] ? buf : "CPU";
}

/* "A", "A & B", "A, B & C" */
static void list_names(const DrpcGameState *g, const int *pos, int n, char *out, int size)
{
    char name[DRPC_NAME];
    int i, len = 0;
    out[0] = 0;
    for (i = 0; i < n; i++) {
        const char *sep = i == 0 ? "" : i == n - 1 ? " & " : ", ";
        len += snprintf(out + len, (size_t)(size - len), "%s%s", sep, who(g, pos[i], name, sizeof(name)));
        if (len >= size) { out[size - 1] = 0; return; }
    }
}

/* The second line of a match: who plays whom. */
static void match_state(const DrpcGameState *g, char *out, int size)
{
    char names[160], fighter[DRPC_NAME];
    int others[4], n = 0, i, count = g->count > 4 ? 4 : g->count;
    drpc_clean(fighter, sizeof(fighter), g->you >= 0 && g->you < 4 ? g->fighters[g->you] : "", DRPC_NAME - 1);
    if (g->format == DRPC_TEAM && count == 4 && g->you >= 0 && g->you < 4) {
        int partner = g->you ^ 1, base = g->you < 2 ? 2 : 0;
        char mate[DRPC_NAME];
        others[0] = base;
        others[1] = base + 1;
        list_names(g, others, 2, names, sizeof(names));
        snprintf(out, (size_t)size, "with %s vs %s", who(g, partner, mate, sizeof(mate)), names);
        return;
    }
    for (i = 0; i < count; i++)
        if (i != g->you) others[n++] = i;
    list_names(g, others, n, names, sizeof(names));
    if (g->format == DRPC_ALL_VS_ALL || !fighter[0])
        snprintf(out, (size_t)size, "vs %s", names);
    else
        snprintf(out, (size_t)size, "as %s vs %s", fighter, names);
}

/* "<text> · <more>" when it fits Discord's limit, else text alone (more alone when text is empty). */
static void append_if_fits(char *text, int size, const char *more)
{
    size_t a = strlen(text), b = strlen(more);
    if (!more[0]) return;
    if (!a) {
        if (b <= DRPC_TEXT_MAX && (int)b < size) memcpy(text, more, b + 1);
        return;
    }
    if (a + 4 + b <= DRPC_TEXT_MAX && (int)(a + 4 + b) < size) {
        memcpy(text + a, DOT, 4);
        memcpy(text + a + 4, more, b + 1);
    }
}

static void players_text(const DrpcGameState *g, char *out, int size)
{
    out[0] = 0;
    if (g->players > 0 && g->max > 0) snprintf(out, (size_t)size, "%d/%d players", g->players, g->max);
}

void drpc_compose(const DrpcGameState *g, DrpcActivity *out)
{
    char details[256], state[256], players[32], server[DRPC_NAME], large[256];
    drpc_clean(server, sizeof(server), g->server, DRPC_NAME - 1);
    players_text(g, players, sizeof(players));
    details[0] = state[0] = 0;
    snprintf(large, sizeof(large), "%s", DRPC_LARGE_TEXT);
    switch (g->where) {
    case DRPC_IN_LAUNCHER:
        snprintf(details, sizeof(details), "In the launcher");
        break;
    case DRPC_IN_MENUS:
    default: {
        static const char *const names[] = { "In the menus", "Main menu", "Story mode", "Battle mode" };
        int off = g->offline >= 0 && g->offline <= DRPC_OFF_BATTLE ? g->offline : DRPC_OFF_MENUS;
        if (g->offline_fight && (off == DRPC_OFF_STORY || off == DRPC_OFF_BATTLE)) {
            snprintf(details, sizeof(details), "%s", names[off]);
            snprintf(state, sizeof(state), "In a fight");
        } else if (g->offline_fight) {
            snprintf(details, sizeof(details), "In a fight");
        } else {
            snprintf(details, sizeof(details), "%s", names[off]);
        }
        break;
    }
    case DRPC_IN_LOBBY:
        snprintf(details, sizeof(details), "Online lobby");
        if (g->ranked_search) snprintf(state, sizeof(state), "Ranked search");
        append_if_fits(state, sizeof(state), server);
        break;
    case DRPC_IN_ROOM:
        if (g->koth) {
            snprintf(details, sizeof(details), "King of the Hill");
            if (g->koth_king)
                snprintf(state, sizeof(state), "King, %d win%s in a row", g->koth_streak, g->koth_streak == 1 ? "" : "s");
            else
                snprintf(state, sizeof(state), "Waiting in line");
            append_if_fits(state, sizeof(state), server);
            append_if_fits(state, sizeof(state), players);
        } else {
            snprintf(details, sizeof(details), "In a room: %s", g->mode[0] ? g->mode : "Online");
            snprintf(state, sizeof(state), "%s", server);
            append_if_fits(state, sizeof(state), players);
        }
        break;
    case DRPC_IN_MATCH:
        snprintf(details, sizeof(details), "%s", g->koth ? "King of the Hill" : g->mode[0] ? g->mode : "Online match");
        if (g->venue[0]) append_if_fits(details, sizeof(details), g->venue);
        match_state(g, state, sizeof(state));
        if (g->koth && g->koth_king) {
            char crowned[256];
            snprintf(crowned, sizeof(crowned), "King" DOT "%s", state);
            if (strlen(crowned) <= DRPC_TEXT_MAX) snprintf(state, sizeof(state), "%s", crowned);
        }
        append_if_fits(state, sizeof(state), server);
        append_if_fits(large, sizeof(large), server);
        append_if_fits(large, sizeof(large), players);
        break;
    }
    set(out->details, details);
    set(out->state, state);
    set(out->large_text, large);
    snprintf(out->large_image, sizeof(out->large_image), "%s", DRPC_LARGE_IMAGE);
}
