/*
 * The port's player-facing settings and start-up paths.
 *
 * Until M6 the program only ran correctly from scripts/run.ps1: it needed four
 * environment variables, the repository root as its working directory and
 * DEFJAM_DATA for the saves. This file makes the executable start by itself,
 * and gives a player one file to edit instead of environment variables.
 *
 *   settings.ini   in the data folder (beside `extracted` and `save`), or
 *                  beside the executable when there is no data folder.
 *                  RECOMP_SETTINGS=<path> names another file;
 *                  RECOMP_SETTINGS=none uses the defaults and writes nothing,
 *                  which is what the test harness does so a player's choices
 *                  never change a regression run.
 *
 * Precedence is the settings library's: default, then the file, then the
 * setting's environment variable. Every RECOMP_* variable still works.
 */

#define _CRT_SECURE_NO_WARNINGS
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recomp_settings.h"
#include "d3d8_present.h"
#include "pc_settings.h"
#include "pc_input.h"
#include "pc_ui.h"
#include "netplay.h"

static const char *const k_aspect[] = { "4:3", "stretch", NULL };
static const char *const k_filter[] = { "smooth", "sharp", NULL };
static const char *const k_aa[] = { "off", "on", NULL };
static const char *const k_anisotropy[] = { "off", "2x", "4x", "8x", "16x", NULL };
static const char *const k_voice[] = { "off", "push_to_talk", "open", NULL };
static const char *const k_voice_pad[] = { "off", "back", "left_stick", "right_stick", "dpad_up", "dpad_down", NULL };

