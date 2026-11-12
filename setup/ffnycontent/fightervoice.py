# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 sweeax-exe (FFNY Online)
"""A new fighter's own voice: the game's speech format (EA SCHl, EA-XA "R2" ADPCM), an encoder and decoder,
and the voice files a new-fighter pack's lines become (``docs/research/new-fighter-voice.md`` in the game
repository).

How a fighter speaks. A voice number N names five things in ``audio/speech2.viv``:

* three banks, each an ``.hdr`` (entry table) and a ``.dat`` (the clips, one SCHl each at 0x100 steps):
  ``speech\\Character\\Def_Jam_Characters\\NNN`` (in-fight lines; entry field 0 is the line's event: 0 the Blazin
  shout, 1 weapon lines, 2 environment-attack lines, 3 lines after a special move, in the order of the bank's
  ``Events.evt``), ``speech\\Taunts\\...\\NNN`` (the taunt lines) and ``speech\\Enviro\\...\\NNN`` (crowd lines,
  event ``Char_Enviro_CRD``); bank clips have no rate tag and play at the header parser's default, 24 kHz;
* two streams, ``speech\\streams\\intro\\NNN.asf`` (the entrance line, 36 kHz, loaded into a 0x25800-byte buffer
  per slot) and ``speech\\streams\\extro\\NNN.asf`` (the victory line, 36 kHz).

Stock voices are 000..039 and 500..538. A new fighter with its own voice gets ``N = 40 + (feID - 100)`` (040..067,
free in every dump), so it never needs a server claim: the place the account service gives the fighter decides
it. The pack holds only the modder's own clips (``voice/*.sch`` bank lines at 24 kHz, ``voice/intro.asf`` and
``voice/win.asf`` at 36 kHz), listed by group in the new-fighter op's gameplay block::

    "voice": {"mode": "own", "lines": {"intro": ["voice/intro.asf"], "win": ["voice/win.asf"],
                                       "taunt": ["voice/taunt-1.sch", ...], "blazin": [...], "weapon": [...],
                                       "environment": [...], "special": [...], "crowd": [...]}}

The installer (``voice_files``) builds the five files at N on the player's PC: every bank keeps the template's
entry table (the same lines, events and order) with the modder's clips in it, a group's clips taking its entries
in turn; a group the modder left empty keeps the template's own clips (from the player's own dump, so no game
audio is ever in a pack). The result depends only on the pack and the stock dump, so every PC composes the same
bytes (the pack is in the content print). The fighter's Character row word 21 (speechID, never read by the speech
code) says N; the game's hook (``src/hooks/fighter_voice.h``) then uses N for the slot when all five files are
there, else the template's voice.

Standard library only. One implementation for both sides: FFNY Studio (``ffnystudio/fightervoice.py``) and the
player's install (``setup/ffnycontent/fightervoice.py`` of the game repository) hold the same text below their
first comment lines.
"""

import array
import struct

FIRST_FE_ID, LAST_FE_ID = 100, 127
FIRST_VOICE = 40
SPEECH_ARCHIVE = "audio/speech2.viv"
GROUP_DIR = "Def_Jam_Characters"
BANKS = ("Character", "Taunts", "Enviro")
# the family word (.hdr +0) of a Def_Jam_Characters bank (the IP group's are one higher)
FAMILY = {"Character": 0, "Taunts": 16625, "Enviro": 40962}
BANK_RATE = 24000           # bank clips carry no rate tag: the header parser's default (0x126589)
STREAM_RATE = 36000         # the intro and extro streams (tag 0x84)
BANK_BLOCK = 1624           # samples per SCDl block, as the stock banks
STREAM_BLOCK = 2408         # as the stock streams
INTRO_MAX_BYTES = 0x25800   # sub_00038440: one buffer per slot; the largest stock intro is 152,312 bytes
EXTRO_MAX_BYTES = 160 * 1024    # the extro buffer is 0x7D000 (0x3C57A); the largest stock extro is 157,496
LAYOUT_VOICE = 25           # Doc: the entry table used when the template's voice has no such bank
CLIP_ALIGN = 0x100          # .hdr byte 7 = 0: entry offsets in 0x100 units
DAT_ALIGN = 0x800

