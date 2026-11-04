# src/hooks — host-side code for the port

Hand-written C and C++ that sits beside the translated game code without
editing it. CMake picks up every `.c` and `.cpp` file in this directory.

| Files | Purpose |
|---|---|
| `d3d11_translator.c`, `nv2a_regs.c` | The Direct3D 11 device and window for the push-buffer translator; the graphics registers that cannot be plain memory |
| `ac97_bm.c` | The audio bus-master registers the game waits on at start-up |
| `pc_settings.c`, `pc_settings.h` | Player-facing settings, `settings.ini` and start-up paths |
| `pc_input.c`, `pc_input.h` | Gamepad, keyboard and mouse input and their settings |
| `pc_ui.cpp`, `pc_launcher.cpp`, `pc_ui_settings.c`, `pc_ui*.h` | The settings screen, shared by the launcher and the in-game overlay |
| `pc_language.c`, `pc_language.h` | Language mods and `[language] game` (the game's own French): rebuild the active mod's files from the dump into the data folder's cache and serve them in place of the originals; its change to the XBE's data sections; its own HD images (a `manifest.ini` beside `language.ini`) added beneath the texture packs ([docs](../../docs/language-packs.md)). `pc_ui_model.h` lists them on the Mods page |
| `save_compat.c`, `save_compat.h` | Keeps profiles from earlier builds readable |
| `options_audio.c`, `options_audio.h` | Hands the saved music / effects / announcer volumes to the sound system after the save is read |
| `custom_music.cpp`, `custom_music.h`, `custom_music_guest.c` | Custom music: plays the player's own songs on a host audio voice and silences the game's music players meanwhile |
| `music_decode.c`, `music_decode.h`, `music_library.cpp`, `music_library.h` | Custom music: MP3/OGG/FLAC/WAV decoding (vendored decoders in `thirdparty/audio/`) and the `<data>/music` folder |
| `pc_launcher_music.cpp`, `pc_launcher_music.h`, `pc_launcher_music.inc` | The launcher's Music card and dialog (add, remove, what plays, shuffle, dropped files) |
| `test_telemetry.c`, `test_observations.c` | Read-only game-state probes for the test harness (`RECOMP_TEST_OBSERVATIONS=1`) |
| `watchpoint.c`, `codeguard.c`, `postcmd.c` | Diagnostics: write watchpoints, code-overwrite detection, synthetic front-end commands |

Replacements for single game functions go in `src/recomp_manual.c`, not here.
