// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 sweeax-exe (FFNY Online)
// Cloud saves in the launcher: see pc_launcher_cloud.h.
#define _CRT_SECURE_NO_WARNINGS
#include "pc_launcher_cloud.h"
#include "pc_launcher_saves.h"
#include "pc_ui_actions.h"
#include "pc_ui_model.h"
extern "C" {
#include "account.h"
#include "recomp_settings.h"
}
#include "pc_settings.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pccloud
{
namespace
{
std::mutex g_mutex;
Status g_status;                        // guarded by g_mutex
Pending g_pending;                      // guarded by g_mutex
bool g_prompt = false, g_prompt_taken = false;
std::atomic<bool> g_busy{false};
std::atomic<bool> g_started{false};

cloud::fs::path state_file() { return cloud::fs::u8path(pcui::folder_path(pcui::Folder::Data)) / "cloud-saves.txt"; }

void log_line(const char *fmt, const std::string &a = "", const std::string &b = "")
{
    fprintf(stderr, "[CLOUD] ");
    fprintf(stderr, fmt, a.c_str(), b.c_str());
    fprintf(stderr, "\n");
    fflush(stderr);
}

void set(Phase phase, const std::string &line, const std::string &busy = "")
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.phase = phase;
    g_status.line = line;
    g_status.busy = busy;
}

cloud::fs::path label_file() { return cloud::fs::u8path(pcui::folder_path(pcui::Folder::Data)) / "cloud-pc.txt"; }

// This PC's label in the cloud's list: never the Windows computer name (it often holds a real name), but a
// short random one made once and kept in the data folder ("PC 7K2F"); the player can rename it.
std::string pc_name()
{
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    std::string l = cloud::read_label(label_file());
    if (!l.empty()) return l;
    std::random_device rd;
    l = cloud::new_label((uint32_t)rd());
    cloud::write_label(label_file(), l);
    return l;
}

// The service's error line of a text answer, or a plain one for the status.
std::string why(const std::string &answer, int status)
{
    std::string e = cloud::text_field(answer, "error");
    if (!e.empty()) return e;
    if (status == 0) return "Cannot reach the FFNY Online account server.";
    return "The cloud answered " + std::to_string(status) + ".";
}

bool signed_in()
{
    AccountState s = account_state();
    return s == ACCOUNT_IN || s == ACCOUNT_CHECKING;
}

struct Local
{
    bool ok = false;
    cloud::Files files;
    std::string hash, error;
    int profiles = 0;
};

Local read_local()
{
    Local l;
    std::vector<std::string> skipped;
    l.ok = cloud::collect(pcsaves::save_dir(), l.files, &skipped, &l.error);
    for (auto &s : skipped) log_line("left out: %s", s);
    if (!l.ok) return l;
    l.profiles = cloud::count_profiles(l.files);
    l.hash = l.files.empty() ? "" : cloud::content_hash(l.files);
    return l;
}

// GET /api/saves: false with the phase and line set.
bool list(cloud::Listing &out)
{
    std::vector<char> buf(32768);
    int status = account_call("GET", "/api/saves?format=text&files=ffny", nullptr, buf.data(), (int)buf.size(), 0, nullptr);
    std::string answer = buf.data();
    if (status == 200) {
        out = cloud::parse_listing(answer);
        if (out.ok) return true;
        set(Phase::FAILED, "The cloud's answer could not be read.");
        return false;
    }
    if (status == -1 || status == 401) set(Phase::SIGNED_OUT, "Sign in (Account) to keep your saves in the cloud.");
    else if (status == 404) set(Phase::UNAVAILABLE, "Cloud saves are not available on the server yet.");
    else set(Phase::FAILED, why(answer, status));
    log_line("list: HTTP %s", std::to_string(status));
    return false;
}

// POST /api/saves: the HTTP status; *answer the text answer.
int upload(const cloud::Files &files, const std::string &hash, const std::string &base, bool force, std::string *answer)
{
    cloud::Bytes zip = cloud::pack(files);
    if (zip.size() > cloud::kMaxArchive) {
        if (answer) *answer = "error=Your saves are too large for the cloud.";
        return 413;
    }
    std::string headers = "X-Save-Hash: " + hash + "\r\nX-Save-Base: " + (base.empty() ? "none" : base) +
                          "\r\nX-Save-PC: " + pc_name().substr(0, 32) + "\r\n";
    if (force) headers += "X-Save-Force: 1\r\n";
    std::vector<char> buf(4096);
    int status = account_send("POST", "/api/saves?format=text&files=ffny", "application/zip", zip.data(), (int)zip.size(),
                              headers.c_str(), buf.data(), (int)buf.size());
    if (answer) *answer = buf.data();
    fprintf(stderr, "[CLOUD] upload: %zu bytes, %zu files, hash %.12s, base %.12s%s: HTTP %d\n", zip.size(), files.size(),
            hash.c_str(), base.empty() ? "none" : base.c_str(), force ? ", forced" : "", status);
    fflush(stderr);
    return status;
}

int sink(void *ctx, const char *data, unsigned n)
{
    auto *out = (cloud::Bytes *)ctx;
    if (out->size() + n > cloud::kMaxArchive + 1) return 0;
    out->insert(out->end(), (const uint8_t *)data, (const uint8_t *)data + n);
    return 1;
}

// GET /api/saves/<id>, checked: false with *error (for the player).
bool download(const cloud::Version &v, cloud::Files &files, std::string *error)
{
    cloud::Bytes zip;
    int partial = 0;
    std::string path = "/api/saves/" + v.id + "?files=ffny";     // build 97: this launcher takes ffny/towers.json
    int status = account_download(path.c_str(), 0, sink, &zip, &partial);
    fprintf(stderr, "[CLOUD] download %s: HTTP %d, %zu bytes\n", v.id.c_str(), status, zip.size());
    fflush(stderr);
    if (status != 200) {
        if (error) *error = status == 404 ? "That cloud save is not there any more." : status == 0
                            ? "Cannot reach the FFNY Online account server." : "The cloud save could not be fetched (" + std::to_string(status) + ").";
        return false;
    }
    if (!cloud::unpack(zip, files, error)) return false;
    if (cloud::content_hash(files) != v.hash) {
        if (error) *error = "The cloud save was not taken: it does not match its checksum.";
        files.clear();
        return false;
    }
    return true;
}

void remember(const std::string &account, const std::string &hash, const std::string &version)
{
    cloud::SyncState s;
    s.account = account;
    s.hash = hash;
    s.version = version;
    s.when = pcsaves::now();
    if (!cloud::write_state(state_file(), s)) log_line("could not write %s", pcui::path_utf8(state_file()));
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status.base = hash;
    g_status.synced = s.when;
}

std::string describe(const cloud::Version &v)
{
    std::string when = saves::describe_time(v.uploaded);
    return v.pc.empty() ? when : when + " (" + v.pc + ")";
}

// The pending restore of version v: fetched, checked, compared with this PC's saves.
void prepare(const cloud::Version &v)
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending = Pending();
        g_pending.version = v;
    }
    Pending p;
    p.version = v;
    if (!download(v, p.files, &p.error)) {
        p.failed = true;
    } else {
        Local here = read_local();
        p.changes = cloud::compare(here.files, p.files);
        p.ready = true;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending = std::move(p);
}