# The modder's groups: key -> (label, where, how many clips at most, seconds at most). ``where``: "intro" /
# "win" (the streams) or (bank, entry field 0 or None for every entry of the bank). The limits are a little under
# the longest stock line of each kind (intro 7.78 s, extro 8.04 s, Blazin 6.38 s, weapon 4.63 s, environment
# 5.51 s, after a special 6.97 s, taunt 4.12 s, crowd 5.65 s; measured over every stock voice).
GROUPS = (
    ("intro", "Intro (walking in)", "intro", 1, 7.8),
    ("win", "Win (after the KO)", "win", 1, 8.0),
    ("taunt", "Taunts", ("Taunts", None), 7, 4.0),
    ("blazin", "Blazin shout", ("Character", 0), 1, 6.0),
    ("weapon", "Weapon lines", ("Character", 1), 8, 4.5),
    ("environment", "Environment attacks", ("Character", 2), 8, 5.5),
    ("special", "After a special move", ("Character", 3), 8, 6.0),
    ("crowd", "Crowd lines", ("Enviro", None), 8, 5.5),
)
GROUP_KEYS = tuple(g[0] for g in GROUPS)
MAX_CLIPS = sum(g[3] for g in GROUPS)
MIN_SECONDS = 0.15
# stock speechIDs that differ from the voice the game plays (the XBE table 0x2DC5D0[coreID], read 2026-11-12):
# Masa's row says 523 (no such voice), the game plays 506; Suspect, Jervis and Starks have none
TEMPLATE_VOICE = {14: 506, 60: None, 61: None, 62: None}


class VoiceError(ValueError):
    pass


def voice_number(fe_id):
    """The own voice number of new fighter ``fe_id`` (40..67), or None."""
    if isinstance(fe_id, int) and not isinstance(fe_id, bool) and FIRST_FE_ID <= fe_id <= LAST_FE_ID:
        return FIRST_VOICE + fe_id - FIRST_FE_ID
    return None


def template_voice(fe_id, speech_word):
    """The voice the game plays for stock fighter ``fe_id`` whose row word 21 is ``speech_word`` (or None)."""
    if fe_id in TEMPLATE_VOICE:
        return TEMPLATE_VOICE[fe_id]
    return speech_word if 0 <= speech_word < 600 else None


def _group_dir(voice):
    return GROUP_DIR if voice < 500 else "Def_Jam_IP_Characters"


def bank_entry(bank, voice, ext):
    """The archive entry of a bank file, e.g. ``speech\\Taunts\\Def_Jam_Characters\\040.hdr``."""
    return "speech\\%s\\%s\\%03d.%s" % (bank, _group_dir(voice), voice, ext)


def stream_entry(kind, voice):
    """``kind`` "intro" or "extro"."""
    return "speech\\streams\\%s\\%03d.asf" % (kind, voice)


def voice_entries(voice):
    """The eight entries a voice needs (the game's hook checks the same)."""
    out = []
    for bank in BANKS:
        out += [bank_entry(bank, voice, "hdr"), bank_entry(bank, voice, "dat")]
    return out + [stream_entry("intro", voice), stream_entry("extro", voice)]


# -- EA-XA R2 ---------------------------------------------------------------------------------------------------
#
# 28 samples a frame. An ADPCM frame is one byte (filter << 4 | shift) and 14 bytes of nibbles (high first); a
# sample is clamp16((nibble * 2^(20 - shift) + c1 * h1 + c2 * h2 + 128) >> 8), the filter's c1/c2 below. A frame
# whose first byte is 0xEE is plain PCM: h1, h2 (big-endian, the frame's last two samples) and 28 big-endian
# samples. The decoder's history runs on across SCDl blocks (measured on the stock clips: carrying it makes the
# block joins continuous, resetting it does not).

