# Adding an ONLINE entry to the game's own main menu: front-end research

Read-only research against the user's own dump (`<data>\extracted`). Nothing was built or run, no repo file
other than this report was touched, and no game bytes are copied here: only names, offsets, sizes, hashes
and short paraphrases. All parsing was done with throw-away Python kept outside the repo (section 9), plus the
repo's own read-only `scripts/refpack.py`, `scripts/big-entry.py`, `scripts/apt-dump.py` and
`scripts/hd_assets.py` for cross-checks.

Tags: **[V]** verified by reading the data or the lifted code, **[I]** inferred (reasoning given),
**[U]** unknown, needs a run or more work.

---

## 0. Summary and recommendation

1. **Format.** Each screen is an EA **APT** movie (compiled Flash, the family OpenSAGE documents): `.apt`
   (movie, "Apt Data" magic), `.const` ("Apt constant file"), `.o` (an EAGL geometry object, ELF32 relocatable)
   and `.xsh` (SHPX texture bank). Each screen is a nested BIGF archive inside a **RefPack**-compressed entry of
   `screens/screens.viv`. [V]
2. **The main menu is hard-coded bytecode.** `screens/intMain/mainMenu.big`: a 9-slot, 3-column grid whose
   array of clip names is built in one frame action. Choosing a slot calls
   `transitionStart("<screen>.swf")` (feloader), which asks the game `Control.GetScreenInfo(...)` (feflow.xml)
   and loads the screen. [V]
3. **Visible labels are baked textures, not text.** Each slot has title/subtitle/description images, in two
   language variants (EN and FR frames). There is no menu-label text table to edit. [V]
4. **There is a vacant slot.** Slot index 6 (bottom-left of the grid) is `null` in the array, has no
   `k...Mode` constant, and its title/subtitle/description frames are empty. The door-logo clip even carries
   frame labels `mmOnline` and `mmCreatePlayer` that show nothing. Earlier layouts in `test/mnMnTest.big` and
   `intMain/demoMenu.big` had `mmOnline` in slot 2, so ONLINE was designed in and removed. [V]
5. **ONLINE artwork exists in the dump**, unused by the shipped menu: a 128x128 "ONLINE" poster
   (`intMain/support/mainMenuButtons.big` and `test/mnMnTest.big`, identical content) and a lightning-fist
   icon (`intMain/support/mainMenuLogosIcons.big`). [V]
6. **No leftover online screens or modules.** `online/chsChar.swf` is only a name in the XBE's screen table (no
   file, no feflow node); the EA-Online (EASN/EASO) ActionScript framework in `main.big` and `menu/scrMenuMain.big`
   is dead on this build; the XBE has no network or XOnline code. `gconfig.xml` lists "5 - Online FE" as a
   build type but the game code only special-cases build types 2, 3 and 4. [V]/[I]
7. **Recommendation: (a)+(c).** Fill slot 6 with a small, hash-gated, append-only patch of the
   `mainMenu` entry (data patch plus about 150 bytes of new bytecode, applied by Setup or by an I/O overlay,
   never distributed), make the slot talk to the port through `FuncCall("Online.*")` strings that
   `funccall_hook` already sees, and draw the lobby, room and venue-vote screens **natively** with the game's
   own TTFs and textures read from the player's files at run time. Authoring new APT screens is not
   realistic (needs EAGL `.o` geometry). Section 5 has hook points, section 7 a staged plan with the
   experiments that remove each [U].
8. **Cheaper fallback (b):** retarget the HIGH SCORES slot (a 4-byte `.const` pointer change, or the same
   string patched in guest memory) and re-skin its textures; costs the High Scores screen.
9. **Effort (rough):** patch tool and validation 3-5 days; FuncCall bridge, input gating and overlay shell
   2-3 days; label/door art 1-2 days; each native screen 2-4 days. Main risk is not the format (decoder
   validated, section 2.3) but unproven VM behaviour of the new bytecode and the guest's acceptance of a
   re-packed entry, both [U] until one run.

---

## 1. How the front end is stored (Q1)

### 1.1 Containers [V]

| Level | What | Evidence |
|---|---|---|
| `screens/screens.viv` | BIG4 archive, 96,883,691 bytes, 8,795 entries; directory is (BE offset, BE size, NUL-terminated name); header holds LE total size, BE count, BE directory size 439,713 | parsed; matches `scripts/big-entry.py` |
| each entry | **RefPack** (signature 0x10FB, 3-byte BE decompressed size). **All 8,795 entries** start with it, none stored raw | scanned |
| decompressed | a nested **BIGF** archive of 3-4 files (screens) or art-only containers (portraits, thumbs) | e.g. mainMenu: 767,600 B -> 2,088,960 B |
| inner files | stored uncompressed | offsets inside the BIGF |

Breakdown of the 8,795 entries: 4,790 `shopThumbs`, 1,727 `portrait`, 1,384 `sketchAssets`, 568 `match`, 170
`thumbs`, and 155 screen/art/support `.big` files (battle 28 plus 3 support, story 24, `_art` 23, intMain 10 plus 3
support, options 9, test 8, trans 4, memoryCard 4, extras 4, venuethumbs 27, root 10, others) plus `feflow.xml`.
I unpacked all 155 of those `.big` entries: all 155 have `.apt` and `.const`, 144 have `.o`, 131 have `.xsh`. [V]

A **literal-only RefPack stream** (no compression, about 1% overhead) round-trips through the repo's
`refpack.decompress` for `mainMenu.big` (2,107,618 bytes for the 2,088,960-byte payload). So a patcher needs no
compressor. Whether the *guest's* decoder accepts it is [U] (standard algorithm, so very likely).

### 1.2 The four inner files, by name

Example `screens/intMain/mainMenu.big` (archive entry offset 0xBC7658, size 767,600):

| Inner file | Offset/size in BIGF | Format |
|---|---|---|
| `mainMenu.apt` | 128 / 73,796 | ASCII `Apt Data`, then little-endian structures; every internal pointer is an absolute offset from the start of the `.apt` stream (no relocation needed to read it) |
| `mainMenu.const` | 73,984 / 4,811 | `Apt constant file`; header gives the movie offset (0x4EC) and a typed item table (type 1 = string pointer); 242 items |
| `mainMenu.o` | 78,848 / 203,329 | **ELF32 little-endian relocatable**, machine field 8 (MIPS), one `.data`, 4,490 relocations, 573 symbols. Symbols are **EAGL** records: `__Model:::<shapeId>`, `__VB:::`, `__RM:::`, `__EAGL::GeoPrimState` (blend, strip), `__EAGL::TAR ... SHAPENAME=<n>,1` (the texture a model uses, by SHPX entry name) |
| `mainMenu.xsh` | 282,240 / 1,806,720 | `SHPX`: magic, LE total size, count (120), tag `G355`, directory of (4-char name, LE offset), then 16-byte image headers + pixels |

