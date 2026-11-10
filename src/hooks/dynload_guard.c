/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* EAGL's DynamicLoader::Resolve (sub_0010AC20) checked before it runs.
 *
 * Resolve links a loaded ELF object in place (docs/research/rollback-async-loads.md 1.3): for every
 * section of type 9 (REL) it walks the entries {r_offset, r_info}, reads the symbol r_info >> 8 of the
 * section's symbol table, resolves an undefined one by name (the other loaded objects, the object's
 * callback at obj+0x42C, the global pool), then patches the word at target section + r_offset. It
 * trusts every one of those numbers. A player's crash (report 5519e0, build 70, main menu after a
 * title switch) died reading a symbol at 0x89D53B60: the relocation entry held text, the symbol
 * before it had type 9, and the callback was 0x00000F68. The object or its image had been overwritten
 * before Resolve ran.
 *
 * So before Resolve, everything it will read is checked against the two guest windows the runtime
 * maps and against the object's own tables: the object, the ELF header, the section table, the
 * symbol table, every relocation entry (its symbol inside the symbol table and of a type Resolve
 * knows, its name readable when it is undefined, its offset inside the target section), the
 * callback, and the other loaded objects Resolve would search. A damaged object is taken back the
 * way a failed dlopen leaves its handle (handle[0] = 0, unlinked from the loaded-object list at
 * 0x3A52A8, its memory left alone since its pointers cannot be trusted to free): Resolve then returns
 * at once without instantiating anything, and the caller sees a load that did not happen. The
 * [DYNLOAD] line names the damage, the caller, the image and the last loads before it.
 *
 * Read only for a sound object; cheap next to the load itself (a few thousand entries at most). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "recomp/gen/recomp_types.h"
#include "dynload_guard.h"

#define LIST_HEAD       0x003A52A8u     /* the loaded objects: +0 next, +4 prev */
#define MODEL_LOADING   0x0033DB50u     /* CModel::_InitData's "model being loaded" (0 otherwise) */
#define OBJ_SIZE        0x430u
#define SECTION_SIZE    0x28u
#define SHT_REL         9u
#define LIST_MAX        65536u
#define NAME_MAX_LEN    512u
#define MAX_SYMBOLS     0x100000u
#define MAX_SECTIONS    4096u

static unsigned g_checked, g_refused, g_entries;
static int g_undefined, g_trace = -1;
static uint32_t g_bad;                  /* the guest address of what was damaged, for the dump */
static char g_detail[160];

static struct { uint32_t caller, handle, image, size; } g_recent[4];
static unsigned g_recent_n;

unsigned dynload_guard_checked(void) { return g_checked; }
unsigned dynload_guard_refused(void) { return g_refused; }

/* The windows the runtime maps (src/hooks/watchpoint.c guest_readable): the low one and the
 * contiguous one. */
static int readable(uint32_t a, uint32_t len)
{
    uint64_t end = (uint64_t)a + len;
    return (a >= 0x1000u && end <= 0x04000000u) || (a >= 0x80000000u && end <= 0x84000000u);
}

static const char *bad(uint32_t at, const char *what, const char *format, uint32_t a, uint32_t b)
{
    g_bad = at;
    snprintf(g_detail, sizeof(g_detail), format, a, b);
    return what;
}

static int name_ok(uint32_t s)
{
    uint32_t i;
    for (i = 0; i < NAME_MAX_LEN; i++) {
        if (!readable(s + i, 1)) return 0;
        if (!MEM8(s + i)) return 1;
    }
    return 0;
}

