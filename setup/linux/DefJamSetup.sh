#!/bin/sh
# Def Jam Setup for Linux. Double-click (or run) this file to open the wizard;
# see "READ ME FIRST.txt" for silent installs. It runs the Python that came
# with this setup, never the system's.
here=$(dirname "$(readlink -f "$0")")
exec "$here/payload/python/bin/python3" -I -B "$here/payload/engine/wizard.py" --payload "$here/payload" "$@"
