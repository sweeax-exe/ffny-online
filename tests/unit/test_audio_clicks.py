"""scripts/audio-clicks.py finds slice-edge defects and leaves smooth sound alone.

The defect it was written for: on Windows the last page of the title's mix buffer read the wrong
memory, so once a lap a slice turned into near-silence from a few samples in to its end. A smooth
tone must give no clicks; the same tone with that defect injected at slice edges must give them,
on slice edges.
"""
import importlib.util
import os

import numpy as np
import pytest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_spec = importlib.util.spec_from_file_location('audio_clicks', os.path.join(ROOT, 'scripts', 'audio-clicks.py'))
ac = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ac)

RATE = 48000


def tone(seconds=2.0):
    t = np.arange(int(RATE * seconds))
    s = (8000 * np.sin(2 * np.pi * 220 * t / RATE)).astype(np.int32)
    return np.stack([s, s], axis=1)


def test_a_smooth_tone_has_no_clicks():
    assert ac.clicks(tone(), RATE) == []


def test_near_silence_to_a_slice_end_clicks_on_slice_edges():
    x = tone()
    lap = 2400
    starts = []
    for start in range(lap - 29, len(x), lap):       # 28 frames to the end of a slice, once a lap
        end = (start // ac.SLICE + 1) * ac.SLICE
        x[start:end] = 0
        x[start + 1, 0] = 63
        starts.append(start)
    found = ac.clicks(x, RATE)
    assert len(found) >= len(starts)
    assert all(i % ac.SLICE in ac.EDGE for i in found)


def test_a_raw_file_round_trips(tmp_path):
    p = tmp_path / 'x.pcm'
    tone(0.5).astype(np.int16).tofile(p)
    rate, x = ac.load(str(p))
    assert rate == RATE and x.shape == (RATE // 2, 2)
    assert 'clicks       0' in ac.report(str(p))


@pytest.mark.parametrize('length', [10, 40])
def test_dropouts_need_32_frames_with_sound_around(length):
    x = tone(0.5)
    x[10000:10000 + length] = 0
    assert len(ac.dropouts(x)) == (1 if length >= 32 else 0)
