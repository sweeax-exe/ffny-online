# macOS and Linux support in recompilation and decompilation ports: survey and implications

Date of research: 2026-10-06. Facts were read from each project's repository (README, build files, CI workflows,
release lists) through the GitHub API and from vendor pages. Where a fact could not be confirmed it is listed in
the "Unverified" section and is not repeated as fact elsewhere. Repo and release data is as of the research date.

## 1. Summary

- Every actively maintained recompilation or decompilation port surveyed that supports macOS ships a native
  build, not a translation-layer recipe. None of the READMEs of Zelda64Recomp, UnleashedRecomp, MarathonRecomp,
  Ship of Harkinian or Dusklight mention Wine or Proton (grep of each README, empty result).
- The common architecture is a small render hardware interface with one backend per OS API: D3D12 on Windows,
  Vulkan on Linux, Metal on macOS (plume for RT64 and the XenonRecomp ports, aurora/Dawn for GameCube ports,
  libultraship/Fast3D for the Ship family). Where Metal is missing, the fallback is Vulkan over MoltenVK
  (UnleashedRecomp bundles MoltenVK today).
- Shader strategy splits in two: shaders known ahead of time are compiled at build time (HLSL to DXIL and SPIR-V,
  then SPIR-V to MSL), and shaders that depend on run-time hardware state are generated as text at run time and
  compiled by glslang (xemu) or Dawn/Tint (aurora).
- Guest memory in the portable ports is a single reserved block addressed as `base + 32-bit guest address`, not a
  fixed low mapping. This is what makes arm64 macOS possible: Apple does not support moving `__PAGEZERO` below 4 GB
  on arm64.
- Only OpenGOAL is documented as shipping x86-64 and relying on Rosetta on Apple Silicon. Its arm64 build is
  labelled experimental and unsupported.
- Apple states Rosetta works through macOS 27, and from macOS 28 remains only for certain older, unmaintained games
  that rely on Intel-based frameworks. A Windows x86-64 executable under CrossOver/GPTK depends on Rosetta as well
  (see unverified list for how CrossOver 26 itself is built).

## 2. Comparison table

| Project | Guest platform | Linux approach | macOS approach | Graphics abstraction | Arch support | Packaging |
|---|---|---|---|---|---|---|
| Zelda64Recomp (N64Recomp + N64ModernRuntime + RT64) | N64 MIPS | Native; x64 and arm64 | Native, universal x86_64+arm64 build | RT64 over plume: D3D12 / Vulkan 1.2 / Metal (argument buffers tier 2) | x64, arm64 (Linux), universal (macOS) | AppImage, Flatpak, zip; macOS zip (v1.2.2) |
| Dinosaur Planet: Recompiled | N64 MIPS | Native x64 | None in release assets or README | RT64 (README lists D3D12 or Vulkan 1.2) | Linux x64, Windows x64 | AppImage, Flatpak, tar.gz (v0.3.0) |
| Ship of Harkinian / 2Ship2Harkinian | N64 decomp source ports | Native, AppImage (SoH), zip (2Ship) | Native macOS app, universal in SoH CI | libultraship Fast3D: D3D11 / OpenGL / Metal (Metal default on macOS in 2Ship) | x64 Linux; universal macOS | AppImage, Mac zip, Win zip |
| Perfect Dark port (fgsfdsfgs) | N64 decomp source port | Native x86_64, i686 | Native x86_64 and arm64 | OpenGL 3.0 / ES3.0 via SDL2 | x86_64, i686, arm64 (macOS, Switch) | CI tarballs |
| sm64ex | N64 decomp source port | Native | Not confirmed from README | GL 1.3 / GL 2.1 / D3D11 / D3D12 (Fast3D) | not confirmed | source build |
| UnleashedRecomp (+ XenonRecomp, XenosRecomp) | Xbox 360 PowerPC | Native; official Flatpak; Steam Deck supported | Merged to main Aug 2025 (PR 745); arm64 built in CI; no macOS release asset yet (latest release v1.0.3, Apr 2025) | plume: D3D12 / Vulkan; macOS currently through plume Vulkan over bundled MoltenVK | x64 Windows/Linux; arm64 macOS | Flatpak, Windows zip; macOS app bundle from CI |
| MarathonRecomp | Xbox 360 PowerPC | Native; Flatpak CI job | README says Windows, Linux, macOS; no macOS CI job in validate.yml; no GitHub releases | plume (same lineage as Unleashed) | not confirmed | Flatpak CI job |
| ReXGlue SDK | Xbox 360 PowerPC | linux-amd64 and linux-aarch64 CI | not confirmed | not confirmed | x64, arm64 Linux | SDK |
| xboxrecomp (sp00nznet) | Original Xbox x86 | OpenGL 3.3 D3D8 backend, "first cut" | Library build path and POSIX memory model; test harness via Docker; no shipped game | D3D8 to D3D11 (Windows) or OpenGL (POSIX) | x86-64 host; arm64 macOS memory fixes landed | libraries only |
| Burnout 3, Crimson Skies and other xboxrecomp ports | Original Xbox x86 | not stated | not stated | D3D11 target; Crimson Skies graphics "Not started" | Windows x86-64 | repo |
| xemu | Original Xbox (emulator) | AppImage x86_64 and aarch64 | Universal macOS build | OpenGL (default) or Vulkan renderer; MoltenVK enablement PR open | x64, arm64 | AppImage, mac zip |
| Dusklight (Twilight Princess) on aurora | GameCube / Wii decomp | AppImage x86_64 and arm64 | Native macOS arm64 and x86_64 | aurora GX layer on WebGPU (Dawn): D3D12 / Vulkan / Metal | x64, arm64 | AppImage, zip, apk, ipa |
| OpenGOAL (Jak) | PS2 game rewritten in GOAL, x86-64 JIT compiler | Native x86_64 | x86_64 under Rosetta; arm64 build experimental, unsupported | OpenGL (glad in third-party) | x86_64 supported | Launcher: AppImage, deb, dmg (aarch64 and x64 launcher) |
| PS2Recomp (ran-j) | PS2 MIPS R5900 | not stated in README | not stated | runtime only; graphics not described | "tested mainly with MSVC" | source |
| Fukami (Ridge Racer V, PS2Recomp-style) | PS2 | Steam Deck / SteamOS 3.7+ | Apple Silicon M1 or later | PCSX2 GS code underneath | arm64 macOS, x86-64 SteamOS | release builds, no JIT |
| OpenRCT2 | Reimplementation (not recomp) | AppImage, tarballs x86_64 | macOS universal zip | not confirmed | x86_64, universal | release assets |
| re3 / reVC | PS2-era decomp | repository returned HTTP 451 (DMCA) | n/a | n/a | n/a | n/a |

