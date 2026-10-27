"""FFNY Online Discord bot: everything that does not need Discord itself.

The server layout, the texts the bot posts, the ticket forms and the small parsers for the
update manifest and the relay's summary line. ffny_bot.py does the Discord side; keeping
this part free of discord.py lets the unit tests check it on any machine.
"""
import json
import re

GITHUB = "https://github.com/sweeax-exe/ffny-online"
DOWNLOAD = GITHUB + "/releases/latest"
DISC_GUIDE = GITHUB + "/blob/main/docs/dump-your-disc.md"
ISSUES = GITHUB + "/issues"
KOFI = "https://ko-fi.com/ffnyonline"
SUPPORT_CHANNEL = "support-the-project"
UPDATE_URL = "http://play.defjam.online:27778/latest.json"
ACCOUNTS_URL = "http://127.0.0.1:8091"      # the account service's local listener for the bot

TEAM_ROLE = "FFNY Team"
MOD_ROLE = "Moderator"
MODDER_ROLE = "Modder"         # modders and FFNY Studio testers; given by the team
SUPPORTER_ROLE = "Supporter"   # a thank-you for donors: shown in the member list, no other perks
APPLY_CHANNEL = "join-the-team"
ALERT_ROLE = "Build Alerts"
MATCH_ROLE = "Match Ping"
TICKET_CATEGORY = "🎫 TICKETS"
TICKET_LOG = "ticket-log"
REPORTS_CHANNEL = "bug-reports"   # F9 reports and crash logs from the games (team only)
ANNOUNCEMENTS = "announcements"
POLLS_CHANNEL = "polls"     # polls only: players may post polls; the bot removes anything else
HONEYPOT_CHANNEL = "honeypot"   # a trap for spam bots: whoever posts here is removed (softban)
HELP_CHANNEL = "get-help"
MAX_OPEN_TICKETS = 2
LEADERBOARD_MIN = 3        # the account service's LeaderboardMin

# Channel kinds: read (everyone reads, only the team and the bot write), chat, polls (everyone may
# post a poll, the bot removes other messages), honeypot (a spam-bot trap), team (team only),
# voice, and stat (the player count in its name; nobody joins it).
LAYOUT = [
    ("📢 INFO", [("welcome", "read"), ("rules", "read"), ("announcements", "read"), (SUPPORT_CHANNEL, "read"),
                ("status", "stat")]),
    ("💬 COMMUNITY", [("general", "chat"), ("find-a-match", "chat"), ("clips-and-screens", "chat"),
                     ("ideas-and-feedback", "chat"), ("off-topic", "chat"), (POLLS_CHANNEL, "polls")]),
    ("🎨 MODDING", [("ffny-studio", "chat"), ("mod-showcase", "chat")]),
    ("🛠️ SUPPORT", [("get-help", "read"), ("faq", "read"), (APPLY_CHANNEL, "read")]),
    ("🎮 VOICE", [("Lobby", "voice"), ("Match 1", "voice"), ("Match 2", "voice")]),
    (TICKET_CATEGORY, []),
    ("🔒 TEAM", [("team-chat", "team"), (TICKET_LOG, "team"), (REPORTS_CHANNEL, "team")]),
    ("🍯 DO NOT POST", [(HONEYPOT_CHANNEL, "honeypot")]),
]

# The ticket forms. A Discord form holds at most five fields; a label is at most 45 characters
# and a placeholder at most 100. Fields: (key, label, style, required, placeholder).
TICKET_TYPES = {
    "bug": {
        "label": "Bug report", "emoji": "🐞", "prefix": "bug",
        "fields": [
            ("what", "What happened?", "paragraph", True,
             "What did you do, what did you expect, what did you see?"),
            ("steps", "How can we see it too?", "paragraph", False,
             "Steps, which screen, how often it happens"),
            ("build", "Build number", "short", True, "The launcher shows it, e.g. 11"),
            ("pc", "PC and controller", "short", False, "e.g. Windows 11, Xbox controller"),
        ],
    },
    "setup": {
        "label": "Setup & install", "emoji": "💿", "prefix": "setup",
        "fields": [
            ("step", "Where does the setup stop?", "paragraph", True,
             "The step and the exact message it shows"),
            ("dump", "Your dump", "short", False, "Disc image (.iso) or extracted folder"),
            ("build", "Setup build", "short", False, "In the file name, e.g. remaster.11"),
        ],
    },
    "online": {
        "label": "Online & connection", "emoji": "🌐", "prefix": "online",
        "fields": [
            ("what", "What happens?", "paragraph", True,
             "Cannot connect, the match does not start, lag, someone drops ..."),
            ("where", "Quick match, room or code?", "short", False, "And how many players"),
            ("build", "Build number", "short", True, "The launcher shows it, e.g. 11"),
        ],
    },
    "other": {
        "label": "Something else", "emoji": "💬", "prefix": "other",
        "fields": [
            ("subject", "Subject", "short", True, ""),
            ("message", "Your message", "paragraph", True, ""),
        ],
    },
    # Applications: only the applicant and the server's admins see them (not the whole team).
    "apply": {
        "label": "Apply as moderator", "emoji": "🛡️", "prefix": "apply", "private": True,
        "fields": [
            ("why", "Why do you want to help?", "paragraph", True,
             "What would you like to do for the community?"),
            ("experience", "Moderation experience", "paragraph", False,
             "Other servers or communities, if any"),
            ("time", "Timezone and when you're online", "short", True, "e.g. CET, evenings and weekends"),
            ("languages", "Languages you speak", "short", True, "e.g. English, French"),
            ("age", "Are you 16 or older?", "short", True, "yes / no"),
        ],
    },
}