C1 = (0, 240, 460, 392)
C2 = (0, 0, -208, -220)
FRAME = 28
PCM_FRAME = 0xEE


def _clamp(v):
    return -32768 if v < -32768 else 32767 if v > 32767 else v


def decode_frames(data, count, h1=0, h2=0):
    """``count`` samples from EA-XA R2 frames ``data``: (samples list, h1, h2, bytes used)."""
    out = []
    p, n = 0, len(data)
    while len(out) < count:
        if p >= n:
            raise VoiceError("the audio data ends early")
        fi = data[p]
        if fi == PCM_FRAME:
            if p + 61 > n:
                raise VoiceError("the audio data ends early")
            h1, h2 = struct.unpack_from(">hh", data, p + 1)
            out.extend(struct.unpack_from(">28h", data, p + 5))
            p += 61
            continue
        if p + 15 > n:
            raise VoiceError("the audio data ends early")
        f = fi >> 4
        if f > 3:
            raise VoiceError("not EA-XA audio (filter %d)" % f)
        c1, c2, q = C1[f], C2[f], 20 - (fi & 15)
        for i in range(14):
            b = data[p + 1 + i]
            for nib in (b >> 4, b & 15):
                if nib > 7:
                    nib -= 16
                v = ((nib << q) if q >= 0 else (nib >> -q)) + c1 * h1 + c2 * h2 + 128 >> 8
                v = -32768 if v < -32768 else 32767 if v > 32767 else v
                out.append(v)
                h2, h1 = h1, v
        p += 15
    return out[:count], h1, h2, p


def _try(x, h1, h2, c1, c2, sh, best):
    """Closed-loop encode of one frame with filter (c1, c2) and shift sh: (error, nibbles, h1, h2), or None
    once the error passes ``best``."""
    q = 20 - sh
    scale = 1 << q
    half = scale >> 1
    err = 0
    nibs = []
    a, b = h1, h2
    for s in x:
        pred = c1 * a + c2 * b + 128
        t = (s << 8) - pred
        n = (t + half) >> q if t >= 0 else -((-t + half) >> q)
        if n > 7:
            n = 7
        elif n < -8:
            n = -8
        v = (n << q) + pred >> 8
        if v > 32767:
            v = 32767
        elif v < -32768:
            v = -32768
        d = v - s
        err += d * d
        if err >= best:
            return None
        nibs.append(n & 15)
        b, a = a, v
    return err, nibs, a, b


def encode_frame(x, h1, h2):
    """One ADPCM frame (15 bytes) for 28 samples ``x`` after history (h1, h2): (bytes, h1, h2)."""
    best = None
    for f in range(4):
        c1, c2 = C1[f], C2[f]
        a, b, peak = h1, h2, 0
        for s in x:
            r = s - ((c1 * a + c2 * b) >> 8)
            if r < 0:
                r = -r
            if r > peak:
                peak = r
            b, a = a, s
        sh = 12
        while sh > 0 and (7 << (12 - sh)) < peak:
            sh -= 1
        for k in (sh, sh - 1, sh + 1):
            if 0 <= k <= 12:
                got = _try(x, h1, h2, c1, c2, k, best[0] if best else 1 << 62)
                if got is not None:
                    best = (got[0], f, k, got[1], got[2], got[3])
    _, f, sh, nibs, a, b = best
    out = bytearray([f << 4 | sh])
    for i in range(0, FRAME, 2):
        out.append(nibs[i] << 4 | nibs[i + 1])
    return bytes(out), a, b