// One check: list, hash, decide, and an UPLOAD at once.
void run_check(bool may_prompt)
{
    if (!enabled()) { set(Phase::OFF, "Cloud saves are off. Your saves stay on this PC."); return; }
    if (!signed_in()) { set(Phase::SIGNED_OUT, "Sign in (Account) to keep your saves in the cloud."); return; }
    set(Phase::WORKING, "", "Checking your cloud saves");
    cloud::Listing listing;
    if (!list(listing)) return;
    Local here = read_local();
    cloud::SyncState state = cloud::read_state(state_file());
    std::string base = state.account == listing.account ? state.hash : "";
    std::string newest = listing.versions.empty() ? "" : listing.versions[0].hash;
    cloud::Decision d = here.ok ? cloud::decide(here.hash, here.profiles > 0, base, newest) : cloud::Decision::NOTHING;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_status.versions = listing.versions;
        g_status.account = listing.account;
        g_status.local_hash = here.hash;
        g_status.local_profiles = here.profiles;
        g_status.base = base;
        g_status.synced = state.account == listing.account ? state.when : 0;
        g_status.decision = d;
    }
    fprintf(stderr, "[CLOUD] check: %zu cloud version(s), this PC %.12s (%d profiles), last sync %.12s: %s\n",
            listing.versions.size(), here.hash.empty() ? "-" : here.hash.c_str(), here.profiles,
            base.empty() ? "never" : base.c_str(), cloud::decision_name(d));
    fflush(stderr);
    if (!here.ok) { set(Phase::FAILED, here.error); return; }
    switch (d) {
    case cloud::Decision::IN_SYNC:
        if (base != newest || state.version != listing.versions[0].id) remember(listing.account, newest, listing.versions[0].id);
        set(Phase::READY, "In sync: the cloud has your saves from " + describe(listing.versions[0]) + ".");
        return;
    case cloud::Decision::NOTHING:
        set(Phase::READY, listing.versions.empty() ? "Nothing in the cloud yet: your saves go up once you have a profile."
                                                   : "Nothing to send.");
        return;
    case cloud::Decision::UPLOAD: {
        for (int i = 0; i < 20 && pcsaves::game_running(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (pcsaves::game_running()) { set(Phase::READY, "The game is running: your saves go up after it closes."); return; }
        set(Phase::WORKING, "", "Uploading your saves");
        std::string answer;
        int status = upload(here.files, here.hash, base, false, &answer);
        if (status == 200 || status == 201) {
            remember(listing.account, here.hash, cloud::text_field(answer, "id"));
            check(false);   // the list again, with the new version (another worker: this one ends)
            return;
        }
        if (status == 409) {    // another PC was faster: ask
            std::lock_guard<std::mutex> lock(g_mutex);
            g_status.decision = cloud::Decision::CONFLICT;
            g_status.phase = Phase::READY;
            g_status.line = "Your cloud saves changed on another PC. Choose which to keep.";
            g_status.busy.clear();
            break;
        }
        set(Phase::FAILED, "Not uploaded: " + why(answer, status));
        return;
    }
    case cloud::Decision::OFFER_RESTORE:
        set(Phase::READY, "Your cloud saves from " + describe(listing.versions[0]) + " can be restored on this PC.");
        break;
    case cloud::Decision::CLOUD_NEWER:
        set(Phase::READY, "Your cloud saves from " + describe(listing.versions[0]) + " are newer than this PC's.");
        break;
    case cloud::Decision::CONFLICT:
        set(Phase::READY, "This PC's saves and your cloud saves both changed. Choose which to keep.");
        break;
    }
    // a question: the newest fetched first, so it can say what a restore would change
    prepare(listing.versions[0]);
    if (may_prompt) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_prompt_taken) g_prompt = true;
    }
}

