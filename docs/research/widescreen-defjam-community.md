# Def Jam: Fight for NY - community knowledge on widescreen, cameras, HUD and modding

Research date: 2026-10-10. Web research only; nothing was built or run.

Scope: Def Jam: Fight for NY (FFNY; PS2/GC/Xbox, 2004), Def Jam Vendetta (2003), and the PSP "The Takeover" (2006).

Method note: many pages could not be opened directly (Cloudflare challenge on consolemods.org and wiki.pcsx2.net, 403 on the PCSX2 forum, 402 on Fandom). Where a claim rests on a search-tool summary of a page I could not open myself, it is marked "(not directly verified)". Patch files were read verbatim from raw repository files.

## TL;DR

1. No platform shipped a native 16:9 option in FFNY. The databases and every community patch agree. The PS2 database lists "16:9 Option = No" for all regions. The only widescreen work is community code.
2. The Xbox build is, per one list, 480p-capable but not 16:9 and not 720p/1080i. This is thin and the column mapping is unverified (see 1.3).
3. The PS2 widescreen patches are small and consistent. They scale the horizontal projection/FOV by 0.75 (float constant `0x3f40`) so the game is Hor+ in effect, plus a "renderfix" and a "menu fix". The official PCSX2 patch has no HUD or FMV handling, and the SLUS patch has a commented-out, optional cutscene zoom.
4. Nothing was found about `gWidescreenMode`, the `gEH.WIDESCREEN` flag or APT widescreen work for this game. The one possible lead is that PS2/GC may have had the flag live, but no source confirms it.
5. Modding scene: thin. EA BIG/VIV archive tools exist and name FFNY. Texture packs exist for Dolphin and PCSX2. The APT toolchain that exists (jonwil's `apt2xml`) targets later EA titles (C&C3/RA3) and its author says it does not work on some other EA titles, so FFNY compatibility is unknown.

---

## Q1. Did any release support 16:9 natively?

### 1.1 PS2 (FFNY and Vendetta): no native 16:9, no 480p

- PSX Data Center's NTSC-U FFNY entry (SLUS-21004) lists: 60 Hz yes, 50 Hz no, Progressive Scan no, 16:9 Option no. Source: https://psxdatacenter.com/psx2/games2/SLUS-21004.html
- The PAL entry (SLES-52507, released 2004-10-01) lists 50 Hz yes, 60 Hz no, Progressive Scan no, 16:9 no. Source: https://psxdatacenter.com/psx2/games2/SLES-52507.html
- The Japanese entry (SLPM-65907) also lists 16:9 as unavailable. Source: https://psxdatacenter.com/psx2/games2/SLPM-65907.html
- Vendetta PS2 entries (NTSC-U SLUS-20639, PAL SLES-51479, JP SLPS-25275) all list 16:9 = No. Sources: https://psxdatacenter.com/psx2/games2/SLUS-20639.html , https://psxdatacenter.com/psx2/games2/SLES-51479.html , https://psxdatacenter.com/psx2/games2/SLPS-25275.html
- The PCSX2 thread about FFNY says the game "isn't meant to run in 16:9 without a widescreen patch" (a user reply, not authoritative). Source: https://www.resetera.com/threads/pcsx2-users-help-me-with-this-weird-ghosting-lines-issue.165478/
- Confidence: medium-high. The database fields come from box and disc data (not from game testing), but they match the fact that the community had to write widescreen patches. I found no manual or review contradicting this.

### 1.2 GameCube (FFNY and Vendetta): no native 16:9 evidence

- The Dolphin wiki page for FFNY (IDs GNWE69, GNWP69) says nothing about widescreen or aspect ratio. It documents only VP6 video artifacts (fix: Manual Texture Sampling) and a non-fatal "Unknown Format Invalid (5)" popup. Source: https://wiki.dolphin-emu.org/index.php?title=Def_Jam:_Fight_for_NY
- The Dolphin wiki page for Vendetta (GDTE69, GDTP69) lists no problems and no widescreen notes. Source: https://wiki.dolphin-emu.org/index.php?title=Def_Jam_Vendetta
- Dolphin's shipped per-game settings for FFNY (`GNWE69.ini`) and Vendetta (`GDTE69.ini`) contain only ActionReplay cheats (health, cash and so on) and no widescreen patch or Gecko section. Sources: https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Data/Sys/GameSettings/GNWE69.ini , https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Data/Sys/GameSettings/GDTE69.ini
- On real hardware, an open Swiss (GameCube loader) issue from 2026-02-28 shows that Swiss's generic "3D widescreen patch" makes FFNY characters look "too skinny". This implies the game has no native 16:9 mode and that a generic patch is being applied. The issue has no maintainer response. Source: https://github.com/emukidid/swiss-gc/issues/1046
- Wikipedia's platform-difference summary for the GC version lists reduced voices, missing music tracks, no blur effect and smaller crowds, and says nothing about display modes. Source: https://en.wikipedia.org/wiki/Def_Jam:_Fight_for_NY
- I found no GameCube 480p or 16:9 status for either game. The ConsoleMods GameCube display-mode list exists (https://consolemods.org/wiki/GameCube:Games_with_Alternate_Display_Modes) but I could not open it and the search excerpt did not include Def Jam.
- Confidence: medium. Absence of any widescreen mention in Dolphin sources is weak evidence; the Swiss issue is the strongest sign.

### 1.3 Xbox (FFNY): 480p yes (one source), 16:9 no, 720p/1080i no

- ConsoleMods' "Xbox: Games with Alternate Display Modes" has a row for "Def Jam: Fight for New York" with values "No | Yes | No | No". Two independent search-tool readings interpreted these as 16:9 = No, 480p = Yes, 720p = No, 1080i = No. I could not load the page (Cloudflare challenge), so the column order is not directly verified. Source: https://consolemods.org/wiki/Xbox:Games_with_Alternate_Display_Modes
- The Neowin roundup of 720p/1080i Xbox games does not appear to include FFNY (not directly verified). Source: https://www.neowin.net/news/these-are-the-games-you-can-play-in-720p-and-1080i-on-the-original-xbox
- The Xbox Patch Hub lists FFNY title IDs (PAL 45410049 v2, USA 45410049 v1, USA-JPN 45418067 v32) but, in the first 100k characters I read, no 480p/720p/widescreen patch for it. Source: https://www.jayxbox.com/Retail-Game-Modification/PatchHub.php
- What the Xbox did when the dashboard was set to widescreen: I found no source stating this for FFNY. General Xbox behavior (my inference, not sourced): a game that does not support 16:9 ignores the dashboard setting and outputs a 4:3 picture, which the TV pillarboxes or stretches.
- The Xbox build has `gWidescreenMode = "off"` hard-coded (our own finding), consistent with no widescreen option.
- Confidence: low-medium for the Xbox display capabilities, because only one list covers them and its columns are unverified.

### 1.4 PSP "The Takeover" (2006)

- No source found stating its aspect ratio or any widescreen option.
- General note (not game-specific): the PSP screen is natively 16:9 (480x272), so a handheld port is inherently 16:9 on its own screen. This is a hardware fact, not evidence about how the game's UI was built. Reference for the hardware: https://en.wikipedia.org/wiki/PlayStation_Portable
- Reviews I reached say only that graphics are "not as sharp as the PS2" and give no HUD, camera or aspect detail. Source: https://www.playstationlifestyle.net/?p=52924
- Possible use: the PSP build is the only FFNY-family release whose UI had to be laid out for 16:9. If its UI assets could be inspected, it might show how EA/AKI handled a wider layout. This is a suggestion, not a finding.
- Confidence: low (essentially unresearched in reviews).

### 1.5 Where the `gWidescreenMode` / `gEH.WIDESCREEN` flag might have been used

- I found no web source mentioning either identifier (searched `gWidescreenMode` with EA/APT/Flash). The flag could be shared EA frontend code, a leftover, or live on another platform. No evidence either way.
- Not found: any manual, interview, or review stating a widescreen option in FFNY on any platform.

---

## Q2. Existing community widescreen patches

### 2.1 PCSX2 (PS2)

All three FFNY regions have a patch in the official PCSX2 patch repository, as do Vendetta NTSC-U, PAL and JP. Filenames (from the repo file tree, `patches/` folder) are in the form `SERIAL_ELFCRC.pnach`. Repo: https://github.com/PCSX2/pcsx2_patches

| Game | Region | Serial | File | Author line |
|---|---|---|---|---|
| FFNY | NTSC-U | SLUS-21004 | `SLUS-21004_4538436F.pnach` | none |
| FFNY | PAL | SLES-52507 | `SLES-52507_873F8ED6.pnach` | ElHecht |
| FFNY | NTSC-J | SLPM-65907 | `SLPM-65907_B2EC9660.pnach` | ElHecht (NTSC-J by Arapapa) |
| Vendetta | NTSC-U | SLUS-20639 | `SLUS-20639_4042248C.pnach` | none |
| Vendetta | PAL | SLES-51479 | `SLES-51479_7B8477F9.pnach` | El_Patas |
| Vendetta | NTSC-J | SLPS-25275 | `SLPS-25275_DBD0E172.pnach` | (file exists, not read) |

Raw URLs follow the pattern https://raw.githubusercontent.com/PCSX2/pcsx2_patches/main/patches/ plus the file name. The repo history for the FFNY NTSC-U file shows a 2023-06-01 "Rename patches based on forum compat list" commit by a PCSX2 maintainer (a rename, not the original authorship).

What the FFNY patches do:

- All set `gsaspectratio=16:9` (PCSX2 then outputs a wide image) and rewrite a few EE instructions.
- PAL (SLES-52507) is the most readable, with comments from the author:
  - Seven word patches at `0x0021e97c`, `0x0021e990`, `0x0021e9a8`, `0x0021e9c0` ("hor fov"), `0x0021ea78`, `0x0021ebb0` ("renderfix left") and `0x0021ebb4` ("renderfix right").
  - The FOV edits replace `0x00000000` (nops) with a float-load of `0x3f40...` (a 0.75 constant, i.e. 3/4) and a multiply. In effect the horizontal projection scale is multiplied by 0.75, which keeps vertical FOV and widens the view (Hor+). This reading is my inference from the instruction encodings.
  - The "renderfix left/right" stores replace `e6000158`/`e600015c` (stores of VU0 registers) with different register stores. This looks like it fixes the cull or clip extents so geometry is not discarded at the new left and right edges. This is an inference; the author only says "renderfix".
  - A separate "menu fix" at `0x0021f164`..`0x0021f1ac` applies the same 0.75 horizontal factor ("hor fov menu fix") to the menu-screen camera code, with the original values (`3c013f80`, `3c014000`, `3c01bf80`) recorded in comments.
- NTSC-J (SLPM-65907) is the same patch at different addresses (`0x0021c82c` through `0x0021d05c`).
- NTSC-U (SLUS-21004) is a different implementation. It replaces about 30 words from `0x00361a34` to `0x00361ad8` with a rewritten float routine. The constants are `0x3fab0000` (about 1.336, which is roughly 4/3) and `0x3e800000`/`0x3f000000` (0.25 and 0.5). It looks like a function that scales horizontal viewport/projection parameters. The address `0x00361a9c` carries the comment "optional zoom for cutscenes": the default patch writes `0x00000000` there, and `0x46010002` (a multiply) can be swapped in. I read this as an optional zoom to hide the 4:3 FMV/cutscene framing, but the author does not say so.
- Same OPL/cheat-device versions exist in the PS2-Widescreen project: https://github.com/PS2-Widescreen/OPL-Widescreen-Cheats (files `CHT/SLUS_210.04.cht`, `CHT/SLES_525.07.cht`, `CHT/SLPM_659.07.cht`). They carry the same hacks and credit ElHecht. The PAL file annotates the lines "hor fov", "renderfix left/right" and "menu fix". The NTSC-U file's header is "wide" with mastercode `903559e8 0c0d5622`.
- Neither PCSX2 patch contains HUD repositioning or FMV resizing lines.

Vendetta patches (much smaller):

- NTSC-U: two patches, at `0x001a17b4` (writes `3c013f10`) and `0x00197bbc` (writes `3c013f22`), flagged "Widescreen Hack".
- PAL (El_Patas): `0x001A1D24` writes `3C013F10` over `3C013F40` ("Increases hor. axis") and `0x00198154` writes `3C013F22` over `3C013F00`. These are float constants, i.e. a hor+ scale (0.5625 is `0x3f10`), similar in spirit to FFNY.
- Sources: https://raw.githubusercontent.com/PCSX2/pcsx2_patches/main/patches/SLUS-20639_4042248C.pnach , https://raw.githubusercontent.com/PCSX2/pcsx2_patches/main/patches/SLES-51479_7B8477F9.pnach

Reported problems:

- ResetEra thread on FFNY in PCSX2: ghosting/bloom ("glow") and faint lines along the bottom and right screen edges, also in cutscenes. Replies suggest skipdraw/hardware hacks and note that the game is not meant for 16:9 without a patch. The thread is unresolved and is about general rendering, not the patch. Source: https://www.resetera.com/threads/pcsx2-users-help-me-with-this-weird-ghosting-lines-issue.165478/
- Search-tool summaries of PCSX2 forum posts (thread "PCSX2 - Widescreen Game Patches", https://forums.pcsx2.net/Thread-PCSX2-Widescreen-Game-Patches?page=237) reported a post by a user about a "partial HUD fix" and a note that zooming out may cause render errors. I could not open the page, and a second search did not find the post again. Treat the "partial HUD fix" claim as unverified.
- The PCSX2 wiki page https://wiki.pcsx2.net/Def_Jam:_Fight_for_NY was reported (second-hand) to list a widescreen patch and a test at 1440p with no issues. I could not open it. Unverified.
- Confidence for the pnach contents: high (read verbatim). Confidence for behavior (stretched HUD, culling, FMV): low; no first-hand reports found.

### 2.2 Dolphin (GameCube)

- No FFNY or Vendetta widescreen Gecko/AR code was found in Dolphin's own game settings (see 1.2), and a third-party Gecko collection (https://github.com/hakami55/Gecko-Codes-for-Dolphin) exposes only a README at top level and I found no Def Jam entry.
- A SweetFX/ReShade listing recommends running FFNY in Dolphin with `wideScreenHack = True` and `AspectRatio = 3` in the game's ini. In older Dolphin builds `AspectRatio = 3` meant "stretch" (my recollection; not sourced). Source: https://sfx.thelazy.net/games/game/2382/
- A Dailymotion clip shows FFNY in Dolphin 4.0 forced to 4:3 on a 1080p display. Source: https://www.dailymotion.com/video/x1k1k3f
- The Swiss "3D widescreen patch" issue (see 1.2) is the only real-hardware report: characters look too skinny, meaning the generic patch stretches rather than widens the FOV correctly.
- Not found: a game-specific Dolphin Hor+ patch and any report on the HUD with Dolphin's generic widescreen hack.
- Confidence: low (sparse).

### 2.3 xemu / Xbox

- xemu does not run FFNY cleanly. PR #2568 ("nv2a: Implement PTIMER alarm interrupts", merged 2026-08-27) improved but did not fix issue #1124: the game reaches "Press start" and menus, with random freezes, stutter and cutscene hangs. The PR discussion has nothing on resolution or widescreen. Source: https://github.com/xemu-project/xemu/pull/2568
- I found no Xbox widescreen patch for FFNY (no xemu patch, no XBE patch).
- Other recompilation projects (not the project this report is for) say:
  - `R-drg/defjam-sw` (Switch/Linux) keeps the Xbox 4:3 image by default and offers `RECOMP_WIDESCREEN=1` to stretch the 4:3 picture to 16:9. The README says nothing on HUD handling. Source: https://github.com/R-drg/defjam-sw
  - `comoria08/defjam_recomp` (Windows) has a settings window with a 4:3 or 16:9 choice. Details not read. Source: https://github.com/comoria08/defjam_recomp
  - A decompilation repo exists (`Cwf1997/defjamfightforny`), but its README is only a short description. Source: https://github.com/Cwf1997/defjamfightforny
- None of these documents how the 3D FOV or HUD are widened.

---

## Q3. Cameras, cutscenes and HUD

Cameras

- Reviews uniformly criticize the camera. WorthPlaying's Xbox review: crowd members can block the view, players sometimes button-mash blind, and in four-fighter matches the camera pulls far back so fighters are small and easy to lose. Some blocking objects go transparent, but not always. Source: https://www.worthplaying.com/article/2004/10/20/reviews/20007-xbox-review-def-jam-fight-for-new-york/
- Wikipedia notes that camera and frame rate were the main criticism across all consoles. Source: https://en.wikipedia.org/wiki/Def_Jam:_Fight_for_NY
- An Engadget review roundup mentions "camera issues" and spotty framerate in passing. Source: https://www.engadget.com/2004-09-20-def-jam-fight-for-ny-review.html
- Multiplayer: 2 to 4 combatants per fight (VideoGamer, https://www.videogamer.com/?p=163999); Battle mode supports up to four players (PSX Data Center PAL entry).
- Not found: any community note on how the wider FOV changes the 1v1 or four-fighter framing, or about off-screen fighters. The only widescreen-specific camera information is the PS2 patch's cull/"renderfix" edit and the optional cutscene zoom.

Cutscenes

- WorthPlaying says cutscenes look good and lip sync matches closely. It does not say whether they are in-engine or pre-rendered (same review URL as above).
- The Dolphin wiki says pre-rendered videos use the VP6 codec (GameCube build), which creates artifacts in emulators. So at least some content is video. Source: https://wiki.dolphin-emu.org/index.php?title=Def_Jam:_Fight_for_NY
- The optional cutscene zoom in the SLUS-21004 pnach implies story scenes render through the 3D camera path and need a framing fix when widened (my inference).
- Not found: whether videos are letterboxed, whether FMVs stretch or pillarbox under widescreen patches. One search result about PCSX2 FMV resizing was about a different game.

HUD

- Health bar: two stacked layers, top "consciousness" (drops fast and can recover) over "physical wellness". It turns fully red when consciousness is very low. Momentum meter: filled by landing moves, countering and taunting, rate depends on the Charisma stat. When full, the fighter activates "Blazin'". Source: https://handwiki.org/wiki/Software:Def_Jam:_Fight_for_NY
- That source does not describe positions or layout. I found no HUD diagram, no description of per-player placement in 4-fighter mode, and no first-hand report of the HUD stretching in widescreen.
- Confidence: HUD layout and widescreen behavior are essentially unresearched in public sources; any statement beyond the above would be speculation.

---

## Q4. Modding scene and tools

Archives

- FFNY uses EA `.big` and `.viv` archives (BIGF/BIGH header, mixed endianness, optional Refpack compression), documented in the Reverse Engineering Wiki, which lists FFNY explicitly and names tools (QuickBMS scripts, Game Extractor, MultiEx Commander, Final BIG Editor, big4f, libbig, bigdecoder.py, `ea_big4.bms`). Source: https://rewiki.miraheze.org/wiki/EA_BIG_BIGF_Archive
- `Aleksei-Miller/vivtool` (Qt GUI to edit Viv/Big files) lists FFNY as supported. The repo was archived on 2026-05-08. Source: https://github.com/Aleksei-Miller/vivtool
- A ResHax thread on enabling subtitles in the PAL Xbox build describes extracting `subtitles.viv`, hex-editing `sc00.bin`, and patching `default.xbe` bytes (a small, language-related patch). It has no UI/APT content. Source: https://reshax.com/topic/17949-def-jam-fight-for-ny-xbox-help-activate-subtitles/
- An HCS thread covers extracting `.mus` audio from VIV containers. Source: https://hcs64.com/mboard/forumlong.php?showthread=62384
- The PSP build uses `.pak` archives with a "PAKN" header and has a QuickBMS script (ResHax thread). A Python tool for a Spanish translation patch of The Takeover also exists. Sources: https://reshax.com/topic/1147-psp-def-jam-fight-for-ny-the-takeover-pak-files/ , https://github.com/MODZARF/Def-Jam-Fight-For-NY-The-Takeover---Traduccion-al-Espanol-PSP-

APT (UI) tooling

- EA APT UI files are covered by `apt2xml`/`xml2apt` (jonwil), which handle C&C3 and Red Alert 3 UI files. The author states Battle for Middle-earth II and C&C4 files will not work because of format differences. So APT has multiple variants and the 2004-era FFNY variant is unproven. Sources: https://www.ppmforums.com/topic-60956/i-updated-my-apt-tools-to-fix-bugs-and-make-them-work/ , https://github.com/jonwil/aptdump (also https://github.com/jonwil/cc3tools)
- The aptdump README is empty and the repo has only source. A thread note says apt2xml now writes the "screen size" values to XML, which suggests APT carries a screen-size field worth checking in FFNY's files.
- Not found: any tool or write-up that opens FFNY's own APT/Flash UI files, and any widescreen UI work for APT in any EA game.

Texture and roster mods

- HD texture packs: Dolphin forum thread (netus01, 2017-10-12, ~5x upscales, characters and venues, install into `Textures/GNW`; the administrator raised copyright concerns about reuse of PS2 textures). Source: https://forums.dolphin-emu.org/Thread-def-jam-fight-for-ny-hd-textures
- PCSX2 HD texture video demos exist (8x native, skipdraw 11-11 used to fix ghosting). Source: https://steamcommunity.com/sharedfiles/filedetails/?id=2992750212
- Reskin/roster work appears only as social posts (TikTok, YouTube, Odysee) with no documented tools. I did not link them.
- A Hidden Palace wiki page documents two August 2004 prototype builds (a PS2 build dated 2004-08-09 23:42, "Beta 0.5", and an Xbox build dated 2004-08-09 06:27). The page had no file-format or widescreen notes. (I am deliberately not linking it, because the site hosts the prototype disc images.)
- The Cutting Room Floor page for this game returned nothing useful when fetched, so I cannot say whether it documents debug or unused content.
- Confidence: low-medium. The scene is small and mostly emulator-texture based.

---

## What I could NOT find

- Any statement (manual, review, interview, forum) of a native widescreen option on any platform, or any observation of `gWidescreenMode`/`gEH.WIDESCREEN` outside our own analysis.
- How the Xbox behaves with a 16:9 dashboard setting for this game, and a verified read of the ConsoleMods Xbox/GameCube rows (Cloudflare blocked).
- Whether the GameCube build supports 480p or has any anamorphic mode.
- The PSP version's aspect handling or any UI details.
- Any report describing FFNY HUD stretch, HUD placement in 4-fighter mode, or FMV letterboxing under widescreen patches.
- Any Dolphin Gecko/AR widescreen code for FFNY or Vendetta, and any xemu or XBE widescreen patch.
- Any tool that reads FFNY's APT UI files, or any APT widescreen modding guide.
- The PCSX2 wiki page and the PCSX2 forum "partial HUD fix" post were not directly readable. Claims about them are second-hand.
- Fandom wiki pages (HUD/cut-content) could not be fetched.

## Source list (all URLs used)

- https://psxdatacenter.com/psx2/games2/SLUS-21004.html
- https://psxdatacenter.com/psx2/games2/SLES-52507.html
- https://psxdatacenter.com/psx2/games2/SLPM-65907.html
- https://psxdatacenter.com/psx2/games2/SLUS-20639.html
- https://psxdatacenter.com/psx2/games2/SLES-51479.html
- https://psxdatacenter.com/psx2/games2/SLPS-25275.html
- https://github.com/PCSX2/pcsx2_patches
- https://github.com/PS2-Widescreen/OPL-Widescreen-Cheats
- https://wiki.dolphin-emu.org/index.php?title=Def_Jam:_Fight_for_NY
- https://wiki.dolphin-emu.org/index.php?title=Def_Jam_Vendetta
- https://github.com/emukidid/swiss-gc/issues/1046
- https://sfx.thelazy.net/games/game/2382/
- https://consolemods.org/wiki/Xbox:Games_with_Alternate_Display_Modes
- https://www.jayxbox.com/Retail-Game-Modification/PatchHub.php
- https://github.com/xemu-project/xemu/pull/2568
- https://github.com/R-drg/defjam-sw
- https://github.com/comoria08/defjam_recomp
- https://www.worthplaying.com/article/2004/10/20/reviews/20007-xbox-review-def-jam-fight-for-new-york/
- https://www.resetera.com/threads/pcsx2-users-help-me-with-this-weird-ghosting-lines-issue.165478/
- https://rewiki.miraheze.org/wiki/EA_BIG_BIGF_Archive
- https://github.com/Aleksei-Miller/vivtool
- https://reshax.com/topic/17949-def-jam-fight-for-ny-xbox-help-activate-subtitles/
- https://www.ppmforums.com/topic-60956/i-updated-my-apt-tools-to-fix-bugs-and-make-them-work/
- https://github.com/jonwil/aptdump
- https://forums.dolphin-emu.org/Thread-def-jam-fight-for-ny-hd-textures
- https://handwiki.org/wiki/Software:Def_Jam:_Fight_for_NY
- https://en.wikipedia.org/wiki/Def_Jam:_Fight_for_NY