static RecompSetting g_display_settings[] = {
    { "textures", "enabled", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
      "Use texture packs from the data folder's mods directory." },
    { "textures", "packs", RECOMP_SETTING_STRING, 0, 0, 0, NULL, "RECOMP_TEXTURE_PACKS", RECOMP_SETTING_RESTART,
      "Pack folder names separated by semicolons. Later packs take precedence.", "faithful-hd" },
    { "textures", "cache_mb", RECOMP_SETTING_INT, 512, 32, 4096, NULL, "RECOMP_TEXTURE_CACHE_MB", RECOMP_SETTING_RESTART,
      "Memory budget for replacement textures in MiB, including their retained upload data." },
    { "textures", "dump", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
      "Capture source textures for making a pack, in hd-work/runtime under the data folder." },
    { "textures", "dump_limit", RECOMP_SETTING_INT, 2000, 0, 65536, NULL, "RECOMP_TEXTURE_DUMP_LIMIT", RECOMP_SETTING_RESTART,
      "Maximum new source images captured in one run." },
    { "online", "server", RECOMP_SETTING_STRING, 0, 0, 0, NULL, "RECOMP_NETPLAY_SERVER", 0,
      "The server for online play: auto (the one with the lowest ping, Europe or North America), eu, na, or a "
      "relay's own address as host or host:port (port 27777 if left out). Accounts and stats stay on Europe.", "auto" },
    { "online", "name", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, 0,
      "Your name, shown to the other player.", "Player" },
    { "online", "fighter", RECOMP_SETTING_INT, 55, 0, 255, NULL, NULL, 0,
      "The fighter you pick in online rooms (fighter.xml ID); chosen in the room." },
    { "online", "voice", RECOMP_SETTING_ENUM, 1, 0, 0, k_voice, NULL, 0,
      "Voice chat with the players in your online room, in the room and in the fight: off, push_to_talk (hold the "
      "key below) or open (sends while you speak; use a headset)." },
    { "online", "voice_key", RECOMP_SETTING_STRING, 0, 0, 0, NULL, NULL, 0,
      "The push-to-talk key: a letter or digit, F1-F12, Tab, CapsLock, LAlt, Mouse4 or Mouse5.", "T" },
    { "online", "voice_pad", RECOMP_SETTING_ENUM, 0, 0, 0, k_voice_pad, NULL, 0,
      "Push to talk on the controller as well: off, back, left_stick or right_stick (a click), dpad_up or "
      "dpad_down. The game sees the press too." },
    { "online", "voice_volume", RECOMP_SETTING_INT, 100, 0, 200, NULL, NULL, 0,
      "Volume of the other players' voices in percent; 0 mutes them." },
    { "online", "voice_mic_volume", RECOMP_SETTING_INT, 100, 0, 400, NULL, NULL, 0,
      "How loud your own voice goes out, in percent (after the automatic level, if that is on)." },
    { "online", "voice_auto_level", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, NULL, 0,
      "Bring your voice to a common loudness automatically (quiet headset microphones up to 6x)." },
    { "online", "rollback", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, NULL, RECOMP_SETTING_RESTART,
      "Beta: rollback netcode in online fights, used only when every player in the match has it on. Your presses "
      "count at once and the game corrects itself when the other player's arrive late. It can still crash." },
    { "online", "input_delay", RECOMP_SETTING_INT, 3, 1, 8, NULL, NULL, 0,
      "Steps (1/60 s each) between a press and its effect in an online fight. More hides more ping; the host's value is used." },
    { "display", "fullscreen", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, NULL, 0,
      "Borderless full screen. Alt+Enter or F11 switches while playing." },
    { "display", "window_width", RECOMP_SETTING_INT, 1280, 320, 16384, NULL, NULL, 0,
      "Window size when not full screen. Resizing the window updates these." },
    { "display", "window_height", RECOMP_SETTING_INT, 960, 240, 16384, NULL, NULL, 0, NULL },
    { "display", "aspect", RECOMP_SETTING_ENUM, 0, 0, 0, k_aspect, NULL, 0,
      "4:3 keeps the console's picture shape with black bars; stretch fills the window." },
    { "display", "widescreen", RECOMP_SETTING_BOOL, 0, 0, 0, NULL, "RECOMP_WIDESCREEN", RECOMP_SETTING_RESTART,
      "Experimental: a 16:9 picture with a wider view of the 3D scene. Menus and the HUD are drawn wider too." },
    { "display", "render_scale", RECOMP_SETTING_INT, 2, 1, 4, NULL, "RECOMP_RENDER_SCALE",
      RECOMP_SETTING_RESTART,
      "Internal resolution as a multiple of the console's 640x480. Higher is sharper and costs GPU time." },
    { "display", "filter", RECOMP_SETTING_ENUM, 0, 0, 0, k_filter, NULL, 0,
      "How the picture is scaled to the window." },
    { "display", "anti_aliasing", RECOMP_SETTING_ENUM, 1, 0, 0, k_aa, NULL, 0,
      "Average every rendered pixel into the window and soften remaining jagged edges." },
    { "display", "sharpen", RECOMP_SETTING_INT, 30, 0, 100, NULL, NULL, 0,
      "Sharpen the picture, more where it is soft and less on strong edges. 0 turns it off." },
    { "display", "anisotropy", RECOMP_SETTING_ENUM, 4, 0, 0, k_anisotropy, NULL, 0,
      "Texture filtering for surfaces seen at a slant, such as floors: higher keeps more detail." },
    { "display", "vsync", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_PRESENT_VSYNC", 0,
      "Pace frames on the display for even motion. Used when the refresh rate is a multiple of 60 (60, 120, 180, 240 Hz); other displays use the game's own 60 fps timer." },
    { "display", "gamma", RECOMP_SETTING_BOOL, 1, 0, 0, NULL, "RECOMP_GAMMA",
      RECOMP_SETTING_RESTART,
      "Apply the game's own brightness curve, as the console does." },
};
#define DISPLAY_SETTING_COUNT (sizeof(g_display_settings) / sizeof(g_display_settings[0]))

/* The display table followed by the input tables (pc_input.c): one array, one
 * registration, one file. */
static RecompSetting g_settings[DISPLAY_SETTING_COUNT + 64 + 16];
static size_t g_setting_count;

static char g_path[MAX_PATH * 4];   /* empty: do not load or save */
static void (*g_fullscreen_notify)(int fullscreen);
static void (*g_window_size_notify)(int width, int height);

static void settings_log(const char *message)
{
    fprintf(stderr, "[SETTINGS] %s\n", message);
}

#if defined(_WIN32)

static int file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void strip_last(char *path)
{
    char *slash = strrchr(path, '\\');
    if (slash) *slash = 0;
}

/* Find the folder that holds `game\default.xbe` and make it current: here,
 * else beside the executable, else up to four levels above it (the build tree
 * is build\<preset>\ under the repository root). */