// Runs fn on a worker of its own, after the one that runs now (one request at a time, none dropped).
template <class F> void work(F fn)
{
    std::thread([fn] {
        while (g_busy.exchange(true)) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fn();
        g_busy = false;
    }).detach();
}
} // namespace

Status status()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Status s = g_status;
    if (g_busy && s.phase != Phase::WORKING) s.busy = s.busy.empty() ? "Working" : s.busy;
    return s;
}

bool enabled() { return recomp_settings_get("cloud", "saves", 1) != 0; }

std::string pc_label() { return pc_name(); }

bool set_pc_label(const std::string &text, std::string *why)
{
    std::string l = cloud::clean_label(text);
    if (l.empty()) {
        if (why) *why = "A PC name needs letters or digits (space, dot, dash and underscore too, 24 at most).";
        return false;
    }
    if (!cloud::write_label(label_file(), l)) {
        if (why) *why = "The name could not be saved in the data folder.";
        return false;
    }
    log_line("this PC is now \"%s\" in the cloud's list", l);
    if (why) *why = "This PC is \"" + l + "\" in your cloud's list from the next upload on.";
    return true;
}

void set_enabled(bool on)
{
    recomp_settings_set("cloud", "saves", on ? 1 : 0);
    pc_settings_save();
    log_line(on ? "cloud saves turned on" : "cloud saves turned off");
    if (on) check(false);
    else set(Phase::OFF, "Cloud saves are off. Your saves stay on this PC.");
}

void check(bool from_start)
{
    if (from_start) {   // every frame until it runs once: cheap while off or signed out (a sign-in starts it)
        if (g_started) return;
        Phase now = !enabled() ? Phase::OFF : !signed_in() ? Phase::SIGNED_OUT : Phase::WORKING;
        if (now != Phase::WORKING) {
            bool differs;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                differs = g_status.phase != now;
            }
            if (differs) set(now, now == Phase::OFF ? "Cloud saves are off. Your saves stay on this PC."
                                                    : "Sign in (Account) to keep your saves in the cloud.");
            return;
        }
        if (g_started.exchange(true)) return;
    }
    // the start check runs even when another request is still ending
    if (from_start) {
        std::thread([] {
            while (g_busy.exchange(true)) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            run_check(true);
            g_busy = false;
        }).detach();
        return;
    }
    if (g_busy) {   // a worker ends right now (the list after an upload): wait for it, then check once
        std::thread([] {
            while (g_busy.exchange(true)) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            run_check(false);
            g_busy = false;
        }).detach();
        return;
    }
    work([] { run_check(false); });
}

bool start_prompt()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_prompt && !g_prompt_taken && (g_pending.ready || g_pending.failed);
}

void prompt_answered()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_prompt = false;
    g_prompt_taken = true;
}

