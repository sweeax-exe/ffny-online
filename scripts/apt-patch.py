#!/usr/bin/env python3
"""Local-only patch that gives the main menu a real ONLINE slot (and the select grid the new fighters' pictures).

The shipped main menu (screens/intMain/mainMenu.big, an EA "APT" movie) lays its
items out in a 3x3 grid and leaves the bottom-left cell empty: slot index 6 is
`null` in the item array, has no mode constant and no label art. This tool fills
that slot. It writes a complete patched copy of the player's own
`screens/screens.viv` to `<data>/override/screens/screens.viv`, which the port
loads in place of the original (the original file is never opened for writing).
Only the entries named below change; the rest of the archive is copied byte for byte.

    python scripts/apt-patch.py                  # build the override
    python scripts/apt-patch.py --check          # report whether it exists and is current
    python scripts/apt-patch.py --data D:\\DefJam\\data

What is patched, all in the `mainMenu.apt` stream (offsets are `.apt` offsets,
every internal pointer there is an absolute offset):

  * a new PlaceObject item `mmOnline` (a clone of the `mmCredits` placement, at
    the free bottom-left grid position), a new 14-item array for frame 0 of the
    `mainMenu` art clip, and the frame record that points at it;
  * a per-frame hook: the `mmOnline` placement carries a clip event (EnterFrame, the mechanism
    the menu's own spotlight mover uses) whose action sends FuncCall("Online.Tick()") every
    frame while the menu is on screen; the menu defines no onEnterFrame of its own, so there
    is nothing to chain to;
  * a copy of the frame-0 action of the menu clip (the whole menu program) with
    three insertions, and the action item re-pointed at the copy:
      - `selectPoster`: first statement sends FuncCall("Online.Focus(<index>)");
      - `keyX`: right after the select sound, `if (oMenu.getIndex() == 6)`
        sends FuncCall("Online.Open()") (every other index runs as before);
      - end of the action: `aMenuArray[6] = "mmOnline"`, which makes the slot
        reachable (the cursor skips null entries).
    The function bodies are edited in place (their size fields and the one
    branch that crosses an insertion are fixed up) rather than wrapped, so no
    behaviour depends on how the VM resolves a redefined name.
  * the poster itself (patch version 3): the shipped menu's poster wall is two baked textures
    of `mainMenu.xsh` (a dim one and the lit one the spotlight reveals), so the other slots'
    posters are not separate shapes. The tool decodes the library's ONLINE poster from
    `screens/intMain/support/mainMenuButtons.big` (also hash-gated), scales it to the
    neighbours' size and paints it over the bottom-left cell of both wall textures, matching
    the lit white level and the dim/lit ratio measured on the STORY poster. Only whole DXT5
    blocks inside one rectangle change, so the poster slides with the wall and is lit and
    dimmed like the others. Texture and bank layout are untouched;
  * battle/cmtype (patch version 4): the screen an online match's summary leads to gets the
    same per-frame FuncCall("Online.Tick()"): a new empty-sprite placement in frame 0 of its main
    timeline carrying an EnterFrame clip event. The screen defines no clip event or onEnterFrame
    of its own and nothing else on it changes; its entry is hash-gated like the main menu's;
  * battle/chooseF2, chooseF3, chooseF4 (patch version 5), the fighter select screens: a grid cell is
    an instance of the library sprite `thumbAll` (300 frames) that shows frame <feID> under the cursor
    and frame <feID + 149> otherwise; the pictures are baked into the screen, and the frames of the
    community fighters (feID 100..127 and the 20 low places, `src/hooks/new_fighter_places.h`) hold none but
    37's (an unused Crow 2's picture, which the patch takes out with a RemoveObject). Each of those 96 frames
    gets one more Action item: `this.attachMovie("emptyClip", "nfThumb", 2)`, the clip moved to the
    cell's corner, and `loadMovie` of the fighter's own thumb file into it (`thumbs/thumb<feID>.swf`
    under the cursor, `thumbs/thumb<feID>off.swf` otherwise, i.e. `screens/thumbs/thumb<feID>.big` and
    `thumb<feID>off.big`, which the launcher writes from the pack's art). Only those frame records
    change; the screens' own program, pictures and stock frames stay byte-identical. Patched once: a
    pack only adds its thumb files. These three entries are optional: one that does not match its
    hashes stays as it is and the tool says so (`note: ... left as it is`), the menu patch is kept;
    patch version 6 adds to the same screens the pictures of the community fighters beyond the places
    (front-end ID 1000 and up, past thumbAll's frames): the screen's main action is copied to the end of
    the .apt with a call after each of the grid's three gotoAndStop calls to a function of its own that,
    for such an ID, loads `thumbs/thumb<ID>[off].swf` by URL (see build_choosef_grid);
  * the `.apt` is re-laid out in its BIGF (other inner files byte-identical),
    wrapped as a literal-only RefPack stream, appended to the archive copy, and
    its directory record and the header total size are updated.

The tool contains no game bytes: only offsets, SHA-256 expectations and the
bytecode and data we author. A different region or version of the game is
refused (the decompressed payload must match the expected hash). After building
it decodes its own output, parses every DefineFunction in the new action to its
exact declared size, checks every branch lands on an instruction boundary,
and byte-compares everything that should not have changed.

Python 3, standard library only.
"""

import argparse
import hashlib
import os
import struct
import sys

# ----------------------------------------------------------------- constants

ENTRY_NAME = "screens/intMain/mainMenu.big"
SCREENS_REL = os.path.join("screens", "screens.viv")

# Expected hashes of the user's own data (identification only).
ENTRY_RAW_SHA256 = "71bec380772ac8e430b80d07b79737af9f66a356eb7a741fe639c09954059e29"
PAYLOAD_SHA256 = "b3619d08e3c52ff5d7e7f7f470fa54efc993934093d2fe0d61f2b27755273263"
APT_SHA256 = "d1183e6673b8f3eac688c4df0d4bdf366dc99fee01c8ae3b8464ba91c8320549"
CONST_SHA256 = "62ef965806a5112eded66bb1a3a61ca170b0bbbd647125bf69300b0d5f45c0a9"

# Offsets inside mainMenu.apt (see docs/research/frontend-menu-integration.md).
APT_LEN = 0x12044                 # 73,796 bytes
ACTION_ITEM_PTR = 0xD3CC          # action item at 0xD3C8 is (type 1, ptr); ptr -> 0x10CF8
ACTION_START = 0x10CF8            # frame-0 action of the menu clip (character 222)
ACTION_STOP_OP = 0x1193C          # final `stop()`; End follows at 0x1193D
FRAME_RECORD = 0x2064             # frame 0 of the art clip (character 16): (13 items, array ptr)
FRAME_ITEMS_OLD = (13, 0x2E14)
MMCREDITS_ITEM = 0x92B8           # PlaceObject of the mmCredits hit target
NEW_SLOT = 6
NEW_DEPTH = 27
NEW_POS = (-61.0, 95.5)           # bottom-left cell: its poster's centre (-62.5, 94) plus the
                                  # (+1.5, +1.5) offset every other slot's hit target has

NEW_CLIP_NAME = "mmOnline"
PATCH_VERSION = 6                 # 1: slot 6 + Focus/Open; 2: + per-frame Online.Tick();
                                  # 3: + the ONLINE poster painted into the poster wall textures;
                                  # 4: + the same per-frame Online.Tick() on battle/cmtype;
                                  # 5: + the community fighters' grid pictures on battle/chooseF2/3/4;
                                  # 6: + grid pictures by URL for fighters beyond the places (ID 1000 and up)
CLIP_EVENT_ENTERFRAME = 0x02
PLACE_HAS_CLIP_ACTIONS = 0x80
CMD_TICK = "Online.Tick()"
CMD_OPEN = "Online.Open()"
CMD_FOCUS_PREFIX = "Online.Focus("
CMD_FOCUS_SUFFIX = ")"

# The poster wall is two baked textures of mainMenu.xsh (a dim one and the lit one the spotlight
# reveals), each 256x512 DXT5, drawn 1:1 with texture pixel (x, y) at menu-clip (x - 153, y - 254),
# i.e. 640x480 screen (x - 2.8, y - 0.1). The vacant slot is the bottom-left cell, where a
# decorative "II" shield is baked in. The tool paints the library's ONLINE poster there.
WALL_DIM_TEX, WALL_LIT_TEX = "5", "14"
WALL_W, WALL_H = 256, 512
POSTER_RECT = (64, 292, 60, 84)       # x, y, w, h in texture pixels; 4-aligned so whole blocks change
REF_RECT = (72, 123, 57, 85)          # the STORY poster, to match brightness against
POSTER_ENTRY_NAME = "screens/intMain/support/mainMenuButtons.big"
POSTER_ENTRY_RAW_SHA256 = "5402eb2ac35b41deff49ea705090cc577ac0d01071e62e754f54aae5b75c3e9c"
POSTER_PAYLOAD_SHA256 = "4f18b962a53690b2e2fce88f2249cad36c9ce3bd678458777ea7961f69ff8c16"
POSTER_XSH, POSTER_TEX = "mainMenuButtons.xsh", "15"
POSTER_TEX_SHA256 = "ec14de8a8db5a5380420063c6029fd8af285f41cf8437c1393eea49b0fea9033"   # its 16,384 DXT5 bytes
POSTER_SRC_W = POSTER_SRC_H = 128
MAX_PAINT_ERROR = 14.0                # mean abs error allowed between the intended and the encoded poster

# battle/cmtype ("CHOOSE MATCH TYPE", where an online match's summary leads): the same per-frame
# Online.Tick(), as a clip event on a new placement in frame 0 of the movie's main timeline.
CMTYPE_ENTRY_NAME = "screens/battle/cmtype.big"
CMTYPE_ENTRY_RAW_SHA256 = "65a210c97f56094d40acc91a647fee205d6a165e2f31a2f024e80347a5156775"
CMTYPE_PAYLOAD_SHA256 = "8cac1358d9041d97ed74f0a05dff0e29fe2bf7495a732a3f594931e975ec733f"
CMTYPE_APT_SHA256 = "9f6b0f14e379c213a0146f5e9ac7aae590c27d5ef2d41cef9a0307ef1931774e"
CMTYPE_CONST_SHA256 = "69382bd9c0153ff49fb2587e5d627a96fffa8c0eac921db513a398235203429b"
CMTYPE_APT_LEN = 60028
CMTYPE_FRAMES = 67
CMTYPE_TEMPLATE_NAME = "rules"        # an empty-sprite placement of frame 0 that is cloned
CMTYPE_TICK_DEPTH = 130
CMTYPE_TICK_NAME = "onlineTick"

# battle/chooseF2, chooseF3, chooseF4 (the fighter select screens, patch version 5): the grid's cells are
# instances of the library sprite "thumbAll" (300 frames). A cell shows frame <feID> (0-based) under the cursor
# and frame <feID + 149> otherwise; the stock frames place one baked picture each, the frames of the community
# fighters (their places: feID 100..127 and 20 low IDs, src/hooks/new_fighter_places.h) place none, but for frames 37
# and 186 (place 37, an unused Crow 2's picture; the patch replaces that placement by a RemoveObject of depth 1, as the
# empty frames have). Each of those frames gets one more Action that attaches an empty clip
# and loads the fighter's own thumb file into it (screens/thumbs/thumb<feID>.big under the cursor,
# thumb<feID>off.big otherwise: the launcher writes both from the pack's art, setup/ffnycontent/newfighter.py).
CHOOSEF_ENTRIES = {
    # entry: (movie name, payload SHA-256, .apt SHA-256, .const SHA-256, thumbAll's character)
    "screens/battle/chooseF2.big": (
        "chooseF2", "e0e91bf970eb679b627a1fec8a8ae1efcd73ff7ee96d006923930a6cdc05bc0f",
        "cee0019857c8d19602f7aa2422e35633ee15feebe58331aae3319d702658ebe0",
        "16a49566356425bcc912c6cba6d259f20a61845bae4e1e77050530d4357e0266", 303),
    "screens/battle/chooseF3.big": (
        "chooseF3", "6e9476f5bf9c5e2727e8c73dbe3e4c4adb1fa65c44374ac360c4f2dacffd5220",
        "066828bef2df3b809d99252df15b5faf4b27b6b4a47bcf593e937992c2eaacd7",
        "fe4dca8dcf6758398540ead9cd7bf2a2bfc62d4ce470b2f0770c69bf6a044d66", 306),
    "screens/battle/chooseF4.big": (
        "chooseF4", "5b7375ff8678a9239c52bd9a38c914190b65a50f49b332c3d27557c5f150040b",
        "a23b71d752873f4e9fb9809be25a78fe76f5091760d17a9c561753462ac6448c",
        "46bc842e5141d979960b4deda2d065ddf0fee33e79c216456e4a396c2b77bcda", 305),
}
CHOOSEF_SPRITE_NAME = "thumbAll"
CHOOSEF_SPRITE_FRAMES = 300
CHOOSEF_EMPTY_NAME = "emptyClip"      # the screens' own exported empty sprite (their portraits load into it)
CHOOSEF_PLACES = tuple(range(100, 128)) + (5, 15, 29, 37, 39, 42, 49, 53, 63, 66, 79, 86, 87, 88, 89, 90, 94, 95, 96,
                                           97)    # the community fighters' front-end IDs (new_fighter_places.h)