## 3. Topics

### 3.1 N64: Zelda64Recomp, N64Recomp, N64ModernRuntime, RT64

- Zelda64Recomp README states Windows, Linux and macOS on x86-64 and arm64. It requires D3D12 (SM6), Vulkan 1.2, or
  Metal argument buffers tier 2, and lists "a Mac with Apple Silicon or an Intel 7th Gen CPU with MacOS 13.0+".
  Linux ships a native binary and a Flatpak, "including on the Steam Deck".
  https://github.com/Zelda64Recomp/Zelda64Recomp (README.md)
- Release v1.2.2 (2025-08-27) has separate assets for Linux ARM64, Linux x64, Linux Flatpak, macOS and Windows.
  https://github.com/Zelda64Recomp/Zelda64Recomp/releases/tag/v1.2.2
- The CI job `build-macos` runs on a macOS 14 runner with `CMAKE_OSX_ARCHITECTURES="x86_64;arm64"`, so the macOS
  binary is universal. The workflow also has native Linux x64 (AppImage), Linux arm64 (AppImage) and Flatpak jobs.
  https://github.com/Zelda64Recomp/Zelda64Recomp/blob/dev/.github/workflows/validate.yml
- RT64 README: "built on the latest APIs (D3D12, Vulkan and Metal)", ubershaders so there is no pipeline-compile
  stutter, "Supports Windows 10, Windows 11, Linux and macOS". It uses `renderbag/plume` as the RHI.
  https://github.com/rt64/rt64 and its `.gitmodules`.
- RT64 compiles its shaders at build time. Its CMake runs DXC on HLSL for SPIR-V (`-spirv`, Vulkan 1.0 target),
  for DXIL, and for the Metal path runs DXC to SPIR-V, then `spirv_cross_msl` to Metal source, then `xcrun metal`
  and `metallib`, and embeds the results as C arrays. It ships prebuilt `dxc` binaries for Windows, Linux (x64 and
  arm64) and macOS (x64 and arm64) in a `rt64/dxc-bin` submodule, plus SPIRV-Cross binaries.
  https://github.com/rt64/rt64/blob/main/CMakeLists.txt
- plume README: "low-level rendering hardware interface (RHI) ... lowest common denominator between ... Direct3D 12,
  Vulkan and Metal", "bring your own compiler" for shaders, MIT licence, and an explicit statement that the API is
  not considered stable. The repository has `plume_d3d12`, `plume_vulkan`, `plume_metal` and `plume_apple.mm`.
  https://github.com/renderbag/plume