SUPPORT_TEXT = f"""# Support the project ❤️
**FFNY Online is free and stays free.** Support only pays for the servers: the online relays in Europe and North America, the accounts and stats, and this Discord bot.

☕ **Ko-fi:** {KOFI}

💖 As a thank-you, supporters get the **Supporter** role here on Discord and show up in their own group in the member list. Mention your Discord name in the Ko-fi message, or open a ticket in #get-help after supporting, and the team adds it.

No perks in the game and no paywalls: supporting is completely voluntary, and everyone gets the same game. Thank you!"""

APPLY_TEXT = """# Join the FFNY Team 🛡️
FFNY Online is growing fast, and we'd love some help keeping the community a good place: welcoming new players, answering questions, helping in tickets and keeping the chat clean.

**What a moderator does:** helps in #get-help tickets, removes spam, gives timeouts when someone breaks the #rules, and keeps things friendly.
**What we ask:** be patient and fair, know the game a bit, and have some time now and then.

Press the button and fill in the short form. Only the server's admins see your application."""

APPLY_ACCEPTED = "Welcome to the FFNY Team! 🎉 You now have the Moderator role: you see the team channels and every ticket. Thanks for helping out."
APPLY_DECLINED = "Thanks a lot for applying to the FFNY Team. We can't take you on right now, but we really appreciate it, and you're welcome to apply again later."

WELCOME = f"""# Welcome to FFNY Online 👊
**Def Jam: Fight for NY** on PC, with online multiplayer: quick match, rooms, your own fighter and a venue vote, right in the game's main menu.

FFNY Online is an unofficial fan project, based on Vlad Yanhursky's native PC port of the Xbox version. You need Windows and **your own USA Xbox disc**: the setup builds the game from it. No game files are shared here or anywhere in the project.

**Get started**
📥 Download: {DOWNLOAD}
💿 Dump your disc: {DISC_GUIDE}
🛠️ Problems? Open a ticket in #get-help
🎮 Looking for opponents? #find-a-match
❤️ Help pay for the servers: {KOFI}

Read #rules, then come and fight."""

RULES = """# Rules
**1. Respect.** No harassment, hate, slurs or threats. Trash talk stays about the fight.
**2. No piracy.** No game files, ISOs, ROM links or "where can I get it". Everyone uses their own disc. This keeps the project alive.
**3. No cheating** or tampering with other players' matches.
**4. Stay on topic** per channel; no spam, no ads, no DM spam.
**5. English** in the public channels, so everyone can follow.
**6. Bugs go into tickets** (#get-help) so nothing gets lost.

Breaking the rules can get you muted or removed. The FFNY Team decides."""

FAQ = [
    ("Which version of the game do I need?",
     "The original **Xbox** version, **USA (NTSC-U)**. PS2, GameCube and PAL discs do not work."),
    ("How do I get the game files from my disc?",
     f"A normal PC drive cannot read Xbox discs. The guide explains the usual ways: {DISC_GUIDE}"),
    ("Windows says the setup is not safe.",
     "The setup is not signed. Choose **More info** and **Run anyway**. It only builds the game from your own dump."),
    ("How do I play online?",
     "Start the game and choose **ONLINE** in the main menu: quick match, rooms, or join a friend by code. No port forwarding needed."),
    ("How do I update?",
     "The launcher offers new builds by itself at its next start. Everyone in a match needs the same build."),
    ("White screen, white boxes or wrong pictures in the game?",
     "That comes from the **HD textures**, which are still experimental. Turn them off in the launcher "
     "(**Textures** page) and restart the game."),
    ("Something does not work.",
     "Open a ticket in #get-help. Screenshots help, and the setup's logs are in `%LOCALAPPDATA%\\DefJamSetup\\logs`."),
]

