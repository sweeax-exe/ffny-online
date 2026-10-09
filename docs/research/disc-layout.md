# Disc layout — Def Jam: Fight for NY (USA), Xbox

Source: `Def Jam - Fight for NY (USA).xiso.iso` (3,023,831,040 bytes), extracted with extract-xiso 2.7.1 to
`<data>\extracted\`. 118 files, 2.9 GB. **Single executable: `default.xbe` (3,284,992 bytes).**

## Top level
```
default.xbe          3.28 MB   the game (XBE, title "DJFFNY", title ID 0x45410049 = EA-073)
fighter.xml, gamedata.xml, gconfig.xml, venue.xml   plain-text data tables (characters, venues, volumes, build type)
assets/   860 MB     BIGF archives (.viv) + XML + misc
audio/    1.7 GB     BIGF archives: music0-4, sfx0-3, speech1-3 + animtags.dat (TAGS) + tv.txt
fonts/    584 KB     befonts.viv, fonts.viv
motion/    67 MB     base00.viv, blazin.viv, wres_all.viv (64 MB, move-set anims), main_all.dat (WAZA), camera.xml
movies/   147 MB     .mad videos (EA MADk codec): intro, attract, eagames, thx, soulplan, SoulTrai, extra; movies.viv; load0.xsh
screens/   93 MB     screens.viv (BIG4 variant) + feflow.xml (front-end flow)
```

## Container / file formats seen (by magic bytes)
| Ext | Magic | What it is | Implication for the port |
|---|---|---|---|
| `.viv` | `BIGF` (one `BIG4`: screens.viv) | EA BIG/VIV archive (standard EA container, well documented; many extractors exist) | File-IO hook can redirect by archive **and** by inner entry name → texture/music overrides |
| `.mad` | `MADk` | EA "MAD" FMV codec (2003-05 era EA Sports/EA Canada) | Video decode is done by game code on CPU → recompiles as-is; no HLE needed |
| `.xsh` | `SHPX` | EA Shape (texture container, Xbox flavour of SSH/GSH/XSH) | Texture-injection target format |
| `.sod` | `SODB` | unknown EA table (djv2.sod) | investigate |
| `.loc` | `LOCH` | localized strings (fetext.loc) | |
| `.dat` | `TAGS` / `WAZA` | audio anim tags / move ("waza") table (AKI engine, Japanese naming) | |
| `.spy` | text `nspies N` | spy-cam definitions | |
| `.xml` | text | fighters, venues, gamedata, gconfig, combos, entities, taunts, camera, feflow | **Data-driven engine**; custom fighters may be partially XML-editable |

## Notable config facts
- `gconfig.xml`: `<buildtype>1</buildtype>` = "Main FE". Comments list build types 0 Debug FE … 5 **Online FE** — the engine had an online front-end variant (this disc doesn't enable it).
- `gconfig.xml` is followed by an opaque `<?x…x?>` hex blob (probably a checksum/signature over the file — must check whether the XBE validates it before we edit configs).
- `assets/aki_cfg.xml`: AKI-engine flags (`test_viewer`, `cp_save_load_to_pc`) — dev leftovers, useful for debugging hooks.
- Volumes are duplicated in `gconfig.xml` and `audio/tv.txt`.

## Archival reference (not used for work)
Full Redump ISO `Def Jam - Fight for NY (USA).iso` 7,825,162,240 bytes — CRC 71d5b621 · MD5 85725321fef4396d020b624cad8e2531 · SHA-1 1380355c5e3bc5f8e4ef8f2fc15e36a3d00e7774 (Redump database values).