SHA-256 (of decompressed data, for patch gating): entry raw `71bec380...9e29`, payload `b3619d08...3263`,
`.apt` `d1183e66...0549`, `.const` `62ef9658...c0a9`, `.o` `3141dbf8...5df9`, `.xsh` `f6fefe97...f889`.
(Full values are in section 9; they identify, they do not reproduce.)

### 1.3 APT structure (matches OpenSAGE, with EA specifics) [V]

* Movie (type 9) at the offset in the `.const` header: frame count/ptr, character count/ptr (a pointer table),
  screen size (640x480 here), 16 ms/frame. Characters: Shape 1, EditText 2, Font 3, Button 4, Sprite 5,
  Image 7, Morph 8, Movie 9, StaticText 10; each starts with a type word and the signature 0x09876543.
  **Image characters carry a texture number = the SHPX entry name** (character 100 -> entry "100").
  Shape characters carry a geometry id = `__Model:::<id>` in the `.o`.
* Frames are (item count, ptr) pairs of item pointers. Item types: 1 Action (ptr to bytecode),
  2 FrameLabel, 3 PlaceObject (0x40 bytes: flags, depth, character, 2x2 matrix, translation, colour, name ptr,
  clip-depth, clip-actions ptr), 4 RemoveObject, 5 BackgroundColor, 9 InitAction. Clip actions are
  (event mask, key, bytecode ptr) triples.
* **Bytecode** is Flash AS1 plus EA's compact opcodes (names as in OpenSAGE and `scripts/apt-dump.py`):
  `ConstantPool` (0x88) lists indices into the `.const` table and later 1-byte operands index that
  *pool*; operands of opcodes >= 0x80 are aligned to 4 bytes (the alignment is on the absolute address, so a
  4-aligned copy of a block keeps it); branch offsets are bytes, relative to the end of the branch
  instruction; `DefineFunction` (0x9B) = name ptr, parameter count, parameter-name ptr, body size, the marker
  words 0x98765432 0x12345678, then the body inline (a nested `DefineFunction` body is skipped by size).
  EA opcodes seen: A1 PushString (inline), A2/A3 PushConstant byte/word, A4-A7 string var/member access,
  AE PushValueOfVar, AF GetNamedMember, B0-B3 CallNamedFunc/Method (Pop variants), B4 PushFloat (f32),
  B5 PushByte, B6 PushShort, B7 PushLong, 59/5A PushZero/PushOne (used as argument counts), 5B-5E
  CallFuncPop/CallFunc/CallMethodPop/CallMethod, 70 `this`, 73/74 true/false, 75/76 null/undefined.
  `GetURL2` (0x9A) has **no operand byte** here.
* **Validation.** My independent decoder (operand sizes as above) parses **2,485 of 2,485** `DefineFunction`
  bodies in all 155 screens to exactly their declared size. That is the strongest evidence that the operand
  table is right. It agrees with the repo's `scripts/apt-dump.py` table. Names for 73-76 follow that table
  (an early guess of mine had true/false swapped; the code only makes sense as 73 = true, 74 = false).
  Residual [I]: exact semantics of a few rare opcodes (5B/5C, 5E, 0x72).

### 1.4 Textures and fonts

* `.xsh` image header: byte 0 is the format (0x60 DXT1, 0x61 DXT3, **0x62 DXT5**, 0x7B 8-bit palettised with a
  0x2A palette record, 0x7D BGRA), u16 width and height at +4/+6, flags at +12 (0x2000 swizzled). All 120
  textures of `mainMenu.xsh` are 0x62 (DXT5, 1 byte/pixel, entries 256-byte-aligned). `scripts/hd_assets.py`
  (`shpx_images`, `archive_entries`, `unpack_refpack`, `dxt_image`) already parses all of this; D3D11 takes
  the DXT5 blocks as BC3 unchanged. [V]
* Fonts: `fonts/fonts.viv` (BIGF, 10 RefPack-wrapped **TrueType** files) feed the APT EditText fields (EA
  FontFusion, XBE string `APT::FontFusion`); `fonts/befonts.viv` holds 14 `FntX` bitmap fonts for the in-game
  2D layer. [V]
* Text database: `assets/misc.viv` entries `cmusen.loc`, `cmuken.loc`, `cmfren.loc` (3,068 UTF-16 strings
  each: SELECT, BACK, USER SETUP, ...), `mcusen.loc`, `mcfr.loc`; `assets/fetext.loc` has 34 strings (match
  type names and blurbs). These are keyed by a name hash I did not resolve ([U]) and are not used for the main
  menu labels. [V]

---

## 2. How the main menu defines and runs its items (Q2)

Screen: `screens/intMain/mainMenu.big` (feflow node `intMain/mainMenu`, screen-table id 15).
Movie 640x480, 4 root frames, 227 characters (120 Image, 65 Shape, 40 Sprite, 1 Morph). Everything below is in
`mainMenu.apt`; offsets are `.apt` offsets.

### 2.1 Where the logic lives [V]

* Root frame 0 places the `mainMenu` timeline clip (character 222, 73 frames). Its **frame-0 action, 0x10CF8
  to 0x1193D**, is the whole menu program (167-entry constant pool). Its action item is at **0xD3C8**.
* Inside it: the class `CMenu` (constructor 0x10D04, prototype methods `resize`, `print`, `grayOut`,
  `swapItem`, `replaceObject`, `resetIndex`, `getIndex`, `setIndex`, `findNext`, `getNumColumn`, `getLastX/Y`,
  `getLastIndex`, `getLastEvent`, `keyEvent`), the language switch `raster` (0x10D60), the highlight handler
  `selectPoster` (0x10E18), the pad handlers `keyDown/keyUp/keyLeft/keyRight` (0x10EEE-0x10FC7), **`keyX`
  (0x1100F, body 0x11028, size 413)**, `keyBack` (0x111C5), `keyOption1`, and the build of the menu at
  0x118BD-0x1193B.
* `CMenu.keyEvent` moves the cursor over a 3-column grid and skips `null` entries (so a `null` slot can never
  be reached); at its end it calls the callback named by the string passed to the constructor
  (`"selectPoster"`) with the highlighted array element. [V] (the call opcode is 0x5B, a name-on-stack call;
  its exact scoping is [I])

### 2.2 The items