static int find_game_root(void)
{
    char dir[MAX_PATH], probe[MAX_PATH + 32];
    int up;

    if (file_exists("game\\default.xbe"))
        return 1;
    if (!GetModuleFileNameA(NULL, dir, sizeof(dir)))
        return 0;
    strip_last(dir);
    for (up = 0; up <= 4 && strchr(dir, '\\'); up++) {
        snprintf(probe, sizeof(probe), "%s\\game\\default.xbe", dir);
        if (file_exists(probe))
            return SetCurrentDirectoryA(dir) != 0;
        strip_last(dir);
    }
    return 0;
}

/* The data folder: DEFJAM_DATA, or the parent of the dump when the dump is a
 * folder named `extracted` (the layout docs/build-and-play.md sets up). Sets
 * DEFJAM_DATA when it was derived, so the save path in main.c follows. */
static int find_data_root(char *root, size_t size)
{
    const char *env = getenv("DEFJAM_DATA");
    char real[MAX_PATH];
    HANDLE h;
    DWORD n;
    const char *p;
    char *name;

    if (env && *env) {
        snprintf(root, size, "%s", env);
        return 1;
    }
    /* `game` is usually a junction; ask where it really is. */
    h = CreateFileA("game", 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    n = GetFinalPathNameByHandleA(h, real, sizeof(real), FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (!n || n >= sizeof(real))
        return 0;
    p = strncmp(real, "\\\\?\\", 4) == 0 ? real + 4 : real;
    snprintf(root, size, "%s", p);
    name = strrchr(root, '\\');
    if (!name || _stricmp(name + 1, "extracted") != 0)
        return 0;
    *name = 0;
    _putenv_s("DEFJAM_DATA", root);
    return 1;
}

/* Started by a double click there is nothing to print to: keep a log, as
 * scripts/run.ps1 does. A caller that redirected the output keeps its own. */
static void open_log_if_needed(void)
{
    HANDLE out = GetStdHandle(STD_ERROR_HANDLE);
    char name[MAX_PATH];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN)
        return;
    CreateDirectoryA("logs", NULL);
    strftime(name, sizeof(name), "logs\\run-%Y%m%d-%H%M%S.log", t);
    freopen(name, "w", stdout);
    strcat(name, ".err");
    freopen(name, "w", stderr);
    /* Buffered, as main.c sets the streams it was given. */
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    setvbuf(stderr, NULL, _IOFBF, 1 << 16);
}

static int program_dir(char *out, size_t size)
{
    if (!GetModuleFileNameA(NULL, out, (DWORD)size))
        return 0;
    strip_last(out);
    return 1;
}

#else

#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void strip_last(char *path)
{
    char *slash = strrchr(path, '/');
    if (slash) *slash = 0;
}

/* The running program's own path, symbolic links resolved. */
static int program_path(char *out, size_t size)
{
    char raw[PATH_MAX], real[PATH_MAX];
#if defined(__APPLE__)
    uint32_t n = sizeof(raw);
    if (_NSGetExecutablePath(raw, &n))
        return 0;
#else
    ssize_t n = readlink("/proc/self/exe", raw, sizeof(raw) - 1);
    if (n <= 0)
        return 0;
    raw[n] = 0;
#endif
    if (!realpath(raw, real))
        return 0;
    snprintf(out, size, "%s", real);
    return 1;
}

/* As on Windows: here, else beside the program, else up to four levels above
 * it (the build tree is build/<preset>/ under the repository root). */
static int find_game_root(void)
{
    char dir[PATH_MAX], probe[PATH_MAX + 32];
    int up;

    if (file_exists("game/default.xbe"))
        return 1;
    if (!program_path(dir, sizeof(dir)))
        return 0;
    strip_last(dir);
    for (up = 0; up <= 4 && dir[0]; up++) {
        snprintf(probe, sizeof(probe), "%s/game/default.xbe", dir);
        if (file_exists(probe))
            return chdir(dir) == 0;
        strip_last(dir);
    }
    return 0;
}

/* DEFJAM_DATA, or the parent of the dump when `game` is a symbolic link to a
 * folder named `extracted`. */
static int find_data_root(char *root, size_t size)
{
    const char *env = getenv("DEFJAM_DATA");
    char real[PATH_MAX];
    char *name;

    if (env && *env) {
        snprintf(root, size, "%s", env);
        return 1;
    }
    if (!realpath("game", real))
        return 0;
    snprintf(root, size, "%s", real);
    name = strrchr(root, '/');
    if (!name || strcasecmp(name + 1, "extracted") != 0)
        return 0;
    *name = 0;
    setenv("DEFJAM_DATA", root, 1);
    return 1;
}

/* Started from a file manager there is no terminal: keep a log then. */
static void open_log_if_needed(void)
{
    char name[PATH_MAX];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    struct stat st;

    /* A terminal, a pipe or a file the caller chose all keep the output;
     * only /dev/null, which is what a launcher hands a program, loses it. */
    if (fstat(STDERR_FILENO, &st) == 0
            && !(S_ISCHR(st.st_mode) && !isatty(STDERR_FILENO)))
        return;
    mkdir("logs", 0755);
    strftime(name, sizeof(name), "logs/run-%Y%m%d-%H%M%S.log", t);
    freopen(name, "w", stdout);
    strcat(name, ".err");
    freopen(name, "w", stderr);
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    setvbuf(stderr, NULL, _IOFBF, 1 << 16);
}

static int program_dir(char *out, size_t size)
{
    if (!program_path(out, size))
        return 0;
    strip_last(out);
    return 1;
}

#endif

/* What scripts/run.ps1 and the harness have always set: the vertical-blank
 * interrupt, the push-buffer executor, its Direct3D 11 output and the USB
 * model. Each is "on if set", so a diagnostic run that needs one off sets
 * RECOMP_NO_DEFAULTS=1 and names the ones it wants. */
static void supply_runtime_defaults(void)
{
    static const char *const names[] = {
        "RECOMP_VBLANK", "RECOMP_PB_EXEC", "RECOMP_PB_D3D11", "RECOMP_USB" };
    size_t i;
    if (getenv("RECOMP_NO_DEFAULTS"))
        return;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!getenv(names[i]))
            _putenv_s(names[i], "1");
}

