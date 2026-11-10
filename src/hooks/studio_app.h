// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// FFNY Studio through the launcher: the home screen's Studio card gets the packaged app from the update
// server, keeps it current and starts it with the launcher's handover (Studio repository,
// docs/ffny-studio-app.md section 7). Windows only; elsewhere the card is hidden.
//
// Trust: studio/studio-latest.json is believed only with a valid studio-latest.json.sig over its exact bytes
// by the project's update key (update_sig.h, the same check as latest.json and graphics.json). It names the zip
// ("setup", fetched beside the manifest; an absolute "url" only when it is on the same host), its size and its
// SHA-256, which the download must match before anything is unpacked.
//
// Layout (<root> = RECOMP_STUDIO_DIR, else <parent of the data folder>\studio, which for a default install is
// %LOCALAPPDATA%\DefJamRecompiled\studio):
//   <root>\cache\<zip>(.part)                 the download, resumed; deleted once installed
//   <root>\versions\<folder>\FFNYStudio\...   one folder per installed version (<folder> = the version)
//   <root>\current.txt                        which folder is in use ("folder", "version", "build"): written to a
//                                             temporary name and moved over the old one, so it always names a
//                                             whole install; the previous version stays until the next one is in
//
// Start: FFNYStudio.exe --install-dir <the folder with installed.ini> --data-root <data>
//        --account-file <data>\account.dat, working folder = the exe's folder. --install-dir only for an
//        installed copy (DEFJAM_INSTALL_DIR, or the launcher's working folder <install>\versions\<id>\source).
//
// The closed test (2026-11-10): Studio goes to testers first, by a Discord role the account service checks. The
// launcher asks GET /api/studio/access with the player's session; the card is hidden when nobody is signed in or
// the account has no access. With access, the manifest and its signature come from GET /api/studio/latest
// ({"manifest": the signed text, "signature"}) and the zip from GET /api/studio/download/<setup>, with the
// session as every other account call; the signature, size and SHA-256 are checked exactly as before. The public
// update folder (studio/studio-latest.json on the update port, https://play.defjam.online/updates/studio/) is
// read only behind a test switch.
//
// Tests: RECOMP_STUDIO_SERVER=host:port (the public path on a local server: plain HTTP, /studio/...; no access
// check), RECOMP_STUDIO_PUBLIC=1 (the public path on the update servers); RECOMP_STUDIO_TEST_KEY=<128 hex
// digits> is the key that must have signed the manifest, taken only with RECOMP_STUDIO_SERVER or
// RECOMP_ACCOUNT_SERVER on this PC (the account path: RECOMP_ACCOUNT_SERVER and RECOMP_ACCOUNT_TEST_TOKEN).
#pragma once

#include <cstdint>
#include <string>

namespace studio
{
struct View
{
    bool supported = false;         // Windows
    bool checked = false;           // a manifest was read (or the check failed) since the launcher started
    bool busy = false;              // a download or install runs
    bool installed = false;
    std::string installed_version;
    int installed_build = 0;
    bool allowed = false;           // the account has Studio access (the closed test), or a test switch reads
                                    // the public folder; false: the card is hidden
    bool available = false;         // a signed manifest is known
    std::string version, notes;     // the published one
    int build = 0;
    uint64_t size = 0;
    bool update = false;            // installed, and the published build is newer
    std::string progress;           // what the background work does now
    double fraction = -1;           // its progress, -1 unknown
    std::string error;              // the last failure, one line
    std::string message;            // the last success, one line ("Studio started")
};

View view();
void launcher_starts();             // every frame of the home screen: a quiet access and manifest check in the
                                    // background at the first call and whenever the player signs in or out
void check();                       // another check, at most every two minutes (hover, press)
void press();                       // the card's button: get, update or open, whichever applies

// The pieces underneath, for tests.
std::string root_folder();
bool launch(std::string *error);
}

// studio_unzip.cpp: unpacks a checked zip (stored and deflated entries, CRC-32 checked) below `dest`. Every
// entry must lie under `prefix` ("FFNYStudio/"); names with "..", a drive, a backslash or a control character
// are refused, as are more than 20000 entries or 4 GiB in all.
namespace studio_zip
{
bool extract(const std::wstring &zip, const std::wstring &dest, const char *prefix, std::string *error);
}