def encode_frames(samples):
    """EA-XA R2 frames for int16 ``samples`` (the first frame plain PCM, as the stock clips start), as a list of
    (frame bytes, samples in it)."""
    out = []
    h1 = h2 = 0
    n = len(samples)
    for start in range(0, n, FRAME):
        x = list(samples[start:start + FRAME])
        got = len(x)
        x += [0] * (FRAME - got)
        if start == 0:
            out.append((struct.pack(">Bhh28h", PCM_FRAME, x[27], x[26], *x), got))
            h1, h2 = x[27], x[26]
        else:
            frame, h1, h2 = encode_frame(x, h1, h2)
            out.append((frame, got))
    return out


# -- SCHl clips -------------------------------------------------------------------------------------------------

def _tag(t, v):
    """A PT tag: the game reads its value big-endian and signed (sub_0012C6E0), so a value whose top bit would
    be set gets a leading zero byte, as EA's own files have it (36000 is 00 8C A0)."""
    n = v.bit_length() // 8 + 1
    return bytes([t, n]) + v.to_bytes(n, "big")


def schl_header(samples, rate=None):
    """The PT header as the stock speech clips have it (Xbox, version 3, codec 0x0A EA-XA, mono), with the rate
    tag only for a stream (``rate``); padded to four bytes."""
    body = b"PT\x07\x00" + bytes([0x06, 0x01, 0x65, 0xFD]) + _tag(0x80, 3) + _tag(0x85, samples)
    if rate is not None:
        body += _tag(0x84, rate)
    body += _tag(0xA0, 0x0A) + _tag(0x8C, 0x24) + b"\xff"
    size = 8 + len(body)
    size += -size % 4
    return b"SCHl" + struct.pack("<I", size) + body + b"\0" * (size - 8 - len(body))


def encode_clip(samples, stream=False):
    """One SCHl clip of int16 ``samples``: a bank line (24 kHz, no rate tag) or, ``stream``, an intro/extro
    stream (36 kHz). The caller resamples to the right rate."""
    if not len(samples):
        raise VoiceError("no audio")
    frames = encode_frames(samples)
    per = (STREAM_BLOCK if stream else BANK_BLOCK) // FRAME
    blocks = [frames[i:i + per] for i in range(0, len(frames), per)]
    out = bytearray(schl_header(len(samples), STREAM_RATE if stream else None))
    out += b"SCCl" + struct.pack("<II", 12, len(blocks))
    for block in blocks:
        data = b"".join(f for f, _ in block)
        data += b"\0" * (-len(data) % 4)
        out += b"SCDl" + struct.pack("<III", 16 + len(data), sum(n for _, n in block), 0) + data
    out += b"SCEl" + struct.pack("<I", 8)
    return bytes(out)


