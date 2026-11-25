"""Build 112's Windows start-up checks: the hybrid-GPU exports (#220)."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def test_hybrid_gpu_exports():
    main = (ROOT / 'src/main.c').read_text(encoding='utf-8')
    assert re.search(r'__declspec\(dllexport\) DWORD NvOptimusEnablement = 0x00000001;', main)
    assert re.search(r'__declspec\(dllexport\) int AmdPowerXpressRequestHighPerformance = 1;', main)