/* What Resolve would read, in its order; NULL when all of it is sound. */
static const char *check(uint32_t obj)
{
    uint32_t eh, sh, shnum, nsym, symtab, strtab, fallback, i, k, node, steps;
    int undefined = 0;
    g_entries = 0;
    eh = MEM32(obj + 0x1C);
    sh = MEM32(obj + 0x18);
    if (!readable(eh, 0x34)) return bad(obj + 0x1C, "ELF header", "at %08X", eh, 0);
    shnum = MEM16(eh + 0x30);
    if (shnum > MAX_SECTIONS || (shnum && !readable(sh, shnum * SECTION_SIZE)))
        return bad(obj + 0x18, "section table", "%u sections at %08X", shnum, sh);
    nsym = MEM32(obj + 0x10);
    symtab = MEM32(obj + 0x14);
    if (nsym > MAX_SYMBOLS || (nsym && !readable(symtab, nsym * 16u)))
        return bad(obj + 0x14, "symbol table", "%u symbols at %08X", nsym, symtab);
    strtab = MEM32(obj + 8);
    for (i = 0; i < shnum; i++) {
        uint32_t s = sh + i * SECTION_SIZE, size, rel, link, target, tsize = 0, j;
        int32_t count;
        int tknown = 0;
        if (MEM32(s + 4) != SHT_REL) continue;
        size = MEM32(s + 0x14);
        count = (int32_t)size / 8;                  /* Resolve: cdq, and 7, add, sar 3 */
        if (count == 0) continue;
        rel = MEM32(s + 0x10);
        link = MEM32(s + 0x18);
        target = MEM32(s + 0x1C);
        if (count < 0 || !readable(rel, (uint32_t)count * 8u))
            return bad(s + 0x10, "relocation section", "%u bytes at %08X", size, rel);
        /* the section the entries patch: the one its address is, the largest if several are */
        for (j = 0; j < shnum; j++) {
            uint32_t t = sh + j * SECTION_SIZE;
            if (MEM32(t + 0x10) == target && (!tknown || MEM32(t + 0x14) > tsize)) {
                tsize = MEM32(t + 0x14);
                tknown = 1;
            }
        }
        for (k = 0; k < (uint32_t)count; k++) {
            uint32_t e = rel + 8u * k, off = MEM32(e), info = MEM32(e + 4), index = info >> 8;
            uint32_t sym = link + (index << 4), type = info & 0xFFu, shndx, width;
            g_entries++;
            if (!readable(sym, 16) || (link == symtab && index >= nsym))
                return bad(e, "relocation symbol", "symbol %u at %08X", index, sym);
            if ((MEM8(sym + 0xC) & 0xFu) > 3u)
                return bad(sym, "symbol type", "type %u, symbol %u", MEM8(sym + 0xC) & 0xFu, index);
            shndx = MEM16(sym + 0xE);
            if (shndx == 0 || shndx >= shnum) {     /* undefined: looked up by its name */
                undefined = 1;
                if (!name_ok(strtab + MEM32(sym)))
                    return bad(sym, "symbol name", "name at %08X, symbol %u", strtab + MEM32(sym), index);
            }
            if (type > 12u)
                return bad(e, "relocation type", "type %u, entry %u", type, k);
            width = type == 5u || type == 6u ? 2u : 4u;
            if (!readable(target + off, 4) || (tknown && ((uint64_t)off + width > tsize)))
                return bad(e, "relocation offset", "offset %08X in a section of %u bytes", off, tsize);
        }
    }
    g_undefined = undefined;
    if (!undefined) return NULL;
    /* undefined symbols: the callback and every other loaded object are asked */
    fallback = MEM32(obj + 0x42C);
    if (fallback && !dynload_code_ok(fallback))
        return bad(obj + 0x42C, "callback", "%08X is not code", fallback, 0);
    for (node = MEM32(LIST_HEAD), steps = 0; node; node = MEM32(node), steps++) {
        if (steps >= LIST_MAX) return bad(LIST_HEAD, "object list", "more than %u objects", LIST_MAX, 0);
        if (!readable(node, OBJ_SIZE)) return bad(node, "object list", "object %08X after %u", node, steps);
        if (node == obj || !MEM32(node + 0x10)) continue;
        if (!readable(MEM32(node + 0x14), 16) || !readable(MEM32(node + 0x420), 4) ||
            !readable(MEM32(node + 0x424), 1) || !readable(MEM32(node + 8), 1))
            return bad(node, "loaded object", "object %08X (%u symbols)", node, MEM32(node + 0x10));
    }
    return NULL;
}

/* handle[0] = 0 and the object out of the list, as a failed dlopen leaves them. */
static void take_back(uint32_t handle, uint32_t obj)
{
    uint32_t prev = 0, node = MEM32(LIST_HEAD), steps = 0, next = 0;
    if (readable(obj, 8)) next = MEM32(obj);
    if (next && !readable(next, OBJ_SIZE)) next = 0;    /* the rest of the list is lost, not followed */
    while (node && steps++ < LIST_MAX) {
        if (node == obj) {
            if (prev) MEM32(prev) = next;
            else MEM32(LIST_HEAD) = next;
            if (next) MEM32(next + 4) = prev;
            break;
        }
        if (!readable(node, 8)) break;
        prev = node;
        node = MEM32(node);
    }
    MEM32(handle) = 0;
}