def parse_clip(data, offset=0):
    """An SCHl clip at ``offset``: {"rate", "samples", "seconds", "codec", "channels", "blocks": [(data offset,
    samples)], "end"}; raises VoiceError for anything the game's speech would not play."""
    b = data
    if b[offset:offset + 4] != b"SCHl":
        raise VoiceError("not an EA audio clip (no SCHl header)")
    size = struct.unpack_from("<I", b, offset + 4)[0]
    if size < 16 or size > 256 or offset + size > len(b) or b[offset + 8:offset + 10] != b"PT":
        raise VoiceError("the clip's header is damaged")
    if b[offset + 10] != 7:
        raise VoiceError("the clip is not made for the Xbox speech engine (platform %d)" % b[offset + 10])
    tags, p, end = {}, offset + 12, offset + size
    while p < end:
        t = b[p]
        p += 1
        if t == 0xFF:
            break
        if t in (0xFC, 0xFD, 0xFE):
            continue
        n = b[p]
        p += 1
        if n > 4 or p + n > end:
            raise VoiceError("the clip's header is damaged")
        if n and b[p] & 0x80 and t in (0x82, 0x84, 0x85, 0xA0):
            raise VoiceError("the clip's header has a negative value (tag 0x%02X)" % t)
        tags[t] = int.from_bytes(b[p:p + n], "big")
        p += n
    codec, channels = tags.get(0xA0), tags.get(0x82, 1)
    rate, count = tags.get(0x84, BANK_RATE), tags.get(0x85, 0)
    if codec != 0x0A:
        raise VoiceError("the clip is not EA-XA audio (codec %r)" % codec)
    if channels != 1:
        raise VoiceError("the clip is not mono")
    q = offset + size
    blocks, total = [], 0
    while True:
        if q + 8 > len(b):
            raise VoiceError("the clip ends early")
        t, s = b[q:q + 4], struct.unpack_from("<I", b, q + 4)[0]
        if s < 8 or q + s > len(b):
            raise VoiceError("the clip's blocks are damaged")
        if t == b"SCDl":
            n = struct.unpack_from("<I", b, q + 8)[0]
            chan = struct.unpack_from("<I", b, q + 12)[0]
            if s < 16 or chan != 0:
                raise VoiceError("the clip's blocks are damaged")
            blocks.append((q + 16, n, q + s))
            total += n
        elif t == b"SCEl":
            q += s
            break
        elif t != b"SCCl":
            raise VoiceError("the clip's blocks are damaged")
        q += s
    if total != count or not count:
        raise VoiceError("the clip's sample count does not match its blocks")
    return {"rate": rate, "samples": count, "seconds": count / float(rate), "codec": codec, "channels": channels,
            "blocks": blocks, "end": q}


def decode_clip(data, offset=0):
    """(int16 samples as array('h'), rate) of the SCHl clip at ``offset``."""
    info = parse_clip(data, offset)
    out = array.array("h")
    h1 = h2 = 0
    for start, n, end in info["blocks"]:
        got, h1, h2, _ = decode_frames(data[start:end], n, h1, h2)
        out.extend(got)
    return out, info["rate"]


def clip_problems(data, stream=False, seconds=None, max_bytes=None):
    """Why ``data`` cannot be one of a voice's clips ([] when it can): an SCHl EA-XA mono clip at the rate the
    game plays it (24 kHz bank line, 36 kHz stream), of a sensible length, that decodes to the end."""
    try:
        info = parse_clip(data)
        decode_clip(data)
    except (VoiceError, struct.error, IndexError) as e:
        return [str(e)]
    out = []
    want = STREAM_RATE if stream else BANK_RATE
    if info["rate"] != want:
        out.append("plays at %d Hz, the game plays these at %d Hz" % (info["rate"], want))
    if info["end"] != len(data):
        out.append("has %d bytes after its end" % (len(data) - info["end"]))
    if info["seconds"] < MIN_SECONDS:
        out.append("is shorter than %.2f s" % MIN_SECONDS)
    if seconds is not None and info["seconds"] > seconds + 0.005:
        out.append("is %.1f s long, at most %.1f s" % (info["seconds"], seconds))
    if max_bytes is not None and len(data) > max_bytes:
        out.append("is %d bytes, at most %d (the game's buffer)" % (len(data), max_bytes))
    return out


def silence_clip(stream=False):
    return encode_clip([0] * (int((STREAM_RATE if stream else BANK_RATE) * 0.2)), stream)


def to_wav(data):
    """A WAV file (16-bit mono) of an SCHl clip: for listening (the review post, Studio's preview)."""
    samples, rate = decode_clip(data)
    if samples.itemsize != 2:
        raise VoiceError("16-bit samples expected")
    if struct.pack("=h", 1) != b"\x01\x00":
        samples.byteswap()
    pcm = samples.tobytes()
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) + b"data" + struct.pack("<I", len(pcm)) + pcm)


# -- banks ------------------------------------------------------------------------------------------------------