int pc_display(const char *key, int fallback)
{
    return recomp_settings_get("display", key, fallback);
}

#if defined(_WIN32)
void pgraph_d3d11_set_anisotropy(int max_anisotropy);   /* toolkit nv2a_pgraph_d3d11.h */
void d3d8_Nv2aSetFlatSqueeze(float squeeze);           /* toolkit nv2a_pb_exec.c */
#endif

/* The widescreen camera (src/recomp_manual.c sub_0001A4E0) takes the setting once, when the
 * title first builds its camera, after the launcher. From then on the picture's shape follows
 * what the camera took, not a later change in the overlay (that one waits for a restart). */
static volatile LONG g_widescreen_latched = -1;

int pc_widescreen_latch(void)
{
    if (g_widescreen_latched < 0) {
        InterlockedCompareExchange(&g_widescreen_latched, pc_display("widescreen", 0) != 0, -1);
        pc_settings_apply_display();
    }
    return (int)g_widescreen_latched;
}

void pc_settings_apply_display(void)
{
    int widescreen = g_widescreen_latched >= 0 ? (int)g_widescreen_latched : pc_display("widescreen", 0) != 0;
    d3d8_present_set_aspect(pc_display("aspect", 0) == 0, widescreen ? 16 : 4, widescreen ? 9 : 3);
    d3d8_present_set_linear_filter(pc_display("filter", 0) == 0);
    d3d8_present_set_vsync(pc_display("vsync", 1));
#if defined(_WIN32)
    /* Direct3D 11 only for now; the Vulkan renderer keeps its plain scale. */
    d3d8_present_set_post(pc_display("anti_aliasing", 1), pc_display("sharpen", 30));
    pgraph_d3d11_set_anisotropy(1 << pc_display("anisotropy", 4));   /* off, 2x .. 16x */
    /* Anamorphic 16:9: menus, HUD and movies are drawn three quarters as wide, so the
     * stretch to 16:9 gives them back their shape, centred. */
    d3d8_Nv2aSetFlatSqueeze(widescreen ? 0.75f : 1.0f);
#endif
}

static void setting_changed(const RecompSetting *s, void *user)
{
    (void)user;
    pc_settings_apply_display();
    pc_input_apply();
    pc_ui_apply_settings();
    if (!strcmp(s->section, "display") && !strcmp(s->key, "fullscreen") && g_fullscreen_notify)
        g_fullscreen_notify(s->value);
    if (!strcmp(s->section, "display") && (!strcmp(s->key, "window_width") || !strcmp(s->key, "window_height")) &&
        g_window_size_notify)
        g_window_size_notify(pc_display("window_width", 1280), pc_display("window_height", 960));
}