CHOOSEF_BAKED_DEPTH = 1               # the depth of the screens' own baked pictures
CHOOSEF_NORMAL_OFFSET = 149           # gotoAndStop(feID + CHAR_SELECTED_OFFSET 150) is 0-based frame feID + 149
CHOOSEF_CLIP_NAME = "nfThumb"
CHOOSEF_DEPTH = 2                     # the baked pictures sit at depth 1
CHOOSEF_ORIGIN = -18.5                # the baked shapes are centred (-18.5..18.5); a thumb file starts at (0, 0)
CHOOSEF_URL_ON = "thumbs/thumb%d.swf"
CHOOSEF_URL_OFF = "thumbs/thumb%doff.swf"

BIG_ALIGN = 128                   # inner BIGF entries are 128-aligned
OUTER_ALIGN = 4

SPEC = {                          # operand layout of opcodes (checked on 2,485 function bodies)
    0x81: "a", 0x83: "pp", 0x87: "a", 0x8A: "a", 0x8B: "p", 0x8C: "p", 0x94: "a",
    0x99: "a", 0x9A: "", 0x9D: "a", 0x9F: "a",
    0xA1: "p", 0xA2: "b", 0xA3: "H", 0xA4: "p", 0xA5: "p", 0xA6: "p", 0xA7: "p",
    0xAE: "b", 0xAF: "b", 0xB0: "b", 0xB1: "b", 0xB2: "b", 0xB3: "b",
    0xB4: "f", 0xB5: "b", 0xB6: "h", 0xB7: "i",
}
OP_END, OP_POP, OP_PUSHZERO, OP_PUSHONE = 0x00, 0x17, 0x59, 0x5A
OP_CALLMETHOD, OP_EQUALS2, OP_ADD2, OP_NOT, OP_SETMEMBER = 0x52, 0x49, 0x47, 0x12, 0x4F
OP_CONSTPOOL, OP_PUSHDATA, OP_DEFFUNC, OP_DEFFUNC2 = 0x88, 0x96, 0x9B, 0x8E
OP_BRANCH, OP_IF = 0x99, 0x9D
OP_PUSHSTRING, OP_PUSHCONST, OP_PUSHVAR, OP_GETMEMBER = 0xA1, 0xA2, 0xAE, 0xAF
OP_CALLNAMEDMETHODPOP, OP_PUSHBYTE = 0xB2, 0xB5
OP_GETVARIABLE, OP_CALLMETHODPOP, OP_PUSHTHISVAR, OP_GETURL2, OP_PUSHFLOAT = 0x1C, 0x5D, 0x70, 0x9A, 0xB4
FUNC_MARKER = struct.pack("<II", 0x98765432, 0x12345678)


class PatchError(Exception):
    pass


# ------------------------------------------------------------------- RefPack