- N64Recomp README does not list host platforms; it says output compiles with MSVC, GCC and Clang, and generated
  functions take `(rdram, ctx)`, i.e. a base pointer plus context. N64ModernRuntime delegates graphics to a
  renderer the host project registers (RT64 recommended) and platform I/O to callbacks.
  https://github.com/N64Recomp/N64Recomp, https://github.com/N64Recomp/N64ModernRuntime
- Dinosaur Planet: Recompiled release v0.3.0 has Linux x64 AppImage, Flatpak and tar.gz plus a Windows zip, and no
  macOS asset. Its README lists D3D12 or Vulkan 1.2.
  https://github.com/DinosaurPlanetRecomp/dino-recomp

### 3.2 N64 source ports: Ship of Harkinian, 2Ship, sm64ex, Perfect Dark

- Ship of Harkinian README: three rendering APIs, "DirectX11 (Windows), OpenGL (all platforms), and Metal (MacOS)";
  Linux is an AppImage, macOS an app. Its macOS CI builds `x86_64;arm64`. Latest release 9.3.0 (2026-10-06) carries
  Mac, Win64 and AppImage assets.
  https://github.com/HarbourMasters/Shipwright
- 2Ship2Harkinian README: same three backends, Metal is the default on macOS; release 5.0.1 has Linux, Mac and Win64
  zips. https://github.com/HarbourMasters/2ship2harkinian
- libultraship (the shared layer) builds on Linux and macOS with plain CMake, SDL, Homebrew or apt packages.
  https://github.com/Kenix3/libultraship
- Perfect Dark port: Linux i686 and x86_64, macOS x86_64 and arm64, Switch arm64; requires OpenGL 3.0 or ES3.0.
  https://github.com/fgsfdsfgs/perfect_dark
- sm64ex README lists GL 1.3, GL 2.1, D3D11 and D3D12 renderers from Emill's n64-fast3d-engine. It does not mention
  macOS in the README text. https://github.com/sm64pc/sm64ex

### 3.3 Xbox 360: UnleashedRecomp, XenonRecomp, XenosRecomp, plume

- UnleashedRecomp README: "Windows and Linux support", D3D12 or Vulkan 1.2, official Flatpak, Steam Deck native
  builds. Its FAQ originally said macOS depends on a Metal backend in plume and "a prototype through the usage of
  MoltenVK could also be currently attempted".
  https://github.com/hedge-dev/UnleashedRecomp
- Issue 455 is closed with "macOS has been added and will be part of the next version update". PR 745 "Add support
  for macOS" merged 2025-08-03. The CI workflow has a `build-macos` job on macOS 15, arm64 only, with debug, release
  and relwithdebinfo presets, producing a `.app` tarball. The release list still ends at v1.0.3 (2025-04-03) with
  only Windows and Flatpak assets, so macOS is a build-from-source or CI-artifact path as of this research.
  https://github.com/hedge-dev/UnleashedRecomp/issues/455,
  https://github.com/hedge-dev/UnleashedRecomp/pull/745,
  https://github.com/hedge-dev/UnleashedRecomp/blob/main/.github/workflows/validate.yml
- The macOS build bundles MoltenVK inside the app, with an ICD JSON, and keeps MoltenVK and a SPIRV-Cross copy as
  submodules. So the shipping macOS path is plume's Vulkan backend over MoltenVK, not plume's Metal backend.
  https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/CMakeLists.txt, `.gitmodules`
- The issue 455 thread records the early state: a custom MoltenVK build with a SPIRV-Cross fix was needed to remove
  shader compile errors; under CrossOver Wine the game reached the title screen with heavy visual bugs in a debug
  build while the release build crashed; D3DMetal crashed at game start; `directx-dxc` from vcpkg had no macOS
  support. These were March 2025 observations on unreleased code.
  https://github.com/hedge-dev/UnleashedRecomp/issues/455
- XenonRecomp README: converts Xbox 360 executables to C++, CPU state passed as an argument, a base pointer is the
  second argument because "the Xbox 360 CPU uses 32-bit pointers"; loads and stores byte-swap; VMX is implemented
  with x86 intrinsics and "Support for ARM64 is implemented using SIMD Everywhere" (simde); MMIO is "currently
  unimplemented" and "may be non-trivial and could require advanced analysis of instructions".
  https://github.com/hedge-dev/XenonRecomp
- UnleashedRecomp's memory class reserves one anonymous block, tries address `0x100000000`, falls back to an
  OS-chosen address, and makes the first 4 KB `PROT_NONE`/`PAGE_NOACCESS`. All guest access is `base + offset`.
  https://github.com/hedge-dev/UnleashedRecomp/blob/main/UnleashedRecomp/kernel/memory.cpp
  The PowerPC guest on an arm64 host is therefore not emulated at all: the lifted C++ is compiled for arm64.
