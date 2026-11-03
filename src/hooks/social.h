/* The Social Update, stage 2a (game side): friends with their online status, private messages,
 * global chat and crew chat through the account service's social API (/api/social/...), for the chat
 * window of the online screens (social_ui.inc). Signed-in players only.
 *
 * Nothing shows until the service says the social features are on: /api/social/* answers 404 while
 * its switch is off. RECOMP_SOCIAL=on turns them on for test runs against a local service started
 * with -social; RECOMP_SOCIAL=off keeps them off whatever the service says.
 *
 * Two threads of its own, started the first time the online screens open (never at boot): one holds
 * the long-poll (GET /api/social/events, 25 s), one does everything else (lists, sends, the presence
 * heartbeat every 60 s and on change). The game's threads only read a copy of the state and queue
 * actions; social_shutdown ends both (at exit, and when the player signs out). Windows only, like the
 * account. */
#ifndef DEFJAM_SOCIAL_H
#define DEFJAM_SOCIAL_H

#ifdef __cplusplus
extern "C" {
#endif

void social_start(void);            /* the online screens opened: start once (signed in only) */
void social_shutdown(void);         /* stop both threads (waits a moment at most) */
int social_enabled(void);           /* the chat window and its toasts are offered */
/* The chat's text box has the keyboard (voice.c: a letter key bound to push-to-talk does not talk
 * while it types). Set by the overlay every frame it is so; it runs out after 200 ms. */
void social_set_typing(int on);
int social_typing(void);

#ifdef __cplusplus
}

#include <string>
#include <vector>

namespace social {

struct Friend {
    long long id = 0;
    std::string name, tag, state, relay, room, mode, seats;    /* state: offline menu room fighting watching */
    bool joinable = false;
    long long last_online = 0;      /* unix seconds, 0 unknown */
};

struct Ref { long long id = 0; std::string name, tag; };

struct Line {                       /* a global or crew chat line */
    long long seq = 0, from = 0, at = 0;
    std::string channel, name, tag, text;
    bool hidden = false, system = false;
};

struct Convo {                      /* one MESSAGES entry */
    Ref with;
    std::string last;
    long long last_from = 0, last_at = 0;
    int unread = 0;
};

struct Dm { long long id = 0, from = 0, at = 0; std::string text; };

struct Thread {
    long long with = 0;
    bool loaded = false, can_message = true;
    long long seen_up_to = 0, seen_at = 0;
    std::vector<Dm> msgs;           /* oldest first */
};

struct CrewInvite { long long crew = 0; std::string tag, text, from; };

struct Toast { int kind = 0; std::string title, text; long long dm_from = 0; unsigned long long at = 0; };
enum { TOAST_DM = 1, TOAST_REQUEST, TOAST_FRIEND, TOAST_CREW, TOAST_NOTICE };

struct View {
    unsigned version = 0;
    bool enabled = false, ready = false, crews = false;
    long long me = 0;
    std::string my_name;
    int unread = 0, requests = 0, friends_online = 0, slow_mode = 0, max_chars = 200, online = 0;
    std::string dms;                /* "" friends only, "everyone", "off" */
    std::vector<Friend> friends;
    std::vector<Ref> in, out, blocked, muted;
    std::vector<Convo> convos;      /* newest first */
    std::vector<Thread> threads;
    std::vector<Line> global;       /* oldest first, every region */
    long long crew = 0;
    std::string crew_tag, crew_name, crew_motto;
    int crew_members = 0, crew_online = 0;
    std::vector<Line> crew_chat;    /* oldest first */
    std::vector<CrewInvite> crew_invites;
    std::string note;               /* the last refusal from the service ("slow mode: ...") */
    unsigned long long note_at = 0;
};

/* A copy of the state when it changed since `have` (true), else nothing (false). */
bool snapshot(View &out, unsigned have);
/* New toasts since the last call. */
std::vector<Toast> take_toasts();

/* Actions: queued, done on the social thread; the result comes back through the view (and its note). */
void send_global(const char *region, const std::string &text);
void send_dm(long long to, const std::string &text);
void send_crew(const std::string &text);
void friend_request(long long id, const std::string &name);     /* by id, else by exact name */
void friend_accept(long long id);
void friend_decline(long long id);
void friend_remove(long long id);
void mute(long long id, bool on);
void open_thread(long long with);   /* load it (once) and mark it read */
void mark_read(long long with);
void crew_join(long long crew);
void crew_dismiss(long long crew);
void reload();                      /* every list again */

/* The presence heartbeat works out the state from the lobby itself; the room's mode name comes from
 * the overlay (the names are read from the player's dump on the overlay's thread). */
void set_mode_name(const std::string &name);
std::string region_of_server(int index);    /* NETSERVER_* -> "eu", "us-east", "us-west"; "" else */

/* Tests: RECOMP_TEST_SOCIAL="step;step;..." runs from the social thread once it is ready (social.cpp). */

}   /* namespace social */
#endif

#endif
