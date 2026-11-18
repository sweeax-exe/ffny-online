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
(`Artist - Title.mp3`) and its folder (the album). Names shown in the game use plain ASCII.
Common Latin accents are removed, punctuation is simplified, and other unsupported characters
appear as `?` to fit the game's font.

## The Songs page

The launcher's **Songs** tab (Advanced) lists every music file it finds, with its title and
artist. **In game** chooses which songs the game takes (the first 27 ticked; the rest show "no
room"), **Plays** chooses how each one plays, **Open songs folder** opens `<data>/songs`, and
**More song folders** adds folders. The choices are kept in `<data>/song-state/library.txt` and
apply the next time the game starts. The restored song and your own songs can each be turned off
there too.

## How many

The game holds **60 songs**: its own 32, "Holler If Ya Hear Me" (a song cut from the release,
restored) and **27 of yours**. Past 27 the rest are left out; the log names them. At most **35
songs can be on** in the Beatbox at once (the game's playlist holds 36: playing a song that is
off is the way to the 36th); a song you tick past that stays off.

The game's own songs always stay: Story fights choose some of them by name (an opponent's song
plays first), so they cannot make room.

## How they play

Both modes play your song from start to end, in the menus and in fights. In a fight the game
jumps between parts of its music as the fight goes (the start, Blazin, knock-downs); with your
songs it keeps the place instead, so the song carries on where it was. A song that ends during a
fight starts again from the top.

**Straight** (the default): your song as it is, no filter.

**Interactive (experimental)**: the same, but the game switches your song between versions as
the fight goes (as it is, muffled, heavy on the bass, bright), every few seconds at most, without
losing its place. Which version plays when is the game's choice; try it and see.

The short clips the game plays on loading screens and as a fight starts are still the template
song's own (an instrumental for Straight, "Holler If Ya Hear Me" for Interactive).

## Next song, previous song

Any song, the game's or your own (`[music]`), in the menus and in fights, offline and online (only
your PC hears it):

| | Keyboard | Controller |
|---|---|---|
| Next song | M | `[audio] next_song_pad` (off by default) |
| Previous song | N | `[audio] prev_song_pad` (off by default) |

Previous song works as on a music player: the first press plays the current song from its start;
a second press within 3 seconds, or a press in the first 3 seconds of a song, goes to the song that
played before it, and further presses go on back. The game keeps a short history of what played
(its songs and yours, the last 16). A game song plays again from the playlist the game has now (in
a fight, the fight's playlist; in a Story fight that is the opponent's song first, then your
Beatbox list), so a song that is not on it is passed over; with nothing earlier to go back to, the
song starts again. The game's song banner shows the song, and a short "Now playing" note names it.

Change the keys or turn them off on the launcher's Basics page (Music) or in
[`[audio]`](settings-reference.md). Test aid: `RECOMP_TEST_NEXT_SONG=40,55,b70,b71` asks for the next
song (a number) or the previous song (`b` and a number) that many seconds in.

Settings: [`[songs]`](settings-reference.md). How it works: [research notes](research/custom-songs.md).