- XenosRecomp README: converts Xbox 360 shader binaries to HLSL ahead of time, "recompiled to DXIL and SPIR-V using
  DXC", designed around Unleashed; "Do not expect the recompiler to work out of the box".
  https://github.com/hedge-dev/XenosRecomp
  The Xenos shader set is fixed on disc, so it can be translated before release. That is not the case for NV2A
  vertex programs and register combiner state that a push buffer sets at run time.
- MarathonRecomp README: "Windows, Linux, and macOS support", Flatpak ID in CI, a named maintainer for macOS.
  No GitHub releases exist and `validate.yml` has Linux, Windows and Flatpak jobs but no macOS job.
  https://github.com/sonicnext-dev/MarathonRecomp
- ReXGlue SDK ("Xbox 360 Recompilation Runtime and Toolkit") lists linux-amd64 and linux-aarch64 CI.
  https://github.com/rexglue/rexglue-sdk

### 3.4 Original Xbox: xboxrecomp, its ports, xemu

- xboxrecomp README: toolkit produces "a native x86-64 .exe" with MSVC; prerequisites say Windows 11/10 (D3D11) "or
  Linux (OpenGL backend)", macOS via homebrew and docker for the test suite. v0.4.0 (May 2026) added a cross-platform
  layer with an OpenGL D3D8 backend beside the D3D11 path. `src/d3d/d3d8_gl.c` calls itself the "FIRST cut"
  of an OpenGL 3.3 core backend with a fixed program for position, diffuse and UV; NV2A vertex-program and register-
  combiner translation to GLSL is listed as next work.
  https://github.com/sp00nznet/xboxrecomp and `src/d3d/d3d8_gl.c`
- The same README records the memory model running on a POSIX host: a sentinel bug meant "let the OS choose" never
  ran, "fatal on arm64 macOS, where every fixed base sits inside `__PAGEZERO`"; `MAP_FIXED` mirrors overwrote live
  mappings; macOS has no `MAP_FIXED_NOREPLACE`. The macOS work covers the memory layer and the conformance test
  harness (Docker, because Rosetta cannot run 32-bit x86). It is not a shipped macOS game.
- Per-title ports (burnout3, crimsonskies-recomp and others by the same author) state Windows x86-64 targets. The
  Crimson Skies README shows "Graphics (D3D8 to D3D11): Not started". No Linux or macOS status is given for them.
  https://github.com/sp00nznet/burnout3, https://github.com/sp00nznet/crimsonskies-recomp
- xemu is an emulator, with software-MMU guest memory through QEMU TCG, so page size and fixed mappings do not apply
  as they do to a recomp. Released 0.8.136 has `macos-universal` and both x86_64 and aarch64 AppImages. The renderer
  setting accepts `NULL`, `OPENGL` (default) or `VULKAN`.
  https://github.com/xemu-project/xemu/releases/tag/v0.8.136, `config_spec.yml`
- xemu's Vulkan renderer builds GLSL text at run time and calls the glslang C interface to produce SPIR-V (target
  Vulkan 1.3, SPIR-V 1.6), using volk, VMA and spirv-reflect.
  https://github.com/xemu-project/xemu/tree/master/hw/xbox/nv2a/pgraph/vk (`glsl.c`, `meson.build`)
- macOS Vulkan in xemu is not in the release: PR 2799 "macOS/MoltenVK: Vulkan renderer support for ARM Macs" is
  open. It enables `VK_KHR_portability_enumeration`, bundles the loader and MoltenVK, adapts buffer, surface,
  texture and display paths "for MoltenVK limitations", and reports a Halo 2 comparison against "the GL 4.1 path"
  on an M3 Ultra. PR 2993 (persist pipeline and SPIR-V caches) is also open.
  https://github.com/xemu-project/xemu/pull/2799, https://github.com/xemu-project/xemu/pull/2993

### 3.5 PS2, GameCube and other ports

- aurora README: SDL3 application layer on Windows, Linux, macOS, iOS, tvOS, Android; GX compatibility layer with
  "D3D12, Vulkan, Metal" built on WebGPU through Chromium's Dawn; a "transferable" pipeline cache for releases.
  https://github.com/encounter/aurora
- aurora generates its fragment code per GX state as text (`lib/gx/shader.cpp` formats WGSL snippets with fmt) and
  lets Dawn/Tint compile it. This is the closest analogue found to NV2A register-combiner translation at run time.
- Dusklight (Twilight Princess) v2.0.3 (2026-09-30) ships linux-arm64 and linux-x86_64 AppImages, macos-arm64 and
  macos-x86_64 zips, Windows x64 and arm64, Android and iOS. Requires D3D12, Vulkan 1.1+ or Metal.
  https://github.com/TwilitRealm/dusklight