HELP_PANEL = """# Need help? Open a ticket
Pick what fits. A short form opens, then you get a private channel with the FFNY Team.

🐞 **Bug report**: something in the game is wrong
💿 **Setup & install**: the setup stops or the game does not start
🌐 **Online & connection**: connecting, matches, lag
💬 **Something else**

Screenshots help a lot. Setup logs: `%LOCALAPPDATA%\\DefJamSetup\\logs`"""

ALERTS_TEXT = "🔔 Want a ping when a new build is out? Use the button below (press again to stop)."

MATCH_TEXT = f"""# Looking for a fight? ⚔️
Post here and ping **@{MATCH_ROLE}**: everyone who opted in gets a ping.
Tell them how to find you: **quick match**, your **room name**, or a **join code** from the ONLINE menu.
Everyone in a match needs the same build. Want the pings yourself? Pick *Match calls* under Channels & Roles."""

# Discord's onboarding for new members (needs a Community server). Default channels everyone
# sees: Discord asks for at least seven, five of them ones @everyone can write in.
ONBOARDING_DEFAULTS = ["welcome", "rules", "announcements", SUPPORT_CHANNEL, "get-help", "faq", APPLY_CHANNEL, "general", "find-a-match",
                       "clips-and-screens", "ideas-and-feedback", "off-topic"]
# Questions: (key, title, [(key, emoji, title, description, roles, channels)]).
ONBOARDING_PROMPTS = [
    ("pings", "What should we ping you for?", [
        ("builds", "🔔", "New builds", "A ping when a new FFNY Online build is out", [ALERT_ROLE], []),
        ("matches", "⚔️", "Match calls", "A ping when someone looks for a fight in #find-a-match",
         [MATCH_ROLE], []),
    ]),
    ("voice", "Do you want the voice channels?", [
        ("voice", "🎙️", "Yes, show them", "Lobby and match channels to talk while you fight", [],
         ["Lobby", "Match 1", "Match 2"]),
    ]),
]

WELCOME_SCREEN = "Def Jam: Fight for NY on PC with online multiplayer. Unofficial fan project."
# Up to five channels on the welcome screen: (channel, emoji, description).
WELCOME_CHANNELS = [
    ("welcome", "👋", "Start here: download and disc guide"),
    ("rules", "📜", "The few rules we have"),
    ("find-a-match", "⚔️", "Find opponents"),
    ("get-help", "🛠️", "Bug reports and setup help"),
    ("announcements", "📢", "New builds and news"),
]


def slug(text, limit=20):
    """A channel-name-safe piece of a user name: lower case, letters, digits and dashes."""
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return (s[:limit].strip("-")) or "player"


def ticket_channel_name(prefix, number, username):
    return "%s-%04d-%s" % (prefix, number, slug(username))


def parse_relay_summary(text):
    """(rooms, clients) from the newest relay summary line, or None."""
    found = None
    for m in re.finditer(r"summary rooms=(\d+) clients=(\d+)", text):
        found = (int(m.group(1)), int(m.group(2)))
    return found


def status_name(summary):
    """The status channel's name for a parse_relay_summary() result, or for relay_totals() of
    several relays: (rooms, clients) or a list of (label, (rooms, clients) or None)."""
    if isinstance(summary, list):
        up = [(label, counts) for label, counts in summary if counts is not None]
        if not up:
            return "🔴 Server offline"
        rooms = sum(c[0] for _, c in up)
        clients = sum(c[1] for _, c in up)
        parts = " · ".join("%s %d" % (RELAY_SHORT.get(label, label), c[1]) for label, c in summary
                           if c is not None)
        return "🟢 %d online · %d room%s · %s" % (clients, rooms, "" if rooms == 1 else "s", parts)
    if summary is None:
        return "🔴 Server offline"
    rooms, clients = summary
    return "🟢 %d online · %d room%s" % (clients, rooms, "" if rooms == 1 else "s")


