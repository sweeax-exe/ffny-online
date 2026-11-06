# Custom songs

Your own music plays in the game like its soundtrack: in the **Beatbox** jukebox, in the menus
(with the song banner in the corner), on the playlist in fights. The port ships no music; you add
your own files, and they never leave your computer.

## Adding songs

1. Put music files in the **songs** folder of the data folder (`<data>/songs`; subfolders are
   fine), or list other folders in `[songs] folders` in `settings.ini`, separated by semicolons.
   Formats: **MP3, FLAC, Ogg Vorbis, WAV**.
2. Start the game. The first start after adding a song converts it into the game's own format
   (a few seconds a song; the copies go in `<data>/cache/songs`). Later starts reuse them until the
   file changes.
3. Open **Beatbox** from the main menu: your songs are in the list, on, after the game's own.

The title, artist and album come from the file's tags; without tags, from the file name
(`Artist - Title.mp3`) and its folder (the album).

## How many

The game holds **60 songs**: its own 32, "Holler If Ya Hear Me" (a song cut from the release,
restored) and **27 of yours**. Past 27 the rest are left out; the log names them. At most **35
songs can be on** in the Beatbox at once (the game's playlist holds 36: playing a song that is
off is the way to the 36th); a song you tick past that stays off.

The game's own songs always stay: Story fights choose some of them by name (an opponent's song
plays first), so they cannot make room.

## How they play

A song is fitted into the frame of one of the game's own (its interactive map): it plays
start to finish in the menus, and in fights its middle loops until the fight is over, like the
game's songs. The short clips the game plays on loading screens and as a fight starts are still
that template song's own.

Settings: [`[songs]`](settings-reference.md). How it works: [research notes](research/custom-songs.md).