- OpenGOAL README: "x86_64 on Windows, Linux and macOS (via Rosetta translation)". macOS doc requires Rosetta 2 on
  Apple Silicon and labels the arm64 build "experimental, unsupported". The launcher itself ships aarch64 and x64
  dmg and app bundles. https://github.com/open-goal/jak-project and `docs/setup/system/macos.md`
- PS2Recomp (ran-j): translates R5900 to C++, "currently tested mainly with MSVC", SSE4/AVX noted for vector paths.
  No Linux or macOS statement in the README. https://github.com/ran-j/PS2Recomp
- Fukami, a Ridge Racer V port using PS2 recompilation: "macOS (Apple silicon) and Linux (Steam Deck)", uses the
  PCSX2 GS code, "No JIT anywhere". https://github.com/danisandoval/fukami
- OpenRCT2 v0.5.5 release assets include a macOS universal zip, Linux x86_64 AppImage and tarballs and Windows
  x64 and arm64 installers. https://github.com/OpenRCT2/OpenRCT2/releases/tag/v0.5.5
- re3/reVC: the GitHub repository API returned HTTP 451 citing a DMCA notice (April 2025), so nothing could be read.

### 3.6 x86 guest code on arm64 hosts

- Surveyed projects with non-x86 guests (MIPS, PowerPC, SH-4) recompile to portable C or C++ and compile it for the
  host, so the guest ISA does not matter; their arm64 issues are SIMD intrinsics (solved with simde in XenonRecomp)
  and memory.
- No surveyed project statically recompiles an x86 guest and ships a native arm64 macOS game. The nearest evidence
  is xboxrecomp's POSIX memory work (above), which is for its test harness. OpenGOAL is the only surveyed x86-64
  project, and it relies on Rosetta; its own arm64 target is marked unsupported.
- Memory model: on arm64 macOS the executable cannot reduce `__PAGEZERO` below 4 GB. Apple's DTS engineer says
  modifying `pagezero_size` "isn't a supportable option in the arm64 environment" and to remove assumptions that
  require the lower 32 bits. https://developer.apple.com/forums/thread/655950
  Consequence: a recomp that needs guest address X to equal host address X cannot run on arm64 macOS. A recomp that
  computes `base + guest_addr` can.
- Fault trapping: UnleashedRecomp and N64Recomp have no hardware-register fault handler; XenonRecomp documents MMIO as
  unsolved. xboxrecomp's NV2A and APU hooks are a Windows vectored exception handler that decodes the faulting
  x86-64 instruction (`src/platform/mmio_decode.h`, `src/nv2a/nv2a_mmio_hook.c`). That decoder is host-ISA
  specific and the POSIX branch is a stub (`win32_compat.c`: "TODO: wire to sigaction").
- Page size: not verified from a primary source here (see unverified list); it is a design constraint to check, not a
  fact this survey establishes.

### 3.7 Translation-layer practice

- Proton or Wine as the Steam Deck answer: none of the five READMEs above mention it. They offer a native Linux
  build and a Flatpak (Zelda64Recomp, UnleashedRecomp, MarathonRecomp) or an AppImage (Ship family, Dusklight,
  Dino Planet, OpenGOAL launcher). UnleashedRecomp and Zelda64Recomp give explicit Steam Deck instructions
  (Flatpak, or "Add to Steam" for the Linux build).
- DXVK Native: upstream describes it as a version of DXVK "used natively without Wine", "primarily useful for game
  and application ports to either avoid having to write another rendering backend", for D3D9 and D3D11 with SDL2,
  SDL3 or GLFW selected via `DXVK_WSI_DRIVER`. Release 3.1.1 (2026-09-15) provides a single native asset built on
  the Steam Runtime ("sniper"), i.e. Linux x86-64. It supplies a slim set of Windows header definitions.
  https://github.com/doitsujin/dxvk
- `D3DCompile` is not part of DXVK. The vendored toolkit's D3D path calls `D3DCompile` at run time
  (`src/d3d/d3d8_vsh.c`, `d3d8_shaders.c`, `d3d8_combiners.c`). A native Linux build on DXVK Native therefore also
  needs a separate HLSL compiler that works outside Windows (not evaluated here).
- D3D11 through DXVK on macOS: the DXVK wiki lists current requirements as Vulkan 1.4 plus
  `VK_EXT_depth_clip_enable`, `VK_KHR_maintenance5`/`6`, `VK_KHR_load_store_op_none`, `VK_EXT_robustness2` and
  `VK_EXT_transform_feedback`. https://github.com/doitsujin/dxvk/wiki/Driver-support
  MoltenVK's user guide advertises Vulkan 1.4 and lists maintenance5/6, robustness2 and depth_clip_enable, but the
  extension list in its user guide does not include `VK_EXT_transform_feedback`.
  https://github.com/KhronosGroup/MoltenVK (Docs/MoltenVK_Runtime_UserGuide.md)
  So current upstream DXVK is not expected to start on MoltenVK unmodified. The macOS DXVK fork's last release is
  v1.10.3 from 2023, and the upstream docs do not mention macOS.
  https://github.com/Gcenx/DXVK-macOS. DXMT (Metal-based D3D11/D3D10 for Wine on macOS) is active and is bundled in
  CrossOver 26 (v0.72). https://github.com/3Shain/dxmt
