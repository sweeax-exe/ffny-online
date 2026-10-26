/*
 * Widescreen (docs/widescreen/02-design.md).
 *
 * The title keeps drawing in its 640x480 coordinates. The renderer's target is
 * a wider canvas over which those coordinates are spread; the game's own 3D
 * view is widened to match (Hor+), and each 2D emitter squeezes or anchors
 * what it draws so it keeps its shape. Everything here returns the original
 * values while widescreen is off, which it always is in a test run
 * (RECOMP_SETTINGS=none) unless a variable asks for it.
 */
#ifndef DEFJAM_WIDESCREEN_H
#define DEFJAM_WIDESCREEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_DESIGN_ASPECT (4.0f / 3.0f)
#define WS_CUTSCENE_ASPECT (16.0f / 9.0f)

enum { WS_HUD_AUTO, WS_HUD_EDGE, WS_HUD_CENTERED };
enum { WS_CUTSCENES_AUTHORED, WS_CUTSCENES_FILL };
enum { WS_FILL_BLUR, WS_FILL_BLACK };

/* Resolve the settings once, after the launcher and before the renderer is
 * created, and hand the canvas to the renderer. Returns the aspect (0 = off). */
float ws_init(int window_w, int window_h, int fullscreen);

/* The output aspect, width / height; 0 when widescreen is off. */
float ws_aspect(void);
/* k = (4/3) / aspect: how much narrower a 4:3 layout must be drawn; 1 when off. */
float ws_k(void);
int ws_hud_mode(void);         /* WS_HUD_EDGE or WS_HUD_CENTERED (auto resolved) */
int ws_cutscene_mode(void);
int ws_side_fill(void);
float ws_fov_cap(void);

/* Placement in the title's 640-wide coordinates (02-design.md section 2). */
static inline float ws_x_center(float x, float k) { return 320.0f + (x - 320.0f) * k; }
static inline float ws_x_left(float x, float k) { return x * k; }
static inline float ws_x_right(float x, float k) { return 640.0f - (640.0f - x) * k; }

/* The front end's song notification (feloader's `chyron` clip, issue #118) is
 * drawn this far below its authored place; the renderer moves it back and
 * anchors it to the left edge (nv2a_ws_set_left_tag). */
#define WS_MENU_LEFT_TAG 8192

/* The APT movie loader (sub_001810B0) is about to resolve `name`.apt; then the
 * next step of the load (sub_00056F50) is handed the resolved data at `apt`,
 * before any of the movie's actions run. */
void ws_apt_load_begin(uint32_t name);
void ws_apt_load_done(uint32_t apt);

/* Hor+: the horizontal field of view (the game's FOV is horizontal, degrees)
 * that shows the same vertical extent at aspect `to` as `fov` does at `from`,
 * capped at `cap`. Unchanged when `to` is not wider. */
float ws_widen_fov(float fov_deg, float from, float to, float cap);

/* The scene viewport's GetViewParams (sub_0001A4E0): the original behaviour
 * when off, Hor+ for full-screen views and the cutscene policy for mode 1
 * when on. Arguments as the guest function's (thiscall, five stack words). */
void ws_view_params(uint32_t viewport, uint32_t mode, uint32_t out_fov,
                    uint32_t out_aspect, uint32_t out_y, uint32_t out_height);

#ifdef __cplusplus
}
#endif

#endif
