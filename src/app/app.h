// What the app's parts share: the install it manages (app_main.cpp) and its own screens over the
// launcher, setup and maintenance (app_setup.cpp).
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "app_os.h"

struct UiResult;

namespace app
{

// The install the app belongs to. `root` is known before anything is installed (the first run's
// destination); the rest comes from its installed.json once there is one.
struct Install
{
    std::filesystem::path root, data, source, executable;
    std::string version;
    std::string source_commit;  // the commit it was built from ("" in an older setup's receipt)
    bool installed = false;     // a completed, playable installation
    std::string problem;        // why an installation that is there cannot be played (Repair fixes it)
};
Install &install();

// Read root/installed.json into install(), supply DEFJAM_DATA, DEFJAM_INSTALL_DIR and the working
// directory, and open the settings file. Returns false (with a reason) when there is no completed
// installation there.
bool load_install(std::string *why);

// The version in root/installed.json, or "" when there is no installation there.
std::string receipt_version(const std::filesystem::path &root);

// Remember an install folder the player chose, so the next start finds it (nothing is kept for the
// usual place).
void remember_install(const std::filesystem::path &root);
// Forget it again (Uninstall).
void forget_install();

// play.lock, held while the app shows the launcher and, through the exec, while the game runs.
// Setup takes it itself, so it is let go while an operation runs.
bool lock_install();
void unlock_install();

// The setup payload this app runs the engine from (--payload, DEFJAM_SETUP_PAYLOAD, or beside the
// program), or empty when it has none.
std::filesystem::path payload();

// The folder the session logs go to.
std::filesystem::path log_dir();

// Defaults for the first run's choices (--dump, --data, or the platform's usual places).
struct FirstRun
{
    std::string dump, install, data;
};
FirstRun &first_run();

// The running program, resolved, and the macOS bundle it is in (empty when it is not in one). On
// Windows the folder holding the installed program and its payload; on Linux the AppImage file.
std::filesystem::path program();
std::filesystem::path bundle_path();

// What Steam and the play wrapper start: the program; on Linux the AppImage file it runs from
// (the program itself is inside a mount that changes on every start).
std::filesystem::path launch_target();

// The app's own per-user folder (its location record, Steam record and answers): on Windows
// %LOCALAPPDATA%\DefJamRecompiled; on Linux $XDG_DATA_HOME/DefJamRecompiled (~/.local/share).
std::filesystem::path state_dir();

// Linux: the AppImage file this program was started from, or empty (app_install_linux.cpp).
std::filesystem::path appimage();
// Linux: the installed AppImage was replaced by the app's own update; record its new version.
void note_updated_home(const std::filesystem::path &file, const std::string &version);

// The app's command line after the program name, and whether it was started by its own update.
std::vector<std::string> &arguments();
bool just_updated();

// The setup payload's version: what this app builds ("" without a payload).
std::string payload_version();
// The commit the setup payload builds ("" without a payload). Betas share a version, so this tells
// one build of 1.0.0 from another.
std::string payload_commit();
// The installed game is the version this app builds but from another commit: a beta moving to the
// next beta or to the release. Clean Rebuild builds it from this app's payload.
bool other_build(const Install &in);
// The installed game is behind this app: an older version, or another build of the same version.
bool game_behind(const Install &in);
// "v1.0.0", or "v1.0.0 (build cd2a298)" when `commit` is given.
std::string build_label(const std::string &version, const std::string &commit = "");

// The top-level values of a JSON object: strings decoded, anything else as its JSON text.
std::map<std::string, std::string> json_values(const std::string &text);

// Whether release version `a` is newer than `b` ("0.13.1" against "0.13.0"; false if either is not one).
bool newer(const std::string &a, const std::string &b);

// A program to exec in place of the game when the launcher's window closes: the updated app.
std::vector<std::string> &relaunch();

// ---- the screens (app_setup.cpp) ---------------------------------------------------------------

// The top of each of the app's pages: the window, the brand line and a heading.
void page_begin(float width, float height, const char *heading);
void page_actions(); // end scrolling body; begin stationary action area
void page_end();
bool back_pressed();
void screen_open_hd();

// Clean Update of the installed game to this app's payload version, with the progress screen.
void start_update_build();

// Resume the setup request after releasing an older Windows shortcut wrapper.
void resume_setup(const std::string &operation, const std::string &title, const std::string &root,
                  const std::string &data, const std::string &dump, bool hd, bool install_tools);


// Draw this frame's screen when the app has one up (first-run setup, maintenance, an operation's
// progress); returns false to let the launcher draw. Inside the launcher's frame.
bool screen_draw(float width, float height, UiResult *result);

// Show Maintenance: Repair and Clean Rebuild of this install.
void screen_open_maintenance();

// An operation is running: the window may not close (a close asks to cancel instead).
bool screen_busy();
void screen_request_cancel();

// ---- Windows and Linux: the app installs itself (app_install.cpp, app_install_linux.cpp) --------------------------------------------

// Where this program stands, decided before anything else when it was started without --payload.
enum class Placement
{
    Development,    // no setup payload built in: a development build, run as it is
    Installed,      // the copy in the app's home folder
    HandOver,       // another copy (the download): relaunch() holds the installed copy to start
    NeedsInstall    // no home yet: the window opens on the install page
};
Placement place_app(std::string *error);

// The installed copy: make sure payload\ beside it is the one built into it. False with the reason.
bool ensure_payload(std::string *error);

// The install page, while it is up; drawn before anything else. And whether it is copying files.
bool install_screen_draw(float width, float height, UiResult *result);
bool install_busy();

// ---- Windows and Linux: Add to Steam (app_steam.cpp) ----------------------------------------------------------

// Maintenance's Steam section: Add to Steam / Remove from Steam, asking before Steam is closed.
void steam_section();
// Ask Steam's state again the next time the section is drawn (Maintenance was opened).
void steam_section_reset();
// The engine is changing Steam's library (the window may not close meanwhile).
bool steam_busy();
// Add to Steam beside Play (Home, and the page that ends an install or update): the width it needs,
// or 0 when there is nothing to show (no Steam, Steam's state not known yet, or already added).
float steam_play_width();
// Draw it, asking in a dialog before Steam is closed. Then IN STEAM, once added in this session.
void steam_play_button(float width, float height);
// Uninstall: take out the Steam shortcut and art this app added (closing and restarting Steam if it
// runs), waiting for it. True when there was nothing to remove or it was removed.
bool steam_remove_now(std::string *message);

// ---- the app's own updates (app_update.cpp) ----------------------------------------------------

// Remove what a finished update left beside the app, once it has started again. At start.
void update_cleanup();

// The update screen, when it is up; called first by screen_draw.
bool update_screen_draw(float width, float height, UiResult *result);

// Whether the app's own update is being downloaded (the window may not close meanwhile), and
// asking that download to stop.
bool update_busy();
void update_request_cancel();

// Start setup/engine.py from the app's payload with its Python and these arguments (after
// --payload), in the plain environment the setup wizards give it (app_setup.cpp).
bool spawn_engine(const std::vector<std::string> &engine_args, os::Process *process, std::string *error);

} // namespace app