```
aMenuArray = [oStory, oBattle, oRewards, oOptions, oJukebox, oExtras, null, oHighScores, oCredits]   // 0x118FB
oMenu      = new CMenu(aMenuArray, "", "selectPoster", 3)                                            // 3 columns
kStoryMode=0 kBattleMode=1 kRewardsMode=2 kOptionsMode=3 kJukeBoxMode=4 kExtrasMode=5 kHighScoreMode=7 kCreditsMode=8
```

Each `oX` is the **name of a clip** (`"mmStory"`, `"mmBattle"`, `"mmRewards"`, `"mmOptions"`, `"mmJukebox"`,
`"mmExtras"`, `"mmHighScores"`, `"mmCredits"`). The `null` is at 0x118EB. There is **no `kOnlineMode`** and
no constant 6. [V]

The `mm*` clips are eight placements in character 16 (the `mainMenu` art clip), frame 0, all using character 1,
an empty one-frame sprite: invisible hit targets whose `_x/_y` tell the spotlight where to glide.
Positions (x, y): Story (-56,-71), Battle (5,-75), Rewards (75,-71), Options (-60,15), Jukebox (1,7),
Extras (65,15), HighScores (-4,96), Credits (62,101). Row 3, column 1 (about -68,100) is free. Character 16's
frame table is at 0x2064: frame 0 is (13 items, item array at 0x2E14), made of three art placements, the
spotlight, the spotlight rim and the eight `mm*` placements. [V]

### 2.3 What is drawn for the selected slot [V]

`selectPoster(name)` does: play the scroll sound (FuncCall `Audio.Play(3)`), `door.doorLogos.gotoAndStop(name)`
(door logo selected by **frame label**), start the spotlight glide to `mainMenu.<name>`, and
`doorText.{title,subtitle,descript}.gotoAndStop(index+1)`. Those three sprites have 9 frames, one per index
0..8, and each frame holds a sprite with two language frames; `raster()` asks `FuncCall("Game.GetLanguage()")`
and shows variant 1 for languages 1-2 and variant 2 for language 3 (the three `cm*en/uken/fren.loc` files suggest
US/UK English and French). The content is **images** (the `.xsh`): cream stencil titles (STORY / SCENARIO,
BATTLE / COMBAT, UNLOCK REWARDS / LES PRIMES, OPTIONS, BEATBOX / JUKEBOX, EXTRAS / BONUS, HIGH SCORES /
MEILLEURS SCORES, CREDITS), orange graffiti subtitles, white grunge description blocks, each split into two or
three textures. So **no label is a string**. Header and footer hints use `~KEY` strings (`~MAIN_MNMENU`,
`~CMN_BACK`, ...) that the guest resolves ([U] where).

Slot 6 has nothing here: `descript` and `subtitle` frame 6 are empty/removed (the `title` sprite's frame 6 has
no items at all), the door-logo sprite has labelled frames `mmOnline` (frame 2) and `mmCreatePlayer`
(frame 7) that only remove the previous logo. [V]

### 2.4 Choosing an item [V]

`keyX(controller)` (guarded by `menuKiller != true`) plays the select sound and compares `oMenu.getIndex()`
with the constants. Each branch parks the spotlight off-screen (`_x = -300`) and calls
`_parent._parent.transitionStart("<screen>.swf")`:

| Index | Constant | Calls |
|---|---|---|
| 0 | kStoryMode | sets `_root.g_modeType`; `options/userID.swf` |
| 1 | kBattleMode | sets `g_modeType`; `battle/cmtype.swf`, then `FuncCall("Game.StartBattleMode()")` |
| 2 | kRewardsMode | `battle/buyrewards.swf` |
| 3 | kOptionsMode | `options/options.swf` |
| 4 | kJukeBoxMode | `jukebox/beatbox.swf` |
| 5 | kExtrasMode | `extras/extras.swf` |
| 7 | kHighScoreMode | `hiScores/hiScores.swf` |
| 8 | kCreditsMode | `options/credits.swf` |

`keyBack` (B) goes to `intMain/preStart.swf`. Inputs reach the clip through `capturedKeyEvent` in
`feloader.apt` (0xD0CC), which names the pad code (Up, Down, Left, Right, A, X, Select, Start, Back, L1, R1,
L2, R2, Option1, Option2) and calls `<g_focusTarget>.key<Name>(controller + 1)`; the menu sets
`_level0._root.g_focusTarget = this` (0x11893). `keyX` is the confirm. The main menu has **no inactivity
timer** (only demo screens and `prestart` do). [V]

### 2.5 How that becomes a feflow transition [V]/[I]

`transitionStart(next)` (feloader.apt 0xBA23): when the front end is not "in game" it runs
`FuncCall("Control.GetScreenInfo(<next>)")`. The game's handler for that is guest `0x55490` (`sub_00055490`,
registered in the `Control` module by `sub_0006ADE0`): it looks the destination up in the **current screen's
feflow transition list** (`sub_000560F0`), strips `.swf`, and returns `tscreen=<between-screen>&type=<n>&data=<size>`
(format strings at VA 0x2960AC-0x296108). The script then loads the next movie and the "between" movie with
`GetURL2`. Type indices are swap 0, panUp 1, panDown 2, doorZoomIn 3. `intMain/mainMenu` has 9 transitions in
`screens/feflow.xml` (preStart, battle/cmtype, options/userID, hiScores/hiScores, jukebox/beatbox,
battle/buyrewards, options/credits, extras/extras, options/options). What the handler returns for a destination
that is not in the list ("NO_RETURN" strings exist) is [I]. For "in game" mode feloader carries its own
hard-coded copy of the flow (`g_aScreens`, `g_aScreenBranches`, 0xC5BE-0xCB63).

`FuncCall(cmd)` in AS is `GetURL2(cmd, "_root")`; the engine routes it to guest `0x00068BA0`, which the port already
wraps as `funccall_hook` (src/recomp_manual.c). The hook runs on the game thread before and after the original,
can skip the original, and `defjam_fe_call` can send any command (docs/research/online-match-setup.md).
There is also a game-to-script channel: an `extern` object (g_inGame, g_Language, gPlatform, g_sLoadFile,
g_bLoadRequest ...) that feloader reads; a `pageloader` clip polls `g_bLoadRequest` only when `g_inGame == 1`.

---

## 3. Leftover online / Xbox Live material (Q3)

