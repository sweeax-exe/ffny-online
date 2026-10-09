# Static XDK library symbols (D3D8/DSOUND/XGRAPHC/XAPILIB) via XbSymbolDatabase

Research date: 2026-09-19. Target: **Def Jam: Fight for NY** (Xbox, USA), XBE library versions
XAPILIB/XGRAPHC/D3D8/D3DX8/XBOXKRNL/DSOUND/LIBCMT all at **1.0.5849** (XDK build 5849). Section
"D3D" at VA `0x002139A0`, size `0x235C0`, plus a "D3DX" section. Ghidra's stock FunctionID
databases do not cover the XDK (verified separately: 0/9,370 functions named).

All commands below were actually run against a scratch checkout in
`<data>\scratch` (not inside this repo) to verify claims rather than trust
webpage summaries. File/line citations point at the pinned commit used for that verification.

---

## 1. XbSymbolDatabase: what it is, coverage, maintenance status

- Repo: https://github.com/Cxbx-Reloaded/XbSymbolDatabase
- License: **MIT** for the code (`LICENSE`: "MIT License, Copyright (c) 2023 The XbSymbolDatabase
  authors"). The individual OOVPA signature-database `.c` files each carry their own
  `// SPDX-License-Identifier: ODbL-1.0` header (Open Database License) — the signature *data*
  (reverse-engineered from retail titles, not Microsoft source) is separately licensed from the
  scanning code. Both are permissive/share-alike, not proprietary.
- Activity: via the GitHub API, `pushed_at: 2025-08-23T20:07:44Z` (last code push), 34 stars, 26
  open issues, most recent merged PR at time of writing is #231
  ("Merge pull request #231 from RadWolfie/d3d-oovpa-updates", Aug 23 2025, adding D3D8 OOVPA
  updates). That's about 13 months before today (2026-09-19) — **maintained but not fast-moving**;
  best described as a small, active-but-slow community project (main contributors: RadWolfie,
  jarupxx, PatrickvL, ergo720), not abandoned.
- It was spun out of Cxbx-Reloaded's old inline HLE database in 2018 specifically so third-party
  tools (not just the emulator) could consume it as a library.
- **How coverage works**: OOVPA = "Optimized (Offset,Value)-Pair Array", invented by the original
  Cxbx author Caustik. It's a byte-pattern/signature "thumbprint" for a function — a set of
  (offset, expected-byte) pairs picked to uniquely identify the function's prologue/body across
  known compiler output, similar in spirit to IDA FLIRT signatures but hand/semi-automatically
  curated per XDK library and build range rather than derived mechanically from a .lib. Each
  library has one `<Lib>_OOVPA.c` file (e.g. `D3D8_OOVPA.c`) containing `REGISTER_OOVPAS(SYM_FUN(...))`
  entries, each carrying one or more `SYM_SIG(<buildlist>)` signature variants keyed to the XDK
  build numbers ("LibV") they were verified against, plus per-title verification comments at the
  top of each file (e.g. `// * [5849] Nickelodeon Tak 2 | 100% | have 210/229 library.`).
- **Libraries covered** (confirmed via `src/OOVPADatabase/` directory listing): `D3D8`,
  `D3D8LTCG`, `DSound`, `JVS`, `XActEng`, `XGraphic`, `XNet`, `XOnline`, `Xapi`. So: **yes** to
  D3D8, D3D8LTCG, DSound (DSOUND), and Xapi (XAPILIB); **yes** to XGraphic (XGRAPHC) too.
  **Not covered**: `XBOXKRNL` and `LIBCMT` have no OOVPA folder/file — this is expected, not a gap:
  - `xboxkrnl.exe` exports are resolved through the XBE's **kernel import thunk table** (a direct
    ordinal→address table baked into every XBE header) rather than pattern-matched — Cxbx-Reloaded
    and `ghidra-xbe` both use that table directly, which is exact and doesn't need OOVPA at all.
  - `LIBCMT` (the C runtime) needs a different approach — Ghidra FunctionID (`.fidb`, built from a
    CRT `.lib` you legally have — MSVC ships CRT `.lib`s independent of the XDK) or IDA FLIRT, not
    XbSymbolDatabase.