# The relays the game offers (src/hooks/netplay_servers.c): label, host. The bot asks each with
# STATUS (docs/netplay-protocol.md, "Relay status") for its players online and rooms.
RELAYS = [("Europe", "play.defjam.online"), ("North America", "na.defjam.online")]
RELAY_SHORT = {"Europe": "EU", "North America": "NA"}
RELAY_PORT = 27777
STATUS_TYPE = 0x24
PROTOCOL_VERSION = 1


def status_request(nonce):
    """A STATUS request: header, nonce, an all-zero build (any), 8 reserved zero bytes."""
    return b"DJNP" + bytes([PROTOCOL_VERSION, STATUS_TYPE]) + int(nonce & 0xFFFFFFFF).to_bytes(4, "little") + bytes(16)


def parse_status_reply(data, nonce):
    """(rooms, online) from a STATUS answer to the request with this nonce, or None."""
    if len(data) < 6 + 4 + 2 + 2 + 2 or data[:4] != b"DJNP" or data[5] != STATUS_TYPE:
        return None
    body = data[6:]
    if int.from_bytes(body[0:4], "little") != (nonce & 0xFFFFFFFF):
        return None
    online = int.from_bytes(body[6:8], "little")
    rooms = int.from_bytes(body[8:10], "little")
    return rooms, online


def parse_manifest(raw):
    """The update manifest's build, version and notes, or None when it is not one."""
    try:
        m = json.loads(raw)
        build = int(m["build"])
    except (ValueError, KeyError, TypeError):
        return None
    return {"build": build, "version": str(m.get("version", "")), "notes": str(m.get("notes", ""))}


def is_new_build(last_build, manifest):
    """True when manifest announces a build after last_build. The first look (no last_build)
    only records the build: the bot should not announce what was already out."""
    return manifest is not None and last_build is not None and manifest["build"] > last_build


def transcript(channel_name, lines):
    """A ticket's transcript as text; lines are (time, author, content, attachment urls)."""
    out = ["Transcript of #%s" % channel_name, ""]
    for when, author, content, files in lines:
        out.append("[%s] %s: %s" % (when, author, content))
        for url in files:
            out.append("    attachment: %s" % url)
    return "\n".join(out) + "\n"


RESULT_MARK = {"win": "🟢 W", "loss": "🔴 L", "draw": "⚪ D"}


def record_line(stats):
    """'12 W · 5 L · 1 D · 71%' for an account's statistics."""
    line = "%d W · %d L" % (stats.get("wins", 0), stats.get("losses", 0))
    if stats.get("draws"):
        line += " · %d D" % stats["draws"]
    if stats.get("wins", 0) + stats.get("losses", 0):
        line += " · %d%%" % stats.get("win_rate", 0)
    return line


def recent_lines(stats, n=5):
    """The last matches, one line each: result, fighter, opponents, venue."""
    out = []
    for m in (stats.get("recent") or [])[:n]:
        opp = ", ".join(m.get("opponents") or []) or "?"
        line = "%s  %s vs %s" % (RESULT_MARK.get(m.get("result"), "?"), m.get("fighter") or "?", opp)
        if m.get("venue"):
            line += "  · " + m["venue"]
        out.append(line)
    return out


def leaderboard_lines(entries):
    medals = {1: "🥇", 2: "🥈", 3: "🥉"}
    return ["%s **%s**  %d W · %d L · %d%%" % (medals.get(e["rank"], "`%2d.`" % e["rank"]), e["name"], e["wins"],
                                               e["losses"], e["win_rate"]) for e in entries]


def report_title(info):
    """The heading of a bug report from the account service's /internal/reports."""
    crash = info.get("kind") == "crash" or bool(info.get("crash"))
    return "%s %s %s" % ("💥" if crash else "🐞", "Crash report" if info.get("kind") == "crash" else "F9 report",
                         info.get("id", "?"))


def report_fields(info):
    """(name, value) pairs for a report's embed; the crash line in a code block, cut to fit."""
    fields = [("Build", info.get("build") or "?"), ("Player", (info.get("player") or "?")[:200]),
              ("Get it", "`python scripts/bug-reports.py get %s`" % info.get("id", "?"))]
    if info.get("crash"):
        fields.append(("Crash", "```%s```" % info["crash"][:900].replace("`", "'")))
    return fields


def keep_in_polls(has_poll, by_bot, by_team):
    """Whether a message in the polls channel stays: polls, and anything the bot or the team posts."""
    return bool(has_poll or by_bot or by_team)


POLLS_ONLY_NOTE = "This channel is for polls only, so your message was removed. Start a poll with the + button → Poll, or chat in #general."


