/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* Media Foundation present? (media_check.h, ticket #193) */
#include "media_check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>

int media_foundation_present(char *missing, size_t size)
{
    static const wchar_t *const dlls[] = { L"MFPlat.dll", L"MFReadWrite.dll" };
    const char *test = getenv("RECOMP_TEST_NO_MEDIA_FOUNDATION");
    int forced = test && *test && strcmp(test, "0") != 0, ok = 1;
    size_t i, used = 0;
    if (missing && size) missing[0] = 0;
    for (i = 0; i < sizeof(dlls) / sizeof(dlls[0]); i++) {
        /* System32 only: a copy next to the game or on PATH is not the system's Media Foundation */
        HMODULE h = forced ? NULL : LoadLibraryExW(dlls[i], NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (h) {
            FreeLibrary(h);
            continue;
        }
        ok = 0;
        if (missing && size > used)
            used += (size_t)snprintf(missing + used, size - used, "%s%ls", used ? ", " : "", dlls[i]);
        if (used >= size) used = size ? size - 1 : 0;
    }
    return ok;
}

int media_check_startup(void)
{
    char missing[64];
    const char *settings = getenv("RECOMP_SETTINGS");
    if (media_foundation_present(missing, sizeof(missing))) return 1;
    fprintf(stderr, "[MEDIA] Media Foundation missing (%s): %s\n", missing, MEDIA_CHECK_MESSAGE);
    fflush(stderr);
    if (!(settings && !_stricmp(settings, "none")))
        MessageBoxA(NULL, MEDIA_CHECK_MESSAGE, "FFNY Online", MB_ICONERROR | MB_OK);
    return 0;
}

#else

int media_foundation_present(char *missing, size_t size)
{
    if (missing && size) missing[0] = 0;
    return 1;
}

int media_check_startup(void) { return 1; }

#endif
