# v0.5.1 release preparation

Candidate: [draft PR #4](https://github.com/vyanhursky/DefJamFFNY-recomp/pull/4),
`feat/release-setup`. Version: 0.5.1; proposed tag: `v0.5.1`.
Notes: `docs/releases/v0.5.1.md`. Tag annotation:

```text
Def Jam: Fight for NY — Recompiled v0.5.1

Game-free Windows setup, optional desktop shortcut, silent installation,
preserved saves/settings, responsive preparation and persistent diagnostics.
```

## Gates before tagging

Review/merge the PR and wait for green CI on the accepted main commit. The source
release validator requires tag/version/notes agreement, an exact tag checkout,
clean source, and main ancestry. An official tag on the current unmerged feature
tip would fail that policy. No local or remote release tag has been created.

Resolve the documented binary acceptance gates before distributing setup publicly:
clean Windows 11 provisioning, VS 2022/restart behavior, physical-machine wizard/
gameplay, Unicode paths, changed-toolkit updates, and signing/SmartScreen treatment.
See `release-setup-notes.md` for evidence and remaining limits.

## Tag and draft release after acceptance

Use a fresh public checkout, not the lane's inherited private origin:

```powershell
git clone --recursive https://github.com/vyanhursky/DefJamFFNY-recomp.git DefJam-release-0.5.1
Set-Location DefJam-release-0.5.1
git switch main
git pull --ff-only
python scripts/check-source-tree.py
git tag -a v0.5.1 -F docs/research/setup-release-tag-message.txt
python scripts/check-release.py v0.5.1
git push origin refs/tags/v0.5.1
```

Verify main is the reviewed accepted PR commit and the version is still 0.5.1
before running the tag command. Do not move/replace an existing published tag.
The push triggers the source release workflow, which runs CI and creates a draft
with `docs/releases/v0.5.1.md` and exactly these assets:

- `DefJamSetup-0.5.1-windows-x64.exe`
- `DefJamSetup-0.5.1-windows-x64.sha256`
- `DefJamSetup-0.5.1-windows-x64.provenance.json`

Inspect the tag, inventory, checksum, provenance and release notes before
publishing the draft. Artifact hashes must come from that tagged build; a local
feature-branch prototype is not substituted for tagged CI output.
