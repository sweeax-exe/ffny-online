#!/usr/bin/env python3
"""Count step clicks in what the audio device was given, and where they fall in the APU's slices.

The voice processor renders each voice in 32-sample slices. A defect in how a slice is read (a
page of a voice's buffer resolved to the wrong memory, a mid-slice underrun filled with zeros)
makes the waveform jump at a slice edge; music and speech jump anywhere. So a click count is
read together with its slice phases: real defects pile up at phases 0-4 and 28-31, chance spreads
them over all 32. audio-holes.py cannot see these: it looks only for a previous lap played again.

    RECOMP_APU_PCM=<file.pcm> game ...       # what the APU hands the device, raw 48 kHz stereo s16
    RECOMP_APU_VOICE_PCM=<prefix> game ...   # voices 0xF8-0xFA before volume: <prefix>F8.pcm etc.
    python scripts/audio-clicks.py <file.pcm|file.wav> [...]

Per file: clicks (samples whose second difference is over 8x the local 5 ms RMS of second
differences and over 3000, at most one per 10 ms), how many sit on a slice edge, the clicks per
minute, and dropouts (runs of 32 or more frames of exact zero with sound within 10 ms on both
sides). On Windows in October 2026 the opening scene gave 386-422 clicks per 330 s, nearly all on
slice edges, before the toolkit fix 3e63e7b, 1 after, and 2 on v0.10.7. On macOS the same scene had
about 1,300 dropouts of one slice before 957c1ba (the sequencer restart, D201), 1 after and on v0.10.5.
Needs numpy.
"""
import argparse
import sys
import wave

try:
    import numpy as np
except ImportError:
    sys.exit('numpy is required (pip install numpy)')

SLICE = 32
EDGE = {0, 1, 2, 3, 4, 28, 29, 30, 31}


def load(path):
    """(rate, frames x 2 int32) from a 16-bit stereo WAV or a raw s16 stereo file."""
    if path.lower().endswith('.wav'):
        with wave.open(path, 'rb') as w:
            if w.getsampwidth() != 2 or w.getnchannels() != 2:
                raise ValueError('%s: want 16-bit stereo' % path)
            rate = w.getframerate()
            data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    else:
        rate = 48000
        data = np.fromfile(path, dtype=np.int16)
    return rate, data[: len(data) // 2 * 2].reshape(-1, 2).astype(np.int32)


def runs(mask, min_len):
    edge = np.flatnonzero(np.diff(np.concatenate([[0], mask.astype(np.int8), [0]])))
    return [(int(a), int(b - a)) for a, b in zip(edge[0::2], edge[1::2]) if b - a >= min_len]


def clicks(x, rate):
    """Sample indices of clicks, either channel, at most one per 10 ms per channel."""
    found = set()
    win = max(1, int(rate * 0.005))
    for ch in range(x.shape[1]):
        s = x[:, ch].astype(np.float64)
        if len(s) < 3:
            continue
        d2 = np.abs(s[2:] - 2 * s[1:-1] + s[:-2])
        rms = np.sqrt(np.convolve(d2 * d2, np.ones(win) / win, mode='same'))
        idx = np.flatnonzero((d2 > 8 * rms) & (d2 > 3000)) + 1
        if len(idx):
            keep = np.concatenate([[True], np.diff(idx) > rate // 100])
            found.update(idx[keep].tolist())
    return sorted(found)


def dropouts(x):
    loud = np.abs(x).max(axis=1)
    out = []
    for a, length in runs(loud == 0, 32):
        if loud[max(0, a - 480):a].max(initial=0) > 300 and loud[a + length:a + length + 480].max(initial=0) > 300:
            out.append(a)
    return out


def per_minute(points, rate, frames):
    counts = [0] * (int(frames / rate // 60) + 1)
    for p in points:
        counts[min(len(counts) - 1, int(p / rate // 60))] += 1
    return counts


def report(path):
    rate, x = load(path)
    c = clicks(x, rate)
    on_edge = sum(1 for i in c if i % SLICE in EDGE)
    d = dropouts(x)
    secs = len(x) / rate
    return ('%s: %.1f s\n  clicks   %5d (%.2f/s), %d on slice edges, per minute %s\n  dropouts %5d, per minute %s'
            % (path, secs, len(c), len(c) / secs if secs else 0, on_edge, per_minute(c, rate, len(x)),
               len(d), per_minute(d, rate, len(x))))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='+')
    for path in ap.parse_args(argv).files:
        print(report(path))
    return 0


if __name__ == '__main__':
    sys.exit(main())
