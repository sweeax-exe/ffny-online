# First-time help cards in a fight (the "BLAZIN'" card): where they come from and how to switch them off

Read-only research from the lifted C (`src/recomp/gen`, read with grep), the XBE strings and the user's own dump.
Nothing was built or run; no repo file other than this report was touched. No game bytes are reproduced here
(addresses, names, short paraphrases only).

Tags: **[V]** verified by reading the exact code or data, **[I]** inferred (reasoning given), **[U]** unknown.
Addresses are guest virtual addresses; `GS` is the game-flow singleton `MEM32(0x2FB148 + 4*0x18)` (see
`online-match-setup.md`), reached by the stock helper `sub_00012CF0(ecx=0x2FB148, idx)`.

---

## 0. Summary

1. The card is the game's own **"first time help"** feature (XBE class names `CFirstTimeHelp`, `FirstTimeHelp::String`,
   `FirstTimeHelpTar`). There are exactly **three cards**: 0 weapon (`TUTOR_WPN_*`), 1 **BLAZIN'** (`TUTOR_BLAZIN_*`),
   2 KO (`TUTOR_KO_*`). It is unrelated to the front end (no FuncCall) and to the loading-screen tips
   (`Game.GetTips`, `battle/tips.swf`). [V]
2. The texts are in the localisation tables `assets/misc.viv|cmusen.loc`, `cmuken.loc`, `cmfren.loc` (the BLAZIN'
   card text is there, with `[R]`/`[G]` button placeholders); the card art is `assets/misc.viv|popups.xsh`
   (`popupsF.xsh` French). [V]
3. **Trigger chain.** A game event posts message `0xF0000033` with a pointer to a record whose first word is the card
   index (0, 1 or 2). A handler, **`sub_000D6670`**, subscribed to that message, asks the singleton
   (`0x346A68`) to show the card. The singleton's per-frame **`sub_000D6450`** then waits 4 frames, posts the pause
   message `0xF0000000`, marks the card as seen, and shows it; the fight loop (`sub_00079B60`) stops stepping
   the simulation while the card is up. [V]
4. **BLAZIN' specifically:** the handler `sub_00035AE0` (subscribed to event `0xF1000004`, which I read as "special meter
   full/empty" [I]) posts card 1 when the meter becomes full, the fighter is a human (the slot's CPU flag
   `GS+0x178+4*slot` is 0) and the **"seen" byte `SaveData+0x61` is 0**, where `SaveData = MEM32(GS+4)`. [V]
