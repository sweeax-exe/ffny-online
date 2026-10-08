/*
 * host_posix.c -- the window, and the Windows-only host files' stand-ins, for
 * hosts that are not Windows.
 *
 * The window is SDL3's. On macOS a window's events can only be read on the
 * process's first thread, so that thread is kept for them: the game boots and
 * runs on a second thread (host_posix_run), and anything that has to touch
 * the window is handed to the first one (call_on_main). On Linux the same
 * arrangement is simply harmless.
 *
 * The toolkit's Vulkan device is told how to make a surface for the window
 * (d3d8_vk.h); it does the rest. With RECOMP_HEADLESS set, or when SDL cannot
 * open a display, there is no window and the game runs on the first thread:
 * frames are still drawn off screen and can be captured, which is all an
 * unattended run needs.
 *
 * src/hooks/watchpoint.c drives the x86 debug registers through the Win32
 * thread API and has no counterpart here.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "d3d8_vk.h"
#include "hooks/pc_settings.h"
#include "hooks/pc_input.h"

static SDL_Window *s_window;
static int         s_have_video;
static int         s_fullscreen;
static volatile int s_drawable_w, s_drawable_h;
static Uint32      s_call_event;
static const char *const *s_extensions;
static Uint32      s_extension_count;

/* ── Work for the first thread ─────────────────────────────────────────── */

typedef struct {
    void (*fn)(void *);
    void *arg;
    pthread_mutex_t lock;
    pthread_cond_t  done_cond;
    int done;
} MainCall;

/* Run fn(arg) on the first thread and wait for it. */
static void call_on_main(void (*fn)(void *), void *arg)
{
    MainCall c;
    SDL_Event ev;

    c.fn = fn;
    c.arg = arg;
    c.done = 0;
    pthread_mutex_init(&c.lock, NULL);
    pthread_cond_init(&c.done_cond, NULL);
    SDL_zero(ev);
    ev.type = s_call_event;
    ev.user.data1 = &c;
    SDL_PushEvent(&ev);
    pthread_mutex_lock(&c.lock);
    while (!c.done)
        pthread_cond_wait(&c.done_cond, &c.lock);
    pthread_mutex_unlock(&c.lock);
    pthread_cond_destroy(&c.done_cond);
    pthread_mutex_destroy(&c.lock);
}

static void refresh_drawable(void)
{
    int w = 0, h = 0;
    if (s_window)
        SDL_GetWindowSizeInPixels(s_window, &w, &h);
    s_drawable_w = w;
    s_drawable_h = h;
}

/* ── The window ────────────────────────────────────────────────────────── */

typedef struct { int w, h; const char *title; } WindowRequest;

