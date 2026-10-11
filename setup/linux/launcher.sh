#!/bin/sh
# Def Jam Recompiled -- plays the game this setup installed.
#
# Written by Def Jam Setup into the install folder; setup fills in the three
# paths below (shell-quoted) and rewrites the file on every install or repair.
# It holds the play lock for as long as the game runs (the descriptor survives
# the exec, so the lock lives exactly as long as the game), supplies the data
# folder and working directory, keeps the session's log and then becomes the
# game. Steam's non-Steam shortcut and the desktop entries both point here.
SOURCE=@SOURCE@
DATA=@DATA@
EXECUTABLE=@EXECUTABLE@
ROOT=$(dirname "$(readlink -f "$0")")

fail() {
    echo "Def Jam Recompiled: $1" >&2
    if [ -n "$DISPLAY$WAYLAND_DISPLAY" ]; then
        if command -v kdialog > /dev/null 2>&1; then
            kdialog --title "Def Jam Recompiled" --sorry "$1"
        elif command -v zenity > /dev/null 2>&1; then
            zenity --warning --title="Def Jam Recompiled" --text="$1"
        fi
    fi
    exit 1
}

[ -x "$EXECUTABLE" ] || fail "No completed installation was found. Run Def Jam Setup to install or repair it."

# Held for the life of the game: an update or repair cannot replace a running game.
exec 9>> "$ROOT/play.lock" || fail "The install folder is not writable."
flock -n 9 || fail "The game or an update is already running."

cd "$SOURCE" || fail "The installed game folder is missing. Run Def Jam Setup to repair it."
export DEFJAM_DATA="$DATA"

# Keep the ten newest session logs so a problem report always has the last run.
LOGS="${XDG_STATE_HOME:-$HOME/.local/state}/DefJamRecompiled/logs"
if mkdir -p "$LOGS" 2> /dev/null; then
    ls -1 "$LOGS" 2> /dev/null | grep '^play-.*\.log$' | sort | head -n -9 | while read -r old; do
        rm -f "$LOGS/$old" "$LOGS/$old.err"
    done
    STAMP=$(date +%Y%m%d-%H%M%S)
    exec "$EXECUTABLE" "$@" > "$LOGS/play-$STAMP.log" 2> "$LOGS/play-$STAMP.log.err"
fi
exec "$EXECUTABLE" "$@"