| Where | What | Use |
|---|---|---|
| `intMain/mainMenu.big`, final array | no Online entry, no `kOnlineMode`; slot 6 is `null` | the *hole* is the hook |
| `intMain/mainMenu.big`, character 50 `doorLogos` | ten frame labels in the order Story, Battle, **Online**, Options, Jukebox, Extras, HighScores, **CreatePlayer**, Rewards, Credits (label strings at 0x10A7C-0x10AEC); frames 2 and 7 only remove the previous logo | door-logo slot exists |
| `test/mnMnTest.big` | older menu: array `[Story, Battle, Online, Options, Jukebox, Extras, -, -, HighScores]`, a placed `mmOnline` clip and `mmOnline_b` poster | design history, art |
| `intMain/demoMenu.big` | `[Story, Battle, null, Options, Jukebox, Extras, null, HighScores, CreatePlayer]` plus `aLockedList [1,1,0,1,...]`; `oOnline`/`mmOnline` constants | Online was locked out in the demo |
| `intMain/support/mainMenuButtons.big` | placements `mm*_b` incl. **`mmOnline_b`**; textures: 11 STORY, **15 ONLINE**, 19 OPTIONS, 23 JUKEBOX, 27 EXTRAS, 31 HIGH SCORES, 37 CREATE PLAYER, 42 BATTLE (all 128x128 posters) | ready-made ONLINE poster |
| `intMain/support/mainMenuLogosIcons.big` | 6 icons (256x256); **17 = lightning-fist** (the ONLINE icon) | ready-made door logo |
| `main.big` (root "main.swf", 1,446 constants) | the shared EA Sports Online shell: `gLobby`, `gEaso`, `EASO_LOBBY/LOGIN/CHALLENGE/VOIP/...` calls, a screen map to `easn/Lobby/slobby`, `easn/Lobby/sroom`, `easn/Lobby/pcreate`, `easn/xlogin/selgtag`, `easn/online/easoMnu1`, a Def Jam stub `Online.EASOplayOnlineGame(...)`, and a stub "DefJam2" server-list string | dead: none of those screens, nor an `Online` module, exist on this build |
| `menu/scrMenuMain.big` | EA-shell test menu: items "Play Def Jam", "Fake connection to server", "EA SPORTS ONLINE", strings "There is no online available" | not in feflow |
| XBE screen table (VA 0x2DFE08, ids 0..31) | id 24 = `online/chsChar.swf`, id 15 = `intMain/mainMenu.swf` | name only: no file in `screens.viv`, no feflow node; referenced by `sub_0006BC90` via `MEM32(id*4+0x2DFE08)` |
| `gconfig.xml` | `<buildtype>`: 0 Debug FE, 1 Main FE, 2 Demo FE, 3 Final FE, 5 **Online FE** (the dump has 1) | `GS+0x33C` (read at `sub_00011E40`); `sub_0006BC90` picks the root loader by it (2 `demoload`, 4 `loader`, otherwise `feloader`; `beloader` and `main` also named); several functions compare it to 3; nothing special-cases 5 in the code I sampled [I] |
| XBE | no strings `live`, `lobby`, `matchmak`, `xlive`, `network`; no XONLINE/XNET section; only the stock XDK `y:\XODash\xonlinedash.xbe` | no Live code to hook |
| FuncCall registry | modules Game, Option, Extra, CreatePlayer, Shops, Controller, Battle, MemCard, Control, Crib, Audio ... no Online | an `Online.*` module has to be ours |

Nothing is a *hidden but live* menu item; the closest is the vacant slot. There is no disabled item to enable.

---

## 4. Options for adding the entry (Q4)

### (a) Patch the player's own files at install time

* **a1. Fill slot 6 (recommended entry mechanism).** Edit `mainMenu.apt` only (data + bytecode), keep every
  other file untouched. Append-only recipe, all offsets from section 2:
  1. Write at the end of the `.apt` (4-aligned): a new PlaceObject item `mmOnline` (clone of the `mmCredits`
     item at 0x92B8, position about (-68, 100), character 1), a new 14-pointer item array, and rewrite the
     frame-0 record of character 16's frame table at 0x2064 (count 13 -> 14, pointer -> new array).
  2. Write a **modified copy of the frame-0 action of character 222** (0x10CF8..0x1193D) at the end, and
     repoint the action item at 0xD3C8. Copy as-is up to just before the final `stop()` and add (pseudo):
     ```
     aMenuArray[6] = "mmOnline";                   // the array is shared by reference with oMenu
     var keyX0 = keyX;  var sel0 = selectPoster;   // capture the originals
     function keyX(n)        { if (oMenu.getIndex() == 6) { _root.PlaySound(_root.FESFX_SELECT);
                                   _level0._root.FuncCall("Online.Open()"); } else keyX0(n); }
     function selectPoster(t){ sel0(t); _level0._root.FuncCall("Online.Focus(" + oMenu.getIndex() + ")"); }
     stop();
     ```
     Strings are inline (A1/A4/A5 string opcodes), so `.const` does not change; the only opcodes used
     already appear in the game's own scripts (SetMember on an array element, CallMethod, FuncCall, `if`).
     Because the copy keeps the original 4-alignment and branch offsets are relative, nothing inside moves; only
     the block length and the frame pointer change.
  3. Deliver: either **Setup** patches `screens/screens.viv` in place (append the new RefPack entry, rewrite
     the one directory record's offset/size and the header total size; the 96 MB archive is not rewritten) after
     checking the SHA-256 of the decompressed entry, or the **I/O layer serves the patched entry** on read (no
     file is changed; whether the toolkit's file layer allows it is [U], the toolkit submodule is not in this
     worktree).
  * Effort: a patch tool of roughly 400 lines (the decoder exists), 2-3 days to hand-assemble and validate with
    `scripts/apt-dump.py --actions` plus my decoder, then one run to see the cursor reach slot 6. Risk: medium-low.
    Unknowns: VM semantics of overriding a function by name in the same timeline [U-low]; guest decoder and
    directory handling of an out-of-order or odd-sized entry [U-low]; any checksum of the archive [U] (none seen in
    the 16-byte header).
  * Art: slot 6's door logo and text frames stay blank unless art is added. Adding a Shape needs an EAGL model in
    `mainMenu.o` plus a texture in `mainMenu.xsh` ([U], substantial), so draw the label natively (see (c)).
* **a2. Retarget an existing item** (the `.const` pointer of "hiScores/hiScores.swf", item at 0x3F0, to a new
  appended string) plus replacing the label textures' pixel data in `mainMenu.xsh` (EN: 93, 95/96 title,
  148-151 subtitle, 204-208 description; same dimensions, so a DXT5 write). Costs the High Scores screen; no
  bytecode written. 1-2 days. Also needs a feflow edit and a destination (below).
