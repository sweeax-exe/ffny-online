/* The retail trans/t1 object puts bitmap 9's quad at (512,512), although
 * its texture matrix is identity and the other nine quads leave (0,0) empty.
 * Restore that quad to the empty first cell before ELF relocation / GPU upload.
 * This changes guest memory only, at every aspect and with or without packs.
 * See docs/research/menu-ui-v1.1-2026-10-26.md for the source and native checks.
 */
#include "menu_geometry.h"

#include <stdio.h>
#include <stdlib.h>

#include "host.h"
#include "recomp/gen/recomp_types.h"

#define T1_OBJECT_SIZE 13089u
#define T1_DATA_OFFSET 64u
#define T1_VERTEX_OFFSET (T1_DATA_OFFSET + 740u)
#define T1_ORIGINAL_FNV 0x208BD986u
#define T1_REPAIRED_FNV 0xABE1D4D6u

void menu_geometry_register(uint32_t object)
{
    uint32_t h = 0x811C9DC5u, i;
    static int enabled = -1, trace;
    static unsigned traced;
    if (enabled < 0) {
        const char *e = getenv("RECOMP_MENU_TRANSITION_REPAIR");
        enabled = !(e && *e == '0');
        trace = getenv("RECOMP_MENU_GEOMETRY_TRACE") != NULL;
    }
    if (!enabled || !object)
        return;
    if (trace && traced++ < 32)
        fprintf(stderr, "[MENU-GEOMETRY] object %08X header %08X %08X sections %08X/%u/%u\n",
                object, MEM32(object), MEM32(object + 4), MEM32(object + 32),
                (unsigned)MEM16(object + 46), (unsigned)MEM16(object + 48));
    /* Cheap structural checks precede the fingerprint. These fields identify
     * the known object's complete section table and final string-table extent;
     * unrelated objects never get a fixed-length scan or an offset write. */
    if (MEM32(object) != 0x464C457Fu || MEM32(object + 4) != 0x00010101u ||
        MEM32(object + 32) != 7452u || MEM16(object + 46) != 40u ||
        MEM16(object + 48) != 6u || MEM16(object + 50) != 3u)
        return;
    if (MEM32(object + 7452u + 5u * 40u + 16u) != 10852u ||
        MEM32(object + 7452u + 5u * 40u + 20u) != 2237u)
        return;
    for (i = 0; i < T1_OBJECT_SIZE; i++)
        h = (h ^ MEM8(object + i)) * 0x01000193u;
    if (h == T1_REPAIRED_FNV)
        return;
    if (h != T1_ORIGINAL_FNV)
        return;
    /* Quantization is 1/8 px in x and 5066/16384 px in y. Subtract the
     * encoded first-cell offsets, rather than rounding decoded positions.
     * The repaired bottom edge shares exactly the next row's encoded y. */
    for (i = 0; i < 4; i++) {
        uint32_t v = object + T1_VERTEX_OFFSET + i * 6u;
        MEM16(v) = (uint16_t)((int16_t)MEM16(v) - 4096);
        MEM16(v + 2) = (uint16_t)((int16_t)MEM16(v + 2) - 1656);
    }
    fprintf(stderr, "[MENU] trans/t1: restored first scenery tile at %08X\n", object);
}