def refpack_decode(src):
    """EA RefPack (0x10FB / 0x90FB). Strict: the output must match the header size."""
    if len(src) < 5:
        raise PatchError("RefPack: truncated header")
    sig = struct.unpack_from(">H", src, 0)[0]
    if sig & 0x3EFF != 0x10FB:
        raise PatchError("not RefPack: signature 0x%04X" % sig)
    step = 4 if sig & 0x8000 else 3
    pos = 2 + (step if sig & 0x0100 else 0)
    size = int.from_bytes(src[pos:pos + step], "big")
    pos += step
    out = bytearray()
    while pos < len(src):
        b0 = src[pos]
        pos += 1
        if b0 < 0x80:
            b1 = src[pos]
            pos += 1
            run, length, dist = b0 & 3, ((b0 & 0x1C) >> 2) + 3, ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = src[pos], src[pos + 1]
            pos += 2
            run, length, dist = (b1 >> 6) & 3, (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = src[pos], src[pos + 1], src[pos + 2]
            pos += 3
            run, length = b0 & 3, ((b0 & 0x0C) << 6) + b3 + 5
            dist = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            run, length, dist = ((b0 & 0x1F) << 2) + 4, 0, 0
        else:
            run, length, dist = b0 & 3, 0, 0
        out += src[pos:pos + run]
        pos += run
        if length:
            if dist > len(out):
                raise PatchError("RefPack: back-reference before start")
            for _ in range(length):
                out.append(out[-dist])
        if b0 >= 0xFC:
            break
    if len(out) != size:
        raise PatchError("RefPack: decoded %d bytes, header says %d" % (len(out), size))
    return bytes(out)


def refpack_encode_literal(data):
    """A valid RefPack stream that stores `data` uncompressed (about 1% larger)."""
    n = len(data)
    if n < (1 << 24):
        out = bytearray(struct.pack(">H", 0x10FB) + n.to_bytes(3, "big"))
    else:
        out = bytearray(struct.pack(">H", 0x90FB) + n.to_bytes(4, "big"))
    i = 0
    while n - i >= 112:
        out.append(0xFB)                      # literal run of 112
        out += data[i:i + 112]
        i += 112
    r = n - i
    if r >= 4:
        k = (r - 4) // 4
        run = 4 * k + 4
        out.append(0xE0 + k)
        out += data[i:i + run]
        i += run
    r = n - i
    out.append(0xFC | r)                      # stop code with 0-3 trailing literals
    out += data[i:i + r]
    return bytes(out)


# ------------------------------------------------------------------- BIG files

def big_records(data):
    """[(name, record_pos, offset, size)] of a BIGF/BIG4 directory (data = at least the directory)."""
    if data[:4] not in (b"BIG4", b"BIGF"):
        raise PatchError("not a BIG archive: %r" % data[:4])
    count = struct.unpack_from(">I", data, 8)[0]
    pos, out = 16, []
    for _ in range(count):
        if pos + 8 > len(data):
            raise PatchError("BIG: truncated directory")
        off, size = struct.unpack_from(">II", data, pos)
        end = data.find(b"\x00", pos + 8)
        if end < 0:
            raise PatchError("BIG: unterminated name")
        out.append((data[pos + 8:end].decode("latin-1"), pos, off, size))
        pos = end + 1
    return out


def align(n, a):
    return (n + a - 1) // a * a


def rebuild_bigf(payload, replace):
    """New BIGF with the same entries in the same order and the same header and names;
    `replace` maps an entry name to its new bytes. Entries are re-laid out 128-aligned."""
    recs = big_records(payload)
    head = bytearray(payload[:min(recs[0][2], len(payload))])
    cursor = recs[0][2]
    blobs = []
    for name, rpos, off, size in recs:
        data = replace.get(name, payload[off:off + size])
        cursor = align(cursor, BIG_ALIGN)
        struct.pack_into(">II", head, rpos, cursor, len(data))
        blobs.append((cursor, data))
        cursor += len(data)
    total = align(cursor, BIG_ALIGN)
    out = bytearray(total)
    out[:len(head)] = head
    struct.pack_into("<I", out, 4, total)
    for at, data in blobs:
        out[at:at + len(data)] = data
    return bytes(out)


def inner_files(payload):
    return {n: bytes(payload[o:o + s]) for n, _, o, s in big_records(payload)}


# ------------------------------------------------------------ .apt / .const model

def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def cstr(b, o):
    e = b.index(b"\x00", o)
    return b[o:e].decode("latin-1")


def const_strings(const):
    """Strings of an `Apt constant file` by index (None for non-string items)."""
    count, table = struct.unpack_from("<II", const, 0x18)
    out = []
    for i in range(count):
        t, v = struct.unpack_from("<II", const, table + 8 * i)
        out.append(cstr(const, v) if t == 1 else None)
    return out


class Ins:
    __slots__ = ("addr", "nxt", "op", "vals", "body")

    def __init__(self, addr, nxt, op, vals, body=None):
        self.addr, self.nxt, self.op, self.vals, self.body = addr, nxt, op, vals, body

    @property
    def target(self):               # branch destination (offset is relative to the next instruction)
        v = self.vals[0]
        return self.nxt + (v - (1 << 32) if v >= 1 << 31 else v)


def decode(buf, start, end=None):
    """Instructions from `start`. With `end`, up to exactly `end` (a function body); without,
    through the End opcode. A DefineFunction is one instruction whose body is skipped by size."""
    out, o = [], start
    while True:
        if end is not None and o >= end:
            break
        if o >= len(buf):
            raise PatchError("bytecode runs off the end at 0x%X" % o)
        st, op = o, buf[o]
        o += 1
        if op == OP_DEFFUNC:
            o = align(o, 4)
            name, npar, pptr, size = struct.unpack_from("<IIII", buf, o)
            o += 16
            if buf[o:o + 8] != FUNC_MARKER:
                raise PatchError("DefineFunction at 0x%X lacks its marker" % st)
            o += 8
            out.append(Ins(st, o + size, op, (name, npar, pptr, size, o - 8 - 4), (o, size)))
            o += size
            continue
        if op == OP_DEFFUNC2:
            raise PatchError("DefineFunction2 at 0x%X is not supported" % st)
        if op in (OP_CONSTPOOL, OP_PUSHDATA):
            o = align(o, 4)
            vals = struct.unpack_from("<II", buf, o)
            o += 8
        elif op in SPEC:
            vals = []
            for ch in SPEC[op]:
                if ch == "b":
                    vals.append(buf[o]); o += 1
                elif ch == "h":
                    vals.append(struct.unpack_from("<h", buf, o)[0]); o += 2
                elif ch == "H":
                    vals.append(struct.unpack_from("<H", buf, o)[0]); o += 2
                elif ch in "if":
                    vals.append(struct.unpack_from("<I", buf, o)[0]); o += 4
                else:                                       # "a" and "p": aligned u32
                    o = align(o, 4)
                    vals.append(u32(buf, o)); o += 4
            vals = tuple(vals)
        elif op < 0x80:
            vals = ()
        else:
            raise PatchError("unknown opcode 0x%02X at 0x%X" % (op, st))
        out.append(Ins(st, o, op, vals))
        if end is None and op == OP_END:
            break
    if end is not None and o != end:
        raise PatchError("body ends at 0x%X, expected 0x%X" % (o, end))
    return out


def check_branches(ins, lo, hi):
    """Every branch must land on an instruction boundary (or the end) inside [lo, hi]."""
    bounds = {i.addr for i in ins} | {ins[-1].nxt} if ins else set()
    for i in ins:
        if i.op in (OP_BRANCH, OP_IF) and i.target not in bounds:
            raise PatchError("branch at 0x%X lands inside an instruction (0x%X)" % (i.addr, i.target))


def verify_block(buf, start, expect_end=None):
    """Decode a top-level block and, recursively, every function body to its exact size.
    Returns (instruction list, number of function bodies checked)."""
    ins = decode(buf, start)
    if expect_end is not None and ins[-1].nxt != expect_end:
        raise PatchError("block ends at 0x%X, expected 0x%X" % (ins[-1].nxt, expect_end))
    check_branches(ins, start, ins[-1].nxt)
    return ins, _nested(buf, ins)


def _nested(buf, ins):
    n = 0
    for i in ins:
        if i.op == OP_DEFFUNC:
            bstart, size = i.body
            sub = decode(buf, bstart, bstart + size)       # raises if it does not end exactly
            check_branches(sub, bstart, bstart + size)
            n += 1 + _nested(buf, sub)
    return n


def pool_slots(buf, ins):
    """Map constant string -> first pool slot of the block's ConstantPool instruction."""
    for i in ins:
        if i.op == OP_CONSTPOOL:
            count, ptr = i.vals
            return [u32(buf, ptr + 4 * k) for k in range(count)]
    raise PatchError("action has no ConstantPool")


# -------------------------------------------------------------------- assembler

class Asm:
    """Bytecode writer that knows the absolute (apt) address it will live at, because the
    VM aligns the operands of many opcodes on the absolute address."""

    def __init__(self, addr):
        self.base, self.buf = addr, bytearray()

    @property
    def cur(self):
        return self.base + len(self.buf)

    def op(self, *b):
        self.buf += bytes(b)

    def align_to(self, n=4):
        while self.cur % n:
            self.buf.append(0)

    def op_u32(self, op, value):
        """An opcode with an aligned 32-bit operand; returns the operand's buffer position."""
        self.buf.append(op)
        self.align_to(4)
        at = len(self.buf)
        self.buf += struct.pack("<I", value & 0xFFFFFFFF)
        return at

    def op_u32x2(self, op, a, b):
        """An opcode with two aligned 32-bit operands (ConstantPool: count, pointer)."""
        self.buf.append(op)
        self.align_to(4)
        self.buf += struct.pack("<II", a & 0xFFFFFFFF, b & 0xFFFFFFFF)

    def patch_u32(self, at, value):
        struct.pack_into("<I", self.buf, at, value & 0xFFFFFFFF)

    def pad_to_multiple_of_4(self):
        """Harmless filler so later aligned code keeps its padding: pairs of PushZero/Pop, and
        PushByte 0/Pop (3 bytes) to move the parity."""
        need = (-len(self.buf)) % 4
        if need == 0:
            return
        if need % 2:                           # 1 or 3: 3 now, then pairs
            self.op(OP_PUSHBYTE, 0, OP_POP)
            need = (-len(self.buf)) % 4
        while need:
            self.op(OP_PUSHZERO, OP_POP)
            need -= 2


def asm_funccall_const(a, string_ptr, slots):
    """`_level0._root.FuncCall(<inline string>)` as the game's own scripts do it."""
    a.op_u32(OP_PUSHSTRING, string_ptr)
    a.op(OP_PUSHONE, OP_PUSHVAR, slots["_level0"], OP_GETMEMBER, slots["_root"],
         OP_CALLNAMEDMETHODPOP, slots["FuncCall"])


def asm_get_index(a, slots):
    a.op(OP_PUSHZERO, OP_PUSHVAR, slots["oMenu"], OP_PUSHCONST, slots["getIndex"], OP_CALLMETHOD)


def asm_keyx_insert(addr, slots, open_ptr, slot=NEW_SLOT):
    """if (oMenu.getIndex() == slot) _level0._root.FuncCall("Online.Open()");"""
    a = Asm(addr)
    asm_get_index(a, slots)
    a.op(OP_PUSHBYTE, slot, OP_EQUALS2, OP_NOT)
    at = a.op_u32(OP_IF, 0)
    after = len(a.buf)
    asm_funccall_const(a, open_ptr, slots)
    a.patch_u32(at, len(a.buf) - after)
    a.pad_to_multiple_of_4()
    return bytes(a.buf)


def asm_select_insert(addr, slots, prefix_ptr, suffix_ptr):
    """_level0._root.FuncCall("Online.Focus(" + oMenu.getIndex() + ")");"""
    a = Asm(addr)
    a.op_u32(OP_PUSHSTRING, prefix_ptr)
    asm_get_index(a, slots)
    a.op(OP_ADD2)
    a.op_u32(OP_PUSHSTRING, suffix_ptr)
    a.op(OP_ADD2, OP_PUSHONE, OP_PUSHVAR, slots["_level0"], OP_GETMEMBER, slots["_root"],
         OP_CALLNAMEDMETHODPOP, slots["FuncCall"])
    a.pad_to_multiple_of_4()
    return bytes(a.buf)


def asm_tick_action(addr, pool_ptr, tick_ptr):
    """The clip-event action: ConstantPool [_level0, _root, FuncCall]; then
    _level0._root.FuncCall("Online.Tick()"); End. Same shape as the game's own clip actions."""
    a = Asm(addr)
    a.op_u32x2(OP_CONSTPOOL, 3, pool_ptr)
    a.op_u32(OP_PUSHSTRING, tick_ptr)
    a.op(OP_PUSHONE, OP_PUSHVAR, 0, OP_GETMEMBER, 1, OP_CALLNAMEDMETHODPOP, 2, OP_END)
    return bytes(a.buf)


def asm_tail(addr, slots, name_ptr, slot=NEW_SLOT):
    """aMenuArray[slot] = "mmOnline";"""
    a = Asm(addr)
    a.op(OP_PUSHVAR, slots["aMenuArray"], OP_PUSHBYTE, slot)
    a.op_u32(OP_PUSHSTRING, name_ptr)
    a.op(OP_SETMEMBER)
    return bytes(a.buf)


# --------------------------------------------------------- generic body editing

def apply_edits(block, block_start, fixups, inserts):
    """`block` holds old bytes starting at old address `block_start`. Fix up u32 fields (old
    coordinates), then insert bytes at old addresses (processed from the end)."""
    b = bytearray(block)
    for addr, delta in fixups:
        rel = addr - block_start
        struct.pack_into("<I", b, rel, (struct.unpack_from("<I", b, rel)[0] + delta) & 0xFFFFFFFF)
    for addr, data in sorted(inserts, key=lambda t: t[0], reverse=True):
        rel = addr - block_start
        b[rel:rel] = data
    return bytes(b)


def branches_crossing(ins, point):
    """Branch instructions of a function body that jump over `point` (forward), and any that
    jump backwards over it (unsupported)."""
    fwd = []
    for i in ins:
        if i.op in (OP_BRANCH, OP_IF):
            if i.nxt <= point < i.target:
                fwd.append(i)
            elif i.target <= point < i.nxt:
                raise PatchError("backward branch at 0x%X crosses the insertion point" % i.addr)
    return fwd


# ------------------------------------------------------------------ the patch

def find_function(ins, apt, name):
    for i in ins:
        if i.op == OP_DEFFUNC and i.vals[0] and cstr(apt, i.vals[0]) == name:
            return i
    raise PatchError("function %r not found in the menu action" % name)


def build_new_apt(apt, const, slot=NEW_SLOT):
    """Return (new .apt bytes, info dict). `apt`/`const` are the user's original streams."""
    if len(apt) != APT_LEN:
        raise PatchError(".apt is %d bytes, expected %d" % (len(apt), APT_LEN))
    if len(apt) % 4:
        raise PatchError(".apt length not 4-aligned")
    strings = const_strings(const)

    # Structural anchors (all are re-validated by decoding, not trusted blindly).
    if (u32(apt, FRAME_RECORD), u32(apt, FRAME_RECORD + 4)) != FRAME_ITEMS_OLD:
        raise PatchError("frame record at 0x%X is not the expected (13 items)" % FRAME_RECORD)
    if u32(apt, ACTION_ITEM_PTR - 4) != 1 or u32(apt, ACTION_ITEM_PTR) != ACTION_START:
        raise PatchError("action item at 0x%X does not point at the menu action" % (ACTION_ITEM_PTR - 4))
    if apt[ACTION_STOP_OP] != 0x07 or apt[ACTION_STOP_OP + 1] != OP_END:
        raise PatchError("menu action does not end with stop() at 0x%X" % ACTION_STOP_OP)

    ins = decode(apt, ACTION_START)
    if ins[-1].nxt != ACTION_STOP_OP + 2 or ins[-2].addr != ACTION_STOP_OP:
        raise PatchError("menu action decodes to a different extent")
    slots_list = pool_slots(apt, ins)
    slots = {}
    for name in ("oMenu", "getIndex", "_level0", "_root", "FuncCall", "PlaySound", "aMenuArray"):
        try:
            slots[name] = next(k for k, c in enumerate(slots_list) if strings[c] == name)
        except StopIteration:
            raise PatchError("constant %r is not in the action's pool" % name)

    sel = find_function(ins, apt, "selectPoster")
    keyx = find_function(ins, apt, "keyX")
    sel_body, sel_size = sel.body
    key_body, key_size = keyx.body
    keyx_ins = decode(apt, key_body, key_body + key_size)

    # keyX: insert right after the select sound (the PlaySound call inside the guard).
    play = next((i for i in keyx_ins if i.op == OP_CALLNAMEDMETHODPOP
                 and strings[slots_list[i.vals[0]]] == "PlaySound"), None)
    if play is None:
        raise PatchError("keyX has no PlaySound call")
    key_point = play.nxt
    crossing = branches_crossing(keyx_ins, key_point)
    if len(crossing) != 1 or crossing[0].op != OP_IF or crossing[0].target != key_body + key_size:
        raise PatchError("keyX guard is not the single branch over the insertion point")
    guard = crossing[0]
    guard_operand = align(guard.addr + 1, 4)
    sel_ins = decode(apt, sel_body, sel_body + sel_size)
    branches_crossing(sel_ins, sel_body)          # start of body: nothing may cross

    # ---- layout of the appended data -------------------------------------------------
    pos = len(apt)
    data = bytearray()

    def put_str(s):
        nonlocal data
        at = pos + len(data)
        data += s.encode("latin-1") + b"\x00"
        return at

    p_open = put_str(CMD_OPEN)
    p_pre = put_str(CMD_FOCUS_PREFIX)
    p_suf = put_str(CMD_FOCUS_SUFFIX)
    p_name = put_str(NEW_CLIP_NAME)
    p_tick = put_str(CMD_TICK)
    while (pos + len(data)) % 4:
        data.append(0)

    # Per-frame hook: a clip event (EnterFrame) on the new placement, the mechanism the menu
    # itself uses for its spotlight mover. Pool: const indices of the three names.
    cidx = []
    for name in ("_level0", "_root", "FuncCall"):
        try:
            cidx.append(strings.index(name))
        except ValueError:
            raise PatchError("constant %r is not in the .const table" % name)
    pool_at = pos + len(data)
    data += struct.pack("<3I", *cidx)
    tick_at = pos + len(data)
    assert tick_at % 4 == 0
    tick_code = asm_tick_action(tick_at, pool_at, p_tick)
    data += tick_code
    while (pos + len(data)) % 4:
        data.append(0)
    entry_at = pos + len(data)
    data += struct.pack("<III", CLIP_EVENT_ENTERFRAME, 0, tick_at)
    clipact_at = pos + len(data)
    data += struct.pack("<II", 1, entry_at)

    # PlaceObject clone of the mmCredits placement.
    place_at = pos + len(data)
    words = list(struct.unpack_from("<16I", apt, MMCREDITS_ITEM))
    if words[0] != 3 or cstr(apt, words[13]) != "mmCredits":
        raise PatchError("0x%X is not the mmCredits placement" % MMCREDITS_ITEM)
    old_count, old_arr = FRAME_ITEMS_OLD
    used_depths = set()
    for k in range(old_count):
        it = u32(apt, old_arr + 4 * k)
        if u32(apt, it) == 3:
            used_depths.add(u32(apt, it + 8))
    if NEW_DEPTH in used_depths:
        raise PatchError("depth %d already used in the art clip" % NEW_DEPTH)
    words[2] = NEW_DEPTH
    words[8] = struct.unpack("<I", struct.pack("<f", NEW_POS[0]))[0]
    words[9] = struct.unpack("<I", struct.pack("<f", NEW_POS[1]))[0]
    words[13] = p_name
    words[1] |= PLACE_HAS_CLIP_ACTIONS
    words[15] = clipact_at
    data += struct.pack("<16I", *words)

    # New frame item array: the old pointers plus the new placement.
    arr_at = pos + len(data)
    old_ptrs = [u32(apt, old_arr + 4 * k) for k in range(old_count)]
    data += struct.pack("<%dI" % (old_count + 1), *(old_ptrs + [place_at]))

    block_at = pos + len(data)
    assert block_at % 4 == 0

    # ---- the modified action copy ----------------------------------------------------
    old_block = apt[ACTION_START:ACTION_STOP_OP + 2]

    def shifted(old_addr, earlier):             # new absolute address of an old address
        return block_at + (old_addr - ACTION_START) + earlier

    sel_code = asm_select_insert(shifted(sel_body, 0), slots, p_pre, p_suf)
    key_code = asm_keyx_insert(shifted(key_point, len(sel_code)), slots, p_open)
    tail_code = asm_tail(shifted(ACTION_STOP_OP, len(sel_code) + len(key_code)), slots, p_name)
    for c in (sel_code, key_code):
        if len(c) % 4:
            raise PatchError("internal: insertion is not a multiple of 4 bytes")

    sel_size_field, key_size_field = sel.vals[4], keyx.vals[4]
    new_block = apply_edits(
        old_block, ACTION_START,
        fixups=[(sel_size_field, len(sel_code)), (key_size_field, len(key_code)),
                (guard_operand, len(key_code))],
        inserts=[(sel_body, sel_code), (key_point, key_code), (ACTION_STOP_OP, tail_code)])
    # keyX is after selectPoster, so nothing else shifts the fields we fixed (they were
    # edited in old coordinates before splicing).

    new_apt = bytearray(apt)
    new_apt += data
    new_apt += new_block
    struct.pack_into("<I", new_apt, ACTION_ITEM_PTR, block_at)
    struct.pack_into("<II", new_apt, FRAME_RECORD, old_count + 1, arr_at)
    info = dict(tick_at=tick_at, tick_len=len(tick_code), clipact_at=clipact_at, entry_at=entry_at,
                block_at=block_at, block_len=len(new_block), data_at=pos, place_at=place_at,
                array_at=arr_at, sel_len=len(sel_code), key_len=len(key_code),
                tail_len=len(tail_code), slots=slots, apt_len=len(new_apt))
    return bytes(new_apt), info


def verify_new_apt(apt, new_apt, info):
    """Decode the patched stream; compare it with the original; return a one-line report."""
    block_at = info["block_at"]
    old_ins = decode(apt, ACTION_START)
    new_ins, nfun = verify_block(new_apt, block_at, expect_end=len(new_apt))
    old_fun = sum(1 for _ in _iter_functions(apt, old_ins))
    new_fun = sum(1 for _ in _iter_functions(new_apt, new_ins))
    if old_fun != new_fun:
        raise PatchError("function count changed (%d -> %d)" % (old_fun, new_fun))
    old_names = sorted(cstr(apt, i.vals[0]) for i in old_ins if i.op == OP_DEFFUNC and i.vals[0])
    new_names = sorted(cstr(new_apt, i.vals[0]) for i in new_ins if i.op == OP_DEFFUNC and i.vals[0])
    if old_names != new_names:
        raise PatchError("function names changed")
    # Same program plus our additions: top-level instruction count grew by exactly the tail
    # (PushVar, PushByte, PushString, SetMember).
    if len(new_ins) != len(old_ins) + 4:
        raise PatchError("top-level instruction count %d -> %d (expected +4)"
                         % (len(old_ins), len(new_ins)))
    # Untouched bytes: the whole original stream except the three rewritten words.
    allowed = {ACTION_ITEM_PTR, FRAME_RECORD, FRAME_RECORD + 4}
    for off in range(0, len(apt), 4):
        if apt[off:off + 4] != new_apt[off:off + 4] and off not in allowed:
            raise PatchError("original .apt byte range 0x%X changed unexpectedly" % off)
    verify_tick_clip(new_apt, info)
    return "%d function bodies decode to their exact size, tick action decodes, %d appended bytes" % (
        nfun, len(new_apt) - len(apt))


def verify_tick_clip(new_apt, info):
    """The per-frame hook: placement -> ClipActions -> (EnterFrame, key 0, action) -> decodable action."""
    pl = struct.unpack_from("<16I", new_apt, info["place_at"])
    if pl[0] != 3 or not pl[1] & PLACE_HAS_CLIP_ACTIONS or pl[15] != info["clipact_at"]:
        raise PatchError("the tick placement does not carry the clip actions")
    if struct.unpack_from("<II", new_apt, info["clipact_at"]) != (1, info["entry_at"]):
        raise PatchError("ClipActions record is wrong")
    if struct.unpack_from("<III", new_apt, info["entry_at"]) != (CLIP_EVENT_ENTERFRAME, 0, info["tick_at"]):
        raise PatchError("clip action entry is wrong")
    tick = decode(new_apt, info["tick_at"])
    if [i.op for i in tick] != [OP_CONSTPOOL, OP_PUSHSTRING, OP_PUSHONE, OP_PUSHVAR, OP_GETMEMBER,
                                OP_CALLNAMEDMETHODPOP, OP_END]:
        raise PatchError("tick action does not decode as expected")
    if cstr(new_apt, tick[1].vals[0]) != CMD_TICK or tick[-1].nxt != info["tick_at"] + info["tick_len"]:
        raise PatchError("tick action string or extent is wrong")


def _iter_functions(buf, ins):
    for i in ins:
        if i.op == OP_DEFFUNC:
            yield i
            bs, sz = i.body
            yield from _iter_functions(buf, decode(buf, bs, bs + sz))


# ------------------------------------------------------- battle/cmtype: per-frame Online.Tick()

def build_cmtype_apt(apt, const):
    """Append a tick clip to frame 0 of cmtype's main timeline. Returns (new .apt, info)."""
    if len(apt) != CMTYPE_APT_LEN or len(apt) % 4:
        raise PatchError("cmtype.apt is %d bytes, expected %d" % (len(apt), CMTYPE_APT_LEN))
    strings = const_strings(const)
    movie = struct.unpack_from("<I", const, 0x14)[0]
    if u32(apt, movie) != 9:
        raise PatchError("cmtype movie header not at 0x%X" % movie)
    nframes, frames = u32(apt, movie + 8), u32(apt, movie + 12)
    nchars, chars = u32(apt, movie + 20), u32(apt, movie + 24)
    if nframes != CMTYPE_FRAMES:
        raise PatchError("cmtype has %d root frames, expected %d" % (nframes, CMTYPE_FRAMES))
    count, arr = struct.unpack_from("<II", apt, frames)
    # Every depth used on the main timeline, and the template placement.
    used, template = set(), None
    for fi in range(nframes):
        n, ip = struct.unpack_from("<II", apt, frames + 8 * fi)
        for k in range(n):
            it = u32(apt, ip + 4 * k)
            if u32(apt, it) == 3:
                w = struct.unpack_from("<16I", apt, it)
                used.add(w[2])
                if fi == 0 and w[1] & 0x20 and w[13] and cstr(apt, w[13]) == CMTYPE_TEMPLATE_NAME:
                    template = w
    if template is None:
        raise PatchError("no %r placement in frame 0" % CMTYPE_TEMPLATE_NAME)
    if CMTYPE_TICK_DEPTH in used:
        raise PatchError("depth %d is already used on the main timeline" % CMTYPE_TICK_DEPTH)
    char = template[3]
    ch = u32(apt, chars + 4 * char) if char < nchars else 0
    if not ch or u32(apt, ch) != 5 or u32(apt, ch + 8) != 1:
        raise PatchError("the template placement is not a one-frame sprite")
    fp = u32(apt, ch + 12)
    if struct.unpack_from("<I", apt, fp)[0] != 0:
        raise PatchError("the template sprite is not empty")
    cidx = []
    for name in ("_level0", "_root", "FuncCall"):
        try:
            cidx.append(strings.index(name))
        except ValueError:
            raise PatchError("constant %r is not in cmtype's .const table" % name)

    pos = len(apt)
    data = bytearray()

    def put_str(text):
        at = pos + len(data)
        data.extend(text.encode("latin-1") + b"\x00")
        return at

    p_name, p_tick = put_str(CMTYPE_TICK_NAME), put_str(CMD_TICK)
    while (pos + len(data)) % 4:
        data.append(0)
    pool_at = pos + len(data)
    data += struct.pack("<3I", *cidx)
    tick_at = pos + len(data)
    tick_code = asm_tick_action(tick_at, pool_at, p_tick)
    data += tick_code
    while (pos + len(data)) % 4:
        data.append(0)
    entry_at = pos + len(data)
    data += struct.pack("<III", CLIP_EVENT_ENTERFRAME, 0, tick_at)
    clipact_at = pos + len(data)
    data += struct.pack("<II", 1, entry_at)
    place_at = pos + len(data)
    words = list(template)
    words[2] = CMTYPE_TICK_DEPTH
    words[1] |= PLACE_HAS_CLIP_ACTIONS
    words[13] = p_name
    words[15] = clipact_at
    data += struct.pack("<16I", *words)
    arr_at = pos + len(data)
    ptrs = [u32(apt, arr + 4 * k) for k in range(count)]
    data += struct.pack("<%dI" % (count + 1), *(ptrs + [place_at]))

    new_apt = bytearray(apt) + data
    struct.pack_into("<II", new_apt, frames, count + 1, arr_at)
    info = dict(frames=frames, tick_at=tick_at, tick_len=len(tick_code), clipact_at=clipact_at,
                entry_at=entry_at, place_at=place_at, array_at=arr_at, apt_len=len(new_apt))
    return bytes(new_apt), info


def verify_cmtype_apt(apt, new_apt, info):
    allowed = {info["frames"], info["frames"] + 4}
    for off in range(0, len(apt), 4):
        if apt[off:off + 4] != new_apt[off:off + 4] and off not in allowed:
            raise PatchError("original cmtype.apt bytes changed unexpectedly at 0x%X" % off)
    verify_tick_clip(new_apt, info)
    if struct.unpack_from("<II", new_apt, info["frames"]) != (u32(apt, info["frames"]) + 1, info["array_at"]):
        raise PatchError("frame-0 record of cmtype is wrong")
    return "tick clip appended (%d bytes), no existing byte changed but the frame-0 record" % (len(new_apt) - len(apt))


def patch_cmtype_entry(raw_entry, extras=None):
    """Raw (RefPack) cmtype entry -> (new raw entry, report lines). Refuses a different version."""
    lines = []
    if sha(raw_entry) != CMTYPE_ENTRY_RAW_SHA256:
        lines.append("note: cmtype entry differs from the reference build (payload is checked instead)")
    payload = refpack_decode(raw_entry)
    if sha(payload) != CMTYPE_PAYLOAD_SHA256:
        raise PatchError("decompressed cmtype payload does not match the expected hash "
                         "(different region or version); refusing to patch")
    files = inner_files(payload)
    apt, const = files.get("cmtype.apt"), files.get("cmtype.const")
    if apt is None or const is None or sha(apt) != CMTYPE_APT_SHA256 or sha(const) != CMTYPE_CONST_SHA256:
        raise PatchError("cmtype.apt/.const do not match the expected hashes")
    new_apt, info = build_cmtype_apt(apt, const)
    lines.append("verify cmtype.apt: " + verify_cmtype_apt(apt, new_apt, info))
    new_payload = rebuild_bigf(payload, {"cmtype.apt": new_apt})
    new_files = inner_files(new_payload)
    if list(new_files) != list(files):
        raise PatchError("cmtype inner file list changed")
    for n in files:
        if n != "cmtype.apt" and files[n] != new_files[n]:
            raise PatchError("cmtype inner file %s changed" % n)
    if new_files["cmtype.apt"] != new_apt:
        raise PatchError("re-laid out cmtype.apt differs from the built one")
    new_raw = refpack_encode_literal(new_payload)
    if refpack_decode(new_raw) != new_payload:
        raise PatchError("cmtype RefPack round trip failed")
    lines.append("cmtype payload %d -> %d bytes, entry %d -> %d bytes (literal RefPack)"
                 % (len(payload), len(new_payload), len(raw_entry), len(new_raw)))
    return new_raw, lines


# --------------------------------- battle/chooseF2/3/4: the community fighters' pictures in the grid

def choosef_frames(places=CHOOSEF_PLACES):
    """[(0-based frame of thumbAll, the thumb file's URL)] for every community fighter: the frame a cell
    shows under the cursor and the one it shows otherwise."""
    out = []
    for fe_id in places:
        out.append((fe_id, CHOOSEF_URL_ON % fe_id))
        out.append((fe_id + CHOOSEF_NORMAL_OFFSET, CHOOSEF_URL_OFF % fe_id))
    return sorted(out)


def asm_choosef_action(addr, sp, url_ptr):
    """The frame action of one community fighter's picture. Every statement is one the game's own sprite
    frame actions use (`this.<method>(...)` as the loaders' `this.loadMovie`, a child clip by its name and
    GetURL2 with the clip as target as the Story sketch screen's thumbs), and every string is an inline
    EA_PushString, so the action needs no ConstantPool:

        this.attachMovie("emptyClip", "nfThumb", 2);
        nfThumb._x = -18.5;  nfThumb._y = -18.5;
        loadMovie(<url>, nfThumb);
    """
    a = Asm(addr)
    a.op(OP_PUSHBYTE, CHOOSEF_DEPTH)
    a.op_u32(OP_PUSHSTRING, sp["clip"])
    a.op_u32(OP_PUSHSTRING, sp["empty"])
    a.op(OP_PUSHBYTE, 3, OP_PUSHTHISVAR)
    a.op_u32(OP_PUSHSTRING, sp["attachMovie"])
    a.op(OP_CALLMETHODPOP)
    for prop in ("_x", "_y"):
        a.op_u32(OP_PUSHSTRING, sp["clip"])
        a.op(OP_GETVARIABLE)
        a.op_u32(OP_PUSHSTRING, sp[prop])
        a.op(OP_PUSHFLOAT)
        a.buf += struct.pack("<f", CHOOSEF_ORIGIN)
        a.op(OP_SETMEMBER)
    a.op_u32(OP_PUSHSTRING, url_ptr)
    a.op_u32(OP_PUSHSTRING, sp["clip"])
    a.op(OP_GETVARIABLE, OP_GETURL2, OP_END)
    return bytes(a.buf)


def _exports(apt, movie):
    """{name: character} of a movie's export table (the first of a repeated name)."""
    count, table = struct.unpack_from("<II", apt, movie + 48)
    out = {}
    for i in range(count):
        name, char = struct.unpack_from("<II", apt, table + 8 * i)
        out.setdefault(cstr(apt, name), char)
    return out


def build_choosef_apt(apt, const, sprite=None):
    """Give thumbAll's frames of the community fighters their loading action. Returns (new .apt, info)."""
    if len(apt) % 4:
        raise PatchError("the .apt length %d is not a multiple of 4" % len(apt))
    movie = struct.unpack_from("<I", const, 0x14)[0]
    if movie + 56 > len(apt) or u32(apt, movie) != 9:
        raise PatchError("movie header not at 0x%X" % movie)
    nchars, chars = u32(apt, movie + 20), u32(apt, movie + 24)
    exports = _exports(apt, movie)
    char = exports.get(CHOOSEF_SPRITE_NAME)
    if char is None or (sprite is not None and char != sprite):
        raise PatchError("%r is not exported as character %s" % (CHOOSEF_SPRITE_NAME, sprite))
    if CHOOSEF_EMPTY_NAME not in exports:
        raise PatchError("the screen exports no %r" % CHOOSEF_EMPTY_NAME)
    ch = u32(apt, chars + 4 * char) if char < nchars else 0
    if not ch or u32(apt, ch) != 5 or u32(apt, ch + 8) != CHOOSEF_SPRITE_FRAMES:
        raise PatchError("%r is not a sprite of %d frames" % (CHOOSEF_SPRITE_NAME, CHOOSEF_SPRITE_FRAMES))
    frames = u32(apt, ch + 12)
    wanted = choosef_frames()
    old, baked = {}, {}
    # the low places' frames (100..127's are as the own-cell patch left them: frame 249 keeps its placement)
    low_frames = {f for f, _ in choosef_frames([p for p in CHOOSEF_PLACES if p < 100])}
    for frame, _ in wanted:
        n, ip = struct.unpack_from("<II", apt, frames + 8 * frame)
        items = [u32(apt, ip + 4 * k) for k in range(n)]
        for k, it in enumerate(items):
            kind = u32(apt, it)
            if kind == 3 and u32(apt, it + 8) == CHOOSEF_DEPTH:
                raise PatchError("frame %d already places something at depth %d" % (frame, CHOOSEF_DEPTH))
            if kind == 3 and u32(apt, it + 8) == CHOOSEF_BAKED_DEPTH and frame in low_frames:
                baked[frame] = k        # a stock picture at a low place (37: Crow 2's): taken out below
            if kind not in (1, 3, 4):
                raise PatchError("frame %d has an item of type %d" % (frame, kind))
        old[frame] = items

    pos = len(apt)
    data = bytearray()

    def put_str(text):
        at = pos + len(data)
        data.extend(text.encode("latin-1") + b"\x00")
        return at

    def pad4():
        while (pos + len(data)) % 4:
            data.append(0)

    sp = dict(clip=put_str(CHOOSEF_CLIP_NAME), empty=put_str(CHOOSEF_EMPTY_NAME),
              attachMovie=put_str("attachMovie"), _x=put_str("_x"), _y=put_str("_y"))
    urls = {frame: put_str(url) for frame, url in wanted}
    remove_at = None
    if baked:
        pad4()
        remove_at = pos + len(data)
        data += struct.pack("<II", 4, CHOOSEF_BAKED_DEPTH)      # RemoveObject, depth 1 (as the empty frames have)
    kept = {}
    for frame in old:
        kept[frame] = list(old[frame])
        if frame in baked:
            kept[frame][baked[frame]] = remove_at
    records = {}
    for frame, url in wanted:
        pad4()
        code_at = pos + len(data)
        data += asm_choosef_action(code_at, sp, urls[frame])
        pad4()
        item_at = pos + len(data)
        data += struct.pack("<II", 1, code_at)
        arr_at = pos + len(data)
        data += struct.pack("<%dI" % (len(kept[frame]) + 1), *(kept[frame] + [item_at]))
        records[frame] = (len(kept[frame]) + 1, arr_at, code_at, url)
    pad4()
    new_apt = bytearray(apt) + data
    for frame, (n, arr_at, _, _) in records.items():
        struct.pack_into("<II", new_apt, frames + 8 * frame, n, arr_at)
    info = dict(frames=frames, records=records, old=old, kept=kept, baked=baked, remove_at=remove_at, strings=sp,
                apt_len=len(new_apt), char=char)
    return bytes(new_apt), info


def verify_choosef_apt(apt, new_apt, info):
    """Only the frame records of the community fighters changed; every new action decodes to the one
    statement sequence, with its strings where they should be; the old items of a frame are kept."""
    frames, records = info["frames"], info["records"]
    allowed = set()
    for frame in records:
        allowed.update((frames + 8 * frame, frames + 8 * frame + 4))
    for off in range(0, len(apt), 4):
        if apt[off:off + 4] != new_apt[off:off + 4] and off not in allowed:
            raise PatchError("original .apt bytes changed unexpectedly at 0x%X" % off)
    shape = [OP_PUSHBYTE, OP_PUSHSTRING, OP_PUSHSTRING, OP_PUSHBYTE, OP_PUSHTHISVAR, OP_PUSHSTRING,
             OP_CALLMETHODPOP] + [OP_PUSHSTRING, OP_GETVARIABLE, OP_PUSHSTRING, OP_PUSHFLOAT, OP_SETMEMBER] * 2 + [
             OP_PUSHSTRING, OP_PUSHSTRING, OP_GETVARIABLE, OP_GETURL2, OP_END]
    for frame, (n, arr_at, code_at, url) in sorted(records.items()):
        if struct.unpack_from("<II", new_apt, frames + 8 * frame) != (n, arr_at):
            raise PatchError("frame record %d is wrong" % frame)
        ptrs = list(struct.unpack_from("<%dI" % n, new_apt, arr_at))
        if ptrs[:-1] != info["kept"][frame] or struct.unpack_from("<II", new_apt, ptrs[-1]) != (1, code_at):
            raise PatchError("frame %d lost an item or its action item is wrong" % frame)
        for k, (was, now) in enumerate(zip(info["old"][frame], ptrs[:-1])):
            if was != now and not (info["baked"].get(frame) == k and now == info["remove_at"]
                                   and struct.unpack_from("<II", new_apt, now) == (4, CHOOSEF_BAKED_DEPTH)):
                raise PatchError("frame %d: item %d changed but its baked picture's removal" % (frame, k))
        ins = decode(new_apt, code_at)
        if [i.op for i in ins] != shape:
            raise PatchError("the action of frame %d does not decode to the expected statements" % frame)
        strings = [cstr(new_apt, i.vals[0]) for i in ins if i.op == OP_PUSHSTRING]
        if strings != [CHOOSEF_CLIP_NAME, CHOOSEF_EMPTY_NAME, "attachMovie", CHOOSEF_CLIP_NAME, "_x",
                       CHOOSEF_CLIP_NAME, "_y", url, CHOOSEF_CLIP_NAME]:
            raise PatchError("the action of frame %d pushes the wrong strings" % frame)
        if ins[-1].nxt > len(new_apt):
            raise PatchError("the action of frame %d runs off the end" % frame)
    return "%d frame actions appended (%d bytes, %d baked picture(s) removed), no existing byte changed but their "         "frame records" % (len(records), len(new_apt) - len(apt), len(info["baked"]))


# --------------------------- battle/chooseF2/3/4 (patch version 6): community fighters beyond the places in the grid
#
# A cell's grid picture is a frame of thumbAll: gotoAndStop(<feID> + 150) in CThumbGrid's redraw and deselect, and
# gotoAndStop(<feID> + 1) in select (1-based). A community fighter beyond the places has a front-end ID of 1000 and
# up (src/hooks/fighter_banks.h): past the sprite's 300 frames, so the call does nothing and the cell keeps frame 0
# (feID 0's picture). Right after each of those three calls the patch calls a function of its own,
#
#     function nfCommunityThumb(nfC, nfI, nfS) {
#         if (nfI < 1000) return;                       // every other cell: as before
#         nfC.gotoAndStop(279);                         // an empty frame (no picture, no action)
#         nfC.attachMovie("emptyClip", "nfThumb", 2);   // as the places' frame actions (patch version 5)
#         nfC.nfThumb._x = -18.5;  nfC.nfThumb._y = -18.5;
#         loadMovie("thumbs/thumb" + nfI + nfS + ".swf", nfC.nfThumb);
#     }
#
# with the cell's clip, its ID and "off" (redraw, deselect) or "" (select): the picture is loaded by its URL, so any ID
# has one (screens/thumbs/thumb<ID>.big and thumb<ID>off.big, which the composer writes for every such fighter). The
# function is defined at the end of the screen's main frame action. The action is copied to the end of the .apt with
# the four insertions (each a multiple of 4 bytes, so every aligned operand keeps its alignment), every branch and
# function size that spans one is re-computed, and the frame's action item points at the copy; the original bytes
# stay where they are.

CHOOSEF_GRID_FUNC = "nfCommunityThumb"
CHOOSEF_GRID_EMPTY_FRAME = 279          # 1-based: frame 278, after the RemoveObject of frame 249 and no action
CHOOSEF_GRID_FIRST_ID = 1000
OP_SUBTRACT, OP_LESS2, OP_GETMEMBER_STACK, OP_CALLFUNCPOP, OP_PUSHSHORT = 0x0B, 0x48, 0x4E, 0x5B, 0xB6
OP_PUSHCONSTWORD, OP_PUSHCONSTBYTE, OP_PUSHVALUEOFVAR, OP_GETNAMEDMEMBER = 0xA3, 0xA2, 0xAE, 0xAF
OP_DEFINELOCAL = 0x3C


def _main_action(apt, const):
    """(item address, block start) of the main timeline's frame-0 action that defines CThumbGrid."""
    movie = struct.unpack_from("<I", const, 0x14)[0]
    nframes, frames = u32(apt, movie + 8), u32(apt, movie + 12)
    strings = const_strings(const)
    for f in range(min(nframes, 1)):
        n, ip = struct.unpack_from("<II", apt, frames + 8 * f)
        for k in range(n):
            item = u32(apt, ip + 4 * k)
            if u32(apt, item) != 1:
                continue
            start = u32(apt, item + 4)
            ins = decode(apt, start)
            if any(i.op == OP_DEFFUNC and i.vals[0] and cstr(apt, i.vals[0]) == "CThumbGrid" for i in ins):
                return item, start, ins, strings
    raise PatchError("no main frame action defines CThumbGrid")


def _grid_sites(apt, ins, strings):
    """The three gotoAndStop calls of a grid cell: [(after address, clip-expression bytes, id-expression bytes,
    suffix)]."""
    slots = pool_slots(apt, ins)

    def name(i):
        return strings[slots[i.vals[0]]] if i.vals[0] < len(slots) else None

    def raw(seq):
        return b"".join(apt[i.addr:i.nxt] for i in seq)

    sites = []

    def walk(body):
        for j, i in enumerate(body):
            if i.op == OP_DEFFUNC:
                b, size = i.body
                walk(decode(apt, b, b + size))
            if i.op != OP_CALLNAMEDMETHODPOP or name(i) != "gotoAndStop":
                continue
            before = body[:j]
            # the clip: thumbClip (redraw), or this.sTargetClip + ".thumbNails.thumb" + clipNum (select, deselect)
            if len(before) >= 2 and before[-2].op == OP_PUSHCONSTWORD and name(before[-2]) == "thumbClip":
                clip = before[-2:]
            elif (len(before) >= 8 and [x.op for x in before[-8:]] == [OP_PUSHTHISVAR, OP_GETNAMEDMEMBER,
                  OP_PUSHCONSTWORD, OP_ADD2, OP_PUSHCONSTWORD, OP_GETVARIABLE, OP_ADD2, OP_GETVARIABLE]
                  and name(before[-6]) == ".thumbNails.thumb" and name(before[-4]) == "clipNum"):
                clip = before[-8:]
            else:
                continue
            rest = before[:-len(clip)]
            if len(rest) < 4 or rest[-1].op != OP_PUSHONE:
                continue
            frame = rest[:-1]
            if (len(frame) >= 10 and frame[-3].op == OP_PUSHCONSTWORD and name(frame[-3]) == "CHAR_SELECTED_OFFSET"
                    and frame[-2].op == OP_GETVARIABLE and frame[-1].op == OP_ADD2):
                idexpr = frame[:-3]
                # redraw: aCharacters[charNum].getID() (7 instructions); deselect: charFrame
                if (len(idexpr) >= 7 and [x.op for x in idexpr[-7:]] == [OP_PUSHZERO, OP_PUSHVALUEOFVAR,
                        OP_PUSHCONSTWORD, OP_GETVARIABLE, OP_GETMEMBER_STACK, OP_PUSHCONSTBYTE, OP_CALLMETHOD]
                        and name(idexpr[-1 - 1]) == "getID"):
                    sites.append((i.nxt, raw(clip), raw(idexpr[-7:]), "off"))
                elif idexpr[-2].op == OP_PUSHCONSTWORD and name(idexpr[-2]) == "charFrame" \
                        and idexpr[-1].op == OP_GETVARIABLE:
                    sites.append((i.nxt, raw(clip), raw(idexpr[-2:]), "off"))
            elif (len(frame) >= 3 and frame[-2].op == OP_PUSHCONSTWORD and name(frame[-2]) == "charFrame"
                  and frame[-1].op == OP_GETVARIABLE and frame[-3].op == OP_DEFINELOCAL):
                # select: charFrame = getID() + 1 just before; the ID is charFrame - 1
                sites.append((i.nxt, raw(clip), raw(frame[-2:]) + bytes([OP_PUSHONE, OP_SUBTRACT]), ""))
    walk(ins)
    if len(sites) != 3 or sorted(s[3] for s in sites) != ["", "off", "off"]:
        raise PatchError("the grid's picture calls are not the three expected (%d found)" % len(sites))
    return sites


def _relocate(apt, start, ins, inserts):
    """The action block from `start` with `inserts` [(old address, bytes)] spliced in (each before the instruction
    at that address, a multiple of 4 bytes long), every branch and function size re-computed. Returns (bytes,
    new-address function for an old instruction address, given the new start)."""
    end = ins[-1].nxt
    inserts = sorted(inserts)
    for p, code in inserts:
        if len(code) % 4 or not start <= p < end:
            raise PatchError("internal: insertion at 0x%X" % p)

    def moved(x, inclusive):
        return (x - start) + sum(len(c) for p, c in inserts if (p <= x if inclusive else p < x))

    out = bytearray()
    last = start
    for p, code in inserts:
        out += apt[last:p] + code
        last = p
    out += apt[last:end]

    def fix(body):
        for i in body:
            if i.op in (OP_BRANCH, OP_IF):
                at = align(i.addr + 1, 4)
                new_end = moved(i.addr, True) + (i.nxt - i.addr)
                off = moved(i.target, False) - new_end
                struct.pack_into("<i", out, moved(i.addr, True) + (at - i.addr), off)
            elif i.op == OP_DEFFUNC:
                b, size = i.body
                new_size = moved(b + size, True) - moved(b, True)
                struct.pack_into("<I", out, moved(i.addr, True) + (i.vals[4] - i.addr), new_size)
                fix(decode(apt, b, b + size))
            elif i.op in (0x94, 0x8F, 0x87, 0x8A, 0x9F, 0x81):
                if any(i.addr < p < i.nxt + 64 for p, _ in inserts):
                    raise PatchError("opcode 0x%02X near an insertion is not handled" % i.op)
    fix(ins)
    return bytes(out), moved


def _asm_site(at, clip, idexpr, suffix_ptr, func_ptr):
    a = Asm(at)
    a.op_u32(OP_PUSHSTRING, suffix_ptr)                 # nfS
    a.buf += idexpr                                     # nfI (no aligned operands: copied as they are)
    a.buf += clip                                       # nfC
    a.op(OP_PUSHBYTE, 3)
    a.op_u32(OP_PUSHSTRING, func_ptr)
    a.op(OP_CALLFUNCPOP)
    a.pad_to_multiple_of_4()
    return bytes(a.buf)


def _asm_function(at, sp):
    """DefineFunction nfCommunityThumb(nfC, nfI, nfS), its body as in the comment above."""
    a = Asm(at)
    a.buf.append(OP_DEFFUNC)
    a.align_to(4)
    size_at = len(a.buf) + 12
    a.buf += struct.pack("<IIII", sp["func"], 3, sp["params"], 0) + FUNC_MARKER
    body = len(a.buf)

    def var(n):
        a.op_u32(OP_PUSHSTRING, sp[n])
        a.op(OP_GETVARIABLE)

    var("nfI")
    a.op(OP_PUSHSHORT)
    a.buf += struct.pack("<h", CHOOSEF_GRID_FIRST_ID)
    a.op(OP_LESS2)
    branch = a.op_u32(OP_IF, 0)
    after = len(a.buf)
    a.op(OP_PUSHSHORT)
    a.buf += struct.pack("<h", CHOOSEF_GRID_EMPTY_FRAME)
    a.op(OP_PUSHONE)
    var("nfC")
    a.op_u32(OP_PUSHSTRING, sp["gotoAndStop"])
    a.op(OP_CALLMETHODPOP)
    a.op(OP_PUSHBYTE, CHOOSEF_DEPTH)
    a.op_u32(OP_PUSHSTRING, sp["clip"])
    a.op_u32(OP_PUSHSTRING, sp["empty"])
    a.op(OP_PUSHBYTE, 3)
    var("nfC")
    a.op_u32(OP_PUSHSTRING, sp["attachMovie"])
    a.op(OP_CALLMETHODPOP)
    for prop in ("_x", "_y"):
        var("nfC")
        a.op_u32(OP_PUSHSTRING, sp["clip"])
        a.op(OP_GETMEMBER_STACK)
        a.op_u32(OP_PUSHSTRING, sp[prop])
        a.op(OP_PUSHFLOAT)
        a.buf += struct.pack("<f", CHOOSEF_ORIGIN)
        a.op(OP_SETMEMBER)
    a.op_u32(OP_PUSHSTRING, sp["prefix"])
    var("nfI")
    a.op(OP_ADD2)
    var("nfS")
    a.op(OP_ADD2)
    a.op_u32(OP_PUSHSTRING, sp["ext"])
    a.op(OP_ADD2)
    var("nfC")
    a.op_u32(OP_PUSHSTRING, sp["clip"])
    a.op(OP_GETMEMBER_STACK)
    a.op(OP_GETURL2)
    end = len(a.buf)
    a.patch_u32(branch, end - after)
    a.patch_u32(size_at, end - body)
    a.pad_to_multiple_of_4()
    return bytes(a.buf)


def build_choosef_grid(apt, const):
    """The community fighters' grid pictures by URL (patch version 6). `apt` may already carry the places' frame
    actions (build_choosef_apt). Returns (new .apt, info)."""
    if len(apt) % 4:
        raise PatchError("the .apt length %d is not a multiple of 4" % len(apt))
    item, start, ins, strings = _main_action(apt, const)
    sites = _grid_sites(apt, ins, strings)
    end = ins[-1].nxt
    if ins[-1].op != OP_END:
        raise PatchError("the main action does not end with End")
    pos = len(apt)
    data = bytearray()

    def put_str(text):
        at = pos + len(data)
        data.extend(text.encode("latin-1") + b"\x00")
        return at

    sp = {k: put_str(v) for k, v in (("func", CHOOSEF_GRID_FUNC), ("nfC", "nfC"), ("nfI", "nfI"), ("nfS", "nfS"),
                                      ("off", "off"), ("on", ""), ("gotoAndStop", "gotoAndStop"),
                                      ("clip", CHOOSEF_CLIP_NAME), ("empty", CHOOSEF_EMPTY_NAME),
                                      ("attachMovie", "attachMovie"), ("_x", "_x"), ("_y", "_y"),
                                      ("prefix", "thumbs/thumb"), ("ext", ".swf"))}
    while (pos + len(data)) % 4:
        data.append(0)
    sp["params"] = pos + len(data)
    data += struct.pack("<3I", sp["nfC"], sp["nfI"], sp["nfS"])
    # the copy keeps the block's address modulo 4, so every aligned operand keeps its padding
    while (pos + len(data)) % 4 != start % 4:
        data.append(0)
    new_start = pos + len(data)
    inserts = [(after, _asm_site(after, clip, idexpr, sp["off"] if sfx else sp["on"], sp["func"]))
               for after, clip, idexpr, sfx in sites]
    inserts.append((ins[-1].addr, _asm_function(ins[-1].addr, sp)))
    block, moved = _relocate(apt, start, ins, inserts)
    new_apt = bytearray(apt) + data + block
    while len(new_apt) % 4:
        new_apt.append(0)
    struct.pack_into("<I", new_apt, item + 4, new_start)
    info = dict(item=item, start=start, end=end, new_start=new_start, sites=[s[0] for s in sites],
                inserted=sum(len(c) for _, c in inserts), strings=sp, moved=moved)
    return bytes(new_apt), info


def verify_choosef_grid(apt, new_apt, info):
    """The copy decodes to its exact end with every function body to its size and every branch on an instruction;
    it is the original instruction for instruction (branch offsets and sizes aside) plus the four insertions; the
    only original bytes changed are the action item's pointer."""
    item, start, new_start = info["item"], info["start"], info["new_start"]
    for off in range(0, len(apt), 4):
        if apt[off:off + 4] != new_apt[off:off + 4] and off != item + 4:
            raise PatchError("original .apt bytes changed unexpectedly at 0x%X" % off)
    old = decode(apt, start)
    new, nfun = verify_block(new_apt, new_start)
    _, old_fun = verify_block(apt, start)
    if nfun != old_fun + 1:
        raise PatchError("function count %d, expected %d" % (nfun, old_fun + 1))

    def flat(buf, ins):
        out = []
        for i in ins:
            if i.op == OP_DEFFUNC:
                b, size = i.body
                out.append((i.op, i.vals[0], i.vals[1], i.vals[2]))
                out += flat(buf, decode(buf, b, b + size))
                out.append(("end",))
            elif i.op in (OP_BRANCH, OP_IF):
                out.append((i.op,))
            else:
                out.append((i.op,) + tuple(i.vals))
        return out
    a, b = flat(apt, old), flat(new_apt, new)
    calls = sum(1 for x in b if x[0] == OP_CALLFUNCPOP) - sum(1 for x in a if x[0] == OP_CALLFUNCPOP)
    if calls != 3:
        raise PatchError("expected three new calls, found %d" % calls)
    # the original sequence is a subsequence of the new one
    k = 0
    for x in b:
        if k < len(a) and x == a[k]:
            k += 1
    if k != len(a):
        raise PatchError("the copied action lost original instructions (%d of %d matched)" % (k, len(a)))
    return "grid pictures by URL: %d bytes inserted at 0x%s, main action copied to 0x%X" % (
        info["inserted"], ", 0x".join("%X" % s for s in info["sites"]), new_start)


def choosef_patcher(entry_name):
    """The Plan patcher of one select screen: raw (RefPack) entry -> (new raw entry, report lines)."""
    movie, payload_sha, apt_sha, const_sha, sprite = CHOOSEF_ENTRIES[entry_name]

    def patch(raw_entry, extras=None):
        payload = refpack_decode(raw_entry)
        if sha(payload) != payload_sha:
            raise PatchError("the decompressed %s payload does not match the expected hash" % movie)
        files = inner_files(payload)
        apt, const = files.get(movie + ".apt"), files.get(movie + ".const")
        if apt is None or const is None or sha(apt) != apt_sha or sha(const) != const_sha:
            raise PatchError("%s.apt/.const do not match the expected hashes" % movie)
        places_apt, info = build_choosef_apt(apt, const, sprite)
        lines = ["verify %s.apt: %s" % (movie, verify_choosef_apt(apt, places_apt, info))]
        new_apt, grid = build_choosef_grid(places_apt, const)
        lines.append("verify %s.apt: %s" % (movie, verify_choosef_grid(places_apt, new_apt, grid)))
        new_payload = rebuild_bigf(payload, {movie + ".apt": new_apt})
        new_files = inner_files(new_payload)
        if list(new_files) != list(files):
            raise PatchError("%s inner file list changed" % movie)
        for n in files:
            if n != movie + ".apt" and files[n] != new_files[n]:
                raise PatchError("%s inner file %s changed" % (movie, n))
        if new_files[movie + ".apt"] != new_apt:
            raise PatchError("re-laid out %s.apt differs from the built one" % movie)
        new_raw = refpack_encode_literal(new_payload)
        if refpack_decode(new_raw) != new_payload:
            raise PatchError("%s RefPack round trip failed" % movie)
        lines.append("%s payload %d -> %d bytes, entry %d -> %d bytes (literal RefPack)"
                     % (movie, len(payload), len(new_payload), len(raw_entry), len(new_raw)))
        return new_raw, lines
    return patch


# ------------------------------------------------------------ textures (DXT5)

def shpx_directory(x):
    """{name: offset} of an SHPX bank (header: magic, size, count, tag; then (4-char name, offset))."""
    if x[:4] != b"SHPX" or struct.unpack_from("<I", x, 4)[0] != len(x):
        raise PatchError("not an SHPX bank")
    count = struct.unpack_from("<I", x, 8)[0]
    out = {}
    for i in range(count):
        name = x[16 + 8 * i:20 + 8 * i].decode("latin-1").strip()
        out[name] = struct.unpack_from("<I", x, 20 + 8 * i)[0]
    return out


def shpx_image(x, name):
    """(data offset, width, height) of a plain DXT5 (type 0x62) image of the bank."""
    off = shpx_directory(x).get(name)
    if off is None:
        raise PatchError("texture %s not in the bank" % name)
    if x[off] != 0x62:
        raise PatchError("texture %s is not DXT5" % name)
    w, h = struct.unpack_from("<HH", x, off + 4)
    if struct.unpack_from("<I", x, off + 12)[0] & 0xF0002000:      # mips or swizzle: not handled
        raise PatchError("texture %s has mips or is swizzled" % name)
    if w % 4 or h % 4 or off + 16 + w * h > len(x):
        raise PatchError("texture %s has an unexpected layout" % name)
    return off + 16, w, h


def _expand565(v):
    return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def _pack565(c):
    return ((c[0] * 31 + 127) // 255) << 11 | ((c[1] * 63 + 127) // 255) << 5 | ((c[2] * 31 + 127) // 255)


def _palette(c0, c1):
    e0, e1 = _expand565(c0), _expand565(c1)
    return [e0, e1, tuple((2 * p + q) // 3 for p, q in zip(e0, e1)), tuple((p + 2 * q) // 3 for p, q in zip(e0, e1))]


def dxt5_decode_block(blk):
    """16 bytes -> 16 (r, g, b, a) tuples in row-major order."""
    a0, a1 = blk[0], blk[1]
    bits = int.from_bytes(blk[2:8], "little")
    tab = [a0, a1]
    if a0 > a1:
        tab += [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        tab += [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    c0, c1, cb = struct.unpack_from("<HHI", blk, 8)
    pal = _palette(c0, c1)
    return [pal[(cb >> (2 * i)) & 3] + (tab[(bits >> (3 * i)) & 7],) for i in range(16)]


def dxt5_encode_block(px):
    """16 (r, g, b, a) tuples -> 16 bytes. The endpoints are the darkest and the brightest pixel,
    which fits black-and-white poster art; c0 >= c1 so every reading of the colour block agrees."""
    lum = [p[0] * 299 + p[1] * 587 + p[2] * 114 for p in px]
    c0, c1 = _pack565(px[lum.index(max(lum))]), _pack565(px[lum.index(min(lum))])
    if c0 < c1:
        c0, c1 = c1, c0
    pal = _palette(c0, c1)
    cb = 0
    for i, p in enumerate(px):
        best = min(range(4), key=lambda k: (p[0] - pal[k][0]) ** 2 + (p[1] - pal[k][1]) ** 2 + (p[2] - pal[k][2]) ** 2)
        cb |= best << (2 * i)
    al = [p[3] for p in px]
    hi_a, lo_a = max(al), min(al)
    if hi_a == lo_a:
        ab = bytes([hi_a, lo_a]) + bytes(6)
    else:
        tab = [hi_a, lo_a] + [((7 - i) * hi_a + i * lo_a) // 7 for i in range(1, 7)]
        bits = 0
        for i, a in enumerate(al):
            bits |= min(range(8), key=lambda k: abs(a - tab[k])) << (3 * i)
        ab = bytes([hi_a, lo_a]) + bits.to_bytes(6, "little")
    return ab + struct.pack("<HHI", c0, c1, cb)


def dxt5_region(data, base, w, rect):
    """Decode a block-aligned rectangle (x, y, w, h) of an image into rows of (r, g, b, a)."""
    x0, y0, rw, rh = rect
    rows = [[None] * rw for _ in range(rh)]
    for by in range(y0 // 4, (y0 + rh) // 4):
        for bx in range(x0 // 4, (x0 + rw) // 4):
            at = base + (by * (w // 4) + bx) * 16
            for i, p in enumerate(dxt5_decode_block(data[at:at + 16])):
                rows[by * 4 + i // 4 - y0][bx * 4 + i % 4 - x0] = p
    return rows


def dxt5_store_region(buf, base, w, rect, rows):
    """Encode `rows` (rectangle-sized) into the bytearray, whole blocks only."""
    x0, y0, rw, rh = rect
    if x0 % 4 or y0 % 4 or rw % 4 or rh % 4:
        raise PatchError("the poster rectangle must be block aligned")
    for by in range(y0 // 4, (y0 + rh) // 4):
        for bx in range(x0 // 4, (x0 + rw) // 4):
            px = [rows[by * 4 + i // 4 - y0][bx * 4 + i % 4 - x0] for i in range(16)]
            at = base + (by * (w // 4) + bx) * 16
            buf[at:at + 16] = dxt5_encode_block(px)


def resample_rgba(rows, size):
    """Bilinear resample of rows of (r, g, b, a) with a in 0..1 to size (w, h). The colour is
    averaged premultiplied by alpha so soft edges do not pick up the colour of transparent pixels."""
    sh, sw = len(rows), len(rows[0])
    dw, dh = size
    out = []
    for y in range(dh):
        fy = min(max((y + 0.5) * sh / dh - 0.5, 0.0), sh - 1.0)
        y0 = int(fy)
        y1 = min(y0 + 1, sh - 1)
        ty = fy - y0
        row = []
        for x in range(dw):
            fx = min(max((x + 0.5) * sw / dw - 0.5, 0.0), sw - 1.0)
            x0 = int(fx)
            x1 = min(x0 + 1, sw - 1)
            tx = fx - x0
            acc = [0.0, 0.0, 0.0, 0.0]
            for yy, wy in ((y0, 1 - ty), (y1, ty)):
                for xx, wx in ((x0, 1 - tx), (x1, tx)):
                    p, wgt = rows[yy][xx], wy * wx
                    acc[0] += p[0] * p[3] * wgt
                    acc[1] += p[1] * p[3] * wgt
                    acc[2] += p[2] * p[3] * wgt
                    acc[3] += p[3] * wgt
            if acc[3] > 1e-6:
                row.append((acc[0] / acc[3], acc[1] / acc[3], acc[2] / acc[3], acc[3]))
            else:
                row.append((0.0, 0.0, 0.0, 0.0))
        out.append(row)
    return out


def _lum(p):
    return (p[0] + p[1] + p[2]) / 3.0


def poster_art(poster_xsh, check_hash=True):
    """The library's ONLINE poster, cropped to its opaque bounding box: rows of (r, g, b, a 0..1)."""
    base, w, h = shpx_image(poster_xsh, POSTER_TEX)
    if (w, h) != (POSTER_SRC_W, POSTER_SRC_H):
        raise PatchError("the ONLINE poster texture is not 128x128")
    if check_hash and sha(bytes(poster_xsh[base:base + w * h])) != POSTER_TEX_SHA256:
        raise PatchError("the ONLINE poster texture does not match the expected hash")
    rows = dxt5_region(poster_xsh, base, w, (0, 0, w, h))
    xs = [x for r in rows for x, p in enumerate(r) if p[3] > 128]
    ys = [y for y, r in enumerate(rows) for p in r if p[3] > 128]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    # The art is black and white; its edge pixels carry a green fringe, and the wall's posters are neutral.
    return [[(_lum(p),) * 3 + (p[3] / 255.0,) for p in r[x0:x1 + 1]] for r in rows[y0:y1 + 1]]


def _outer_blocks(rect):
    """The smallest 4-aligned rectangle containing `rect`."""
    x0, y0, rw, rh = rect
    ax0, ay0 = x0 // 4 * 4, y0 // 4 * 4
    return (ax0, ay0, (x0 + rw + 3) // 4 * 4 - ax0, (y0 + rh + 3) // 4 * 4 - ay0)


def paint_poster(xsh, poster_xsh, check_hash=True):
    """New mainMenu.xsh with the ONLINE poster painted over the vacant bottom-left slot of both wall
    textures. Same size, same directory; only whole DXT5 blocks inside POSTER_RECT change.
    Returns (new bytes, report line, the intended pixels per texture, for verification)."""
    art = poster_art(poster_xsh, check_hash)
    px0, py0, pw, ph = POSTER_RECT
    scaled = resample_rgba(art, (pw, ph))
    dim_base, dw, dh = shpx_image(xsh, WALL_DIM_TEX)
    lit_base, lw, lh = shpx_image(xsh, WALL_LIT_TEX)
    if (dw, dh) != (WALL_W, WALL_H) or (lw, lh) != (WALL_W, WALL_H):
        raise PatchError("the wall textures are not 256x512")
    # Match the neighbours: white level from the STORY poster of the lit wall; the dim wall is the
    # lit wall darkened by the ratio measured on the same poster.
    ref = _outer_blocks(REF_RECT)
    lit_ref = dxt5_region(xsh, lit_base, lw, ref)
    dim_ref = dxt5_region(xsh, dim_base, dw, ref)
    ref_l = sorted(_lum(p) for r in lit_ref for p in r)
    opaque = sorted(_lum(p) for r in art for p in r if p[3] > 0.9)
    gain = min(1.0, max(0.5, ref_l[int(len(ref_l) * 0.95)] / max(1.0, opaque[int(len(opaque) * 0.95)])))
    sums_l = [sum(p[k] for r in lit_ref for p in r) for k in range(3)]
    sums_d = [sum(p[k] for r in dim_ref for p in r) for k in range(3)]
    ratio = [min(1.0, max(0.05, d / max(1.0, l))) for d, l in zip(sums_d, sums_l)]
    out = bytearray(xsh)
    expected = {}
    for name, base, w, factors in ((WALL_LIT_TEX, lit_base, lw, [gain] * 3),
                                   (WALL_DIM_TEX, dim_base, dw, [gain * r for r in ratio])):
        wall = dxt5_region(xsh, base, w, POSTER_RECT)
        rows = []
        for y in range(ph):
            row = []
            for x in range(pw):
                wp, sp = wall[y][x], scaled[y][x]
                a = max(0.0, min(1.0, sp[3]))
                row.append(tuple(int(round(min(255.0, max(0.0, wp[k] * (1 - a) + sp[k] * factors[k] * a))))
                                 for k in range(3)) + (255,))
            rows.append(row)
        dxt5_store_region(out, base, w, POSTER_RECT, rows)
        expected[name] = rows
    line = "ONLINE poster %dx%d painted at texture px (%d, %d) of walls %s/%s (gain %.2f, dim ratio %.2f)" % (
        pw, ph, px0, py0, WALL_LIT_TEX, WALL_DIM_TEX, gain, sum(ratio) / 3)
    return bytes(out), line, expected


def verify_paint(old, new, expected):
    """Outside the poster blocks nothing changed; inside, the encoded blocks are close to the intent."""
    if len(old) != len(new) or old[:16] != new[:16] or shpx_directory(old) != shpx_directory(new):
        raise PatchError("texture bank size or directory changed")
    x0, y0, rw, rh = POSTER_RECT
    allowed = set()
    for name in (WALL_LIT_TEX, WALL_DIM_TEX):
        base, w, h = shpx_image(new, name)
        for by in range(y0 // 4, (y0 + rh) // 4):
            for bx in range(x0 // 4, (x0 + rw) // 4):
                allowed.add(base + (by * (w // 4) + bx) * 16)
        got = dxt5_region(new, base, w, POSTER_RECT)
        err = n = 0
        for y in range(rh):
            for x in range(rw):
                for k in range(3):
                    err += abs(got[y][x][k] - expected[name][y][x][k])
                    n += 1
        if err / n > MAX_PAINT_ERROR:
            raise PatchError("texture %s: the encoded poster is too far from the intended picture (%.1f)"
                             % (name, err / n))
    for at in range(0, len(old), 16):       # every 16-byte cell: only poster blocks may differ
        if old[at:at + 16] != new[at:at + 16] and at not in allowed:
            raise PatchError("the texture bank changed outside the poster blocks (offset 0x%X)" % at)
    return "%d poster blocks re-encoded, nothing else in the bank changed" % len(allowed)


# --------------------------------------------------------- entry / archive level

def sha(b):
    return hashlib.sha256(b).hexdigest()


def patch_entry(raw_entry, extras=None):
    """Given the archive's raw (RefPack) mainMenu entry, and the raw ONLINE poster library entry
    (`extras[POSTER_ENTRY_NAME]`), return (new raw entry, report lines)."""
    lines = ["patch version %d" % PATCH_VERSION]
    if sha(raw_entry) != ENTRY_RAW_SHA256:
        lines.append("note: archive entry differs from the reference build (payload is checked instead)")
    payload = refpack_decode(raw_entry)
    if sha(payload) != PAYLOAD_SHA256:
        raise PatchError("decompressed mainMenu payload does not match the expected hash "
                         "(different region or version); refusing to patch")
    files = inner_files(payload)
    apt, const = files.get("mainMenu.apt"), files.get("mainMenu.const")
    if apt is None or const is None or sha(apt) != APT_SHA256 or sha(const) != CONST_SHA256:
        raise PatchError("mainMenu.apt/.const do not match the expected hashes")
    new_apt, info = build_new_apt(apt, const)
    lines.append("verify .apt: " + verify_new_apt(apt, new_apt, info))
    xsh = files.get("mainMenu.xsh")
    if xsh is None:
        raise PatchError("mainMenu.xsh missing")
    poster_raw = (extras or {}).get(POSTER_ENTRY_NAME)
    if poster_raw is None:
        raise PatchError("the poster library entry %s was not supplied" % POSTER_ENTRY_NAME)
    if sha(poster_raw) != POSTER_ENTRY_RAW_SHA256:
        lines.append("note: poster library entry differs from the reference build (payload is checked instead)")
    poster_payload = refpack_decode(poster_raw)
    if sha(poster_payload) != POSTER_PAYLOAD_SHA256:
        raise PatchError("the poster library %s does not match the expected hash; refusing to patch"
                         % POSTER_ENTRY_NAME)
    poster_xsh = inner_files(poster_payload).get(POSTER_XSH)
    if poster_xsh is None:
        raise PatchError("%s missing" % POSTER_XSH)
    new_xsh, paint_line, intended = paint_poster(xsh, poster_xsh)
    lines.append(paint_line)
    lines.append("verify .xsh: " + verify_paint(xsh, new_xsh, intended))
    new_payload = rebuild_bigf(payload, {"mainMenu.apt": new_apt, "mainMenu.xsh": new_xsh})
    # Inner file check: everything except the .apt is byte-identical and the .apt starts as before.
    new_files = inner_files(new_payload)
    if list(new_files) != list(files):
        raise PatchError("inner file list changed")
    for n in files:
        if n not in ("mainMenu.apt", "mainMenu.xsh") and files[n] != new_files[n]:
            raise PatchError("inner file %s changed" % n)
    if new_files["mainMenu.apt"] != new_apt or new_files["mainMenu.xsh"] != new_xsh:
        raise PatchError("re-laid out .apt/.xsh differ from the built ones")
    new_raw = refpack_encode_literal(new_payload)
    if refpack_decode(new_raw) != new_payload:
        raise PatchError("RefPack round trip failed")
    lines.append("payload %d -> %d bytes, entry %d -> %d bytes (literal RefPack)"
                 % (len(payload), len(new_payload), len(raw_entry), len(new_raw)))
    return new_raw, lines


class Plan:
    """Everything needed to write or compare the patched archive. The main entry is patched by
    `patcher(raw, extras)`; `more` lists further entries as (name, patcher(raw)) pairs, or
    (name, patcher(raw), True) for an optional one: an optional entry that is missing or that its
    patcher refuses stays as it is and leaves a note line. Each patched entry is appended to the
    archive (4-aligned) and its directory record points at the copy."""

    def __init__(self, src_path, entry_name=ENTRY_NAME, patcher=None, extra_entries=None, more=None):
        self.src = src_path
        self.size = os.path.getsize(src_path)
        if more is None and patcher is None:
            more = [(CMTYPE_ENTRY_NAME, patch_cmtype_entry)]
            more += [(name, choosef_patcher(name), True) for name in CHOOSEF_ENTRIES]
        self.lines = []
        with open(src_path, "rb") as f:
            top = f.read(32)
            if top[:4] not in (b"BIG4", b"BIGF"):
                raise PatchError("%s is not a BIG archive" % src_path)
            hdr = struct.unpack_from(">I", top, 12)[0]
            if not 16 < hdr < 64 * 1024 * 1024:
                raise PatchError("implausible directory size %d" % hdr)
            f.seek(0)
            self.head = bytearray(f.read(hdr + 4096))
            self.records = big_records(self.head)

            def read(name):
                rec = [r for r in self.records if r[0] == name]
                if len(rec) != 1:
                    raise PatchError("%s not found in the archive" % name)
                if rec[0][2] + rec[0][3] > self.size:
                    raise PatchError("directory entry %s runs past the end of the file" % name)
                f.seek(rec[0][2])
                return rec[0][1], f.read(rec[0][3])

            self.rec_pos, raw = read(entry_name)
            self.old_off, self.old_size = [r for r in self.records if r[0] == entry_name][0][2:]
            extras = {}
            wanted = [POSTER_ENTRY_NAME] if extra_entries is None and patcher is None else (extra_entries or [])
            for name in wanted:
                extras[name] = read(name)[1]
            jobs = [(entry_name, self.rec_pos, (patcher or patch_entry), (raw, extras), False)]
            for item in (more or []):
                name, fn, optional = item[0], item[1], len(item) > 2 and bool(item[2])
                try:
                    rpos, raw2 = read(name)
                except PatchError as e:
                    if not optional:
                        raise
                    self.lines.append("note: %s left as it is (%s)" % (name, e))
                    continue
                jobs.append((name, rpos, fn, (raw2,), optional))
        self.blobs = []                     # [(archive offset, bytes)]
        cursor = self.size
        for name, rpos, fn, args, optional in jobs:
            try:
                new_raw, lines = fn(*args)
            except PatchError as e:
                if not optional:
                    raise
                self.lines.append("note: %s left as it is (%s)" % (name, e))
                continue
            self.lines += lines
            at = align(cursor, OUTER_ALIGN)
            self.blobs.append((at, new_raw))
            struct.pack_into(">II", self.head, rpos, at, len(new_raw))
            cursor = at + len(new_raw)
        self.append_at, self.new_raw = self.blobs[0]     # the main entry
        self.final_size = cursor
        total = struct.unpack_from("<I", self.head, 4)[0]
        if total != self.size:
            self.lines.append("note: header total size %d differs from the file size %d" % (total, self.size))
        struct.pack_into("<I", self.head, 4, self.final_size)


CHUNK = 8 * 1024 * 1024


def write_override(plan, dst):
    tmp = dst + ".tmp"
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    try:
        with open(plan.src, "rb") as s, open(tmp, "wb") as d:
            n = len(plan.head)
            d.write(plan.head[:min(n, plan.size)])
            s.seek(min(n, plan.size))
            while True:
                chunk = s.read(CHUNK)
                if not chunk:
                    break
                d.write(chunk)
            for at, blob in plan.blobs:
                d.write(b"\x00" * (at - d.tell()))
                d.write(blob)
        if os.path.getsize(tmp) != plan.final_size:
            raise PatchError("written size %d != expected %d" % (os.path.getsize(tmp), plan.final_size))
        problems = compare_override(plan, tmp)
        if problems:
            raise PatchError("verification of the written file failed: " + problems[0])
        os.replace(tmp, dst)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


def compare_override(plan, path):
    """[] when `path` is exactly the patched archive for this plan, else the differences."""
    problems = []
    if os.path.getsize(path) != plan.final_size:
        return ["size %d, expected %d" % (os.path.getsize(path), plan.final_size)]
    n = min(len(plan.head), plan.size)
    with open(path, "rb") as o, open(plan.src, "rb") as s:
        if o.read(n) != bytes(plan.head[:n]):
            problems.append("header/directory differs")
        s.seek(n)
        left = plan.size - n
        while left > 0:
            a, b = o.read(min(CHUNK, left)), s.read(min(CHUNK, left))
            if a != b:
                problems.append("archive body differs from the original")
                break
            left -= len(a)
        else:
            cursor = plan.size
            for at, blob in plan.blobs:
                o.seek(cursor)
                if o.read(at - cursor).strip(b"\x00"):
                    problems.append("alignment padding is not zero")
                if o.read(len(blob)) != blob:
                    problems.append("appended entry differs")
                cursor = at + len(blob)
    return problems


# ------------------------------------------------------------------------- CLI

def default_data():
    return os.environ.get("DEFJAM_DATA", "")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", default=default_data(), help="data folder (default $DEFJAM_DATA)")
    ap.add_argument("--check", action="store_true", help="report whether the override exists and is current; write nothing")
    ap.add_argument("--rebuild", action="store_true", help="rewrite even when the override is current")
    args = ap.parse_args(argv)
    if not args.data:
        print("error: no data folder (pass --data or set DEFJAM_DATA)", file=sys.stderr)
        return 2
    src = os.path.join(args.data, "extracted", SCREENS_REL)
    dst = os.path.join(args.data, "override", SCREENS_REL)
    if not os.path.isfile(src):
        print("error: %s not found" % src, file=sys.stderr)
        return 2
    if os.path.abspath(src) == os.path.abspath(dst):
        print("error: source and override are the same file", file=sys.stderr)
        return 2
    try:
        plan = Plan(src)
    except PatchError as e:
        print("refused: %s" % e, file=sys.stderr)
        return 2
    for line in plan.lines:
        print(line)
    if not os.path.isfile(dst):
        state, code = "missing", 1
    else:
        problems = compare_override(plan, dst)
        state, code = ("current", 0) if not problems else ("stale (%s)" % problems[0], 1)
    if args.check:
        print("override %s: %s" % (dst, state))
        return code
    if state == "current" and not args.rebuild:
        print("override %s is already current (%d bytes)" % (dst, os.path.getsize(dst)))
        return 0
    write_override(plan, dst)
    print("wrote %s (%d bytes); original untouched" % (dst, os.path.getsize(dst)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
