# Launcher redesign proposal — 2026-10-19

Status: approved by Vlad on 2026-10-19 and implemented on `codex/launcher-redesign`. This preserves the proposal; see [implementation and playtest](launcher-ui-implementation-2026-10-19.md) for delivered behavior and evidence.

## Recommendation

Use two levels: a focused **Home** screen for launching and common choices, and **Advanced** for the complete settings library. Keep **Mods**, **Open Logs**, **Open Folder** and **Update / Repair** directly accessible from Home. Mods opens the same Mods page available inside Advanced. These are destinations and actions, not additional settings stores.

Keep the existing Dear ImGui implementation. Give the launcher a designed shell and keep the overlay compact, with shared setting controls, labels, persistence and navigation. The browser concept accompanying this proposal demonstrates the design; it is not a proposed browser dependency for the shipped game.

The visual direction is a restrained New York fight poster: charcoal brickwork behind an ivory torn-paper title panel, ochre gold for Play and focus, and oxblood red for selected navigation. Keep texture away from form fields. The alternative is a mostly paper composition with a smaller title and a wider form. Recommend the charcoal/paper split because it gives Play and the title a clear hierarchy while retaining room for settings.

## What was reviewed

Local source: `src/hooks/pc_ui.cpp`, `pc_ui_internal.h`, `pc_ui_settings.c`, `pc_settings.c`, `pc_settings.h`, `pc_launcher.cpp`, `pc_launcher_sdl.cpp`, `setup/linux/launcher.sh`, the launcher/settings/setup documentation and the existing launcher screenshot and branding. Source is authoritative where documentation is stale; for example, the settings reference still describes widescreen as Windows-only, although the current shared settings UI exposes it across builds.

External source snapshots reviewed on 2026-10-19:

| Project | Relevant source | Useful pattern |
|---|---|---|
| FIFA Street / ReStreet | [`LauncherForm.cs`](https://github.com/samuelit0davila/fifa-street-pc-port/blob/c3733ad81f3886624d0455db9a78f1556cd4f9ec/Launcher/FifaStreetLauncher/LauncherForm.cs), [`FifaStreetLauncher.csproj`](https://github.com/samuelit0davila/fifa-street-pc-port/blob/c3733ad81f3886624d0455db9a78f1556cd4f9ec/Launcher/FifaStreetLauncher/FifaStreetLauncher.csproj) | A dominant Play button, branding on the left, a compact form on the right, small utility buttons, consistent backdrop/control styling and DPI handling. The inspected source uses .NET 8 WinForms with custom painting. Its Advanced panel is specifically executable/data paths; Compatibility contains renderer troubleshooting. The README also describes a newer online launcher with a browser fallback, so the supplied offline screenshot and inspected WinForms implementation should not be presented as the architecture of every current edition. |
| Dusklight | [`prelaunch.cpp`](https://github.com/TwilitRealm/dusklight/blob/34a144a493b7f57adeb4034d382317c826eb5c8d/src/dusk/ui/prelaunch.cpp), [`settings.cpp`](https://github.com/TwilitRealm/dusklight/blob/34a144a493b7f57adeb4034d382317c826eb5c8d/src/dusk/ui/settings.cpp), [`theme.rcss`](https://github.com/TwilitRealm/dusklight/blob/34a144a493b7f57adeb4034d382317c826eb5c8d/res/rml/theme.rcss), navigation and controller UI | Home separates Play, Settings, Mods and Quit. Settings uses task-based tabs, a control pane with a contextual explanation pane, saved values, modified indicators and explicit restart handling. UI uses RmlUi documents/styles with C++ components. Its tabs include Prelaunch, Video, Input, Audio, Gameplay, Cheats, Interface and Tools; those reflect its capabilities and are not a checklist to duplicate in Def Jam. |

Use FIFA Street's visual hierarchy and direct utility actions, and Dusklight's separation of Home, settings and Mods plus contextual help. Def Jam already has the rendering and input foundation to do this without adopting either project's UI stack.

## Current problems and existing strengths

`ui_draw()` currently renders seven equally prominent tabs: Display, Textures, Controllers, Pad buttons, Keyboard & mouse, General and Debug. On a smaller display those labels compete for width. Display mixes basic presentation, widescreen internals and picture adjustments. General contains save-affecting unlock tools alongside ordinary startup preferences, while the Debug tab contains the recorder and scene tools. Textures combines using packs with making them.

Retain the working pieces: one settings registry and `settings.ini`, saving as values change, environment overrides, keyboard/gamepad navigation, button capture, live controller feedback, DPI/text scaling, launcher skip/force flags and the separate pre-game host windows. The launcher is already rendered independently of the guest. Windows uses Win32/D3D11; macOS/Linux use SDL3's renderer, with the macOS event loop on the first thread. The overlay uses the game's presentation integration.

## Level 1 — Home

At approximately 1100 × 720 logical pixels, use a 38/62 split. Left: title, version/channel, a compact launch summary and a large **Play**. Right: six common controls and a controller summary with **Configure**. Bottom: the utility strip. Native window close and a modest Quit action remain available.

| Home control | Existing setting / behavior |
|---|---|
| Display mode | `display.fullscreen`: Windowed / Borderless fullscreen. Do not suggest exclusive fullscreen support. |
| Window size | A labeled size selector writing `window_width` and `window_height` together. Show only for windowed mode; reflect arbitrary existing dimensions as Custom rather than overwriting them. Advanced retains numeric dimensions. |
| Picture shape | `display.aspect`: preserve all existing choices; visibly mark 21:9 and 32:9 experimental. |
| Render scale | `display.render_scale`: 1×–4×; preserve the current 2× default. The 4:3 equivalents are 640×480, 1280×960, 1920×1440 and 2560×1920; widescreen uses a wider canvas. Show dimensions only when derived from the actual selected canvas. Internal rendering and host window size are separate. |
| VSync | `display.vsync`; explain that the existing pacing behavior depends on display refresh. |
| HD textures | `textures.enabled`, with installed-pack availability and a Manage shortcut. With no usable local pack, show “Not installed” and a route to Mods/Setup instead of an ineffective enabled state. Never generate or download a pack just by toggling it. |

Show detected devices and player slots as a short summary, with Configure opening Advanced → Controls. Keep **Skip launcher next time** as a small footer preference, backed by the existing `launcher.skip` key and its existing Shift/`--launcher` recovery.

Do not add a performance-profile dropdown initially. Def Jam does not have validated Quality/Balanced/Performance presets. A later profile feature would need platform/scene measurements and should change a documented set of rendering keys only, report Custom when they differ, and preserve personal input/picture preferences.

## Level 2 — Advanced

Home's Advanced action replaces the Home content in the same window. Use a clear Back to launcher action and six short tabs; wrap or use a compact vertical tab list at constrained widths. Keep the settings area scrollable and the footer available. Opening Mods or Configure from Home selects the appropriate tab; Back restores focus to the invoking control.

| Advanced tab | Sections and complete existing setting ownership |
|---|---|
| **Display** | Window: fullscreen, window width/height, aspect, vsync, filter, render scale. Widescreen: HUD position, cutscene framing, 4:3 side fill, FOV cap; show when applicable. Picture: brightness, game brightness curve (`gamma`), vignette enable and strength. Home controls reference these same settings. |
| **Controls** | Players & devices: live pad view, slot assignment/status, keyboard player, startup controller count, SDL/XInput backend where supported. Bindings: existing gamepad mapping/detect/default and keyboard/mouse add/clear/default. Feel: both stick dead zones, trigger/button thresholds, stick source mapping, rumble strength/floor/minimum duration/connect buzz and rumble test. Pointer: hide idle cursor and fullscreen confinement. Use section selectors within this page, retaining the current capture behavior. |
| **Mods** | Initially label the page **Texture packs** under the Mods tab. Installed local pack list, enable switch, explicit priority ordering and missing/invalid-pack status, plus Open Mods Folder. Bind to `textures.enabled` and `textures.packs`; later entries retain their current winning priority. A collapsed Memory section owns `textures.cache_mb`, preserving the current 2048 MiB default. Texture capture moves to Diagnostics. An installed-pack list, reordering UI and validation are proposed new presentation features over the existing local pack loader. |
| **Interface** | Startup: launcher skip and `debug.skip_intro` under the user label “Skip startup movies”; preserve the INI key for compatibility. Overlay: key and pad chord. Appearance: existing `ui.scale`; preserve display DPI scaling. Version and licenses can be a small About section or Maintenance details. |
| **Diagnostics** | Recorder: every current recording/mark/retention/hitch/hang/audio/game-state/guest-RAM setting and current run status/Mark a bug in the overlay. Texture authoring: `textures.dump` and `dump_limit`. Developer tools: `debug.unlock_all`, Story scenes and EA developer menu. Keep scene actions overlay-only until a separate prelaunch scenario design is implemented. Put the existing save-impact explanation beside Unlock All and require its existing deliberate user action; never enable it through a rendering preset. |
| **Maintenance** | Installation, data, saves, mods, active settings file and actual play-log paths; version/channel and About/licenses; Open Logs, Open Folder and Update / Repair. Reset settings with the existing confirmation, preferably reset-current-page and reset-all actions distinguished from saves. New page-reset behavior must use the explicit ownership list rather than broad INI section names, because `debug` and `textures` settings are intentionally split across pages. |

Audio volume/output and game language stay in the game's own options until the port exposes real settings for them. Subtitles are still an open feature request; the redesign must not display a working subtitle switch. GPU/API/monitor/refresh selectors, FXAA/MSAA, online services and a downloadable mod catalog are separate capability work, not implied by this redesign.

## Utility button behavior

| Button | Proposed destination/action |
|---|---|
| **Advanced** | Open the complete settings view, remembering the last category within the current session. |
| **Mods** | Open Advanced → Mods directly. Both routes share state and widgets. |
| **Open Logs** | Open the active play-log directory, with the latest log selected where the platform supports it. Resolve the real log location; installed macOS/Linux wrappers write outside the source tree. Offer setup logs separately in Maintenance. |
| **Open Folder ▾** | Default: data folder. Menu: Data, Saves, Mods, Installation and Settings file. Label each destination so players can distinguish installation from data. |
| **Update / Repair** | Open the existing setup workflow for this installation. Its existing release picker, verification and build/update engine remain authoritative. Setup may require a locally built game and must not appear to be an instant binary patch. No automatic network request is needed just to open the launcher. |

The setup handoff requires implementation, not merely a file-open call. Track/resolve the install root and an available setup app/tool using the installed receipt or a small explicit launch context. Do not assume the downloaded installer still exists. If unavailable, offer Choose Setup or the release page. The existing play/setup locks span the launcher too: request the handoff, close the launcher/game process, release wrapper/inherited locks, then allow setup to act. A waiting setup process or wrapper-owned handoff must not inherit a live play lock. Failed handoff must leave the installation usable. In the overlay, show “Close game and open setup” as the necessary workflow.

Folder actions use ShellExecute, macOS open or Linux xdg-open with argument arrays/native calls, preserving paths with spaces and Unicode. If a file manager cannot open in Steam Gaming Mode, display/copy the resolved path and offer Desktop Mode guidance. Do not silently report success.

## Visual system and interaction

- Palette: soot `#161513`, panel `#24211E`, warm paper `#E9DFC8`, muted gold `#D6AD52`, oxblood `#8D2D2A`. Gold is the primary action/focus color; red marks navigation and deliberate developer warnings. Use measured contrast for the actual text sizes.
- Brickwork stays faint and mostly behind the title, with irregular broad blocks rather than a high-contrast repeating grid. Torn edges are limited to the title paper or one separator. Form surfaces are opaque and quiet.
- Use a condensed display face for the title, page headings and Play; an easily read sans-serif for labels and values. Bundle only appropriately licensed fonts with notices, or use a reliable fallback. The current system-font approach produces different typography across OSes.
- Use crisp rectangular controls with small corner radii and generous spacing. A subtle clipped corner on Play is enough character; avoid distressing body text or forcing italic/slanted form values.
- Use a persistent focus outline and selected text/markers, not color alone. Target roughly 44 logical pixels for actions and comfortable labels on the Deck. Test keyboard, mouse and gamepad without a pointer.
- Keep layout responsive to logical size, actual display DPI and `ui.scale`. At 1280×800/Steam Deck, shrink the decorative hero and reflow the utility strip before reducing control targets. At very large UI scales, allow scrolling with fixed navigation/footer rather than clipping controls.
- Do not animate the backdrop. A brief focus/selection transition is sufficient. The overlay gets the same controls and palette without the decorative title/brick composition obscuring play.

## Code architecture

```text
Win32/D3D11 launcher host          SDL3 launcher host
               \                  /
                Launcher shell: Home / Advanced / Mods
                      |                   |
               shared controls + page metadata
                      |                   |
            existing RecompSetting registry / settings.ini
                      |                   |
              existing runtime apply callbacks

Game presentation -> compact overlay shell -> same controls/pages

Shell action request -> platform service -> folders / setup handoff
```

Suggested extraction, retaining the current external launcher entry points:

- `pc_ui.cpp`: keep context/input integration and overlay lifecycle initially. Separate rendering from OS actions.
- `pc_ui_theme.*`: palette, spacing, typography, focus/button drawing and background geometry. Start with procedural brick/paper shapes using ImGui's draw list; this is straightforward across the existing rendering backends and needs no new texture loader.
- `pc_ui_controls.*`: reusable setting rows, resolution choices, overrides and contextual help. Existing `setting_widget()` and capture helpers are the starting point.
- `pc_ui_pages.*`: six page renderers and a small table of keys, UI labels, page ownership, availability and application timing. Store added presentation metadata alongside, not inside a fork of the toolkit's registry.
- `pc_launcher_view.*`: Home and navigation state; a small `LauncherViewState` with route, selected category and return-focus target. The shell calls shared pages, instead of having separate Home/Advanced values.
- `pc_ui_actions.*`: platform actions and a resolved installation/data/log/setup context. Extend the current result/action boundary to request maintenance handoff after the UI loop exits.

Keep all existing INI names/defaults, unknown-key round-tripping, environment precedence, test bypass and launcher flags. In an overridden row show the effective value with “Set for this run by RECOMP_…” and disable editing; do not show a stored value as if it were active. Display mode and window size controls remain a presentation over the current settings.

Preserve save-as-you-change behavior. Show **Saved · applies when you press Play** in the prelaunch screen for startup-only changes. In the overlay distinguish **Applied now** from **Next launch**. The present `restart_needed()` snapshots only `RECOMP_SETTING_RESTART`, while aspect has mixed live/startup semantics, so application timing needs a setting-specific rule. Widescreen-to/from modes must report the actual next-launch behavior, not just the raw flag. Do not let the new theme or context teardown leave obsolete restart snapshots.

## Delivery sequence and acceptance

1. Extract shared controls/pages and presentation metadata; retain the current screen while checking persistence, overrides, bindings and timing.
2. Add Home, Advanced navigation, complete settings ownership and direct Mods/Controls routing. No default changes. Verify no option disappears.
3. Add the theme, fonts/fallbacks, procedural backdrop and compact overlay styling. Native screenshots on Windows, macOS and Steam Deck at normal/high DPI and 100/150/200% UI scale; test focus return, Back/Esc/B, shoulder tab switching and capture cancellation.
4. Add real pack inventory, folder/log actions and the setup handoff with installation-lock handling and missing-setup fallback. Verify installed and developer-checkout layouts separately.
5. Run relevant existing settings/input tests, launcher screenshot/input hooks and the quick regression. Verify the game launches with the chosen options, skip/force recovery works and the overlay changes the same values. Any changes to renderer/runtime behavior require the repository's full regression policy; UI-only work should not change those systems.

Completion requires a clean Home at Deck size, every current setting accounted for, consistent Home/Advanced/overlay values, correct effective override values, truthful live/next-launch labels, no save-affecting developer option enabled accidentally, real path/error handling and a setup handoff that does not deadlock against the play lock. No milestone definitions change as part of this proposal.

The separate in-conversation concept was inspected in the browser at desktop and 1024-pixel widths and at 320 pixels (288-pixel content, equal client/scroll width). Home/Advanced shared render-scale selection, all six categories, direct Mods routing, window-size disabling in fullscreen and both visual variants were checked; no browser console errors were reported. These checks validate the concept, not native launcher/gamepad behavior. No game was run and no native code changed.