POLL_PANEL = """# 🗳️ Ideas & polls
Got an idea for FFNY Online? A new mode, a feature, a change? Put it to the community!

**How it works:** press **Start a poll**, describe your idea in one line and send. Everyone votes 👍 or 👎.
Want other choices (for example which venue or which mode first)? Add your own answers, one per line.

A poll runs 2 days unless you pick another length (1 to 14 days) in the form. The ideas with the most votes go on our list for the next builds."""

POLL_MAX_ANSWERS = 10       # Discord's limit
POLL_ANSWER_LEN = 55        # Discord's limit for an answer's text
POLL_QUESTION_LEN = 300     # Discord's limit for the question
POLL_COOLDOWN_S = 600       # one poll per player every ten minutes
POLL_DAYS = 2               # a poll's length when the form leaves it empty
POLL_MAX_DAYS = 14          # the longest a player may pick (Discord allows 32)
POLL_IDEA_ANSWERS = ("👍 Yes, add it", "👎 No")   # the answers when the form leaves them empty


def parse_days(text):
    """The poll length field as days: empty is POLL_DAYS; "7", "7d", "7 days", "2w" or "1 week" count;
    None when it is not a whole number of days from 1 to POLL_MAX_DAYS."""
    text = " ".join((text or "").lower().split())
    if not text:
        return POLL_DAYS
    m = re.fullmatch(r"(\d{1,3})\s*(d|day|days|w|wk|week|weeks)?", text)
    if not m:
        return None
    days = int(m.group(1)) * (7 if (m.group(2) or "d").startswith("w") else 1)
    return days if 1 <= days <= POLL_MAX_DAYS else None


def parse_poll(question, answers_text, days_text=""):
    """The poll form's fields as (question, answers, hours), or (None, error) when they do not make a
    poll: 2 to 10 different non-empty answers, one per line, and a length of 1 to POLL_MAX_DAYS days."""
    question = " ".join((question or "").split())
    if not question:
        return None, "Your poll needs a question."
    answers, seen = [], set()
    for line in (answers_text or "").splitlines():
        text = " ".join(line.split()).lstrip("-*• ").strip()
        if text and text.lower() not in seen:
            seen.add(text.lower())
            answers.append(text[:POLL_ANSWER_LEN])
    if not answers:
        answers = list(POLL_IDEA_ANSWERS)      # an idea: thumbs up or down
    if len(answers) < 2:
        return None, "Give at least two different answers, one per line (or none for 👍 / 👎)."
    if len(answers) > POLL_MAX_ANSWERS:
        return None, "At most %d answers, please." % POLL_MAX_ANSWERS
    days = parse_days(days_text)
    if days is None:
        return None, "Pick a length from 1 to %d days (for example 3 or 1w), or leave it empty for %d." % (
            POLL_MAX_DAYS, POLL_DAYS)
    return (question[:POLL_QUESTION_LEN], answers, days * 24), None


# ---- automatic moderation (spam bots) ----

HONEYPOT_TEXT = """# 🍯 Do not post here
This channel is a trap for spam bots. **Anything posted here gets you removed from the server automatically.**
Real members: please just ignore this channel."""

SPAM_CHANNELS = 3            # the same message in this many channels ...
SPAM_WINDOW_S = 60           # ... within this many seconds: a spam bot
NEW_MEMBER_S = 24 * 3600     # members this new (and without roles) get the strict link rules
MASS_MENTIONS = 5            # this many user/role mentions in one message
TIMEOUT_S = 3600             # a timeout lasts an hour
STRIKE_WINDOW_S = 24 * 3600  # a second strike within this long: softban

SAFE_DOMAINS = (
    "defjam.online", "github.com", "githubusercontent.com", "ko-fi.com", "youtube.com", "youtu.be",
    "twitch.tv", "clips.twitch.tv", "medal.tv", "streamable.com", "imgur.com", "tenor.com", "giphy.com",
    "klipy.com", "discord.com/channels", "cdn.discordapp.com", "media.discordapp.net", "x.com", "twitter.com",
    "reddit.com", "tiktok.com", "instagram.com", "google.com", "steamcommunity.com", "store.steampowered.com",
    "microsoft.com", "nvidia.com", "amd.com",
)
INVITE = re.compile(r"(discord\.gg|discord(?:app)?\.com/invite|dsc\.gg|discord\.me)/\S+", re.I)
URL = re.compile(r"https?://([^\s/<>]+)(/\S*)?", re.I)
SCAM_WORDS = re.compile(
    r"(free\s*nitro|nitro\s*(gift|free|giveaway)|steam\s*gift|gift\s*card|airdrop|crypto\s*(giveaway|bonus)|"
    r"\$\s*\d+\s*(gift|free)|onlyfans|18\+\s*(content|leak)|nudes?\b|leaked\s*(pics|video)|"
    r"claim\s*(your|now)|first\s*\d+\s*(people|users)|i'?m\s*leaving\s*cs|selling\s*(my\s*)?(skins|account))", re.I)