* **feflow.xml** is a plain loose text file, trivially editable (add a screen node and a transition) but a new
  *destination screen* must exist; see the screen loader below.

### (b) Intercept at run time, files untouched

* **b1. In-guest patch of the loaded movie.** Possible in principle (the `.const` strings and the `.apt` stay
  plain after loading), but the loader links characters in place (`sub_0015F950` rewrites per-type index
  tables) and I could not determine from the lifted C whether the code bytes are byte-identical in memory ([U]).
  Needs a memory-search experiment (see 7, E4) to find the block, e.g. by the clip names. Same code as a1, no
  file I/O involved.
* **b2. Redirect an item by FuncCall.** `funccall_hook` can swallow `Control.GetScreenInfo(hiScores/hiScores.swf)`
  and open a native screen. The script would still load `hiScores.swf` unless the loader is redirected
  (guest `sub_00069CC0(name)` builds `screens/%s`, then the movie-slot allocator `sub_001819C0` through the
  pointer at `MEM32(0x345238)`; `sub_001810B0` opens `<name>.apt` and `<name>.const`). Returning to the menu
  afterwards is awkward (`Game.StartMainMenu(n)`, handler `0x46610`, posts a start message to the run module
  at `GS+0x38C`; [I]). Hacky, and it still needs relabelled art.
* **b3. Re-skin by HD texture pack.** The repo's texture packs replace by content hash
  (`docs/hd-textures.md`), so the "ONLINE" label could replace the HIGH SCORES images with no archive edit.
  Source ids (v1 identity from `hd_assets.texture_id`): EN title 93 `a2b5e4b9...0266`, FR title 96
  `95d15398...1c51c`, tails 95 `294f0c09...7651` and 99 `16d9e252...fafbd`; subtitle 148 `72998790...cefb`,
  150 `2f0447a9...1720`, 151 `5dbf8323...916b`; description 204 `0f01f709...1e59`, 205 `3776659c...830f`,
  207 `34c33f07...87f`, 208 `8d45e601...aaf1`. Requires packs enabled and a pack that keeps the dimensions;
  nothing else changes.
* **b4. "Swap a label string in memory."** Not applicable: there is no label string (section 2.3).

### (c) Native screens styled like the game

Draw the lobby/room/venue-vote screens in the port's own overlay (`pc_ui_overlay`, ImGui on the D3D11 present;
Vulkan on macOS/Linux) with the game's own textures and TTFs read at run time (section 6). No APT authoring.
Needs: (1) an `Online.Open()`/`Online.Focus(i)` bridge; (2) pad gating so the still-running menu does not react
(the game takes pad input through the port's input layer, `pc_input.c`; zero its virtual pads while the overlay
is up [I]; the menu has no timers to expire [V]); (3) leaving simply closes the overlay: the AS menu never
moved, so nothing to restore; (4) the match start reuses `automatch` (`defjam_fe_call` sequence in
online-match-setup.md). Effort per screen 2-4 days. Risk low technically; "in the game's style" is a design job.
Authoring *real* APT screens instead would require writing `.o` geometry and `.xsh` banks plus bytecode; not
realistic.

### Recommendation

**a1 for the entry, c for the screens, b3 only as a cheap fallback for artwork.** Concretely:

1. `scripts/apt-patch.py` (new, local-only): input the user's `screens.viv`; check the SHA-256 of the decompressed
   `mainMenu.big` payload (section 9); build the appended blocks; emit a patched entry (literal-only RefPack is
   fine); patch the directory record. Never writes anything it did not read from the user's own files and the
   recipe contains no EA bytes (only offsets, expected hashes, and bytecode we author).
2. Port hooks (all existing infrastructure): in `funccall_hook` (guest `0x00068BA0`, src/recomp_manual.c) match
   `Online.Focus(i)` and `Online.Open()` before calling `sub_00068BA0`; skip the original; set `g_eax` to a
   small result string in guest memory if needed (the original returns `NO_FOUND` for unknown modules).
3. Overlay: draw while an `online_ui_active` flag is set; gate pad input.
4. Label art for slot 6: native overlay of the ONLINE poster/icon while `Online.Focus(6)` is the last focus,
   positioned from the `doorText` (143,81) and door (140.6,28.5) origins; revisit a real texture patch later.

---

## 5. Concrete hook points