void pc_settings_on_fullscreen(void (*notify)(int fullscreen))
{
    g_fullscreen_notify = notify;
}

void pc_settings_on_window_size(void (*notify)(int width, int height))
{
    g_window_size_notify = notify;
}

const char *pc_settings_path(void) { return g_path; }

void pc_settings_save(void)
{
    if (g_path[0] && recomp_settings_save(g_path))
        fprintf(stderr, "[SETTINGS] could not write %s\n", g_path);
}

void pc_display_set(const char *key, int value)
{
    if (recomp_settings_set("display", key, value) > 0 && g_path[0]) {
        if (recomp_settings_save(g_path))
            fprintf(stderr, "[SETTINGS] could not write %s\n", g_path);
    }
}

/* The launcher shows on every launch unless the player turned it off (docs/launcher in
 * pc_ui.h has the exact rule). It edits the settings, so the input layer is started again
 * afterwards and anything read once at start-up is read after it. */
#if !defined(_WIN32)
int host_posix_have_video(void);       /* src/host_posix.c */
int host_posix_shift_held(void);

static const char *const *g_argv;
static int g_argc;

void pc_settings_set_args(int argc, char **argv)
{
    g_argc = argc;
    g_argv = (const char *const *)argv;
}

static int has_arg(const char *flag)
{
    int i;
    for (i = 1; i < g_argc; i++)
        if (g_argv[i] && !strcmp(g_argv[i], flag))
            return 1;
    return 0;
}
#endif

#if defined(_WIN32)
static WCHAR *g_start_env;                  /* the environment as the process got it */
static WCHAR g_start_dir[MAX_PATH * 2];     /* and its working directory */

void pc_settings_capture_start(void)
{
    LPWCH env = GetEnvironmentStringsW();
    if (env) {
        size_t n = 0;
        while (env[n] || env[n + 1]) n++;
        n += 2;
        g_start_env = (WCHAR *)malloc(n * sizeof(WCHAR));
        if (g_start_env) memcpy(g_start_env, env, n * sizeof(WCHAR));
        FreeEnvironmentStringsW(env);
    }
    if (!GetCurrentDirectoryW(MAX_PATH * 2, g_start_dir)) g_start_dir[0] = 0;
}

/* After the launcher, the game runs in a fresh process, started exactly as with the launcher
 * skipped (the same command line plus --no-launcher, the environment and directory this one was
 * started with), and this process waits for it and exits with its code. The launcher's
 * Direct3D 11, fonts, artwork, input, update check and sign-in leave threads and allocations
 * behind; some of them land in the guest's low 4 GB the moment start-up releases it for the
 * memory layout, and on some PCs the game then did not start at all. Players found that ticking
 * "Skip the launcher" made it work (2026-10-21). Waiting keeps DefJamLauncher's play.lock held
 * for the game's whole life, as before. Returns only when the process could not be started (the
 * game then starts here, as it used to). */