- **Build-5849 coverage, confirmed directly**: grepping the pinned OOVPA files for `5849`:
  - `D3D8_OOVPA.c:29` — `[5849] Nickelodeon Tak 2 | 100% | have 210/229 library.`
  - `D3D8LTCG_OOVPA.c:14` — `[5849] Grand Theft Auto - San Andreas | 100% | Only has 50%-ish of the library compiled with xbe build.`
    (plus `D3D8LTCG_OOVPA.c:49` includes a dedicated `D3D8LTCG/5849.inl` signature file)
  - `XGraphic_OOVPA.c:26` — `[5849] Nickelodeon Tak 2 | 100% | have 9/11 library.`
  - `Xapi_OOVPA.c:27` — `[5849] Nickelodeon Tak 2 | 100% | have 40/50 library.`
  - `DSound_OOVPA.c:36-37`:
    ```
    // * [5849] Nickelodeon Tak 2      | 100% | Contain full library.
    // * [5849] Def Jam Fight for NY   | 100% | Contain full library.
    ```
    **This is a direct, title-specific hit: DSound at build 5849 has already been verified
    against Def Jam: Fight for NY itself** by the XbSymbolDatabase maintainers, "full library."
  - D3D8/D3D8LTCG/XGraphic/Xapi have **not** been verified against Def Jam specifically (no "Def
    Jam" line in those files) — coverage there is inferred from same-build (5849) verification
    against other titles (Tak 2, GTA:SA), which is normal for OOVPA (signatures are keyed to XDK
    build, not to a specific game, since the library code itself is what's being matched) but is
    UNVERIFIED for this exact title/binary until you actually run the scan.

## 2. Running it against an XBE

**It is a library first, not a front-end tool** — the wiki says explicitly: "XbSymbolDatabase is
not meant as a front-end, since it is design to be a library"
(https://github.com/Cxbx-Reloaded/XbSymbolDatabase/wiki/Overview-of-the-project-&-Glossary-of-terms).
However:

- There **is** a first-party CLI, built from source, no prebuilt Windows binaries in Releases
  (checked https://github.com/Cxbx-Reloaded/XbSymbolDatabase/releases — latest is `v4.0.166`
  (pre-release, 2024-12-13) / `v3.1.160` (latest stable, 2024-06-17); release notes are about the
  library/OOVPA data, no binary CLI asset attached). Source layout: `projects/cli/` (built as
  target `XbSymbolDatabaseCLI`, CMake option `XBSDB_BUILD_CLI`).
- **Exact build + scan command line**, verified by downloading and reading
  `GTTeancum/OpenXML1xbox`'s `scripts/scan-xdk-symbols.ps1` (this is a sibling Xbox
  static-recompilation project — X-Men Legends, using the same `xboxrecomp` toolkit this repo is
  built on — that already automated this exact workflow):

  ```powershell
  # clone + pin a known-good commit
  git clone https://github.com/Cxbx-Reloaded/XbSymbolDatabase.git work/reference/XbSymbolDatabase
  git -C work/reference/XbSymbolDatabase checkout --detach 20eced544726f5558c5a408458f38a086cc4e543

  # configure + build just the CLI (MSVC / VS2022, x64)
  cmake -S work/reference/XbSymbolDatabase -B build/symbols -G "Visual Studio 17 2022" -A x64 `
      -DXBSDB_BUILD_CLI=ON -DXBSDB_BUILD_UNITTEST=OFF -DXBSDB_INSTALL_LIB=OFF -DXBSDB_INSTALL_CLI=OFF
  cmake --build build/symbols --config Release --target XbSymbolDatabaseCLI --parallel 2

  # scan
  & build/symbols/projects/cli/Release/XbSymbolDatabaseCLI.exe game/default.xbe -d -e |
      Set-Content -Encoding utf8 analysis/xdk-symbols.log
  ```
  The pinned commit hash (`20eced5...`) matches a real, findable commit — GitHub search turned up
  "Merge pull request #231 from RadWolfie/d3d-oovpa-updates ·
  Cxbx-Reloaded/XbSymbolDatabase@20eced5" — i.e. it's pinned right at the same Aug-2025 D3D8 OOVPA
  update mentioned above, so it already includes current D3D8 signature coverage.
  No submodules are required for XbSymbolDatabase itself (CMakeLists.txt has no
  `add_subdirectory` pulling external deps beyond its own `projects/`).
  `-d -e` are CLI flags (dump + export, based on the output it produces); exact flag semantics
  weren't in the file itself — treat as "known-working invocation", confirm with `--help` when you
  build it.
- **Output format**: one line per matched function, of the form
  `LIBRARY__FUN__SymbolName = 0xADDRESS` (this is the format `parse-xdk-symbols.py` in the same
  repo parses: `re.match(r'(\w+)__FUN__(.*?) = (0x[0-9a-fA-F]+)$', line)`), which is then turned
  into a JSON array of `{start, name, library}` objects.
- **Alternative: Python bindings**, https://github.com/mborgerson/PyXbSymbolDatabase (GPL-2.0,
  wraps XbSymbolDatabase as a git submodule, installs via
  `pip install --user git+https://github.com/mborgerson/PyXbSymbolDatabase`, needs CMake + a C
  compiler to build the extension at install time). Usage: `python -m XbSymbolDatabase default.xbe`,
  or programmatically `from XbSymbolDatabase import XbSymbolScan`. Output columns: library name,
  version, flags (hex), 8-hex-digit address, symbol name — example lines shown in the repo:
  `D3D8 0 1 00192390 D3DDEVICE`, `XAPILIB 3911 40 00018b0c XapiBootDash`. This may be the faster
  path if the project's tooling is already Python-first (it is — `xboxrecomp`'s own pipeline is
  Python), avoiding a separate MSVC/CMake CLI build.