| What | Where | Tag |
|---|---|---|
| FuncCall entry (every `FuncCall`, `GetURL2 ..., "_root"`) | guest `0x00068BA0`; wrapped as `funccall_hook` (src/recomp_manual.c ~556); dispatcher `sub_000457D0`, module table `MEM32(0x341534)` count, objects `MEM32(0x3412B0+4*i)` | [V] |
| Send a command from native | `defjam_fe_call(const char *)` (src/recomp_manual.c ~582) | [V] |
| Control module | registered by `sub_0006ADE0`; `Control.GetScreenInfo` = `0x55490`, `GetPrevScreenInfo` = `0x55570` | [V] |
| feflow lookup | `sub_000560F0` (strip `.swf`, search current screen's transitions); formats at VA 0x2960AC-0x296108 | [V]/[I] |
| Screen-name table | VA 0x2DFE08 (ids 0 `intMain/legal1.swf`, 1 `intMain/preStart.swf`, 2 `matchloading`, 3-6 `battle/matchSm1-4`, 7-9 `chooseF2-4`, 10 `battle/rematch`, 11 `story/crib`, ... 15 `intMain/mainMenu.swf`, 20 `battle/cmtype`, 23 `main.swf`, 24 `online/chsChar.swf`, 25 `options/userID`, 33 `options/credits`, 34 `battle/oldmload`); consumer `sub_0006BC90` | [V] |
| Root loader choice | `sub_0006BC90`: names at VA 0x29A670 (`main`), 0x29A678 (`beloader`), 0x29A684 (`demoload`), 0x29A690 (`loader`), 0x29A698 (`feloader`) | [V] |
| Screen load | `sub_00069CC0(name)` (`screens/%s`, VA 0x299AD8), slot allocator `sub_001819C0` via `MEM32(0x345238)`, file open `sub_001810B0` (`.apt` VA 0x2C04C0, `.const` VA 0x2C04B8, open call `sub_001E7160`); APT mount at `sub_00069F10` (string `screens/screens.viv|` VA 0x299880) | [V]/[I] |
| Movie link step | `sub_0015F950` (per-type index fix-ups), queue `sub_001480B0`, see `docs/research/m3-movie-display.md` | [V] |
| Run-module restart | `Game.StartMainMenu(n)` handler `0x46610` | [I] |
| Menu data to patch | `screens/screens.viv` entry `screens/intMain/mainMenu.big` (offset 0xBC7658, size 767,600; inner `mainMenu.apt`): action item 0xD3C8 -> 0x10CF8; `null` slot at 0x118EB; array build 0x118FB; `new CMenu` 0x1190B; `keyX` 0x1100F; frame record of character 16 at 0x2064; `mmCredits` placement 0x92B8; `.apt` length 0x12044 | [V] |
| Pad input | `capturedKeyEvent` feloader.apt 0xD0CC; port side `src/hooks/pc_input.c` | [V]/[I] |
| Native draw | `pc_ui_overlay(device, context, rtv, w, h, user)` (src/hooks/pc_ui.h) | [V] |

---

## 6. Fonts and textures a native screen can reuse (Q5)

All paths are in the player's data; read at run time, never copied into the repo. Texture names are SHPX entry
names inside the named screen.

**Fonts** (`fonts/fonts.viv`, each RefPack-wrapped TTF, most-used first by screens): `TradeBld.ttf` (68 text
fields; body/list), `TradeCon.ttf` (36), `SchwartzHand_Bold2.ttf` (27; the hand-lettered orange look),
`FERRO___.TTF` / `Ferro Outline.ttf` (stencil headers), `TradeGothicLTStd-BoldExt.ttf`,
`TradeGothicBold_Outline.ttf`, `AachenStd-Bold.ttf`, `EA05_Body.ttf`, `TeaChest.ttf`. Bitmap `FntX` fonts in
`fonts/befonts.viv` (`TrGth13/19/21/28/32`, `Subtitl`, `ferrob`, `lucida10`, `trade12`, `ErrorMsg`, `ErrorTit`,
`TrGthX7/X9/X10`).

**Style references** (all DXT5 unless noted; 640x480 design space):

| Need | Archive / entry : textures | Notes |
|---|---|---|
| Brick/grunge wall and paper sign | `intMain/mainMenu.big` 5, 18, 53 (door with lamp, 512x512), `options/options.big` 4, 8, 71 (paper sign, 512x512), 7/70 (64x512 edge strips) | backgrounds |
| Cream stencil titles, orange graffiti subtitles, white grunge text | `intMain/mainMenu.big` 56-99, 148-215 (EN + FR); `battle/cmtype.big` 102-177 (match types) | the "red/gold" look is orange (EditText colour 0xFFFFA81A) on cream/white with black outlines |
| Selector brackets and labels | `battle/cmtype.big` 10 (red dashed bracket, 128x128); `options/options.big` 26 (red label 64x32), 20 (cream), 22 (wood), 24 (grey) | |
| Door/menu icons | `intMain/mainMenu.big` 21-48 (grey icons: box, headphones, CD, fist, lock 44, film strip, speaker); **ONLINE** `intMain/support/mainMenuLogosIcons.big` 17 (256x256) | |
| Posters incl. ONLINE | `intMain/support/mainMenuButtons.big` 11, **15**, 19, 23, 27, 31, 37, 42; `test/mnMnTest.big` 18 (same ONLINE content, id `526ab883...44ca`) | |
| Xbox button glyphs, header paper strip | `xHead.big` 15 (A), 17 (Y), 19 (B), 21 (X) (32x32), 7 (512x128 strip) | footers |
| Spotlight/glow | `intMain/mainMenu.big` 223/224; `intMain/support/mainMenuButtons.big` 77, 78 (32x32) | |
| Transition slab, popups | `trans/t1.big` (`t1.xsh`, 1.97 MB), `intMain/message.big` (popup, 33 textures) | |
| Fighter portraits | `screens/portrait/*.big` (1,727 entries: `p_CT_*`, `t_CT_*`, `t_CO_*`, `mp*`, `vp*`), `_art/chooseCharacter_thumbnails.big`, `_art/chooseCharacter_stats.big` | |
| Venues | `venuethumbs/venThumb<feID>.big` (27 + `venLocked`, `venueselected`, `thumbvenueall`), `match/1_venue/vn_ML_<n>.big`, `_art/chooseVenueArt.big`, `chsVnPol.big`, `chooseVenueMap.big` | the venue-vote screen |
| Match types | `match/2_type/mTypeLogo_<type>_{e,f}.big` (11 types x EN/FR) | room settings |
| VS/versus layouts | `match/0_bg/paper_{1v1,ffa3,ffa4,team}.big`, `battle/matchSm1-4.big` | room screen |
| List scroller | `_art/scrollAssets.big`, `_art/scroller.big`, `_art/selectionHighlightAnims.big`, `_art/infoStickers.big` | lobby list [I, not inspected] |

Decode: unwrap RefPack, read the BIGF, read the SHPX directory, upload the blocks as BC3 (types 0x60/61/62 are
BC1/2/3; 0x7B needs its palette record and, if flagged 0x2000, un-swizzling; see `hd_assets.py`).
Composites: titles are split into 128+32 or 128+64 wide pieces, side by side.

---

## 7. Staged plan and the experiments that remove each unknown

| Step | Work | Removes |
|---|---|---|
| E1 offline | `apt-patch` tool in scratch; assemble the section 4 tail; decode the result with `scripts/apt-dump.py --actions` and an independent decoder; check every `DefineFunction` still parses to its size | assembly mistakes |
| E2 run | patched `mainMenu` through the I/O overlay or a patched copy of `screens.viv`; confirm the game boots, the cursor reaches slot 6 and `[FUNCCALL] Online.Focus(6)` / `Online.Open()` appear in the log | guest RefPack acceptance, VM override-by-name, spotlight position |
| E3 run | gate pad input, open an empty overlay, return | pad gating, no side effects |
| E4 optional | search guest memory for the loaded block (clip names `mmCredits` etc.) with `scripts/mem-find.py` to learn if the `.apt` bytes are intact after linking | whether b1 (in-guest patch) is an option |
| E5 | add `Online.*` result strings; wire lobby to the relay hub (online-match-setup.md) | |

---

## 8. Risks and unknowns

* [U] The toolkit's file layer is not in this worktree; an I/O overlay for a single archive entry may or may not be
  straightforward. Install-time patching of the player's `screens.viv` is the fallback and is explicitly allowed.
* [U] Whether the guest tolerates a re-packed entry with a different size/offset, and a literal-only RefPack.
* [U] Exact VM semantics of redefining `keyX`/`selectPoster` after capturing the old values; the alternative is
  to edit the original bodies in place (re-size the nested `DefineFunction` headers and branch offsets).
* [U] Language selection: `Game.GetLanguage` returns a setting id (`GS`-side, handler `0x46CB0`); I read
  1 and 2 as English variants and 3 as French from the frame logic only.
* [I] Native overlay must not draw while the guest is mid-transition; the overlay can ignore this because the
  menu is never left.
* Localisation: only EN and FR texture variants exist; an ONLINE label would need both (or a native text label).
* Legal: the patch recipe holds offsets, expected hashes and our own bytecode only; the art is read from the
  player's files at run time. Our own art (a replacement ONLINE label) is ours to ship.

---

## 9. Method and reproducibility

Scratch tooling (not in the repo): `<scratch>\viv.py` (BIG4 directory), `lib.py`/`apt.py` (RefPack, BIGF,
`.const`, `.apt` structure), `dec.py` (stack decompiler used for the excerpts above), `val.py` (the 2,485-body
validation), `xsh.py`/`sheet.py` (SHPX to contact sheets, viewed to confirm what each texture is). Cross-checks
that anyone can run: `python scripts/big-entry.py screens/screens.viv`,
`python scripts/refpack.py screens/screens.viv screens/intMain/mainMenu.big`,
`python scripts/apt-dump.py screens/screens.viv screens/intMain/mainMenu.big --actions`, and
`scripts/hd_assets.py` for SHPX ids.

Full hashes (SHA-256, identification only):

* `screens/intMain/mainMenu.big` raw entry `71bec380772ac8e430b80d07b79737af9f66a356eb7a741fe639c09954059e29`;
  decompressed `b3619d08e3c52ff5d7e7f7f470fa54efc993934093d2fe0d61f2b27755273263`.
* `mainMenu.apt` `d1183e6673b8f3eac688c4df0d4bdf366dc99fee01c8ae3b8464ba91c8320549`;
  `.const` `62ef965806a5112eded66bb1a3a61ca170b0bbbd647125bf69300b0d5f45c0a9`;
  `.o` `3141dbf8c8d8663e585c855362733e80540af0302c5335cc96ecbd313bb05df9`;
  `.xsh` `f6fefe979a622dc340c5b2d984a3d9e04f30ad13fc75bcf497f31bae8d59f889`.
* ONLINE poster texture (identical in `mainMenuButtons.big` entry 15 and `mnMnTest.big` entry 18):
  `526ab883927181a717592611416c2af371874cb26a2308a01228b421ed7244ca`; lightning-fist icon (`mainMenuLogosIcons.big`
  17) `a6e7c14ecee42e628d8be37f8b050a2f84b2958fc7cad86c66f5b0b3a37f7d28`.
* Archive entries (offset, size): `mainMenuButtons.big` 0xCD9518, 81,342; `mainMenuLogosIcons.big` 0xCED2D8,
  104,098; `mainMenuMTDText.big` 0xD0697C, 22,135; `mnMnTest.big` 0x55A302C, 438,372; `demoMenu.big` 0xA9F674,
  752,606; `feloader.big` 0xB4578, 191,325; `loader.big` 0xE30D8, 25,564; `main.big` 0xE94B4, 37,194;
  `scrMenuMain.big` 0x1B38A8C, 2,433.

---

## 10. Implementation status (a1 patch tool)

`scripts/apt-patch.py` (stdlib only; tests in `tests/unit/test_apt_patch.py`, 315 pass, the real-dump check is
skipped without `DEFJAM_DATA`) builds `<data>/override/screens/screens.viv` from the player's own
`extracted/screens/screens.viv`. `--check` reports `missing`, `stale` or `current` and writes nothing (exit 0 only
when current); the original is only ever read, the copy is written to `.tmp` and renamed after it has been
re-compared with the plan.

What it does, all inside the `screens/intMain/mainMenu.big` entry (everything else is copied byte for byte):

* **Gate.** Refuses unless the decompressed payload, `mainMenu.apt` and `mainMenu.const` match the SHA-256 constants
  of section 9 (the raw-entry hash is informational, so a different RefPack encoder of the same payload is fine).
* **Data.** Appends a clone of the `mmCredits` placement as `mmOnline` (depth 27, position (-62, 99)), a new
  14-pointer frame array, and rewrites the frame-0 record of character 16 (0x2064) to point at it.
* **Code.** Appends a copy of the menu clip's frame-0 action and re-points its action item (0xD3CC). The copy has
  three insertions, all from opcodes the game's own scripts already use:
  1. first statement of `selectPoster`: `_level0._root.FuncCall("Online.Focus(" + oMenu.getIndex() + ")")`;
  2. in `keyX`, right after the select sound: `if (oMenu.getIndex() == 6) _level0._root.FuncCall("Online.Open()")`;
  3. before the final `stop()`: `aMenuArray[6] = "mmOnline"`.
  **The original function bodies are edited in place, not wrapped or redefined.** Their `DefineFunction` size fields
  grow by the insertion length (a multiple of 4 so every later aligned operand keeps its padding; the filler is
  PushZero/Pop pairs) and the one branch that crosses the `keyX` insertion (the `menuKiller` guard) is lengthened.
  Operand alignment is computed on the absolute address the code will have.
* **Repack.** The `.apt` is re-laid out in the entry's BIGF (same entry order, 128-aligned, other inner files
  identical), wrapped as a literal-only RefPack stream (767,600 -> 2,110,976 bytes), appended to the archive copy
  (4-aligned), and its directory record and the header total size are updated: 96,883,691 -> 98,994,668 bytes.
