# Dump your own disc

FFNY Online is built from **your own copy** of the game: the setup reads a dump of your disc,
checks it and compiles the game on your PC. Nothing from the game is downloaded or included,
and this project does not help anyone get the game any other way.

## Which copy works

Only the **original Xbox version, USA (NTSC-U)**, of *Def Jam: Fight for NY* (title ID
`45410049`). The setup compares your dump with the [manifest](../config/dump-manifest.json)
and stops if it is a different release.

- **PS2 and GameCube** discs cannot be used: the port is built from the Xbox program.
- Other regions (PAL) are not supported yet.

## What the setup accepts

Either of these, made from your disc:

- a **disc image**: an `.iso` in the Xbox format (often called XISO) or a full-disc image, or
- an **extracted dump**: the game's folder with `default.xbe` and the folders beside it.

## Ways to make it

An original Xbox disc cannot be read in an ordinary PC DVD drive: the game data sits in a
part of the disc that normal drives do not show. These are the usual ways for your own disc:

1. **A modified original Xbox.** If you have a softmodded or hardmodded Xbox, copy the game
   from the disc to its hard disk with the dashboard you use (most offer "copy game to hard
   disk" or a file manager), then copy that game folder to your PC over FTP. That folder is an
   extracted dump.
2. **A PC DVD drive with Kreon firmware.** Some older Samsung, Philips and Lite-On drives can
   run a firmware that reads Xbox discs; with an Xbox disc backup tool it makes a disc image of
   your disc. This is how preservation projects dump Xbox discs.
3. **You already have an image** of your own disc, for example the one you use with an Xbox
   emulator: you can use that file directly.

## Then

Run the setup, choose your image or folder, and follow the wizard
([Windows setup](setup-installer.md)). If the check fails, the setup says which file or size
does not match; it is almost always a different release or an incomplete copy.

Please never share game files or ask for them in this project's issues or discussions.
