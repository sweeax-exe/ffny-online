#!/usr/bin/env python3
"""Name the host addresses of a crash report from the build's PDB (dbghelp, no debugger).

    python scripts/symbolize-host.py logs/mcp/copy0.log.err          # the last [CRASH] block in a log
    python scripts/symbolize-host.py 0x140928058 0x1409C07F2 --base 0x140000000

The crash report (src/main.c) prints the exe base and every stack word that points into the exe;
this maps each to `function+offset` of build/win-x64-release/defjam_recomp.exe. A scan, not a walk:
stale words from earlier calls show too. Reads only.

The exe must be the one that crashed. Setup compiles the game on each player's PC with Visual Studio 2022's Build Tools
(MSVC 14.44), so a player's report read with a build by another toolset names other functions (the fighter select crash
of builds 96-109 was read as the global chat that way). Build the release's commit with 14.44 and pass --exe; a word
that does not follow a call in the exe is marked, and a warning says so when the first ones do not.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import re
import struct
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EXE = os.path.join(REPO, "build", "win-x64-release", "defjam_recomp.exe")


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                ("Index", wt.ULONG), ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG),
                ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
                ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG),
                ("Name", ctypes.c_char * 256)]


def addresses_from_log(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    i = text.rfind("[CRASH]")
    if i < 0:
        sys.exit("no [CRASH] in " + path)
    block = text[i:i + 20000]
    m = re.search(r"exe base 0x([0-9A-F]+)", block)
    base = int(m.group(1), 16) if m else 0x140000000
    addrs = [int(a, 16) for a in re.findall(r"^\s+\[(?:rip|\d+)\] 0x([0-9A-F]+)", block, re.M)]
    m = re.search(r"at RIP=0x([0-9A-F]+)", block)
    if m and not addrs:
        addrs = [int(m.group(1), 16)]
    return base, addrs


def call_before(exe):
    """A function: whether an offset of the exe's image follows a call, as a return address does."""
    data = open(exe, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count, opt = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    sections = []
    for i in range(count):
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, pe + 24 + opt + 40 * i + 8)
        if struct.unpack_from("<I", data, pe + 24 + opt + 40 * i + 36)[0] & 0x20000000:     # executable
            sections.append((va, min(vsize, rsize), raw))

    def check(rva):
        for va, size, raw in sections:
            if va + 8 <= rva < va + size:
                b = data[raw + rva - va - 8:raw + rva - va]
                return b[3] == 0xE8 or any(b[8 - n] == 0xFF and (b[9 - n] >> 3 & 7) == 2 for n in range(2, 8))
        return False
    return check


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("items", nargs="+", help="a log file, or addresses")
    ap.add_argument("--base", default=None, help="the exe base the addresses belong to (default from the log)")
    ap.add_argument("--exe", default=None, help="another build's exe (its .pdb beside it), e.g. an installed copy")
    a = ap.parse_args()
    if os.path.isfile(a.items[0]):
        base, addrs = addresses_from_log(a.items[0])
    else:
        base, addrs = 0x140000000, [int(x, 0) for x in a.items]
    if a.base:
        base = int(a.base, 0)
    exe = os.path.abspath(a.exe) if a.exe else EXE
    dbg = ctypes.WinDLL("dbghelp")
    dbg.SymInitializeW.argtypes = [wt.HANDLE, wt.LPCWSTR, wt.BOOL]
    dbg.SymLoadModuleExW.restype = ctypes.c_uint64
    dbg.SymLoadModuleExW.argtypes = [wt.HANDLE, wt.HANDLE, wt.LPCWSTR, wt.LPCWSTR, ctypes.c_uint64, wt.DWORD,
                                     ctypes.c_void_p, wt.DWORD]
    dbg.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFO)]
    h = wt.HANDLE(0x5EED)
    dbg.SymSetOptions(0x2 | 0x10)                 # undecorated names, load lines lazily
    if not dbg.SymInitializeW(h, os.path.dirname(exe), False):
        sys.exit("SymInitialize failed")
    if not dbg.SymLoadModuleExW(h, None, exe, None, ctypes.c_uint64(base), 0, None, 0):
        sys.exit("cannot load " + exe)
    after_call, seen = call_before(exe), []
    for addr in addrs:
        sym = SYMBOL_INFO()
        sym.SizeOfStruct = ctypes.sizeof(SYMBOL_INFO) - 256
        sym.MaxNameLen = 255
        disp = ctypes.c_uint64(0)
        if dbg.SymFromAddr(h, ctypes.c_uint64(addr), ctypes.byref(disp), ctypes.byref(sym)):
            ret = after_call(addr - base)
            seen.append(ret)
            print("0x%X  %s+0x%X%s" % (addr, sym.Name.decode(errors="replace"), disp.value, "" if ret else "  (not after a call)"))
        else:
            print("0x%X  ?" % addr)
    if len(addrs) > 2 and len(seen) > 1 and not any(seen[:3]):
        print("WARNING: none of the first words follows a call in this exe: it is not the one that crashed (see --help)")


if __name__ == "__main__":
    main()