* **Self-check before writing.** The new action decodes to its end; all 25 `DefineFunction` bodies (nested ones
  included) parse to exactly their declared sizes; every branch lands on an instruction boundary; the function
  names and count are unchanged; the top-level program grew by exactly the 4 tail instructions; every byte of the
  original `.apt` is unchanged except the three rewritten words; the other inner files are identical; the RefPack
  round-trips; the written file is compared with the plan before the rename.

Real run on this machine: `override/screens/screens.viv`, 98,994,668 bytes; `--check` says `current` (exit 0); an
independent parse shows 8,795 directory records, only `screens/intMain/mainMenu.big` differing (offset 96,883,692,
size 2,110,976), 200 random other entries byte-identical.

Still unknown, and only a game run (E2) can answer:

* Whether the guest accepts the larger literal-only RefPack entry at a new offset and the re-laid-out BIGF.
* **Whether the VM relocates APT pointers by walking structures or by a table.** Every new pointer (strings, the
  placement, the array, the action) is an absolute `.apt` offset like the originals and is reachable from the
  movie through re-pointed original records, so a structural walk would cover it; a separate relocation list would
  not. This is the main risk of the whole approach.
* The cursor reaching slot 6 and the spotlight gliding to the new placement; `Online.Focus(<n>)` firing on every
  move (including `255` for "no target") and `Online.Open()` on confirm in the `[FUNCCALL]` log. The slot shows
  no label or door logo until native art is drawn over it.