# Hijacked accounts (stolen login tokens, e.g. after the Double Counter breach) post these from old,
# trusted-looking members: fake MrBeast/crypto giveaways and casino "promo codes". Judged for everyone.
HIJACK_WORDS = re.compile(
    r"(mr\.?\s*beast|beast\s*(games|giveaway|casino)|crypto\s*casino|casino\s*(bonus|promo)|promo\s*code|"
    r"bonus\s*code|withdraw(al)?\s*(now|instantly|your)|\$\d{3,}[\d,]*\s*(free|bonus|giveaway|reward)|"
    r"(usdt|btc|eth|solana)\s*(giveaway|bonus|reward)|i\s*just\s*won\s*\$)", re.I)
LOOKALIKE = re.compile(r"(d[il1]sc[o0]r[dcl][^.\s/]*\.(?!com\b|gg\b)[a-z]{2,}|st[e3]am(?!community\.com|powered\.com)"
                       r"[a-z0-9-]*\.(com|ru|net|org|xyz|gift|link|io|cc)|dlscord|disc0rd|discorcl|steamcornmunity)", re.I)


def normalize_text(text):
    """A message's text for comparing copies: lower case, single spaces, no zero-width characters."""
    text = re.sub(r"[\u200b-\u200f\u2060\ufeff]", "", text or "")
    return " ".join(text.lower().split())


def link_domains(text):
    return [m.group(1).lower().split("@")[-1] for m in URL.finditer(text or "")]


def is_safe_domain(domain):
    domain = domain.lower().removeprefix("www.")
    return any(domain == d.split("/")[0] or domain.endswith("." + d.split("/")[0]) for d in SAFE_DOMAINS)


def judge_message(text, mentions, everyone_attempt, is_new, has_roles):
    """What a member's message deserves: None, or (action, reason) with action "timeout" or
    "softban". Team members are never judged (the caller skips them)."""
    text = text or ""
    if mentions >= MASS_MENTIONS:
        return "timeout", "mass mentions (%d)" % mentions
    if everyone_attempt and URL.search(text):
        return "timeout", "@everyone with a link"
    if LOOKALIKE.search(text):
        return "timeout", "a fake Discord/Steam link"
    if HIJACK_WORDS.search(text) and (URL.search(text) or INVITE.search(text) or "code" in text.lower()):
        return "timeout", "a giveaway/casino scam (hijacked account?)"
    newcomer = is_new and not has_roles
    if newcomer and SCAM_WORDS.search(text) and URL.search(text):
        return "timeout", "a scam message with a link"
    if newcomer and INVITE.search(text):
        return "timeout", "a server invite from a new member"
    if newcomer and any(not is_safe_domain(d) for d in link_domains(text)) and SCAM_WORDS.search(text):
        return "timeout", "an unknown link with scam words"
    return None


class SpamTracker:
    """Remembers each member's recent messages to spot the same text in several channels."""

    def __init__(self):
        self.recent = {}        # user id -> list of (time, channel id, normalized text, message id)

    def add(self, user_id, now, channel_id, text, message_id, attachments=()):
        """Records a message; returns the (channel id, message id) of every copy when the same text has
        now been posted in SPAM_CHANNELS different channels within SPAM_WINDOW_S, else None.
        attachments: (filename, size) pairs, so image-only spam (the usual fake-giveaway picture)
        counts as the same message too."""
        norm = normalize_text(text)
        if attachments:
            norm += " [files " + ",".join("%s:%d" % (n.lower(), sz) for n, sz in sorted(attachments)) + "]"
        items = [x for x in self.recent.get(user_id, []) if now - x[0] <= SPAM_WINDOW_S]
        items.append((now, channel_id, norm, message_id))
        self.recent[user_id] = items[-30:]
        if len(norm) < 6 and not URL.search(norm):
            return None
        same = [x for x in items if x[2] == norm]
        if len({x[1] for x in same}) >= SPAM_CHANNELS:
            self.recent[user_id] = [x for x in items if x[2] != norm]
            return [(x[1], x[3]) for x in same]
        return None
