/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* The launcher's update download: one at a time per Windows session, in a folder found by its wide
 * path (src/hooks/updater.c; tests/unit/test_updater_guard_native.py). Windows only, header only. */
#pragma once
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

/* One download and install at a time on this PC's session: the tray launcher and a launcher opened
 * in front both downloaded the same setup into %TEMP%\DefJamUpdate, the second one's CreateFileW
 * (no sharing) failed, and its player read "Could not prepare the download folder" (build 53 reports).
 * The named mutex is held for the download and the install; WAIT_ABANDONED (a launcher that closed
 * while it held it) hands it over. */
#define UPDATE_MUTEX L"Local\\FFNY-Online-Update-Download"

/* Takes the update lock: 1 and *held (to give to update_unlock; NULL when no mutex could be made, which
 * goes on without one, as before), or 0 when another launcher holds it. */
static int update_lock(HANDLE *held)
{
    HANDLE m = CreateMutexW(NULL, FALSE, UPDATE_MUTEX);
    DWORD w;
    *held = NULL;
    if (!m) return 1;
    w = WaitForSingleObject(m, 0);
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) {
        *held = m;
        return 1;
    }
    CloseHandle(m);
    return w == WAIT_TIMEOUT ? 0 : 1;
}

static void update_unlock(HANDLE held)
{
    if (!held) return;
    ReleaseMutex(held);
    CloseHandle(held);
}

/* `<temp>\DefJamUpdate\<name>` (or the folder itself for a NULL or empty name) as a wide path:
 * GetTempPathW, since the user's temp folder may hold letters the ANSI code page has not (GetTempPathA
 * before build 54). Returns 0 when it does not fit in cap characters. */
static int update_path(wchar_t *out, size_t cap, const wchar_t *name)
{
    wchar_t tmp[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, tmp);
    int k;
    if (!n || n > MAX_PATH) return 0;
    k = _snwprintf(out, cap, name && name[0] ? L"%sDefJamUpdate\\%s" : L"%sDefJamUpdate", tmp, name ? name : L"");
    return k > 0 && (size_t)k < cap;
}
