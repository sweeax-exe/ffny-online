# Audio decoders (vendored)

Single-file decoders that read the player's own music files for custom songs
(`src/hooks/song_convert.c`, docs/research/custom-songs.md). Compiled into the game; nothing is
downloaded at runtime. Approved by the owner on 2026-11-05. Each file is the unmodified file from
its upstream release tag; update by replacing the file and these lines.

| File | Upstream | Release | Commit | SHA-256 | Licence |
|---|---|---|---|---|---|
| `dr_mp3.h` | github.com/mackron/dr_libs | `mp3-0.7.3` (2026-01-17) | `5690d4671d7ad07ae6021756d7222eb159745f06` | `d80e6800ddde29d50988ce894affe601858802d1bb8f047f98b84b33ae66c11a` | public domain or MIT-0 |
| `dr_flac.h` | github.com/mackron/dr_libs | `flac-0.13.3` (2026-01-17) | `69d777c482775858e8ea8a7b047c9bcd451febc8` | `a35468f278f17cf9538ce189debf76a5854a77e139980f7b5185d67566a0c837` | public domain or MIT-0 |
| `dr_wav.h` | github.com/mackron/dr_libs | `wav-0.14.5` (2026-03-03) | `fa931f3285ced10ace628f7f1ac951e1951e7ea6` | `c69816d0e0ac6cab3df8bf2f42df451d4531c2bfa34f7d7a69e959e6ed399038` | public domain or MIT-0 |
| `stb_vorbis.c` | github.com/nothings/stb | 1.22 (2021-07-11) | `1ee679ca2ef753a528db5ba6801e1067b40481b8` | `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636` | public domain or MIT |

Each file carries its full licence text at its end.