static void start_game_process(void)
{
    static WCHAR exe[32768];
    const WCHAR *line = GetCommandLineW();
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    size_t n;
    WCHAR *cmd;
    if (!GetModuleFileNameW(NULL, exe, 32768)) return;
    n = wcslen(line) + 32;
    cmd = (WCHAR *)malloc(n * sizeof(WCHAR));
    if (!cmd) return;
    swprintf(cmd, n, L"%ls --no-launcher", line);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    pc_input_stop();                /* the controllers go to the game's process */
    netplay_stop();                 /* and so does any online session (one player, one session) */
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, g_start_env ? CREATE_UNICODE_ENVIRONMENT : 0,
                        g_start_env, g_start_dir[0] ? g_start_dir : NULL, &si, &pi)) {
        fprintf(stderr, "[UI] could not start the game in its own process (error %lu); starting it here\n",
                (unsigned long)GetLastError());
        fflush(stderr);
        free(cmd);
        return;
    }
    free(cmd);
    fprintf(stderr, "[UI] the game runs in its own process (%lu); this one waits for it\n",
            (unsigned long)pi.dwProcessId);
    fflush(stderr);
    CloseHandle(pi.hThread);
    /* In the background meanwhile (tray icon, update checks); when the game closes the launcher
     * opens again, offering an update found in the meantime. */
    int update = pc_launcher_watch(pi.hProcess);
    n = wcslen(line) + 32;
    cmd = (WCHAR *)malloc(n * sizeof(WCHAR));
    if (cmd) {
        swprintf(cmd, n, L"%ls --launcher", line);
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        PROCESS_INFORMATION again;
        if (CreateProcessW(exe, cmd, NULL, NULL, FALSE, g_start_env ? CREATE_UNICODE_ENVIRONMENT : 0,
                           g_start_env, g_start_dir[0] ? g_start_dir : NULL, &si, &again)) {
            fprintf(stderr, "[UI] the game closed%s: the launcher opens again\n",
                    update ? " with an update waiting" : "");
            fflush(stderr);
            CloseHandle(again.hThread);
            CloseHandle(again.hProcess);
        }
        free(cmd);
    }
    GetExitCodeProcess(pi.hProcess, &code);
    ExitProcess(code);
}
#endif

void test_control_start(void);  /* src/hooks/test_control.c */

static void run_launcher_if_wanted(void)
{
    test_control_start();       /* tests only (RECOMP_CONTROL_PORT): the live control channel */
    netplay_autostart();        /* tests only: already connecting while the launcher shows */
#if defined(_WIN32)
    const char *settings = getenv("RECOMP_SETTINGS");
    const char *cmd = GetCommandLineA();
    int test_run = (settings && !_stricmp(settings, "none")) ||
                   (getenv("RECOMP_PAD_SCRIPT") && !getenv("RECOMP_PAD_HOST"));
    int force = cmd && strstr(cmd, "--launcher") != NULL;
    int no = cmd && strstr(cmd, "--no-launcher") != NULL;
    int shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    if (!pc_launcher_wanted(recomp_settings_get("launcher", "skip", 0), force && !no, no, shift, test_run))
        return;
    if (!pc_launcher_run()) {
        fprintf(stderr, "[UI] the launcher was closed; not starting the game\n");
        fflush(stderr);
        ExitProcess(0);
    }
    if (!getenv("RECOMP_LAUNCHER_SAME_PROCESS")) start_game_process();
    pc_input_restart();
#else
    const char *settings = getenv("RECOMP_SETTINGS");
    int test_run = (settings && !strcasecmp(settings, "none")) ||
                   (getenv("RECOMP_PAD_SCRIPT") && !getenv("RECOMP_PAD_HOST")) ||
                   getenv("RECOMP_HEADLESS") != NULL;
    int force = has_arg("--launcher");
    int no = has_arg("--no-launcher");

    /* Nothing to show it in without a display; the game itself then runs off screen. */
    if (!host_posix_have_video())
        return;
    if (!pc_launcher_wanted(recomp_settings_get("launcher", "skip", 0), force && !no, no,
                            host_posix_shift_held(), test_run))
        return;
    if (!pc_launcher_run()) {
        fprintf(stderr, "[UI] the launcher was closed; not starting the game\n");
        fflush(stderr);
        pc_input_stop();
        fflush(stdout);
        _Exit(0);
    }
    pc_input_restart();
#endif
}

/* debug-env.txt in the data folder: one RECOMP_NAME=value per line ('#' starts a comment), for a
 * player to try a runtime switch that has no setting, such as RECOMP_SKIP_RT_SAMPLE=1, when we ask
 * them to. Only RECOMP_ names, and never over a value the environment already has. */
static void load_debug_env(const char *root)
{
    char path[MAX_PATH * 2], line[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s" HOST_SEP "debug-env.txt", root);
    f = fopen(path, "r");
    if (!f) {   /* a file made in Explorer with the extensions hidden is debug-env.txt.txt */
        snprintf(path, sizeof(path), "%s" HOST_SEP "debug-env.txt.txt", root);
        f = fopen(path, "r");
    }
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char *eq, *end = line + strlen(line);
        while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) *--end = 0;
        if (strncmp(line, "RECOMP_", 7) || !(eq = strchr(line, '=')) || eq - line > 64) continue;
        *eq = 0;
        if (getenv(line)) continue;
#if defined(_WIN32)
        _putenv_s(line, eq + 1);
#else
        setenv(line, eq + 1, 0);
#endif
        fprintf(stderr, "[SETTINGS] debug-env.txt: %s=%s\n", line, eq + 1);
    }
    fclose(f);
}