- Also worth knowing: `XboxDev/ghidra-xbe` (the Ghidra XBE loader,
  https://github.com/XboxDev/ghidra-xbe) does symbol recovery via pattern-matching against
  XbSymbolDatabase automatically on import — if a Ghidra pass is already part of the pipeline
  (per `research_conventions.md` in this same `docs/research/` folder), that may name statically
  linked D3D8/XAPI/etc. functions for free without a separate CLI step at all. Worth trying first
  since it slots directly into the existing Ghidra-based workflow.

## 3. D3D8 vs D3D8LTCG

- **D3D8LTCG** ("Link-Time Code Generation") is a whole-program-optimized build of the D3D8
  library where the compiler inlines/reorders/merges code across the library and game at link
  time. Per the XbSymbolDatabase issue tracker and NGEMU forum discussion, **the code inside
  D3D8LTCG functions is not static** the way normal D3D8 is — "even though one signature might
  work for one app/game, it's not guaranteed to work for the next", and "there is no standard way
  to disassemble the D3D8LTCG library in IDA or use automated tools to locate the functions."
  D3D8LTCG signatures in the database (`D3D8LTCG_OOVPA.c`) are versioned per-title/per-build (e.g.
  a dedicated `D3D8LTCG/5849.inl` file, only verified so far against GTA: San Andreas at 5849) —
  i.e., LTCG coverage is much more brittle and much more likely to need per-title re-verification.
  Games known to use D3D8LTCG include Conker, Midtown Madness 3, Unreal Championship 2.
- **How to tell which a title uses — authoritatively**: the XBE header's **library-version table**
  names the library directly. If it lists `D3D8LTCG`, the title is LTCG; if it lists plain `D3D8`
  (as Def Jam's header does, per the versions you already pulled: XAPILIB/XGRAPHC/D3D8/D3DX8/
  XBOXKRNL/DSOUND/LIBCMT, all 1.0.5849, with `D3D8` not `D3D8LTCG`), it's the standard,
  section-separated library. Community confirmation: "games using the D3D8LTCG library can be
  identified by checking an XBE dump, where the library name will explicitly include 'LTCG'... if
  you just see 'D3D8', it's non-LTCG."
- Your secondary observation — a distinctly named **"D3D" section** (VA `0x002139A0`, size
  `0x235C0`) plus a separate **"D3DX"** section — is consistent with, and corroborates, a
  non-LTCG build: LTCG output tends to get merged into fewer/undifferentiated sections rather than
  preserving one section per statically-linked library the way a conventional (non-LTCG) link
  does. The XBE header's library-version list naming plain `D3D8` is the authoritative signal
  though; the section name is just supporting evidence.
- **Conclusion for Def Jam: Fight for NY**: it uses standard (non-LTCG) **D3D8**, build 5849. This
  is the easier, more-reliable-signature case of the two.
- **Does XbSymbolDatabase handle both?** Yes — both `D3D8_OOVPA.c` and `D3D8LTCG_OOVPA.c` exist
  as separate signature sets. **Does it require knowing which?** Not strictly — the CLI/library
  presumably tries applicable signature sets and reports whichever library name/version matches
  what's actually in the XBE's library-version table (that's literally the "library" column in
  the PyXbSymbolDatabase example output above) — but knowing in advance that Def Jam is plain D3D8
  (not LTCG) sets the right coverage/confidence expectation: expect the more reliable ~210/229-style
  hit rate seen for Tak2, not the much shakier LTCG situation.

## 4. Concrete D3D8 symbols it can identify, and rough count

Verified directly by downloading `D3D8_OOVPA.c` (pinned repo, `src/OOVPADatabase/D3D8_OOVPA.c`,
704 lines) and grepping for `REGISTER_OOVPAS(SYM_FUN(...))` entries: **232 distinct D3D8 function
signatures registered**, consistent with the title-comment figures ("have 210/229 library" etc. —
~229-232 is roughly "full library" for this XDK era). All four of your example names are present
and confirmed by line number in that file:

| Symbol | Line | Signature params (from `SYM_FUN`) |
|---|---|---|
| `D3DDevice_SetRenderState` | 455 | `PARAM(psh, State), PARAM(psh, Value)` |
| `D3DDevice_DrawVertices` | 294 | `PARAM(psh, PrimitiveType), PARAM(psh, StartVertex), PARAM(psh, VertexCount)` |
| `D3DDevice_Swap` | 597 | `PARAM(psh, Flags)` |
| `Direct3D_CreateDevice` | 683 | `PARAM(psh, Adapter), PARAM(psh, DeviceType), PARAM(psh, hFocusWindow), PARAM(psh, BehaviorFlags), PARAM(psh, pPresentationParameters), PARAM(psh, ppReturnedDeviceInterface)` |

Other examples present: `D3DDevice_SetTexture` (541), `D3DDevice_SetVertexShader` (573),
`D3DDevice_CreateVertexBuffer`/`_2`, `D3DDevice_CreateTexture`/`_2`, `D3DDevice_Clear`,
`D3DDevice_CopyRects`, `D3DDevice_BeginPush`/`EndPush`/`BeginPushBuffer`, `D3DDevice_ApplyStateBlock`,
`D3DDevice_CreateStateBlock`/`DeleteStateBlock`, `D3DDevice_CreatePixelShader`/`CreateVertexShader`,
etc. — 188 symbols matched a `(D3DDevice|D3D|Direct3D|D3DResource)_*` naming grep alone, plus
additional non-prefixed or differently-prefixed entries bringing the registered total to 232.

For a 5849-era, non-LTCG, "full library" title (the closest verified comparator is Nickelodeon
Tak 2, itself at 5849), expect on the order of **~200-230 named D3D8 functions** recoverable by
address, i.e. the great majority of the D3D8 API surface Def Jam actually linked.

## 5. Alternatives if XbSymbolDatabase falls short

1. **Donor-library signature matching (Ghidra FunctionID `.fidb` from a real `d3d8.lib`, or IDA
   FLIRT `.sig` from the same)** — **requires a file you almost certainly cannot legally obtain
   or redistribute.** `d3d8.lib`/`d3d8ltcg.lib` only exist inside the Microsoft Xbox
   Development Kit (XDK), which was NDA'd hardware-locked developer tooling, never publicly sold,
   and is Microsoft's copyrighted/proprietary SDK — there is no legitimate public source for it.
   Community requests for exactly this ("XDK patterns needed!" on NGEMU, asking for anyone with
   XDK + IDA FLIRT access to generate `d3d8.lib`/`d3d8ltcg.lib` signatures) went unanswered in the
   general sense — which is presumably *why* the community built XbSymbolDatabase's hand-curated
   OOVPA signatures from retail-binary reverse engineering instead of from the real library
   object code. **If you happen to possess a legitimate personal-archive XDK copy**, generating a
   `.fidb`/`.sig` from its `.lib` files is mechanically straightforward (FLAIR `pcf`/`sigmake` for
   IDA; various Ghidra FID-generator scripts, e.g.
   https://github.com/threatrack/ghidra-fid-generator, for Ghidra) — but per this repo's own
   hygiene rule ("the repo will be public... never distribute built executables... Hashes are
   fine, bytes are not"), **the resulting signature database itself would be a derivative of
   Microsoft's proprietary library and should not be committed/shared publicly**, only used
   locally, exactly like the game dump itself.
2. **IDA FLIRT** generically: community `.sig` collections
   (https://github.com/Maktm/FLIRTDB, https://github.com/push0ebp/sig-database) were checked and
   do **not** appear to carry Xbox XDK signatures (they're general Windows/DOS/compiler runtime
   sigs) — same legal gap as above, since a working D3D8 `.sig` would need the real `.lib`.
3. **Ghidra FunctionID for the parts XbSymbolDatabase explicitly doesn't cover** — `LIBCMT` (the
   C runtime). This is the *legal* donor case: MSVC ships CRT `.lib` files with the compiler
   itself (not the XDK), independent of any Xbox-specific NDA'd content, so building a `.fidb`
   from a period-appropriate MSVC CRT and running it is clean. This is exactly what
   `xboxrecomp`'s own docs describe doing (`tools/ghidra_naming`: "FidDb recognises the statically
   linked CRT/XDK helpers and names a few hundred of them") and what
   `research_conventions.md` in this same folder already documents for `ghidra-xbe`.
4. **`ghidra-xbe`'s built-in XbSymbolDatabase pattern matching** (see §2) — not really a separate
   alternative so much as the same signatures applied automatically inside Ghidra on import; worth
   trying as the first, lowest-effort path since it may already be wired into this project's
   Ghidra pass.
5. Manual/semi-automated cross-referencing against **other same-XDK-build titles** already
   reverse-engineered by the OOVPA/Cxbx community (Tak 2, GTA:SA at 5849) as a sanity check when
   OOVPA gives a low-confidence or partial match — not a tool, just a way to validate results.

## 6. Existing examples of XbSymbolDatabase driving a recomp/static-analysis project

- **`GTTeancum/OpenXML1xbox`** (https://github.com/GTTeancum/OpenXML1xbox) — "X-Men Legends
  original Xbox to Windows recompilation feasibility project targeting Direct3D 8", built on the
  same `xboxrecomp` toolkit this repo (`defjam-recomp`) uses. Its `scripts/scan-xdk-symbols.ps1`
  + `scripts/parse-xdk-symbols.py` (both fetched and read verbatim for this report, see §2 for the
  exact commands) build the XbSymbolDatabase CLI, scan `game/default.xbe`, and — per its own
  description found via search — "recover[s] 301 named XDK function records, including calling
  conventions, into ignored `analysis/` metadata." This is a directly transferable, already-solved
  reference implementation of the exact same problem this task is asking about, for a sibling
  toolkit-based recomp project.
- **`ghidra-xbe`** (https://github.com/XboxDev/ghidra-xbe) uses XbSymbolDatabase internally to
  auto-name statically-linked XAPI/D3D8/etc. functions on XBE import into Ghidra — already
  documented in this repo's own `docs/research/research_conventions.md` (§2, "XboxDev/ghidra-xbe"
  row) as a known tool, independently corroborating that this is an established, working use of
  XbSymbolDatabase for static analysis (not just for Cxbx-Reloaded's own emulation HLE).
- Cxbx-Reloaded itself is of course the primary consumer, but that's runtime HLE detection inside
  an emulator, not a standalone recompilation/static-analysis pipeline like this project's.

---

## Bottom line

XbSymbolDatabase is viable for Def Jam: Fight for NY at XDK build 5849, non-LTCG D3D8. DSound at
5849 has already been verified against this exact title ("Contain full library"); D3D8/XGraphic/
Xapi are verified at 5849 against a different title (Nickelodeon Tak 2, ~91-100% library coverage)
so results for Def Jam itself are expected-but-not-yet-directly-confirmed until run. A sibling
xboxrecomp-based project (`GTTeancum/OpenXML1xbox`) already automated this exact workflow and its
script is copy-pasteable.

**Next command** (from repo root, PowerShell, once a `work/reference/` scratch area and a VS2022
(or VS Build Tools, matching what's already installed per this repo's environment notes)
toolchain are available):

```powershell
git clone https://github.com/Cxbx-Reloaded/XbSymbolDatabase.git work/reference/XbSymbolDatabase
cmake -S work/reference/XbSymbolDatabase -B build/symbols -G "Visual Studio 16 2019" -A x64 `
    -DXBSDB_BUILD_CLI=ON -DXBSDB_BUILD_UNITTEST=OFF -DXBSDB_INSTALL_LIB=OFF -DXBSDB_INSTALL_CLI=OFF
cmake --build build/symbols --config Release --target XbSymbolDatabaseCLI --parallel 2
& build/symbols/projects/cli/Release/XbSymbolDatabaseCLI.exe game/default.xbe -d -e |
    Set-Content -Encoding utf8 analysis/xdk-symbols.log
```
(Generator string adjusted to `Visual Studio 16 2019` since that's the confirmed-available
toolchain per `CLAUDE.md`/environment notes — VS2022 is not confirmed present here; swap back if
it turns out to be installed.)

## Sources

- https://github.com/Cxbx-Reloaded/XbSymbolDatabase
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/wiki/Overview-of-the-project-&-Glossary-of-terms
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/releases
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/blob/master/src/OOVPADatabase/D3D8_OOVPA.c
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/blob/master/src/OOVPADatabase/D3D8LTCG_OOVPA.c
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/blob/master/src/OOVPADatabase/DSound_OOVPA.c
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/blob/master/src/OOVPADatabase/XGraphic_OOVPA.c
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/blob/master/src/OOVPADatabase/Xapi_OOVPA.c
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/blob/master/CMakeLists.txt
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/issues/85 (D3D8 non-LTCG vs LTCG symbol gap example)
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/issues/79 (LTCG versioning scheme discussion)
- https://github.com/Cxbx-Reloaded/XbSymbolDatabase/commit/20eced5 (pinned commit used by OpenXML1xbox)
- https://github.com/GTTeancum/OpenXML1xbox
- https://raw.githubusercontent.com/GTTeancum/OpenXML1xbox/main/scripts/scan-xdk-symbols.ps1
- https://raw.githubusercontent.com/GTTeancum/OpenXML1xbox/main/scripts/parse-xdk-symbols.py
- https://github.com/GTTeancum/xboxrecomp
- https://github.com/mborgerson/PyXbSymbolDatabase
- https://pypi.org/project/PyXbSymbolDatabase/
- https://github.com/XboxDev/ghidra-xbe
- https://www.ngemu.com/threads/d3d8ltcg-5849-database.131728/ (D3D8LTCG vs D3D8 discussion; page required paid/tollbit access at fetch time — UNVERIFIED beyond the search snippet)
- https://www.ngemu.com/threads/xdk-patterns-needed.109507/ (community request for XDK FLIRT signatures — appears unfulfilled)
- https://github.com/Maktm/FLIRTDB and https://github.com/push0ebp/sig-database (general FLIRT sig collections, no Xbox XDK content found)
- https://github.com/threatrack/ghidra-fid-generator (generic Ghidra FID generation tooling)
- `docs/research/research_conventions.md` (this repo, existing internal research on ghidra-xbe/XbSymbolDatabase)
