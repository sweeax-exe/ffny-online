/* Custom songs: finding the player's music files and keeping the Songs page's choices
 * (song_library.h). */
#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include "recomp_settings.h"
#include "song_convert.h"
#include "song_library.h"

static void scan_dir(const char *dir, SongEntry *out, int *n, int max, int depth)
{
    if (depth > 8 || *n >= max) return;
#if defined(_WIN32)
    WCHAR pattern[SONG_LIBRARY_PATH];
    char pat8[SONG_LIBRARY_PATH + 4];
    WIN32_FIND_DATAW fd;
    snprintf(pat8, sizeof pat8, "%s\\*", dir);
    if (!MultiByteToWideChar(CP_UTF8, 0, pat8, -1, pattern, SONG_LIBRARY_PATH)) return;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char name[SONG_LIBRARY_PATH], full[SONG_LIBRARY_PATH];
        if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, NULL, NULL)) continue;
        if (name[0] == '.') continue;
        if ((size_t)snprintf(full, sizeof full, "%s\\%s", dir, name) >= sizeof full) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) scan_dir(full, out, n, max, depth + 1);
        else if (song_is_music_file(full) && *n < max) snprintf(out[(*n)++].path, SONG_LIBRARY_PATH, "%s", full);
    } while (*n < max && FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while (*n < max && (e = readdir(d)) != NULL) {
        char full[SONG_LIBRARY_PATH];
        struct stat st;
        if (e->d_name[0] == '.') continue;
        if ((size_t)snprintf(full, sizeof full, "%s/%s", dir, e->d_name) >= sizeof full || stat(full, &st)) continue;
        if (S_ISDIR(st.st_mode)) scan_dir(full, out, n, max, depth + 1);
        else if (song_is_music_file(full)) snprintf(out[(*n)++].path, SONG_LIBRARY_PATH, "%s", full);
    }
    closedir(d);
#endif
}

static int entry_cmp(const void *a, const void *b)
{
    return strcmp(((const SongEntry *)a)->path, ((const SongEntry *)b)->path);
}

void song_library_id(const char *path, char *out, size_t size)
{
    uint32_t h = 2166136261u;
    for (const char *p = path; *p; p++) {
        char c = *p == '\\' ? '/' : (char)tolower((unsigned char)*p);
        h = (h ^ (uint8_t)c) * 16777619u;
    }
    snprintf(out, size, "u%08x", h);
}

void song_library_folder(const char *data_root, char *out, size_t size)
{
    snprintf(out, size, "%s" HOST_SEP "songs", data_root);
#if defined(_WIN32)
    WCHAR w[SONG_LIBRARY_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, out, -1, w, SONG_LIBRARY_PATH)) CreateDirectoryW(w, NULL);
#else
    mkdir(out, 0755);
#endif
}

static void library_path(const char *data_root, char *out, size_t size)
{
    snprintf(out, size, "%s" HOST_SEP "song-state" HOST_SEP "library.txt", data_root);
}

int song_library_scan(const char *data_root, SongEntry *out, int max)
{
    char root[SONG_LIBRARY_PATH], folders[RECOMP_SETTING_TEXT_MAX], path[SONG_LIBRARY_PATH], line[SONG_LIBRARY_PATH + 64];
    int n = 0;
    if (!data_root || !*data_root) return 0;
    snprintf(root, sizeof root, "%s" HOST_SEP "songs", data_root);
    scan_dir(root, out, &n, max, 0);
    recomp_settings_get_text("songs", "folders", folders, sizeof folders, "");
    for (char *p = folders; *p;) {
        char *semi = strchr(p, ';');
        if (semi) *semi = 0;
        while (*p == ' ') p++;
        if (*p) scan_dir(p, out, &n, max, 0);
        if (!semi) break;
        p = semi + 1;
    }
    qsort(out, (size_t)n, sizeof *out, entry_cmp);
    /* The same file reached twice (a folder inside another one listed): keep one. */
    int k = 0;
    for (int i = 0; i < n; i++)
        if (!k || strcmp(out[k - 1].path, out[i].path)) out[k++] = out[i];
    n = k;
    for (int i = 0; i < n; i++) {
        song_library_id(out[i].path, out[i].id, sizeof out[i].id);
        out[i].on = 1;
        out[i].interactive = 0;
    }
    library_path(data_root, path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char id[32], state[16], mode[16];
            if (sscanf(line, "%31s %15s %15s", id, state, mode) < 2) continue;
            for (int i = 0; i < n; i++)
                if (!strcmp(out[i].id, id)) {
                    out[i].on = strcmp(state, "off") != 0;
                    out[i].interactive = !strcmp(mode, "interactive");
                }
        }
        fclose(f);
    }
    return n;
}

int song_library_save(const char *data_root, const SongEntry *e, int n)
{
    char path[SONG_LIBRARY_PATH], part[SONG_LIBRARY_PATH + 8], dir[SONG_LIBRARY_PATH];
    library_path(data_root, path, sizeof path);
    snprintf(dir, sizeof dir, "%s" HOST_SEP "song-state", data_root);
#if defined(_WIN32)
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
    snprintf(part, sizeof part, "%s.part", path);
    FILE *f = fopen(part, "w");
    if (!f) return 0;
    fputs("# Your songs: which are in the game and how they play (written by the launcher's Songs page).\n"
          "# id on|off straight|interactive path\n", f);
    for (int i = 0; i < n; i++)
        fprintf(f, "%s %s %s %s\n", e[i].id, e[i].on ? "on" : "off", e[i].interactive ? "interactive" : "straight", e[i].path);
    int ok = fclose(f) == 0;
    remove(path);
    if (!ok || rename(part, path)) { remove(part); return 0; }
    return 1;
}