void fetch(const std::string &version_id)
{
    cloud::Version v;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto &x : g_status.versions)
            if (x.id == version_id) v = x;
        g_pending = Pending();
        if (v.id.empty()) {
            g_pending.failed = true;
            g_pending.error = "That cloud save is not in the list any more.";
            return;
        }
        g_pending.version = v;
    }
    work([v] {
        set(Phase::WORKING, "", "Fetching the cloud save");
        prepare(v);
        Pending p = pending();
        set(Phase::READY, p.failed ? p.error : "The cloud save from " + describe(v) + " is ready to restore.");
    });
}

Pending pending()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_pending;
}

void drop_pending()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_pending = Pending();
}

bool restore_pending(std::string *message)
{
    Pending p = pending();
    if (!p.ready) { if (message) *message = "Nothing was changed: the cloud save is not fetched yet."; return false; }
    std::string note = describe(p.version);
    bool ok = cloud::restore(p.files, pcsaves::save_dir(), pcsaves::backup_dir(), pcsaves::now(), note, message);
    if (ok) {
        // the base is the cloud's newest: the restored saves are what this PC plays now, so when they are not
        // that newest (an older version, or profiles put aside came with them) they go up as the newest
        Status s = status();
        std::string newest = s.versions.empty() ? p.version.hash : s.versions[0].hash;
        std::string newest_id = s.versions.empty() ? p.version.id : s.versions[0].id;
        remember(s.account, newest, newest_id);
        drop_pending();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_prompt = false;
            g_prompt_taken = true;
        }
        check(false);
    }
    return ok;
}

void keep_this_pc()
{
    work([] {
        Status s = status();
        if (s.versions.empty()) return;
        cloud::Listing listing;
        if (!list(listing) || listing.versions.empty()) return;
        cloud::Version newest = listing.versions[0];
        if (s.decision == cloud::Decision::OFFER_RESTORE) {
            // this PC starts without them: their hash becomes the base, so this PC's first saves go up as
            // the newest version and the cloud keeps the old ones
            remember(listing.account, newest.hash, newest.id);
            set(Phase::READY, "Not restored. Your cloud saves stay in the cloud; this PC's go up once it has a profile.");
            drop_pending();
            return;
        }
        set(Phase::WORKING, "", "Keeping both");
        Pending p = pending();
        cloud::Files theirs = p.ready && p.version.id == newest.id ? p.files : cloud::Files();
        std::string error;
        if (theirs.empty() && !download(newest, theirs, &error)) { set(Phase::FAILED, error); return; }
        std::string made;
        if (!cloud::keep_as_backup(theirs, pcsaves::backup_dir(), pcsaves::now(), describe(newest), &made, &error)) {
            set(Phase::FAILED, error);
            return;
        }
        Local here = read_local();
        if (!here.ok || !here.profiles) { set(Phase::FAILED, here.ok ? "This PC has no profile to send." : here.error); return; }
        std::string answer;
        int status = upload(here.files, here.hash, newest.hash, true, &answer);
        if (status == 200 || status == 201) {
            remember(listing.account, here.hash, cloud::text_field(answer, "id"));
            drop_pending();
            set(Phase::READY, "This PC's saves are now your newest cloud save. The other one is in your backups and "
                              "stays in the cloud's list.");
            run_check(false);
            return;
        }
        set(Phase::FAILED, "Not uploaded: " + why(answer, status));
    });
}

void delete_all()
{
    work([] {
        set(Phase::WORKING, "", "Deleting your cloud saves");
        char buf[1024];
        int status = account_call("DELETE", "/api/saves?format=text", nullptr, buf, (int)sizeof(buf), 0, nullptr);
        log_line("delete: HTTP %s", std::to_string(status));
        if (status == 204) {
            std::error_code ec;
            cloud::fs::remove(state_file(), ec);
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_status.versions.clear();
                g_status.base.clear();
                g_status.synced = 0;
                g_pending = Pending();
            }
            set(Phase::READY, enabled() ? "Your cloud saves are deleted. Your saves on this PC are as they were; "
                                          "they go up again after you play, unless you turn cloud saves off."
                                        : "Your cloud saves are deleted. Your saves on this PC are as they were.");
            return;
        }
        set(Phase::FAILED, "Not deleted: " + why(buf, status));
    });
}
} // namespace pccloud

extern "C" void cloud_saves_after_game(void)
{
    account_init();
    if (!pccloud::enabled() || account_state() != ACCOUNT_IN) return;
    std::atomic<bool> done{false};
    std::thread t([&done] {
        pccloud::check(true);
        done = true;
    });
    t.join();
    // check(true) started its worker: wait for it (about 30 s at most; the process ends after)
    for (int i = 0; i < 300 && (pccloud::status().phase == pccloud::Phase::WORKING || pccloud::status().phase == pccloud::Phase::IDLE); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
