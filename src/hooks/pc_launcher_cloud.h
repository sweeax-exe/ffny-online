// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// Cloud saves in the launcher (build 91): the account service's /api/saves (server relay/accounts/cloudsaves.go)
// and this PC's sync state (<data>/cloud-saves.txt), on a worker thread of their own. The rules underneath are
// cloud_saves.h; the Saves page's Cloud view and the start-up question are in pc_launcher_saves.inc.
//
// At every launcher start (the launcher opens again when the game closes, so that is also "after the game"),
// signed in and with the [cloud] saves setting on: the list of versions is fetched, this PC's saves hashed and
// cloud::decide picks what happens. UPLOAD goes up at once; OFFER_RESTORE (a new PC), CLOUD_NEWER and CONFLICT
// fetch the cloud's newest and wait for the player (start_prompt), who sees what a restore would change. A
// restore always backs up the PC's saves first; "Keep this PC's saves" keeps the cloud's newest as a local
// backup before this PC's go up, so both are kept. Nothing is restored or overwritten without the player.
//
// Tests: RECOMP_ACCOUNT_SERVER + RECOMP_ACCOUNT_TEST_TOKEN (a local account service), RECOMP_TEST_SAVES steps
// cloud, cloud-sync, cloud-restore:<row>, cloud-restore-ok, cloud-off, cloud-on, cloud-delete, cloud-delete-ok,
// cloud-name:<label> (pc_launcher_saves.inc), RECOMP_LAUNCHER_PAGE=saves-cloud.
#pragma once
#include "cloud_saves.h"

#include <string>
#include <vector>

namespace pccloud
{
enum class Phase {
    OFF,            // the setting is off
    SIGNED_OUT,     // not signed in
    IDLE,           // not asked yet this run
    WORKING,        // a request runs (busy says which)
    READY,          // the newest answer is in
    UNAVAILABLE,    // the service has no cloud saves (404)
    FAILED,         // the service could not be reached, or refused (line says why)
};

struct Status
{
    Phase phase = Phase::IDLE;
    std::string busy;                   // "Checking", "Uploading", ... while WORKING
    std::string line;                   // one plain line for the player
    cloud::Decision decision = cloud::Decision::NOTHING;
    std::vector<cloud::Version> versions;   // newest first
    std::string account;                // the account the list belongs to
    std::string local_hash, base;
    int local_profiles = 0;
    int64_t synced = 0;                 // when this PC and the cloud last agreed (0 never)
};
Status status();

bool enabled();
void set_enabled(bool on);
// This PC's label in the cloud's list (<data>/cloud-pc.txt, made once at random, "PC 7K2F"; never the Windows
// computer name) and its rename on the CLOUD view. *why: for the player.
std::string pc_label();
bool set_pc_label(const std::string &text, std::string *why);

// The start check (once per run) and Sync now: list, hash, decide; UPLOAD goes up. In the background.
void check(bool from_start);
// A question for the player, once per run: OFFER_RESTORE, CLOUD_NEWER or CONFLICT, with the newest fetched.
bool start_prompt();
void prompt_answered();

// A version fetched and checked for a restore (fetch), with what it would change.
struct Pending
{
    bool ready = false, failed = false;
    std::string error;
    cloud::Version version;
    cloud::Files files;
    std::vector<cloud::Change> changes;
};
void fetch(const std::string &version_id);
Pending pending();
void drop_pending();
// Restores the pending version (the game must be closed; the caller checks). *message for the player.
bool restore_pending(std::string *message);
// CLOUD_NEWER / CONFLICT: the cloud's newest is kept as a local backup, then this PC's saves go up (forced).
// OFFER_RESTORE: "Not now": this PC starts without them and later uploads as usual. In the background.
void keep_this_pc();
// Every cloud version of the account goes (DELETE /api/saves). In the background.
void delete_all();
} // namespace pccloud

extern "C" {
// The launcher skipped (the watcher only waits for the game): after the game closes, an UPLOAD decision goes up
// before the process ends (at most about 30 s); anything else waits for the next launcher start.
void cloud_saves_after_game(void);
}