- CrossOver 26 (Feb 2026) ships Wine 11.0, D3DMetal 3.0, DXMT 0.72 and vkd3d 1.18 per Phoronix.
  https://www.phoronix.com/news/CrossOver-26 It is a paid product; the Mac requirement is Apple Silicon (M1 or
  better) per CodeWeavers.
- Apple Game Porting Toolkit: Apple's page describes the evaluation environment as for testing a Windows game on
  Apple silicon "before porting, not for shipping to players", names CrossOver and Homebrew as community users, and
  separately offers the Metal Shader Converter (DXIL to Metal) for shipping native ports.
  https://developer.apple.com/games/game-porting-toolkit/
  A CodeWeavers description of the D3DMetal licence quotes a licence to "install, internally use, and test" for
  developing, testing or evaluating video games on Apple products, and distinguishes the shippable shader converter.
  I could not fetch the licence text itself (see unverified list). Practical reading: the project cannot bundle
  D3DMetal; a user could run the Windows build in their own CrossOver or GPTK install.
- Whisky: archived on GitHub (last push 2025-05-11), README says "no longer actively maintained"; last release v2.3.5
  (2025-04-05); built on CrossOver 22.1.1 and GPTK. https://github.com/Whisky-App/Whisky
  Gcenx publishes community GPTK builds (3.0-3, March 2026). https://github.com/Gcenx/game-porting-toolkit
- Rosetta 2: Apple Support: "Rosetta is available for any Mac with Apple silicon using macOS 27 or earlier. Starting
  with macOS 28, the next major macOS release, Rosetta functionality will be available only for certain older,
  unmaintained games that rely on Intel-based frameworks."
  https://support.apple.com/en-us/102527
  macOS 26.4 shows warnings when launching Rosetta apps (MacRumors, 2026-02-16).
  https://www.macrumors.com/2026/02/16/macos-tahoe-26-4-rosetta-2-warnings/
  Apple's wording is a narrow games exception, not a general guarantee for a new game.

### 3.8 Runtime shader generation options

| Option | What it takes | Used by | Notes |
|---|---|---|---|
| Generate GLSL, compile with glslang C API | link glslang; target Vulkan 1.3 / SPIR-V 1.6 | xemu Vulkan renderer (nv2a) | Direct fit for NV2A vertex programs and combiners; xemu has the closest shipping precedent |
| shaderc (Google) | wraps glslang, simpler API; active (push 2026-10-02) | not found in the surveyed projects | Same inputs as glslang; extra dependency |
| Generate HLSL, compile with DXC library to SPIR-V | ship dxcompiler (large) | RT64 and XenosRecomp use DXC at build time only | vcpkg `directx-dxc` has no macOS support (issue 455); RT64 ships its own prebuilt macOS dxc binaries; run-time use on macOS not shown by anyone surveyed |
| SPIRV-Cross | SPIR-V to MSL, GLSL, HLSL | RT64 build time (to MSL); MoltenVK internally; Unleashed submodule | A build-time or Metal-backend tool, not a source language |
| Generate WGSL, compile via Dawn/Tint | Dawn dependency | aurora (`lib/gx/shader.cpp`) | Brings WebGPU as the whole abstraction |
| Metal Shader Converter | DXIL to Metal, Apple tool, shippable | not found in surveyed projects | Needs DXIL, so needs DXC first |
| Pipeline and shader cache | persist SPIR-V and pipeline caches | aurora (transferable cache), xemu PR 2993 (open), RT64 uses ubershaders | Reduces first-use stutter from run-time compile |

Current DXVK 3.x translates D3D11 bytecode to SPIR-V with its own `dxbc-spirv` project. The HLSL that this project's
renderer generates is compiled to DXBC by `D3DCompile`, which would be the translation-layer seam.

## 4. Facts about this repo that matter (read from the tree, not from the web)

- Lifted memory macros are `*(volatile T *)((uintptr_t)(uint32_t)(addr) + g_xbox_mem_offset)`
  (`tools/xboxrecomp/templates/runtime/recomp_types.h`), so generated code is already base-relative. The runtime tries
  `0x10000` first, then other low bases, then lets the OS choose (`src/kernel/xbox_memory_layout.c`, with a comment
  that arm64 macOS needs the OS-chosen path). Whether this game's own hand-written hooks, kernel bridge or patches
  assume a host pointer equals a guest address is not verified here.
