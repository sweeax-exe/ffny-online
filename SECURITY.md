# Security

## Supported versions

Only the latest release is supported. Fixes are made on `main` and shipped in
the next release.

## Reporting a vulnerability

Please do not open a public issue for a security problem. Report it privately
through GitHub's
[private vulnerability reporting](https://github.com/vyanhursky/DefJamFFNY-recomp/security/advisories/new).
Describe the problem, the version affected and how to reproduce it. Do not
include game data.

In scope: the Windows setup installer and its release artifacts, the build and
test scripts, the release workflow, and the port's own code in `src/`.

Problems in the generic runtime or translator belong to the
[toolkit](https://github.com/sp00nznet/xboxrecomp). The game's own code is not
maintained here.

## Verifying a download

Each release attaches a `.sha256` file and a `.provenance.json` file beside the
installer. Compare the installer's SHA-256 with the published one before running
it:

```powershell
Get-FileHash .\DefJamSetup-<version>-windows-x64.exe -Algorithm SHA256
```

The installer is unsigned, so Windows SmartScreen may warn about it.