static void make_window_main(void *arg)
{
    WindowRequest *r = arg;

    s_window = SDL_CreateWindow(r->title, r->w, r->h,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
                                | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!s_window) {
        fprintf(stderr, "[HOST] no window: %s\n", SDL_GetError());
        return;
    }
    SDL_SetWindowMinimumSize(s_window, 320, 240);
    s_extensions = SDL_Vulkan_GetInstanceExtensions(&s_extension_count);
    if (!s_extensions) {
        fprintf(stderr, "[HOST] SDL cannot name its Vulkan extensions: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_window);
        s_window = NULL;
        return;
    }
    refresh_drawable();
}

typedef struct { void *instance; void *surface; int ok; } SurfaceRequest;

static void make_surface_main(void *arg)
{
    SurfaceRequest *r = arg;
    r->ok = SDL_Vulkan_CreateSurface(s_window, (VkInstance)r->instance, NULL,
                                     (VkSurfaceKHR *)r->surface);
    if (!r->ok)
        fprintf(stderr, "[HOST] no Vulkan surface: %s\n", SDL_GetError());
    refresh_drawable();
}

static int create_surface(void *instance, void *surface, void *user)
{
    SurfaceRequest r = { instance, surface, 0 };
    (void)user;
    call_on_main(make_surface_main, &r);
    return r.ok;
}

static void drawable_size(int *w, int *h, void *user)
{
    (void)user;
    *w = s_drawable_w;
    *h = s_drawable_h;
}

static void set_fullscreen_main(void *arg)
{
    int on = arg != NULL;
    if (!s_window || on == s_fullscreen)
        return;
    s_fullscreen = on;
    SDL_SetWindowFullscreen(s_window, on != 0);
    if (on) SDL_HideCursor(); else SDL_ShowCursor();
    refresh_drawable();
    fprintf(stderr, "[TRANS] %s\n", on ? "full screen" : "windowed");
    fflush(stderr);
}

/* display.fullscreen changed, on whichever thread changed it. */
static void fullscreen_setting_changed(int fullscreen)
{
    SDL_Event ev;
    static MainCall unused;
    (void)unused;
    /* Not call_on_main: this can arrive on the first thread itself (a hotkey),
     * which must not wait for itself. */
    SDL_zero(ev);
    ev.type = s_call_event + 1;
    ev.user.code = fullscreen;
    SDL_PushEvent(&ev);
}

/* Open the window the picture goes to and tell the graphics device about it.
 * Returns 0 when there is none; the device then draws off screen. */
int host_posix_open_window(int width, int height, const char *title)
{
    static D3D8VkHost host;
    WindowRequest r = { width, height, title };

    if (!s_have_video)
        return 0;
    call_on_main(make_window_main, &r);
    if (!s_window)
        return 0;
    host.instance_extensions = s_extensions;
    host.instance_extension_count = s_extension_count;
    host.create_surface = create_surface;
    host.drawable_size = drawable_size;
    d3d8_vk_set_host(&host);
    pc_settings_on_fullscreen(fullscreen_setting_changed);
    if (pc_display("fullscreen", 0))
        fullscreen_setting_changed(1);
    return 1;
}

/* ── The first thread ──────────────────────────────────────────────────── */

static int (*s_game_main)(void);
static volatile int s_game_done, s_game_result;

static void *game_thread(void *unused)
{
    (void)unused;
    s_game_result = s_game_main();
    s_game_done = 1;
    {
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = s_call_event + 2;
        SDL_PushEvent(&ev);
    }
    return NULL;
}

/* A key by where it is on the keyboard, as the Windows virtual-key code the
 * input layer's bindings are written in. Shift, Ctrl and Alt are reported by
 * side and as the generic key, held while either side is, as the Win32 window
 * reports them. */
static void key_to_input(SDL_Scancode sc, int down)
{
    static unsigned char side[3][2];
    int vk = 0, mod = -1, right = 0;

    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)        vk = 'A' + (sc - SDL_SCANCODE_A);
    else if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)   vk = '1' + (sc - SDL_SCANCODE_1);
    else if (sc == SDL_SCANCODE_0)                           vk = '0';
    else if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) vk = 0x70 + (sc - SDL_SCANCODE_F1);
    else if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) vk = 0x61 + (sc - SDL_SCANCODE_KP_1);
    else switch (sc) {
    case SDL_SCANCODE_KP_0:      vk = 0x60; break;
    case SDL_SCANCODE_RETURN:    vk = 0x0D; break;
    case SDL_SCANCODE_KP_ENTER:  vk = 0x0D; break;
    case SDL_SCANCODE_ESCAPE:    vk = 0x1B; break;
    case SDL_SCANCODE_BACKSPACE: vk = 0x08; break;
    case SDL_SCANCODE_TAB:       vk = 0x09; break;
    case SDL_SCANCODE_SPACE:     vk = 0x20; break;
    case SDL_SCANCODE_LEFT:      vk = 0x25; break;
    case SDL_SCANCODE_UP:        vk = 0x26; break;
    case SDL_SCANCODE_RIGHT:     vk = 0x27; break;
    case SDL_SCANCODE_DOWN:      vk = 0x28; break;
    case SDL_SCANCODE_LSHIFT:    mod = 0; right = 0; break;
    case SDL_SCANCODE_RSHIFT:    mod = 0; right = 1; break;
    case SDL_SCANCODE_LCTRL:     mod = 1; right = 0; break;
    case SDL_SCANCODE_RCTRL:     mod = 1; right = 1; break;
    case SDL_SCANCODE_LALT:      mod = 2; right = 0; break;
    case SDL_SCANCODE_RALT:      mod = 2; right = 1; break;
    default: break;
    }
    if (mod >= 0) {
        static const int generic[3] = { 0x10, 0x11, 0x12 };          /* VK_SHIFT, CONTROL, MENU */
        static const int sided[3][2] = { { 0xA0, 0xA1 }, { 0xA2, 0xA3 }, { 0xA4, 0xA5 } };
        side[mod][right] = (unsigned char)down;
        pc_input_host_key(sided[mod][right], down);
        pc_input_host_key(generic[mod], side[mod][0] || side[mod][1]);
        return;
    }
    if (vk)
        pc_input_host_key(vk, down);
}

