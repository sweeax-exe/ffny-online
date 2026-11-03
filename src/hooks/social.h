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
    bool hidden = false, system = false, mine = false;   /* mine: this PC's own (room chat) */
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

struct CrewMember { long long id = 0; std::string name, role, state; };   /* role: boss lieutenant member */

struct Toast { int kind = 0; std::string title, text; long long dm_from = 0; unsigned long long at = 0; };
enum { TOAST_DM = 1, TOAST_REQUEST, TOAST_FRIEND, TOAST_CREW, TOAST_NOTICE };

struct View {
    unsigned version = 0;
    bool enabled = false, ready = false, crews = false;
    long long me = 0;
    std::string my_name;
    int unread = 0, requests = 0, friends_online = 0, slow_mode = 0, max_chars = 200, online = 0;
    std::string dms;                /* "" friends only, "everyone", "off" */
    bool show_avatar = true;        /* this account's Discord picture is shown to others (stage 2c) */
    std::vector<Friend> friends;
    std::vector<Ref> in, out, blocked, muted;
    std::vector<Convo> convos;      /* newest first */
    std::vector<Thread> threads;
    std::vector<Line> global;       /* oldest first, every region */
    long long crew = 0;
    std::string crew_tag, crew_name, crew_motto;
    int crew_members = 0, crew_online = 0;
    /* the crew's page (stage 2c): my role, the roster, the numbers of the design's cards */
    std::string crew_role, crew_region, crew_league;
    long long crew_founded = 0;
    int crew_level = 0, crew_xp = 0, crew_level_xp = 0, crew_next_xp = 0, crew_wins = 0, crew_losses = 0, crew_streak = 0,
        crew_rating = 0, crew_turf = 0;
    bool crew_locked = false;
    std::vector<CrewMember> crew_roster;        /* boss, lieutenants, members; online first */
    std::vector<Ref> crew_invited, crew_applied;    /* officers only */
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

/* Stage 2c. Block: no friendship, requests, messages or chat lines either way (the service keeps it;
 * room lines of a blocked account are hidden here). Report: a line with up to 5 lines before it, for
 * the team (channel "room", "eu"/"us-east"/"us-west", "crew:N" or "dm:N"; msg the line's id where the
 * service has one, text and context for room lines, which only this PC saw). */
void block(long long id, const std::string &name, bool on);
void report(long long id, const std::string &channel, long long msg, const std::string &text,
            const std::vector<std::string> &context);
void set_dms(const std::string &policy);    /* "friends", "everyone", "off" */
void set_show_avatar(bool on);
/* Crews: create, invite (by id, else exact name), leave, and the officers' kick and role. */
void crew_create(const std::string &name, const std::string &tag, const std::string &region);
void crew_invite(long long id, const std::string &name);
void crew_leave();
void crew_kick(long long id);
void crew_role(long long id, const std::string &role);     /* boss lieutenant member */
void crew_accept(long long id);                             /* an application */

/* Avatars (stage 2c): the account service's 64x64 copy of a player's Discord picture
 * (GET /api/avatar/N.png; the game never contacts Discord). avatar_want queues a download on a thread
 * of its own (once per id until forgotten); avatar_take hands over the finished ones, decoded to
 * RGBA (ok false: none, the overlay draws initials). */
struct AvatarPixels { long long id = 0; bool ok = false; int w = 0, h = 0; std::vector<unsigned char> rgba; };
void avatar_want(long long id);
std::vector<AvatarPixels> avatar_take();
void avatar_forget(long long id);

/* The presence heartbeat works out the state from the lobby itself; the room's mode name comes from
 * the overlay (the names are read from the player's dump on the overlay's thread). */
void set_mode_name(const std::string &name);
std::string region_of_server(int index);    /* NETSERVER_* -> "eu", "us-east", "us-west"; "" else */

/* Tests: RECOMP_TEST_SOCIAL="step;step;..." runs from the social thread once it is ready (social.cpp). */

}   /* namespace social */
#endif

#endif
