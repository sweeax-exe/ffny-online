# Maintaining the GitHub introduction

The README introduces the released port and directs players to installation,
settings, compatibility and contribution guides. Keep detailed instructions and
release-specific measurements in those guides rather than repeating them.

## Proposed About description

> Native Def Jam: Fight for NY port for Windows, macOS, Linux and Steam Deck. Static recompilation of the Xbox game, with modern controls and optional HD textures. Bring your own USA Xbox dump.

The repository owner can apply this text in GitHub's About settings after
review. About is repository metadata, so merging a documentation PR does not
change it. Suggested additions to the existing topics: `linux`, `steam-deck`,
`sdl3`. Keep version numbers and experimental features out of About.

## README structure

Use one H1 for the project name, H2 headings for stable subjects and H3 headings
for capabilities within a subject. Keep the page in player order: introduction
and setup action, gameplay, features, installation, customization, future
direction, compatibility, contributing, technical identity and credits.

Use ordinary Markdown and descriptive links. Small tables work well for
platform choices and project stages. Avoid deeply nested lists and large HTML
layout tables. Keep one gameplay still and one short preview in the README;
put additional examples in the [media index](media/README.md) or feature guides.

## Where information belongs

| Information | Maintained document | README treatment |
|---|---|---|
| Supported dump | [Dump manifest](../config/dump-manifest.json) | USA Xbox requirement and link |
| Install, update and repair | [Setup installers](setup-installer.md) and platform build guides | Setup choice and link |
| Setting names and defaults | [Settings reference](settings-reference.md) | Feature benefit and link |
| Texture formats and load order | [HD textures](hd-textures.md) | Released capability and link |
| User-visible limits | [Known issues](known-issues.md) | Material limits and link |
| Version changes and measurements | [Release notes](releases) and [GitHub releases](https://github.com/vyanhursky/DefJamFFNY-recomp/releases) | Latest-release link |
| Future work | Linked issues/PRs and plans | Released, beta, proposed or stretch stage |
| Capture provenance | [Media index](media/README.md) | Platform and texture context in captions |

Keep essential qualifiers beside the feature: up to four **local** players;
**4:3** remains available and **widescreen is beta** on all three platforms;
the supported frame-rate target is **60 fps**.
Do not confuse 120-Hz display support with 120-fps gameplay. The Windows present
interval is display Hz divided by 60, and the default flip timer is 60 Hz.
Fully unlocked frame rates remain future work because game timing must be
preserved. Diagnostic timer overrides are not supported higher-frame-rate play.

## Expanding mod support

Update the existing `Texture packs and mod support` subsection when new
capabilities ship. Promote it to `## Mod support` in the same position if it
becomes substantial. Avoid adding a permanent release announcement at the end
of the README.

Use a capability table when needed: texture replacements, data overrides,
managed installation/updates and scripts/code mods. Give each a stage,
supported platforms and a guide link. A prototype does not establish support
for arbitrary new fighters, stages or restored content.

Add separate player installation/use and author format/API guides as those
capabilities become available. Replace the proposal paragraph with released
support, leave remaining ambitions in future direction and add an actual
working example. Historical announcements belong in release notes.

## Release documentation pass

- Check README, About, the docs index, setup guides, known issues, settings
  reference and media captions against the released behavior.
- Move shipped features out of future direction and update platform qualifiers
  in the same change. Keep beta/proposal links current as work merges or changes.
- Distinguish build checks, scripted gameplay and owner playtesting. A CI badge
  alone does not establish every mode's compatibility.
- Refresh media when the demonstrated appearance or capability changes; an
  unchanged feature does not need a new capture for every version.
- Check links, heading anchors, media fingerprints and narrow-screen readability.
  Documentation-only edits need documentation/source checks, not game regressions.

Follow the [media capture plan](media/capture-plan.md) for the next visual refresh.