- x87 state is double-backed in the runtime (`recomp_types.h`), and MMX conversion has a non-x86 branch, so numeric
  runtime code is not tied to x86 hardware.
- Hardware register trapping is Win32-only: `nv2a_mmio_hook.c` and `apu_mmio_hook.c` decode x86-64 host instructions
  in a vectored handler; `mmio_decode.h` says anything it does not recognise returns 0 and stops, by design. The POSIX
  branch is a stub.
- The renderer uses Win32 D3D11/DXGI (`CMakeLists.txt` links `d3d11 dxgi dxguid`, `src/hooks/d3d11_translator.c`
  creates a window via `CreateWindowExW`) and the toolkit's NV2A path (`src/nv2a/nv2a_pgraph_d3d11.c`, 3,544 lines
  total in `src/nv2a`) plus run-time `D3DCompile` for generated HLSL.

## 5. Unverified or could not confirm

- Whether any surveyed project statically recompiles an x86 guest and ships a native arm64 macOS game. None found; this is
  an absence of evidence, not a proof.
- Apple Silicon 16 KB page size and its effect on 4 KB-granular trapping. I could not read Apple's documentation
  (the pages are rendered by script and returned no text). Treat 16 KB as the design assumption to verify, not as a
  surveyed fact.
- Whether x86-64 code under Rosetta can map below 4 GB, reduce `__PAGEZERO`, and deliver x86 register context to a
  vectored or signal handler for faulting instructions. Not checked.
- The text of Apple's GPTK / D3DMetal licence. Only the Apple page's "not for shipping to players" and a secondary
  quotation were read; the CodeWeavers blog returned HTTP 403.
- Whether CrossOver 26's stable Wine components on Apple Silicon still run as x86-64 under Rosetta 2. One search
  summary said so; the CodeWeavers pages I could fetch did not state it.
- CrossOver price and exact current macOS minimum.
- Whether Whisky's closure notice states a reason; only the README notice was read.
- Whether DXVK Native is used in shipped commercial or community ports, and whether a DXVK Native build on macOS exists.
- Whether Metal can support the geometry shader or point-sprite behaviours this game's NV2A state might need; not
  surveyed.
- sm64ex macOS status (README silent), OpenRCT2 graphics backend, OpenGOAL's macOS graphics path, MarathonRecomp
  macOS build artifact status beyond the README, ReXGlue macOS status, PS2Recomp renderer and Linux/macOS status.
- Whether MarathonRecomp or UnleashedRecomp's macOS builds use plume's Metal backend later; as of the code read,
  UnleashedRecomp bundles MoltenVK.
- Exact `vm.mmap_min_addr` on Steam Deck, which bears on mapping the Xbox base at `0x10000` on Linux.
- Whether `vkd3d-shader` can stand in for `D3DCompile` for this renderer's generated HLSL.
- xemu's page-size handling on 16 KB hosts and any xemu Metal work beyond PR 2799.
- re3/reVC (blocked, see 3.5).

## 6. Implications for this project

### 6.1 The two options

Option A, translation layer: ship the current Windows x64 D3D11 build and let Wine/Proton (Linux, Steam Deck) or
CrossOver/GPTK (macOS) run it. Variant A2 keeps D3D11 source but links DXVK Native on Linux.

Option B, native: x86-64 Linux build plus arm64 macOS build, one Vulkan renderer (MoltenVK on macOS first, plume or
Metal later if wanted).

### 6.2 Comparison