def parse_hdr(h):
    """A bank header: {"family", "voice", "fields", "count", "unit", "entries": [(offset, (field bytes))],
    "total"} as sub_001393F0 reads it (entry size = (byte 4 & 0x7F) + 2, offsets big-endian u16 in units of
    (byte 7 + 1) * 0x100, the end of the last at the u16 at +8)."""
    if len(h) < 12:
        raise VoiceError("bank header too short")
    family, voice, nf, count = struct.unpack_from("<HHBB", h, 0)
    nf &= 0x7F
    es = nf + 2
    if 12 + es * count > len(h):
        raise VoiceError("bank header too short")
    unit = (h[7] + 1) << 8
    entries = []
    for i in range(count):
        e = h[12 + es * i:12 + es * (i + 1)]
        entries.append(((e[0] << 8 | e[1]) * unit, tuple(e[2:])))
    return {"family": family, "voice": voice, "fields": nf, "count": count, "unit": unit, "entries": entries,
            "total": struct.unpack_from("<H", h, 8)[0] * unit}


def bank_clips(hdr, dat):
    """The clips of a bank, [bytes], cut by its header."""
    info = parse_hdr(hdr)
    ends = [o for o, _ in info["entries"][1:]] + [info["total"]]
    return [dat[o:e] for (o, _), e in zip(info["entries"], ends)]