5. **The "seen" flags** are three bytes in the loaded save data: `MEM32(GS+4) + 0x60 / 0x61 / 0x62` (weapon /
   BLAZIN' / KO). They are set to 1 when the card is shown, so they are per-save, which is exactly why two PCs with
   different saves diverge. [V] (that they are written to disk with the save is [I])
6. **There is no option** to turn these cards off: the `Option` FuncCall module (SetVibration, SetDifficulty,
   volumes, SetTargetIndicator, SetControllerConfig, SetMusicMix, screen brightness/position, SetAutoSaveEnabled
   and their getters), `options.big`, `gconfig.xml` and the profile have no such setting. [V] for the lists, [I]
   for the absence elsewhere.
7. **Smallest safe hook:** the handler `sub_000D6670` is reached **only indirectly** (message bus), so a manual override
   in `recomp_lookup_manual()` can replace it: when `netplay_active()`, do `g_esp += 4; return;` and call nothing.
   At that point nothing has been touched (no request, no pause, no seen flag), so the fight simply continues. It
   covers all three cards. Details and the fallbacks in section 4. [V] mechanism, [I] that nothing else needs it.
8. Dismissal (if ever preferred): the card closes on the **release** of **A** on the combined pad (`sub_000D6450`
   tail), read through the controller manager, not through the netplay-synchronised input. Section 3.5. [V]/[I]

---

## 1. The strings and the data

### 1.1 XBE strings [V]

| Text | VA | Role |
|---|---|---|
| `CFirstTimeHelp::SimpleQuad`, `... Button`, `FirstTimeHelpTar`, `FTHMenuGrey`, `FirstTimeHelp::String` (x4), `FTH::Tokenizer string` | 0x2B3084-0x2B3214 | the card's draw objects (the generic memory-card popup class has parallel `Popup:*` names at 0x2B2B00) |
| `CtexPaper`, `texTitle`, `texNote`, `texFooter`, `xboxa/b/x/y`, `rstk` | 0x2B2DA3-0x2B2DF8 | texture slots: the paper card, title, body, footer, Xbox button glyphs |
| `assets/misc.viv|popups.xsh`, `popupsF.xsh` | 0x2B2AE4 / 0x2B2AC8 | the card art (English / French) |
| ` CONTINUE` (x2) | 0x2B30E4, 0x2B30F0 | footer text next to the A glyph |
| `TUTOR_WPN_TITLE` / `TUTOR_BLAZIN_TITLE` / `TUTOR_KO_TITLE`; `..._DESC` | 0x2B2E00 / 0x2B2E10 / 0x2B2E24 | localisation keys |

The keys sit in a static table of three 0xC4-byte records starting at **VA 0x2B2E38**:
`{ const char *title_key; char desc_key[192]; }` (record 0 = weapon at 0x2B2E38 with its description key inline at
0x2B2E3C; record 1 = **BLAZIN'** at 0x2B2EFC / 0x2B2F00; record 2 = KO at 0x2B2FC0 / 0x2B2FC4). `sub_000D6450` copies
record `idx` (0x31 dwords) onto its stack and passes it to the show routine `sub_000D6180`. [V]

### 1.2 Texts and art in the dump [V]

`assets/misc.viv` entries `cmusen.loc`, `cmuken.loc`, `cmfren.loc` (3,068 UTF-16 strings each, RefPack-wrapped) hold
the card text ("... to enter BLAZIN' mode. Once BLAZIN', grab your opponent [G] and move [R] again to unleash a
BLAZIN' move.", title "BLAZIN'"); the keys are looked up by hash (`sub_00045160/00045180`), so the key names do not
appear in the file. `popups.xsh` / `popupsF.xsh` (paper card) and `hudTutor.xsh` / `hudTutrF.xsh` (the separate
Tutorial-mode HUD) are in the same archive.

---

## 2. The trigger chain (who decides to show a card)

### 2.1 The singleton: class `CFirstTimeHelp` at VA 0x346A68 [V]

Constructor `sub_000D51C0` (lazy: both `sub_00079D60` and `sub_000D6670` build it on first use, guarded by the bit
`0x34728C & 1`, with the destructor registered through `sub_002007DD(0x2109C0)`). It has no vtable. Fields (offset ->
absolute address):

| Field | Offset | Absolute | Meaning |
|---|---|---|---|
| enabled | +0x184 | **0x346BEC** | 0 until the match's init runs; 1 afterwards; cleared by shutdown |
| active | +0x185 | 0x346BED | a card is pending or showing (set by the request and again by the show routine) |
| pad / mask | +0x188 / +0x18C | 0x346BF0 / 0x346BF4 | owner pad (-1 = any) and an optional button mask (0) |
| `show_ok` | +0x190 | 0x346BF8 | initial 1; the card is only shown while nonzero (set by `sub_000D5500`) |
| delay | +0x250 | 0x346CB8 | frames until the card is shown (4 after a request) |
| counter | +0x24C | 0x346CB4 | 30 after the card is shown, counts down before input is accepted |
| card index | +0x254 | 0x346CBC | 0, 1, 2, or 4 = none |

### 2.2 Lifetime, per match [V]

* **Init `sub_000D66E0`** (`ecx = 0x346A68`) is called unconditionally from `sub_000793A0` (the back-end game mode
  starting a match). If `+0x184` is 0 it caches `GS+0x338`, loads the textures `popups.xsh` etc., **subscribes the
  handler `sub_000D6670` to message `0xF0000033`** (`sub_000BD610(msg, handler)`) and sets `+0x184 = 1`. If the
  singleton is already enabled it returns at once.
* **Shutdown `sub_000D5690`** (called from `sub_00079580`) unsubscribes (`sub_000BD6A0`), closes any card
  (`sub_000D5460`), frees the textures and clears `+0x184`.
* The message bus: `sub_000BD720(msg, data)` = Post (a synchronous loop over the subscribers of `msg`, each called
  as `handler(msg, data)`, cdecl; the return value is ignored), `sub_000BD610` = subscribe, `sub_000BD6A0` =
  unsubscribe, `sub_000BE030` = declare a message id (all declared once in `sub_00011E40`). The tables are at
  `0x39E844` (count), `0x39E84C` (entries), stride `[0x39E850]`, subscribers per entry `[0x39E854]`.

### 2.3 Who posts `0xF0000033` [V]

Three places, each with the card index in a local record whose first dword is the index:

| Poster | Index | Conditions I could read |
|---|---|---|
| `sub_00035AE0` (handler of event **`0xF1000004`**, registered in `sub_0003C6E0`) | **1 = BLAZIN'** | event byte (+4) nonzero; `MEM8(GS->+4 + 0x61) == 0` (BLAZIN' not seen); `MEM32(GS + 0x178 + 4*slot) == 0` (the fighter is a human; that dword is the per-slot CPU flag of the setup block, see online-match-setup.md section 2); byte `0x340540` is 0. The same function also updates blazing bookkeeping (`0x33EA04`, `0x3403BA`), plays the speech `sub_001367D0` and sends `sub_00034110(0x2B)`, which fits the log's `CRD_Special_Meter_CST.DAT` read just before. |
| `sub_000BD860` (a HUD/event translator, +0x2C stack record) | 2 = KO | a loop over the fighters tests for a downed human; the KO seen byte `+0x12` of the same block `GS+4 -> +0x50` |
| `sub_000BD860` (second site, at 0xBDE58) | 0 = weapon | a loop over the weapon list at `0x3494E0/0x3494E4` (stride 0x34); the weapon seen byte `+0x10` |

(The meaning "meter full" of event `0xF1000004` and the exact KO/weapon conditions are [I]; the BLAZIN' path is [V].)

### 2.4 The handler and the request [V]

* **`sub_000D6670(msg, data)`**, cdecl, only reached through the bus (no direct caller): if `data == 0` return; build
  the singleton if needed; **return if `byte[0x346BEC]` (enabled) is 0**; if a card is already active and its delay
  is 0 return; otherwise `Request(*(int*)data)`.
* **`sub_000D5560`** (thiscall, 1 argument, `ret 4`) = Request: if `+0x185 == 0`: `+0x254 = idx; +0x250 = 4; +0x185 = 1`.
* **`sub_000D5500`** (indirect only, thiscall, 1 byte argument) = set `show_ok`; when a card is active it also
  posts pause (`0xF0000000`, argument nonzero) or resume (`0xF0000001`).

### 2.5 The per-frame update and the "seen" flags [V]

`sub_000D6450` (thiscall, called every frame from the back-end game mode's update `sub_00079B60` at 0x79C2E-0x79C3A,
only on frames where the simulation would step):

```
if (!enabled || !active) return;
if (delay > 0) {
    if (--delay != 0) return;                       // 4 frames after the request
    if (show_ok) {
        if (byte(0x3BE13E)&1 || byte(0x3BE14E)&1 || byte(0x3BE15E)&1 || GS[0x338] != 0)
            { active = 0; card = 4; delay = 0; return; }        // cancelled: a result is already recorded, or GS+0x338 set
        Post(0xF0000000)                            // pause
        SaveData = GS[4];  SaveData[0x50 + 0x10 + card] = 1;    // the "seen" flag
        sub_000D6180(card record)                   // build and show the card; sets active = 1, counter = 30
    }
}
// card up: dismissal handling (section 3.5)
```

The three bytes at `0x3BE13E / 0x3BE14E / 0x3BE15E` are the +0xE bytes of the first three records of the match
result history ring at `0x3BE130` (match-end-state.md), so no card appears once a result exists. `GS+0x338` is a
byte the game clears at setup and a setter (`sub_000790A0`) can raise; I did not find who sets it ([U], it looks
like a "no help now" flag).

While a card is up, `sub_00079B60` clears its "step the simulation" local after calling the update (0x79C3F-0x79C53), so
**the whole simulation, including `netplay_on_step` (`sub_00087700`), stops on that PC.** In lockstep the other PC then
waits for it. [V] for the code path, [I] for the netplay consequence.

### 2.6 Pause/resume [V]

Messages `0xF0000000` / `0xF0000001` are handled by `sub_0003C3C0` / `sub_0003C3E0` (subscribed in `sub_0003C6E0`),
each a two-instruction wrapper that tail-jumps to **`sub_0003A340(pause, 1)`**, which stores the flag in byte
`0x340348`, resets `0x3407BC` to 1.0f and mutes/restores sound and effects (skipped when byte `0x340540` is set).

---

## 3. Switching the cards off

### 3.1 Is there an option? [V]/[I]

No. `Option` module methods (handlers registered by the title; XBE strings): GetVibration, SetVibration,
SetDifficulty, GetDifficulty, Set/GetMusicVolume, Set/GetSFXVolume, Set/GetAnnouncerVolume,
Set/GetInteractiveMusicMode, Set/GetTargetIndicator, Set/GetControllerConfig, Set/GetMusicMix,
Set/GetScreenBrightness, Set/GetScreenPosition, Set/GetAutoSaveEnabled. `options.big` and `cribOptions.big` expose
difficulty, vibration, autosave and the volumes only. `gconfig.xml` has no tips key (its only gameplay-like
setting is `buildtype`). The word "Tips"/"GetTips" in the module list is the loading-screen tip text. So there is no FuncCall that
switches the cards off.

### 3.2 Making both PCs agree without a hook: preset the flags [I]

Write `1` to the three bytes `MEM32(GS+4)+0x60 .. +0x62` on both PCs before the fight (GS+4 is the loaded save
data; `GS = MEM32(0x2FB1A8)`; the pointer can be null before a save is loaded, check it). Pros: no code hook, the
posters see "already seen". Cons: the bytes belong to the player's save: if the game autosaves afterwards the
tutorials are marked seen for good, so restore them after the match (or only do it when the match is online and
restore on exit). Racy with whatever loads the save at match start. Not recommended as the primary route.

### 3.3 Recommended: swallow the request while netplay is active [V]/[I]

`sub_000D6670` is only reached through the message bus (`scripts/callers.py 0x000D6670` reports no direct callers; the
two address references are the subscribe/unsubscribe pushes at `sub_000D66E0` and `sub_000D5690`), so
`recomp_lookup_manual()` (indirect calls only) can replace it:

```c
/* guest 0x000D6670: first-time-help request handler, cdecl (msg, data), return value ignored */
extern void sub_000D6670(void);
static void tip_request_hook(void)
{
    if (netplay_active()) {      /* online lockstep: no card, no pause, no seen flag */
        g_esp += 4;              /* cdecl: pop the return address, the poster pops the arguments */
        return;
    }
    sub_000D6670();              /* offline: unchanged */
}
/* in recomp_lookup_manual(): */
if (xbox_va == 0x000D6670u) return tip_request_hook;
```

What it must return or set: nothing. The poster (`sub_000BD720`) ignores the result and pops the two arguments itself,
and nothing is mutated before the handler runs its body (the request, `+0x250`, `+0x185`, the pause message and the
seen byte are all downstream of it), so the fight just continues and no state is left half-set. It covers all three
cards, survives each match's init/shutdown (the subscription is recreated every match, the hook is address-based),
and costs one comparison per card event. Confirm first with `RECOMP_TRACE_ICALL=0x000D6670` or
`RECOMP_WATCH_EXEC=0x000D6670,0x000D6450` that the handler is hit through `recomp_lookup_manual` (it should be: the
FuncCall handler at `0x00068BA0` is wrapped the same way).

Equivalent without the indirection: `void sub_000D6670_enter(void) { if (netplay_active()) MEM32(g_esp + 8) = 0; }`
(argument 2 is `data`; the handler returns at once on a null pointer). That needs a re-lift to emit the `_enter` call,
so the manual-lookup form is cheaper. A third form, `sub_000BD720_enter` on message `0xF0000033`, runs on every
event of every kind and is not needed.

Other places I would **not** use: clearing the enabled byte `0x346BEC` after init (the next match's init sees 0 and
re-initialises, doubling the subscription unless shutdown ran first; ordering with `netplay_on_match_start` is
unknown [U]); hooking `sub_000D6450` (it runs every frame and the cancel path needs the same fields reset);
hooking `sub_000D6180` (the pause message is already posted by then).

### 3.4 What else the cards touch [V]

Only the singleton's own fields, the pause path above, the textures it loads at init (`popups.xsh`, the Xbox glyph
textures) and the seen byte. Suppressing the request does not skip loading the textures (init still runs at match
start), which is fine for lockstep.

### 3.5 If dismissal is preferred over suppression [V]/[I]

The card closes in the tail of `sub_000D6450` (label 0xD65A0), reached when the 30-frame counter has run out:

```
ctrlmgr = Get(0x19);  ctrl = sub_000DCBE0(ctrlmgr, this[0x188]);   // 0x188 == -1: the combined "any pad" controller at ctrlmgr+0x724
held = sub_000DD2B0(ctrl, 0);      // ctrl+0x84
released = sub_000DD2B0(ctrl, 3);  // (ctrl+0x84 ^ ctrl+0x88) & ctrl+0x88: bits that went up this frame
if (released == 0 || held != 0) return;                 // only on a clean release
if (mask(0x18C) ? (released & mask) : (released == 0x10)) { if (Get(3)) sub_000334A0(1); sub_000D5460(); }   // close + resume
```

So **A (bit 0x10 of the button word [I]) released while nothing else is held, on any pad**, dismisses; the close routine
`sub_000D5460` posts resume (`0xF0000001`), frees the card textures, resets `+0x185/+0x188/+0x18C`. The state is read from
the controller manager's own per-pad words, i.e. the local PC's pad, not the synchronised stream the netplay hooks
substitute at `sub_001BAA00`; faking the release would mean writing `ctrl+0x84/0x88` on the combined controller, and
the PC that shows the card would still have stalled the simulation for the frames in between. Suppression is the
better route.

---

## 4. Unknowns and how to close them

* [U] Where `GS+0x338` is raised and what it means (it cancels the card). Harmless for the hook.
* [U] Whether the three seen bytes are written to disk (they are in the in-memory save block at `GS+4`; the block is
  what the memory-card code serialises, I did not trace it). Matters only for the preset-flags alternative.
* [U] The exact KO and weapon trigger conditions in `sub_000BD860` (not needed for the hook).
* [I] That event `0xF1000004` means "special meter full"; the log in the report request (speech `CRD_Special_Meter_CST`
  then the card) agrees.
* Run check that closes the loop with one fight: `RECOMP_WATCH_EXEC=0x000D6670,0x000D5560,0x000D6450,0x000D6180`
  should show 0x000D6670 hit once when the meter fills and nothing after it with the hook on; with the hook off it
  shows the whole chain and the pause.