static void handle_event(const SDL_Event *ev)
{
    if (ev->type == s_call_event) {
        MainCall *c = ev->user.data1;
        c->fn(c->arg);
        pthread_mutex_lock(&c->lock);
        c->done = 1;
        pthread_cond_signal(&c->done_cond);
        pthread_mutex_unlock(&c->lock);
        return;
    }
    if (ev->type == s_call_event + 1) {
        set_fullscreen_main(ev->user.code ? (void *)1 : NULL);
        return;
    }
    switch (ev->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        /* Closing the window ends the run; the line keeps a deliberate close
         * from reading like a crash. */
        fprintf(stderr, "[TRANS] window closed by the user; exiting (%s)\n",
                ev->type == SDL_EVENT_QUIT ? "quit request: Cmd+Q, the Dock, or Ctrl+C in the terminal"
                                           : "the window's close button");
        fflush(stderr);
        fflush(stdout);
        pc_input_stop();                    /* no pad left vibrating */
        _Exit(0);
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
        refresh_drawable();
        if (ev->type == SDL_EVENT_WINDOW_RESIZED && !s_fullscreen && s_window
                && !(SDL_GetWindowFlags(s_window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))) {
            int w = 0, h = 0;
            SDL_GetWindowSize(s_window, &w, &h);
            if (w > 0 && h > 0) {
                pc_display_set("window_width", w);
                pc_display_set("window_height", h);
            }
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: pc_input_set_focus(1); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:   pc_input_set_focus(0); break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        int down = ev->type == SDL_EVENT_KEY_DOWN;
        /* F11 or Alt+Enter, once per press. */
        if (down && !ev->key.repeat
                && (ev->key.key == SDLK_F11
                    || (ev->key.key == SDLK_RETURN && (ev->key.mod & SDL_KMOD_ALT)))) {
            pc_display_set("fullscreen", !s_fullscreen);
            break;
        }
        if (!ev->key.repeat)
            key_to_input(ev->key.scancode, down);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        /* VK_LBUTTON 1, VK_RBUTTON 2, VK_MBUTTON 4, VK_XBUTTON1 5, VK_XBUTTON2 6 */
        static const int vk[6] = { 0, 1, 4, 2, 5, 6 };
        if (ev->button.button < 6)
            pc_input_host_key(vk[ev->button.button], ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        pc_input_host_wheel(ev->wheel.y > 0 ? 1 : ev->wheel.y < 0 ? -1 : 0);
        break;
    default:
        break;
    }
}

/* Run the game. With a display: on its own thread, this one serving the
 * window until the game returns. Without: here. */
int host_posix_run(int (*game_main)(void))
{
    pthread_t th;
    pthread_attr_t attr;

#ifdef DEFJAM_VULKAN_LIBRARY
    /* SDL loads the Vulkan loader by name, from places that do not include a
     * package manager's prefix; say where the build found it. */
    if (!getenv("SDL_VULKAN_LIBRARY"))
        SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, DEFJAM_VULKAN_LIBRARY);
#endif
    if (getenv("RECOMP_HEADLESS") || !SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        if (!getenv("RECOMP_HEADLESS"))
            fprintf(stderr, "[HOST] no display (%s); running without a window\n", SDL_GetError());
        return game_main();
    }
    s_have_video = 1;
    s_call_event = SDL_RegisterEvents(3);
    s_game_main = game_main;

    /* The title's main thread: the stack the first thread would have had. */
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
    if (pthread_create(&th, &attr, game_thread, NULL) != 0) {
        fprintf(stderr, "[HOST] could not start the game thread\n");
        return 1;
    }
    pthread_attr_destroy(&attr);
    while (!s_game_done) {
        SDL_Event ev;
        if (SDL_WaitEventTimeout(&ev, 100))
            handle_event(&ev);
    }
    pthread_join(th, NULL);
    SDL_Quit();
    return s_game_result;
}

/* ── Debug-register watchpoints: Windows only ──────────────────────────── */

int watchpoint_init(void)
{
    if (getenv("RECOMP_WATCH_WRITE") || getenv("RECOMP_WATCH_EXEC"))
        fprintf(stderr, "[HOST] RECOMP_WATCH_WRITE and RECOMP_WATCH_EXEC use the x86"
                        " debug registers and are not available on this host\n");
    return 0;
}

int watchpoint_handle_trap(void *ctx) { (void)ctx; return 0; }
