# src/patches — gameplay patches (opt-in)

Reserved for behavioral changes to the game itself, such as a widescreen view.
Each goes behind a runtime setting so the unmodified path stays testable. CMake
picks up every `.c` file here. There are none yet: fixes that make the game run
belong in `src/hooks` or `src/recomp_manual.c`.
