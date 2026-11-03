// What the app's parts share: the install it manages (app_main.cpp) and its own screens over the
// launcher, setup and maintenance (app_setup.cpp).
#pragma once

#include <filesystem>
#include <string>

struct UiResult;

namespace app
{

// The install the app belongs to. `root` is known before anything is installed (the first run's
// destination); the rest comes from its installed.json once there is one.
struct Install
{
    std::filesystem::path root, data, source, executable;
    std::string version;
    bool installed = false;     // a completed, playable installation
    std::string problem;        // why an installation that is there cannot be played (Repair fixes it)
};
Install &install();

// Read root/installed.json into install(), supply DEFJAM_DATA, DEFJAM_INSTALL_DIR and the working
// directory, and open the settings file. Returns false (with a reason) when there is no completed
// installation there.
bool load_install(std::string *why);

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

// ---- the screens (app_setup.cpp) ---------------------------------------------------------------

// Draw this frame's screen when the app has one up (first-run setup, maintenance, an operation's
// progress); returns false to let the launcher draw. Inside the launcher's frame.
bool screen_draw(float width, float height, UiResult *result);

// Show Maintenance: Repair and Clean Rebuild of this install.
void screen_open_maintenance();

// An operation is running: the window may not close (a close asks to cancel instead).
bool screen_busy();
void screen_request_cancel();

} // namespace app
