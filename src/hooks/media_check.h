/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Windows N and KN editions ship without Media Foundation (ticket #193, build 112). The runtime's movie
 * player and WMA decoder use it, so the .exe loads MFPlat.dll and MFReadWrite.dll late (/DELAYLOAD, see
 * CMakeLists.txt) and checks for them first thing: a player without them gets this message instead of
 * Windows' own "MFPlat.DLL was not found". The setup's launcher (setup/main.cpp) says the same before it
 * starts the game. */
#ifndef DEFJAM_MEDIA_CHECK_H
#define DEFJAM_MEDIA_CHECK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_CHECK_MESSAGE \
    "Your Windows is an N edition without media components. Install the Media Feature Pack: " \
    "Settings > Apps > Optional features > Add a feature > Media Feature Pack, then restart."

/* 1 when Media Foundation can be loaded from System32 (always 1 off Windows). Otherwise 0, with the DLL(s)
 * missing named in `missing` ("MFPlat.dll", "MFPlat.dll, MFReadWrite.dll"). RECOMP_TEST_NO_MEDIA_FOUNDATION=1
 * (tests) reports both missing. */
int media_foundation_present(char *missing, size_t size);

/* WinMain's first check: with Media Foundation there, 1. Without it, the message (a message box; a test run,
 * RECOMP_SETTINGS=none, only writes it to stderr) and 0: the game must not go on, its first movie would end it. */
int media_check_startup(void);

#ifdef __cplusplus
}
#endif

#endif