* Whether `Add2` of a string and a number formats the index as plain digits in this VM (the game's own scripts
  concatenate numbers the same way).

### 10.1 Patch version 2: a per-frame `Online.Tick()`

E2 passed for version 1 (`Online.Focus(n)` and `Online.Open()` reach `funccall_hook`; `logs/run-20261020-143257.log.err`).
The idle menu sends no FuncCall of its own, but the online match is started from inside the hook, so version 2
adds one that fires every frame while the menu is on screen.

* **How.** The new `mmOnline` placement now carries a **clip event**: its PlaceObject has the HasClipActions flag
  (0xA6) and a ClipActions record (1 entry: event mask 0x02 = EnterFrame, key 0, action). The action is the
  game's own shape: ConstantPool `[_level0, _root, FuncCall]`, then
  `_level0._root.FuncCall("Online.Tick()")`, End. The art clip it lives in is on stage as long as the menu is,
  and goes away with it, so the ticks stop when the menu is left.
* **Why a clip event and not `onEnterFrame = function`.** I checked `mainMenu` first: it defines no
  `onEnterFrame`/`setInterval` of its own (nothing to chain to), and its only periodic code is exactly one clip
  event, the spotlight mover (EnterFrame, `motionIndex += 0.16` each frame), so per-frame clip events are proven to run in
  this screen on this VM. The `onEnterFrame` method form is present in other front-end screens (options,
  hiScores, chsVenue, unlockchar ...) and in `feloader`, but not in this one, so I did not rely on it. Fallbacks if
  the clip event were ever not enough: a looping two-frame timeline action (a frame action that sends the call, plus
  `gotoAndPlay` back), or `setInterval` (used in `main.big` and `battle/rewards1`); neither was needed.
* **Identity.** `PATCH_VERSION = 2` is printed by the tool, and `--check` compares the override with the freshly
  built plan byte for byte, so a version-1 override reports `stale` (the appended entry differs). The override was
  regenerated: 98,994,797 bytes, `--check` says `current`.
* **Verification added.** The self-check decodes the tick action (ConstantPool, PushString, PushOne, PushVar,
  GetMember, CallNamedMethodPop, End), checks the placement flag, the ClipActions record and the entry; a
  unit test pins the action's bytes.
* **Residual uncertainty.** The event runs once per rendered frame of the APT clock (the same one that advances
  the spotlight), so it is as steady as the menu animation, not a fixed wall-clock rate; it also only fires
  while the guest thread is running the front end (it is a FuncCall like any other). The tick fires from frame 0
  of the menu, before the cursor is touched, and `Online.Tick()` is unknown to the dispatcher (`NO_FOUND`), so the
  hook must swallow it like the other `Online.*` commands.

### 10.2 Patch version 3: the ONLINE poster is part of the wall

Player feedback on version 2: the poster drawn from outside was half the size of the others and visible while
the wall slid in and out. The reason is how the shipped menu draws its posters, which this section records.

* **How the neighbours are drawn.** There are no per-slot poster shapes. The whole poster wall is **two baked
  textures** of `mainMenu.xsh`: entry `5` (dim, ambient) and entry `14` (the lit copy), each 256x512 DXT5, with
  the 64x512 strips `4` and `13` at the right edge. Shape 6 (`5`+`4`) is placed at depth 2 of the art clip
  (character 16) and again at depth 6 under the spotlight's clip mask; frame 1 swaps depth 6 to shape 15
  (`14`+`13`), the lit copy. The spotlight circle (shape 7) is the mask. So the posters slide with the clip, and
  the spotlight reveals the bright texture. The `mm*` placements are only invisible hit targets.
  `intMain/support/mainMenuButtons.big` is a design-time library; the shipped menu never loads it. [V]
* **Where the bottom-left slot is.** It is baked as a decorative "DEF JAM II" shield with torn paper. Mapping
  [V from the neighbours' hit targets]: texture pixel (x, y) is drawn at menu-clip (x - 153 - 3.5, y - 254 + 14),
  1:1, that is 640x480 screen **(x - 2.8, y - 0.1)** (the menu clip sits at (316.2, 239.9) in the root, the art
  clip at (-162.5, 0) in it). The other posters are about 58x85 px (STORY x 72-129, y 123-208 in texture px;
  OPTIONS x 66-124; HIGH SCORES x 127-185, y 292-377).
* **What version 3 does.** `scripts/apt-patch.py` decodes the library's ONLINE poster (`mainMenuButtons.big`,
  texture `15`, 128x128; its raw entry, payload and the 16,384 poster bytes are hash-gated), crops it to its opaque
  box (64x87), makes it neutral grey (the art has a green fringe), resamples it to **60x84** and paints it over the
  slot in **both** wall textures at texture px **(64, 292)** (block aligned). Brightness is matched against the STORY
  poster: the lit texture gets gain 0.92, the dim texture the measured dim/lit ratio (0.34). Only whole DXT5 blocks
  inside that rectangle (630 blocks over the two textures) are re-encoded; every other byte of the bank is
  unchanged, and the tool checks that, and that the encoded poster is within a mean error of 14 grey levels of the
  intended picture. Nothing else about the movie changes: `Online.Tick`, `Online.Focus` and `Online.Open` are
  exactly as in version 2, and the slot's door-logo/title/subtitle/description frames stay empty (the port keeps
  drawing the door text while `Online.Focus(6)` is current).
* **The slot's geometry for alignment.** Poster rectangle in the 640x480 menu space, at the menu's resting position:
  **x 61.2-121.2, y 291.9-375.9 (60x84), centre (91.2, 333.9)**. The hit target `mmOnline` is at menu-clip
  (-61.0, 95.5), the centre plus the (+1.5, +1.5) every other slot has, so the spotlight centres on it like the others.
  While the wall slides (panDown/panUp) all of this moves with the clip.
* **What to look for in the first run.** (1) the poster in the lit circle and dimmed outside it, with the same slide
  as its neighbours; (2) a torn-paper remnant of the old shield below it (24 px) and to its left, left on purpose;
  (3) a slightly softer look than the neighbours (it is a 4-colour-per-block re-encode of a resampled picture);
  (4) **HD texture packs:** the identity of textures `5` and `14` changes, so a pack built from the original
  textures no longer matches them and the wall draws at the original resolution (the HD generation would have to be
  run on the patched archive to cover it); (5) the cursor still glides to the poster (hit target moved from
  (-62, 99) to (-61, 95.5)).