static void dump(const char *label, uint32_t at)
{
    char hex[3 * 32 + 1], text[33];
    uint32_t i, start = at & ~15u;
    int n = 0;
    if (!readable(start, 32)) { fprintf(stderr, "[DYNLOAD]   %s %08X: not in guest memory\n", label, at); return; }
    for (i = 0; i < 32; i++) {
        uint8_t c = MEM8(start + i);
        n += snprintf(hex + n, sizeof(hex) - (size_t)n, "%02X ", c);
        text[i] = c >= 0x20 && c < 0x7F ? (char)c : '.';
    }
    text[32] = 0;
    fprintf(stderr, "[DYNLOAD]   %s %08X: %s|%s|\n", label, start, hex, text);
}

/* At dlopen (sub_00109F80), before it edits the image in place: a log line when the image is no ELF
 * object or its section table runs past it. Read only: dlopen then goes on as the game wrote it, and
 * the check at Resolve is the one that refuses. The line comes first in a report, with the caller. */
void dynload_guard_image(uint32_t handle, uint32_t caller, uint32_t outer)
{
    static unsigned told;
    uint32_t image, size, shoff, shnum;
    const char *why = NULL;
    if (!readable(handle, 0x30)) return;
    image = MEM32(handle + 0x10);
    size = MEM32(handle + 0x14);
    if (!readable(image, 0x34)) why = "not in guest memory";
    else if (MEM32(image) != 0x464C457Fu) why = "no ELF header";
    else {
        shoff = MEM32(image + 0x20);
        shnum = MEM16(image + 0x30);
        if (MEM16(image + 0x2E) != SECTION_SIZE) why = "a section header size other than 40";
        else if (shoff < size && (uint64_t)shoff + (uint64_t)shnum * SECTION_SIZE > size)
            why = "a section table past the image's end";
    }
    if (!why || told >= 20) return;
    told++;
    fprintf(stderr, "[DYNLOAD] dlopen of a damaged image: %s; caller %08X (from %08X), handle %08X, image %08X "
            "(%u bytes)\n", why, caller, outer, handle, image, size);
    dump("image", image);
    fflush(stderr);
}

int dynload_guard_check(uint32_t handle, uint32_t caller, uint32_t outer)
{
    uint32_t obj, image, size;
    const char *why;
    unsigned i;
    if (!readable(handle, 0x30)) {
        fprintf(stderr, "[DYNLOAD] Resolve of handle %08X (caller %08X): the handle is not in guest memory\n",
                handle, caller);
        return 0;
    }
    obj = MEM32(handle);
    image = MEM32(handle + 0x10);
    size = MEM32(handle + 0x14);
    if (!obj) return 0;                                 /* dlopen failed: Resolve returns */
    g_checked++;
    if (!readable(obj, OBJ_SIZE)) why = bad(handle, "object", "at %08X", obj, 0);
    else if (MEM8(obj + 0xC)) return 0;                 /* resolved already: Resolve returns */
    else why = check(obj);
    if (!why) {
        g_recent[g_recent_n % 4].caller = outer ? outer : caller;
        g_recent[g_recent_n % 4].handle = handle;
        g_recent[g_recent_n % 4].image = image;
        g_recent[g_recent_n % 4].size = size;
        g_recent_n++;
        if (g_trace < 0) g_trace = getenv("RECOMP_DYNLOAD_TRACE") != NULL;
        if (g_trace)
            fprintf(stderr, "[DYNLOAD] checked #%u: caller %08X, handle %08X, image %08X (%u bytes), %u relocations, "
                    "%s\n", g_checked, caller, handle, image, size, g_entries, g_undefined ? "undefined symbols" : "all defined");
        return 0;
    }
    g_refused++;
    take_back(handle, obj);
    if (g_refused <= 20) {
        fprintf(stderr, "[DYNLOAD] Resolve refused a damaged object (#%u of %u loads): %s, %s; caller %08X (from %08X), "
                "handle %08X, object %08X, image %08X (%u bytes), owner %08X, callback %08X, model being loaded "
                "%08X; the load is taken back\n", g_refused, g_checked, why, g_detail, caller, outer, handle, obj, image,
                size, readable(obj, OBJ_SIZE) ? MEM32(obj + 0x428) : 0, readable(obj, OBJ_SIZE) ? MEM32(obj + 0x42C) : 0,
                MEM32(MODEL_LOADING));
        dump("damaged", g_bad);
        dump("image", image);
        for (i = g_recent_n > 4 ? g_recent_n - 4 : 0; i < g_recent_n; i++)
            fprintf(stderr, "[DYNLOAD]   load before: caller %08X, handle %08X, image %08X (%u bytes)\n",
                    g_recent[i % 4].caller, g_recent[i % 4].handle, g_recent[i % 4].image, g_recent[i % 4].size);
        fflush(stderr);
    }
    return 1;
}