def build_bank(template_hdr, bank, voice, clips):
    """(hdr, dat) of ``bank`` for ``voice``: the template header's entries, fields and trailer with ``clips``
    (one SCHl per entry) placed at 0x100 steps."""
    info = parse_hdr(template_hdr)
    if len(clips) != info["count"]:
        raise VoiceError("%s: %d clips for %d entries" % (bank, len(clips), info["count"]))
    h = bytearray(template_hdr)
    struct.pack_into("<HH", h, 0, FAMILY[bank], voice)
    h[7] = 0
    es = info["fields"] + 2
    dat = bytearray()
    for i, clip in enumerate(clips):
        dat += b"\0" * (-len(dat) % CLIP_ALIGN)
        unit = len(dat) // CLIP_ALIGN
        if unit > 0xFFFF:
            raise VoiceError("%s: the bank is too large" % bank)
        h[12 + es * i] = unit >> 8
        h[12 + es * i + 1] = unit & 0xFF
        dat += clip
    total = -(-len(dat) // CLIP_ALIGN)
    if total > 0xFFFF:
        raise VoiceError("%s: the bank is too large" % bank)
    struct.pack_into("<I", h, 8, total)
    dat += b"\0" * (-len(dat) % DAT_ALIGN)
    return bytes(h), bytes(dat)


def group_of(bank, fields):
    """The modder's group of a bank entry (its first field is the event)."""
    for key, _, where, _, _ in GROUPS:
        if isinstance(where, tuple) and where[0] == bank and where[1] in (None, fields[0] if fields else None):
            return key
    return None


def _groups():
    return {g[0]: g for g in GROUPS}


def lines_problems(lines, read=None):
    """What is wrong with a voice's ``lines`` {group: [pack paths]} ([] when nothing); ``read(path)`` gives a
    file's bytes to check the clips too (the account service and Studio pass it)."""
    if not isinstance(lines, dict):
        return ["lines must be an object of groups"]
    out = []
    groups = _groups()
    extra = sorted(set(lines) - set(groups))
    if extra:
        out.append("unknown line group%s %s (known: %s)" % ("s" if len(extra) > 1 else "", ", ".join(extra),
                                                           ", ".join(GROUP_KEYS)))
    seen, count = set(), 0
    for key in GROUP_KEYS:
        if key not in lines:
            continue
        paths = lines[key]
        _, label, where, most, seconds = groups[key]
        stream = where in ("intro", "win")
        if not (isinstance(paths, list) and 1 <= len(paths) <= most):
            out.append("%s: one to %d clip%s" % (key, most, "s" if most > 1 else ""))
            continue
        ext = ".asf" if stream else ".sch"
        for p in paths:
            if not (isinstance(p, str) and p.startswith("voice/") and p.endswith(ext) and "/" not in p[6:]
                    and 6 < len(p) <= 64 and all(c.isalnum() or c in "-_." for c in p[6:])):
                out.append("%s: %r is not a voice/<name>%s file" % (key, p, ext))
                continue
            if p in seen:
                out.append("%s: %s is listed twice" % (key, p))
            seen.add(p)
            count += 1
            if read is not None:
                try:
                    data = read(p)
                except (OSError, KeyError, ValueError):
                    out.append("%s: %s is not in the pack" % (key, p))
                    continue
                limit = INTRO_MAX_BYTES if where == "intro" else EXTRO_MAX_BYTES if where == "win" else None
                out += ["%s: %s %s" % (key, p, why) for why in clip_problems(data, stream, seconds, limit)]
    if count == 0 and not out:
        out.append("an own voice needs at least one line")
    return out


def voice_problems(v, read=None):
    """What is wrong with a gameplay block's ``voice`` ([] when nothing)."""
    if not isinstance(v, dict):
        return ["voice must be an object"]
    mode = v.get("mode")
    if mode == "template":
        return [] if set(v) == {"mode"} else ['voice: {"mode": "template"} holds nothing else']
    if mode != "own":
        return ['voice: mode is "template" or "own"']
    if set(v) != {"mode", "lines"}:
        return ['voice: an own voice holds "mode" and "lines"']
    return lines_problems(v.get("lines"), read)


def voice_files(current, names, template_fe_id, template_speech, fe_id, lines, read):
    """The eight files of new fighter ``fe_id``'s own voice: [(entry, bytes)] to add to ``SPEECH_ARCHIVE``.
    ``current(entry)`` reads an entry of the player's speech archive, ``names()`` lists them, ``read(path)`` a pack
    file; ``template_speech`` is the template's row word 21. Raises VoiceError."""
    voice = voice_number(fe_id)
    if voice is None:
        raise VoiceError("a new fighter's voice needs its place (100..127)")
    problems = lines_problems(lines)
    if problems:
        raise VoiceError(problems[0])
    have = {n.replace("/", "\\").lower() for n in names()}
    taken = [e for e in voice_entries(voice) if e.lower() in have]
    if taken:
        raise VoiceError("%s is in the speech archive already" % taken[0])
    tvoice = template_voice(template_fe_id, template_speech)
    own = {k: [read(p) for p in lines.get(k, [])] for k in GROUP_KEYS}
    out = []
    for bank in BANKS:
        layout = tvoice
        if layout is None or bank_entry(bank, layout, "hdr").lower() not in have:
            layout = LAYOUT_VOICE
        hdr = current(bank_entry(bank, layout, "hdr"))
        info = parse_hdr(hdr)
        stock = bank_clips(hdr, current(bank_entry(bank, layout, "dat"))) if layout == tvoice else None
        turn, clips = {}, []
        for i, (_, fields) in enumerate(info["entries"]):
            key = group_of(bank, fields)
            mine = own.get(key) or []
            if mine:
                k = turn.get(key, 0)
                clips.append(mine[k % len(mine)])
                turn[key] = k + 1
            elif stock is not None:
                clips.append(stock[i])
            else:
                clips.append(silence_clip())
        h, d = build_bank(hdr, bank, voice, clips)
        out += [(bank_entry(bank, voice, "hdr"), h), (bank_entry(bank, voice, "dat"), d)]
    for kind, key in (("intro", "intro"), ("extro", "win")):
        if own[key]:
            data = own[key][0]
        else:
            data = None
            for name in ([stream_entry(kind, tvoice), "speech\\streams\\%s\\%03d%s1.asf" % (kind, tvoice, kind[0])]
                         if tvoice is not None else []):
                if name.lower() in have:
                    data = current(name)
                    break
            if data is None:
                data = silence_clip(stream=True)
        out.append((stream_entry(kind, voice), data))
    return out