| Criterion | A: translation layer | B: native Linux x86-64 + macOS arm64, Vulkan |
|---|---|---|
| Engineering effort | Lowest for Linux (Proton runs the existing exe; nothing to port). On macOS, no code but a user-side CrossOver/GPTK install. A2 (DXVK Native) is a smaller port than B on Linux but still needs a `D3DCompile` replacement and a window/WSI port | Highest. Needs: Vulkan renderer for NV2A (state, shaders, combiners), POSIX or Mach fault handling for MMIO, SDL3 or similar for window, audio and input, a Linux and macOS build, CI, and arm64 audit of hand-written hooks |
| Risk | Depends on third parties: Whisky is unmaintained; D3DMetal cannot be bundled; Rosetta shrinks after macOS 27 and the Windows build is x86-64; DXVK 3.x is not expected to run on MoltenVK. Debugging an MMIO vectored handler under Wine plus Rosetta is out of your control. The one datapoint for a recomp under Wine on a Mac (UnleashedRecomp, March 2025) was a crash or heavy visual corruption, on unreleased code and a different API | Risk is in your own code: the MMIO trap path on arm64 (the x86-64 decoder cannot be reused), arm64 audit of the memory model, NV2A to Vulkan fidelity. MoltenVK is a feature-subset implementation; UnleashedRecomp needed a SPIRV-Cross fix early on. Risk is tractable because the surveyed projects show the pattern works |
| User experience | Linux/Steam Deck: install through Steam as non-Steam game with Proton; no official support path, user tunes Proton version. macOS: user buys or sets up CrossOver or GPTK, installs Rosetta, no packaged app; performance overhead of two translation layers | Native Linux binary, optionally Flatpak, as Zelda64Recomp and UnleashedRecomp ship. macOS `.app` bundle with Apple Silicon support; no Rosetta dependency if arm64. Matches what players of the surveyed ports already expect |
| Maintenance | Low code maintenance, high support burden (per-Wine-version regressions, GPTK changes, closed or paid tools). Windows build stays the single build | A third and fourth platform to test, three code paths for window, input and audio, and Vulkan validation work. Offset by one renderer shared by Linux and macOS and an abstraction layer you can adopt (plume is MIT but its README says its API is not stable) |
| Longevity | Rosetta end state (from macOS 28, games exception only) limits any x86-64 Mac route, including an x86-64 native build | arm64-native macOS does not depend on Rosetta |

### 6.3 What the survey implies

1. Ship native builds if macOS matters. Every maintained port that supports macOS does this; none documents a
   Wine route. A translation-layer-only macOS plan would be unlike every project surveyed and rests on a licence
   that bars bundling the key component (D3DMetal) and on Rosetta, which Apple is narrowing.
2. The memory model is not the blocker it looks like. Generated code is already `base + offset`, and the runtime
   already falls back to an OS-chosen base. The remaining check is this game's own hooks, which should be audited for
   host-pointer-equals-guest-address assumptions.
3. The real arm64 blocker is hardware-register trapping. The current mechanism decodes the faulting host
   x86-64 instruction. On x86-64 Linux the same decoder can be reused behind a `SIGSEGV` handler reading
   `ucontext_t` (the toolkit has a stub waiting for this). On arm64 macOS it cannot be reused. Options, none
   verified by a surveyed project: (a) lift MMIO accesses as explicit calls when the address is constant or provably
   in a device range, and leave a small decoder for the rest, noting that XenonRecomp calls the general analysis
   "non-trivial"; (b) write an A64 load/store decoder, which has fixed-width encodings but still sees whatever the
   host compiler emits for `volatile` accesses; (c) keep an x86-64 macOS build under Rosetta as a transitional
   product, accepting the Rosetta timeline. Option (c) is a stopgap only. This is the highest-risk item in B and
   should be prototyped first.
4. Do the Vulkan renderer once. Build it from the NV2A state tracker, not from the D3D11 code. Generate GLSL (or
   SPIR-V) from the NV2A vertex program and combiner state and compile it with glslang's C API, the approach xemu's
   Vulkan renderer already uses for the same hardware; add a persistent pipeline and SPIR-V cache from the start
   (aurora and xemu PR 2993 are both moving that way). Do not plan on DXC as the run-time compiler; no surveyed
   project does that, and `directx-dxc` had no macOS package.
5. For macOS graphics, start with Vulkan over MoltenVK (UnleashedRecomp does this today, bundling MoltenVK with the
   app and an ICD file). Treat a Metal backend through plume or a SPIRV-Cross/MSL path as optional later work, since
   plume's API is not stable and RT64's own Metal path depends on a build-time DXC chain.
6. Order of work that follows from the above: Linux x86-64 first (same ISA, existing decoder, Vulkan renderer
   shared with macOS, immediate Steam Deck target), then arm64 macOS using the same renderer once the MMIO trap
   path is solved. Keep the Windows D3D11 build as is until the Vulkan renderer is at parity, so there is a
   comparison reference.
7. As a no-code interim, the existing Windows build under Proton on Steam Deck is a reasonable thing to let users try, with
   no support commitment. It is not evidence that the build works; the MMIO vectored handler and
   `D3DCompile` use under Wine are untested here.
8. DXVK Native (A2) is only worth considering for a quick Linux-only milestone. It does nothing for macOS (DXVK 3.x
   requirements versus MoltenVK's extension list), it does not provide `D3DCompile`, and it would leave the project
   with a renderer that has to be thrown away for macOS.

### 6.4 Recommendation

Plan for option B in this order: x86-64 Linux with a new Vulkan renderer and a `SIGSEGV`-based MMIO path, then arm64 macOS
on the same renderer over MoltenVK, with the arm64 MMIO mechanism prototyped before committing to macOS dates.
Use Proton or CrossOver only as unsupported, user-side fallbacks, and do not plan to bundle GPTK/D3DMetal.
