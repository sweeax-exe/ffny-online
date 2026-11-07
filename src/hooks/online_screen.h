/* The online screens inside the game: the ONLINE entry of the main menu (slot 6, filled by
 * scripts/apt-patch.py) and the screens it opens (hub, room list, room, fighter choice, venue
 * vote), drawn by the port in the game's look with the art scripts/online-art.py takes from the
 * player's own dump (<data>/online-art). See docs/research/frontend-menu-integration.md. */
#ifndef DEFJAM_ONLINE_SCREEN_H
#define DEFJAM_ONLINE_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* From the FuncCall hook (via automatch_on_funccall): screens, Online.Focus(n), Online.Open(). */
void online_menu_on_funccall(const char *text);

#ifdef __cplusplus
}

#include "imgui.h"

namespace online_screen {

/* Load fonts and textures; call with the overlay's device before ImGui::NewFrame. */
void prepare(void *d3d11_device);

/* The main menu is up: the ONLINE poster and, while slot 6 has the cursor, its door. */
bool decor_wanted();
void draw_decor(ImDrawList *dl, ImVec2 display);

/* EXIT on the main menu: a glass chip drawn with the decor (the grid has no free cell). The overlay
 * opens the prompt on the pad's VIEW, ESC or a click on the chip, only while exit_wanted() (the main
 * menu, no fight, no session, no room). exit_chip_at takes window pixels. RECOMP_TEST_OPEN_EXIT=1
 * (tests) opens it once at the first main menu. draw_exit_prompt: 0 still open, 1 NO, 2 YES. */
bool exit_wanted();
bool exit_chip_at(float x, float y);
bool take_exit_test_request();
void exit_prompt_begin();
int draw_exit_prompt(ImVec2 display);

/* The player chose ONLINE (once per choice). */
bool take_open_request();

/* The online screens; returns false when the player leaves them (back to the main menu). */
bool draw(ImVec2 display);

/* The last false from draw() was a match starting, not a press: nothing to keep from the game. */
bool closed_for_match();

/* A room's match loads: the versus screen (the fighters, the venue, the mode) is up, drawn over the
 * game with the online screens closed and no input taken; draw it with draw_versus this frame. */
bool versus_wanted();
void draw_versus(ImDrawList *dl, ImVec2 display);

/* RECOMP_TEST_ONLINE_GALLERY: the name of the page the gallery shows, for the overlay's captures;
 * nullptr otherwise. */
const char *shot_label();

/* Room chat over a room's match (the online screens closed): the last lines for a few seconds, and a
 * spectator's text bar (ENTER). Fighters never type during their match: their keys stay the fight's.
 * fight_chat_wanted() once a frame; draw_fight_chat returns true while the bar takes the keyboard
 * (then the game gets no input on this PC: a spectator's only). */
bool fight_chat_wanted();
bool draw_fight_chat(ImDrawList *dl, ImVec2 display);
bool fight_chat_typing();

/* Tests (RECOMP_TEST_QUICK_CHAT): the pad presses the quick-chat test makes, merged into the overlay's
 * own pad input (bit 0 the wheel's button, L3, or R3 when test_pad_r3; bit 1 A; the left stick -1..1). */
bool test_pad(int *buttons, float *lx, float *ly);
bool test_pad_r3();

}
#endif

#endif
