#!/usr/bin/env python3
"""Cross-architecture proof for the deterministic math layer (src/detmath).

Builds tests/detmath/dm_vectors.c with src/detmath/detmath.c several ways and checks that every
build prints the same result hashes (one per function and input set, 1M inputs each by default):

  msvc-x64        cl /O2 /fp:precise (the shipped Windows compiler), run natively
  clang-x64       clang --target=x86_64-linux-gnu -O2, freestanding ELF, run in WSL
  clang-x64-fma   the same with -march=haswell (FMA3 available, contraction still off)
  clang-arm64     clang --target=aarch64-linux-gnu -O2, freestanding ELF, run under qemu-aarch64
  clang-arm64-O0  the same unoptimised
and, as the negative control that shows the check can fail,
  arm64-fused     -ffp-contract=fast (the compiler fuses multiply-adds): must DIFFER.

With --trace FILE (a game run's RECOMP_DETMATH_TRACE) every build also replays the operands the
game really used and must agree on those too.

Needs: Visual Studio's cl and its bundled clang + lld (VC\\Tools\\Llvm), and a WSL distribution
whose binfmt runs aarch64 ELF files through qemu-user (Fedora: qemu-user-static-aarch64). Nothing
is downloaded.

    python tests/detmath/check.py [--n 1000000] [--trace logs/detmath-trace.bin] [--out DIR]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SRC = [REPO / "tests/detmath/dm_vectors.c", REPO / "src/detmath/detmath.c"]
VS = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community")
LLVM = VS / "VC/Tools/Llvm/x64/bin"
WSL_DISTRO = os.environ.get("DETMATH_WSL", "FedoraLinux-45")


def wsl_path(p):
    p = str(Path(p).resolve())
    return "/mnt/" + p[0].lower() + p[2:].replace("\\", "/")


def build_msvc(out, n):
    vcvars = VS / "VC/Auxiliary/Build/vcvars64.bat"
    exe = out / "dm_msvc.exe"
    script = out / "build-msvc.cmd"
    lines = ["@echo off", 'call "%s" >nul 2>nul' % vcvars,
             'cl /nologo /O2 /fp:precise /DDM_N=%d /Fe:"%s" %s' % (n, exe, " ".join('"%s"' % s for s in SRC))]
    script.write_text("\r\n".join(lines) + "\r\n")
    subprocess.run(["cmd", "/c", str(script)], check=True, cwd=out, stdout=subprocess.DEVNULL)
    return exe


def build_elf(out, name, target, n, extra):
    elf = out / f"dm_{name}.elf"
    subprocess.run([str(LLVM / "clang.exe"), f"--target={target}", "-ffreestanding", "-nostdlib", "-static",
                    "-fno-stack-protector", "-fno-builtin", "-DDM_FREESTANDING", f"-DDM_N={n}", "-fuse-ld=lld",
                    "-Wl,-e,_start", "-o", str(elf)] + extra + [str(s) for s in SRC], check=True)
    return elf


def fma_count(binary):
    """Fused multiply-add instructions in the binary (arm64 fmadd/fmsub/fnmadd/fnmsub, x86 vfmadd...)."""
    dis = subprocess.run([str(LLVM / "llvm-objdump.exe"), "-d", str(binary)], capture_output=True, text=True).stdout
    return len(re.findall(r"\tv?fn?m(?:add|sub)", dis))


def hashes(text):
    return [line for line in text.splitlines() if " hash=" in line and not line.startswith("arch")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=1000000)
    ap.add_argument("--trace")
    ap.add_argument("--out")
    a = ap.parse_args()
    out = Path(a.out or tempfile.mkdtemp(prefix="detmath-")).resolve()
    out.mkdir(parents=True, exist_ok=True)
    builds = {"msvc-x64": build_msvc(out, a.n)}
    elf_cfg = {
        "clang-x64": ("x86_64-linux-gnu", ["-O2", "-ffp-contract=off"]),
        "clang-x64-fma": ("x86_64-linux-gnu", ["-O2", "-ffp-contract=off", "-march=haswell"]),
        "clang-arm64": ("aarch64-linux-gnu", ["-O2", "-ffp-contract=off"]),
        "clang-arm64-O0": ("aarch64-linux-gnu", ["-O0", "-ffp-contract=off"]),
        "arm64-fused": ("aarch64-linux-gnu", ["-O2", "-ffp-contract=fast"]),
    }
    for name, (target, extra) in elf_cfg.items():
        builds[name] = build_elf(out, name.replace("-", "_"), target, a.n, extra)
    results = {}
    args = [a.trace] if a.trace else []
    for name, binary in builds.items():
        if name == "msvc-x64":
            res = subprocess.run([str(binary)] + args, capture_output=True, text=True, check=True).stdout
        else:
            targs = " ".join("'" + wsl_path(t) + "'" for t in args)
            res = subprocess.run(["wsl.exe", "-d", WSL_DISTRO, "--", "sh", "-c", f"'{wsl_path(binary)}' {targs}"],
                                 capture_output=True, check=True).stdout.decode("utf-8", "replace").replace("\0", "")
        (out / f"{name}.txt").write_text(res)
        results[name] = hashes(res)
        print(f"{name:15s} {res.splitlines()[0]:12s} {len(results[name]):3d} hashes, "
              f"fused multiply-adds in the binary: {fma_count(binary) if name != 'msvc-x64' else 'n/a (x64 SSE2)'}")
    ref = results["msvc-x64"]
    ok = True
    for name, res in results.items():
        same = res == ref
        expect_same = name != "arm64-fused"
        verdict = "identical" if same else f"{sum(1 for x, y in zip(res, ref) if x != y)} of {len(ref)} lines differ"
        flag = "ok" if same == expect_same else "FAIL"
        if same != expect_same:
            ok = False
        print(f"  {flag:4s} {name:15s} vs msvc-x64: {verdict}")
    print("outputs in", out)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