int pc_settings_init(void)
{
    char root[MAX_PATH * 2];
    const char *env = getenv("RECOMP_SETTINGS");
    int have_game = find_game_root();
    int have_root, read;

    if (have_game)
        open_log_if_needed();
    if (!have_game) {
        host_fatal(
            "The game files were not found.\n\n"
            "Create a folder, junction or symbolic link named 'game' beside this program\n"
            "(or in the repository root) that holds your own extracted dump, with\n"
            "default.xbe in it. See docs/build-and-play.md.");
        return 0;
    }
    supply_runtime_defaults();
    have_root = find_data_root(root, sizeof(root));
    if (have_root) load_debug_env(root);
    /* Batches that sample a surface the game rendered into draw stale memory on the D3D11 path:
     * black on some PCs, a white full-screen layer over the fight (and no health bars) on others
     * (docs/research/hd-white-textures.md B4). Left out by default since build 17, which is what the
     * black PCs showed; RECOMP_SKIP_RT_SAMPLE=0 in debug-env.txt brings them back. */
    if (!getenv("RECOMP_SKIP_RT_SAMPLE"))
#if defined(_WIN32)
        _putenv_s("RECOMP_SKIP_RT_SAMPLE", "1");
#else
        setenv("RECOMP_SKIP_RT_SAMPLE", "1", 0);
#endif
    /* The 64 MiB contiguous window, where the game keeps textures and vertex data, is a bump
     * allocator that never frees unless reclaim is on. A few matches into a session it ran dry
     * ("[CONTIG] arena exhausted", 636 times in a player's log of 2026-10-21) and the game drew
     * with the buffers it did not get: the announcer word sheet or glow strips full-screen over
     * the fight, from a won match on, until a restart. Upstream found and fixed the same in
     * v0.9.1 (vyanhursky/DefJamFFNY-recomp issue #26) by this default; rollback's audio arena
     * (12 MiB of the same window) makes it matter more. RECOMP_NO_HEAP_RECLAIM=1 keeps the old
     * behaviour for comparison. */
    if (!getenv("RECOMP_HEAP_RECLAIM") && !getenv("RECOMP_NO_HEAP_RECLAIM"))
#if defined(_WIN32)
        _putenv_s("RECOMP_HEAP_RECLAIM", "1");
#else
        setenv("RECOMP_HEAP_RECLAIM", "1", 0);
#endif

    if (env && !_stricmp(env, "none")) {
        g_path[0] = 0;
    } else if (env && *env) {
        snprintf(g_path, sizeof(g_path), "%s", env);
    } else if (have_root) {
        snprintf(g_path, sizeof(g_path), "%s" HOST_SEP "settings.ini", root);
    } else if (program_dir(g_path, MAX_PATH)) {
        strcat(g_path, HOST_SEP "settings.ini");
    }

    memcpy(g_settings, g_display_settings, sizeof(g_display_settings));
    g_setting_count = DISPLAY_SETTING_COUNT +
        pc_input_settings(g_settings + DISPLAY_SETTING_COUNT, 64);
    g_setting_count += pc_ui_settings(g_settings + g_setting_count, 16);
    recomp_settings_register(g_settings, g_setting_count);
    recomp_settings_set_log(settings_log);
    read = g_path[0] ? recomp_settings_load(g_path) : 0;
    {   /* The first server was known by its address; installs that saved it move to the name. */
        char server[128];
        recomp_settings_get_text("online", "server", server, sizeof(server), "");
        /* Build 30: the old default, the Europe relay's name, becomes "auto" (the nearest of Europe and
         * North America); a player who wants Europe only picks it again in the hub or the launcher. */
        if ((!strcmp(server, "142.132.178.17") || !_stricmp(server, "play.defjam.online") ||
             !_stricmp(server, "play.defjam.online:27777")) && !getenv("RECOMP_NETPLAY_SERVER") &&
            recomp_settings_reset_to_default("online", "server") > 0 && g_path[0])
            recomp_settings_save(g_path);
    }
    /* "Skip the launcher at start" left the players' menus (2026-10-22): players who had ticked it
     * kept asking how to get the launcher back. A player's own settings.ini that still has it is
     * set back once; a settings file a test names (RECOMP_SETTINGS) keeps it. */
    if (!env && g_path[0] && recomp_settings_get("launcher", "skip", 0) &&
        recomp_settings_reset_to_default("launcher", "skip") > 0) {
        recomp_settings_save(g_path);
        fprintf(stderr, "[SETTINGS] the launcher shows again at start (the skip setting is gone)\n");
    }
    if (!g_path[0])
        fprintf(stderr, "[SETTINGS] defaults only (RECOMP_SETTINGS=none)\n");
    else if (read < 0)
        fprintf(stderr, "[SETTINGS] could not read %s; using defaults\n", g_path);
    else {
        fprintf(stderr, "[SETTINGS] %s (%d value(s) read)\n", g_path, read);
        /* Write it out on first run, and after an upgrade adds a setting, so
         * there is always a complete, commented file to edit. */
        if (read < (int)g_setting_count && recomp_settings_save(g_path))
            fprintf(stderr, "[SETTINGS] could not write %s\n", g_path);
    }
    if (have_root)
        fprintf(stderr, "[SETTINGS] data folder %s\n", root);
    else
        fprintf(stderr, "[SETTINGS] no data folder found; saves use the runtime's default location\n");

    pc_input_start();
    run_launcher_if_wanted();
    /* What the picture runs with, for bug reports: input delay and frame pacing questions start here. */
    fprintf(stderr, "[SETTINGS] display: %s %dx%d, render scale %d, vsync %s, anti-aliasing %s, anisotropy %d, "
            "sharpen %d, aspect %d, HD textures %s; voice %d\n",
            pc_display("fullscreen", 0) ? "full screen" : "window",
            pc_display("window_width", 1280), pc_display("window_height", 960), pc_display("render_scale", 2),
            pc_display("vsync", 1) ? "on" : "off", pc_display("anti_aliasing", 1) ? "on" : "off",
            pc_display("anisotropy", 0), pc_display("sharpen", 30), pc_display("aspect", 0),
            recomp_settings_get("textures", "enabled", 0) ? "on" : "off", recomp_settings_get("online", "voice", 1));

    /* Settings read once at start-up by code that looks at the environment (after the launcher,
     * which may have changed them). */
    if (!getenv("RECOMP_GAMMA") && !pc_display("gamma", 1))
        _putenv_s("RECOMP_GAMMA", "0");

    {
        char value[MAX_PATH*2], packs[RECOMP_SETTING_TEXT_MAX];
        if (!getenv("RECOMP_TEXTURE_PACKS")) {
            recomp_settings_get_text("textures","packs",packs,sizeof packs,"faithful-hd");
            _putenv_s("RECOMP_TEXTURE_PACKS",recomp_settings_get("textures","enabled",0) ? packs : "");
        }
        if (!getenv("RECOMP_TEXTURE_CACHE_MB")) {
            snprintf(value,sizeof value,"%d",recomp_settings_get("textures","cache_mb",512));
            _putenv_s("RECOMP_TEXTURE_CACHE_MB",value);
        }
        if (!getenv("RECOMP_TEXTURE_DUMP_LIMIT")) {
            snprintf(value,sizeof value,"%d",recomp_settings_get("textures","dump_limit",2000));
            _putenv_s("RECOMP_TEXTURE_DUMP_LIMIT",value);
        }
        if (!getenv("RECOMP_TEXTURE_DUMP_DIR") && have_root && recomp_settings_get("textures","dump",0)) {
            snprintf(value,sizeof value,"%s/hd-work/runtime",root);
            _putenv_s("RECOMP_TEXTURE_DUMP_DIR",value);
        }
    }

    d3d8_present_enable_scaling(1);
#if defined(_WIN32)
    d3d8_present_set_overlay(pc_ui_overlay, NULL);
#else
    pc_ui_register_overlay();
#endif
    d3d8_present_set_render_scale((unsigned)pc_display("render_scale", 2));
    pc_settings_apply_display();
    recomp_settings_on_change(setting_changed, NULL);
    netplay_autostart();        /* tests only: RECOMP_NETPLAY (src/hooks/netplay.c) */
    fflush(stderr);
    return 1;
}
