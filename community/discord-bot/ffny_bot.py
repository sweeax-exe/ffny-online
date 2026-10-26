"""FFNY Online Discord bot.

Builds the server's layout on its first start (roles, categories, channels, the welcome, rules,
FAQ and help texts; it only adds what is missing and never deletes), runs the ticket system
(a panel in #get-help: a form per kind, then a private channel with the team; closing posts a
transcript to #ticket-log), announces new builds from the update manifest and keeps the
player count from the relay's summary line in the status channel's name.

Configuration (environment):
  FFNY_BOT_TOKEN_FILE  file with the bot token (default /etc/ffny-bot/token)
  FFNY_BOT_STATE       state file (default /var/lib/ffny-bot/state.json)
  FFNY_BOT_GUILD       server id; without it the first server the bot is in
"""
import asyncio
import datetime
import io
import json
import logging
import os
import sys

import aiohttp
import discord
from discord import app_commands
from discord.ext import tasks

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ffny_core as core  # noqa: E402

log = logging.getLogger("ffny-bot")
STATE_PATH = os.environ.get("FFNY_BOT_STATE", "/var/lib/ffny-bot/state.json")
TOKEN_FILE = os.environ.get("FFNY_BOT_TOKEN_FILE", "/etc/ffny-bot/token")
ORANGE = discord.Colour(0xF28C28)


# ---- state ------------------------------------------------------------------------------------

def load_state():
    try:
        with open(STATE_PATH, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_state(state):
    tmp = STATE_PATH + ".new"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(state, f, indent=1)
    os.replace(tmp, STATE_PATH)


# ---- the client -------------------------------------------------------------------------------

class FfnyBot(discord.Client):
    def __init__(self):
        intents = discord.Intents.default()
        intents.message_content = True      # ticket transcripts
        super().__init__(intents=intents)
        self.tree = app_commands.CommandTree(self)
        self.state = load_state()
        self.state.setdefault("tickets", {})
        self.state.setdefault("messages", {})
        self.ready_once = False
        self.last_poll = {}             # user id -> when they last started a poll (cooldown)
        self.spam = core.SpamTracker()
        self.strikes = {}               # user id -> when they last got a timeout (a second one: softban)

    async def setup_hook(self):
        self.add_view(HelpPanel())
        self.add_view(TicketControls())
        self.add_view(AlertToggle())
        self.add_view(ApplyPanel())
        self.add_view(ApplicationControls())
        self.add_view(PollPanel())

    def guild(self):
        want = os.environ.get("FFNY_BOT_GUILD")
        if want:
            return self.get_guild(int(want))
        return self.guilds[0] if self.guilds else None

    async def on_ready(self):
        log.info("logged in as %s in %d server(s)", self.user, len(self.guilds))
        guild = self.guild()
        if guild is None:
            log.warning("not in a server yet: waiting for the invite")
            return
        await self.start_in(guild)

    def trusted(self, member):
        """The team, moderators and supporters: never touched by the automatic moderation."""
        if not isinstance(member, discord.Member):
            return False
        if member.guild_permissions.manage_messages or member.guild_permissions.administrator:
            return True
        names = {core.TEAM_ROLE, core.MOD_ROLE, core.SUPPORTER_ROLE}
        return any(r.name in names for r in member.roles)

    async def on_message(self, message):
        if message.guild is None or message.author.bot or message.webhook_id:
            return
        if await self.automod(message):
            return
        await self.polls_only(message)

    async def automod(self, message):
        """Spam bots: the honeypot channel, the same text in several channels, scam links and mass
        mentions. Returns True when it acted on the message."""
        author, guild, channel = message.author, message.guild, message.channel
        if self.trusted(author) or not isinstance(author, discord.Member):
            return False
        now = datetime.datetime.now(datetime.timezone.utc)
        if getattr(channel, "name", None) == core.HONEYPOT_CHANNEL:
            await self.softban(guild, author, "posted in #%s" % core.HONEYPOT_CHANNEL, message.content)
            return True
        copies = self.spam.add(author.id, now.timestamp(), channel.id, message.content, message.id)
        if copies:
            for channel_id, message_id in copies:
                ch = guild.get_channel(channel_id)
                if ch:
                    try:
                        await ch.get_partial_message(message_id).delete()
                    except discord.HTTPException:
                        pass
            await self.softban(guild, author, "the same message in %d channels" % len({c for c, _ in copies}),
                               message.content)
            return True
        joined = author.joined_at or now
        is_new = (now - joined).total_seconds() < core.NEW_MEMBER_S
        has_roles = len(author.roles) > 1
        mentions = len(message.raw_mentions) + len(message.raw_role_mentions)
        everyone = "@everyone" in message.content or "@here" in message.content
        verdict = core.judge_message(message.content, mentions, everyone, is_new, has_roles)
        if not verdict:
            return False
        _, reason = verdict
        try:
            await message.delete()
        except discord.HTTPException:
            pass
        last = self.strikes.get(author.id)
        if last and (now - last).total_seconds() < core.STRIKE_WINDOW_S:
            await self.softban(guild, author, reason + " (second time)", message.content)
        else:
            self.strikes[author.id] = now
            try:
                await author.timeout(datetime.timedelta(seconds=core.TIMEOUT_S), reason="FFNY automod: " + reason)
            except discord.HTTPException as error:
                log.warning("timeout of %s failed: %s", author, error)
            await self.automod_log(guild, author, "timed out for 1 hour", reason, message.content)
        return True

    async def softban(self, guild, member, reason, content):
        """Ban (deleting their messages of the last hour) and unban at once: they are out and can come back."""
        try:
            await guild.ban(member, reason="FFNY automod: " + reason, delete_message_seconds=3600)
            await guild.unban(member, reason="FFNY automod: softban")
        except discord.HTTPException as error:
            log.warning("softban of %s failed: %s", member, error)
            return
        await self.automod_log(guild, member, "removed (softban, may rejoin)", reason, content)

    async def automod_log(self, guild, member, action, reason, content):
        excerpt = (content or "").replace("`", "'")[:300]
        log.info("automod: %s %s: %s", member, action, reason)
        await self.ticket_log(guild, "🛡️ **Automod:** %s (`%s`) %s: %s\n> %s" % (
            member.mention, member.id, action, reason, excerpt or "(no text)"))

    async def polls_only(self, message):
        """#polls holds polls only: any other message from a player is removed, with a short note."""
        channel = message.channel
        if getattr(channel, "name", None) != core.POLLS_CHANNEL:
            return
        author = message.author
        team = self.role(message.guild, core.TEAM_ROLE)
        by_team = isinstance(author, discord.Member) and (
            author.guild_permissions.manage_messages or (team is not None and team in author.roles))
        if core.keep_in_polls(getattr(message, "poll", None) is not None, author.bot, by_team):
            return
        try:
            await message.delete()
            await channel.send(f"{author.mention} {core.POLLS_ONLY_NOTE}", delete_after=10,
                               allowed_mentions=discord.AllowedMentions(users=[author]))
        except discord.HTTPException as error:
            log.warning("could not tidy #%s: %s", core.POLLS_CHANNEL, error)

    async def on_guild_join(self, guild):
        log.info("joined %s", guild.name)
        if guild == self.guild():
            await self.start_in(guild)

    async def start_in(self, guild):
        if self.ready_once:
            return
        self.ready_once = True
        await self.build_server(guild)
        self.tree.copy_global_to(guild=guild)
        await self.tree.sync(guild=guild)
        watch_builds.start(self)
        update_status.start(self)
        watch_reports.start(self)

    # -- layout --

    def role(self, guild, name):
        return discord.utils.get(guild.roles, name=name)

    async def build_server(self, guild):
        team = self.role(guild, core.TEAM_ROLE) or await guild.create_role(
            name=core.TEAM_ROLE, colour=ORANGE, hoist=True, mentionable=True, reason="FFNY bot layout")
        for name in (core.ALERT_ROLE, core.MATCH_ROLE):
            if not self.role(guild, name):
                await guild.create_role(name=name, mentionable=True, reason="FFNY bot layout")
        if not self.role(guild, core.SUPPORTER_ROLE):
            await guild.create_role(name=core.SUPPORTER_ROLE, colour=discord.Colour(0xFF5E8A), hoist=True,
                                    mentionable=False, reason="FFNY bot layout: donors")
        if not self.role(guild, core.MOD_ROLE):
            # Moderation without kicks or bans: those stay with the owner.
            perms = discord.Permissions(manage_messages=True, moderate_members=True, manage_threads=True,
                                        mute_members=True, move_members=True, read_message_history=True)
            await guild.create_role(name=core.MOD_ROLE, permissions=perms, colour=discord.Colour(0x3BA55D),
                                    hoist=True, mentionable=True, reason="FFNY bot layout")
        everyone, me = guild.default_role, guild.me
        bot_all = discord.PermissionOverwrite(view_channel=True, send_messages=True, manage_channels=True,
                                              manage_messages=True, embed_links=True, attach_files=True,
                                              read_message_history=True, connect=True)
        for cat_name, channels in core.LAYOUT:
            category = discord.utils.get(guild.categories, name=cat_name)
            private = cat_name == core.TICKET_CATEGORY or any(k == "team" for _, k in channels)
            if category is None:
                over = {me: bot_all}
                if private:
                    over[everyone] = discord.PermissionOverwrite(view_channel=False)
                    over[team] = discord.PermissionOverwrite(view_channel=True, send_messages=True,
                                                             read_message_history=True)
                category = await guild.create_category(cat_name, overwrites=over, reason="FFNY bot layout")
                log.info("created category %s", cat_name)
            for name, kind in channels:
                await self.ensure_channel(guild, category, name, kind, team, bot_all)
        save_state(self.state)
        await self.invite(guild)
        await self.post_texts(guild)
        await self.community(guild)

    async def invite(self, guild):
        """The server's permanent invite (on #welcome), made once and kept in the state."""
        code = self.state.get("invite")
        if code:
            try:
                await self.fetch_invite(code)
                return code
            except discord.NotFound:
                pass
        channel = self.text_channel(guild, "welcome")
        if channel is None:
            return None
        inv = await channel.create_invite(max_age=0, max_uses=0, unique=False, reason="permanent invite")
        self.state["invite"] = inv.code
        save_state(self.state)
        log.info("permanent invite: %s", inv.url)
        return inv.code

    async def community(self, guild):
        """Community mode, the welcome screen and the onboarding questions. Each step logs and
        goes on when Discord refuses it, so the rest of the setup still runs."""
        rules, team_chat = self.text_channel(guild, "rules"), self.text_channel(guild, "team-chat")
        if "COMMUNITY" not in guild.features and rules and team_chat:
            try:
                await guild.edit(community=True, rules_channel=rules, public_updates_channel=team_chat,
                                 verification_level=discord.VerificationLevel.low,
                                 explicit_content_filter=discord.ContentFilter.all_members,
                                 default_notifications=discord.NotificationLevel.only_mentions,
                                 reason="FFNY community setup")
                log.info("community mode on")
            except discord.HTTPException as e:
                log.warning("community mode: %s", e)
                return
        try:
            channels = [discord.WelcomeChannel(channel=c, description=d, emoji=e)
                        for name, e, d in core.WELCOME_CHANNELS if (c := self.text_channel(guild, name))]
            await guild.edit_welcome_screen(description=core.WELCOME_SCREEN, welcome_channels=channels,
                                            enabled=True, reason="FFNY welcome screen")
        except discord.HTTPException as e:
            log.warning("welcome screen: %s", e)
        try:
            await self.http.request(discord.http.Route("PUT", "/guilds/{guild_id}/onboarding", guild_id=guild.id),
                                    json=self.onboarding(guild), reason="FFNY onboarding")
            log.info("onboarding set")
        except discord.HTTPException as e:
            log.warning("onboarding: %s", e)

    def snowflake(self, key):
        """A stable id per onboarding question and answer, so a redeploy edits them in place."""
        ids = self.state.setdefault("onboarding_ids", {})
        if key not in ids:
            ids[key] = str(discord.utils.time_snowflake(discord.utils.utcnow()) + len(ids))
        return ids[key]

    def onboarding(self, guild):
        def channel_id(name):
            c = discord.utils.get(guild.channels, name=name)
            return str(c.id) if c else None
        prompts = []
        for pkey, title, options in core.ONBOARDING_PROMPTS:
            opts = []
            for okey, emoji, otitle, desc, roles, chans in options:
                opts.append({"id": self.snowflake(pkey + "." + okey), "title": otitle, "description": desc,
                             "emoji_name": emoji,
                             "role_ids": [str(r.id) for n in roles if (r := self.role(guild, n))],
                             "channel_ids": [i for n in chans if (i := channel_id(n))]})
            prompts.append({"id": self.snowflake(pkey), "type": 0, "title": title, "options": opts,
                            "single_select": False, "required": False, "in_onboarding": True})
        save_state(self.state)
        return {"prompts": prompts, "enabled": True, "mode": 0,
                "default_channel_ids": [i for n in core.ONBOARDING_DEFAULTS if (i := channel_id(n))]}

    async def ensure_channel(self, guild, category, name, kind, team, bot_all):
        everyone, me = guild.default_role, guild.me
        if kind == "stat":
            known = guild.get_channel(self.state.get("status_channel", 0))
            if known:
                return known
            ch = await guild.create_voice_channel(
                core.status_name(None), category=category, reason="FFNY bot layout",
                overwrites={everyone: discord.PermissionOverwrite(connect=False), me: bot_all})
            self.state["status_channel"] = ch.id
            return ch
        pool = category.voice_channels if kind == "voice" else category.text_channels
        existing = discord.utils.get(pool, name=name)
        if existing:
            return existing
        over = {me: bot_all}
        if kind == "honeypot":
            over[everyone] = discord.PermissionOverwrite(view_channel=True, send_messages=True, add_reactions=False,
                                                         create_public_threads=False, create_private_threads=False)
        if kind == "polls":
            over[everyone] = discord.PermissionOverwrite(send_messages=True, send_polls=True, add_reactions=True,
                                                         create_public_threads=False, create_private_threads=False,
                                                         send_messages_in_threads=False)
        if kind == "read":
            over[everyone] = discord.PermissionOverwrite(send_messages=False, add_reactions=True,
                                                         create_public_threads=False)
            over[team] = discord.PermissionOverwrite(send_messages=True)
        elif kind == "team":
            over[everyone] = discord.PermissionOverwrite(view_channel=False)
            over[team] = discord.PermissionOverwrite(view_channel=True, send_messages=True)
        if kind == "voice":
            ch = await guild.create_voice_channel(name, category=category, overwrites=over, reason="FFNY bot layout")
        else:
            ch = await guild.create_text_channel(name, category=category, overwrites=over, reason="FFNY bot layout")
        log.info("created #%s", name)
        return ch

    def text_channel(self, guild, name):
        return discord.utils.get(guild.text_channels, name=name)

    async def put(self, guild, key, channel_name, content=None, embed=None, view=None, pin=False):
        """Posts a fixed message once and edits it to the current text on later starts."""
        channel = self.text_channel(guild, channel_name)
        if channel is None:
            return
        mid = self.state["messages"].get(key)
        if mid:
            try:
                msg = await channel.fetch_message(mid)
                await msg.edit(content=content, embed=embed, view=view)
                return
            except discord.NotFound:
                pass
        msg = await channel.send(content=content, embed=embed, view=view)
        if pin:
            await msg.pin(reason="FFNY bot")
        self.state["messages"][key] = msg.id
        save_state(self.state)

    async def post_texts(self, guild):
        welcome = core.WELCOME
        if self.state.get("invite"):
            welcome += "\n\n🔗 Bring your friends: https://discord.gg/" + self.state["invite"]
        await self.put(guild, "welcome", "welcome", content=welcome)
        await self.put(guild, "match", "find-a-match", content=core.MATCH_TEXT, pin=True)
        await self.put(guild, "rules", "rules", content=core.RULES)
        faq = discord.Embed(title="FAQ", colour=ORANGE)
        for q, a in core.FAQ:
            faq.add_field(name=q, value=a, inline=False)
        await self.put(guild, "faq", "faq", embed=faq)
        await self.put(guild, "help", core.HELP_CHANNEL, content=core.HELP_PANEL, view=HelpPanel())
        await self.put(guild, "alerts", core.ANNOUNCEMENTS, content=core.ALERTS_TEXT, view=AlertToggle())
        await self.put(guild, "apply", core.APPLY_CHANNEL, content=core.APPLY_TEXT, view=ApplyPanel())
        await self.put(guild, "support", core.SUPPORT_CHANNEL, content=core.SUPPORT_TEXT)
        await self.put(guild, "polls", core.POLLS_CHANNEL, content=core.POLL_PANEL, view=PollPanel(), pin=True)
        await self.put(guild, "honeypot", core.HONEYPOT_CHANNEL, content=core.HONEYPOT_TEXT, pin=True)

    # -- tickets --

    def open_tickets(self, user_id):
        return [int(cid) for cid, t in self.state["tickets"].items() if t["owner"] == user_id]

    async def open_ticket(self, interaction, kind, answers):
        guild = interaction.guild
        info = core.TICKET_TYPES[kind]
        team = self.role(guild, core.TEAM_ROLE)
        category = discord.utils.get(guild.categories, name=core.TICKET_CATEGORY)
        number = self.state.get("ticket_counter", 0) + 1
        self.state["ticket_counter"] = number
        user = interaction.user
        over = {
            guild.default_role: discord.PermissionOverwrite(view_channel=False),
            user: discord.PermissionOverwrite(view_channel=True, send_messages=True, attach_files=True,
                                              embed_links=True, read_message_history=True),
            guild.me: discord.PermissionOverwrite(view_channel=True, send_messages=True, manage_channels=True,
                                                  read_message_history=True, attach_files=True),
        }
        private = info.get("private", False)       # applications: the applicant and the admins only
        if team and not private:
            over[team] = discord.PermissionOverwrite(view_channel=True, send_messages=True, attach_files=True,
                                                     manage_messages=True, read_message_history=True)
        channel = await guild.create_text_channel(
            core.ticket_channel_name(info["prefix"], number, user.name), category=category, overwrites=over,
            topic="%s ticket #%d by %s (%d)" % (info["label"], number, user, user.id), reason="FFNY ticket")
        self.state["tickets"][str(channel.id)] = {"owner": user.id, "kind": kind, "number": number,
                                                  "opened": datetime.datetime.now(datetime.timezone.utc).isoformat()}
        save_state(self.state)
        embed = discord.Embed(title="%s %s #%d" % (info["emoji"], info["label"], number), colour=ORANGE)
        for key, label, _, _, _ in info["fields"]:
            value = answers.get(key, "").strip()
            if value:
                embed.add_field(name=label, value=value[:1024], inline=False)
        if private:
            embed.set_footer(text="Only you and the server's admins see this channel.")
            await channel.send(user.mention, embed=embed, view=ApplicationControls(),
                               allowed_mentions=discord.AllowedMentions(users=True))
            return channel
        embed.set_footer(text="Add screenshots or logs here. Setup logs: %LOCALAPPDATA%\\DefJamSetup\\logs")
        ping = user.mention + (" " + team.mention if team else "")
        await channel.send(ping, embed=embed, view=TicketControls(),
                           allowed_mentions=discord.AllowedMentions(users=True, roles=True))
        await self.ticket_log(guild, "📬 %s opened %s (%s)" % (user.mention, channel.mention, info["label"]))
        return channel

    async def ticket_log(self, guild, text, file=None):
        channel = self.text_channel(guild, core.TICKET_LOG)
        if channel:
            await channel.send(text, file=file, allowed_mentions=discord.AllowedMentions.none())

    async def member(self, guild, user_id):
        """A member by id, asked of Discord when not in the cache (the bot has no members intent, so
        only recently active members are cached); None when they left the server."""
        found = guild.get_member(user_id)
        if found:
            return found
        try:
            return await guild.fetch_member(user_id)
        except (discord.NotFound, discord.HTTPException):
            return None

    def is_admin(self, member):
        perms = getattr(member, "guild_permissions", None)
        return bool(perms and (perms.administrator or perms.manage_guild))

    def is_team(self, member):
        return any(r.name == core.TEAM_ROLE for r in getattr(member, "roles", [])) or \
            getattr(member, "guild_permissions", None) and member.guild_permissions.administrator

    async def close_ticket(self, interaction):
        channel = interaction.channel
        ticket = self.state["tickets"].get(str(channel.id))
        if ticket is None:
            await interaction.response.send_message("This is not a ticket channel.", ephemeral=True)
            return
        if interaction.user.id != ticket["owner"] and not self.is_team(interaction.user):
            await interaction.response.send_message("Only the ticket's owner or the team can close it.", ephemeral=True)
            return
        await interaction.response.send_message("Closing this ticket in 5 seconds; the transcript is saved.")
        lines = []
        async for m in channel.history(limit=None, oldest_first=True):
            content = m.content
            if m.embeds and not content:
                e = m.embeds[0]
                content = (e.title or "") + " | " + " | ".join("%s: %s" % (f.name, f.value) for f in e.fields)
            lines.append((m.created_at.strftime("%Y-%m-%d %H:%M"), str(m.author), content,
                          [a.url for a in m.attachments]))
        text = core.transcript(channel.name, lines).encode("utf-8")
        owner = await self.member(interaction.guild, ticket["owner"])
        if not core.TICKET_TYPES.get(ticket["kind"], {}).get("private"):     # applications stay with the admins
            await self.ticket_log(interaction.guild, "📪 %s closed #%s (opened by <@%d>)" % (
                interaction.user.mention, channel.name, ticket["owner"]),
                file=discord.File(io.BytesIO(text), filename=channel.name + ".txt"))
        if owner:
            try:
                await owner.send("Your FFNY Online ticket #%s is closed. Thanks for the report! Transcript:" % channel.name,
                                 file=discord.File(io.BytesIO(text), filename=channel.name + ".txt"))
            except discord.HTTPException:
                pass        # DMs closed
        del self.state["tickets"][str(channel.id)]
        save_state(self.state)
        await asyncio.sleep(5)
        await channel.delete(reason="FFNY ticket closed")


bot = FfnyBot()


# ---- views and forms --------------------------------------------------------------------------

class TicketForm(discord.ui.Modal):
    def __init__(self, kind):
        info = core.TICKET_TYPES[kind]
        super().__init__(title=info["label"])
        self.kind = kind
        self.inputs = {}
        for key, label, style, required, placeholder in info["fields"]:
            field = discord.ui.TextInput(
                label=label, required=required, placeholder=placeholder or None,
                style=discord.TextStyle.paragraph if style == "paragraph" else discord.TextStyle.short,
                max_length=1000 if style == "paragraph" else 100)
            self.inputs[key] = field
            self.add_item(field)

    async def on_submit(self, interaction):
        await interaction.response.defer(ephemeral=True, thinking=True)
        channel = await bot.open_ticket(interaction, self.kind, {k: f.value for k, f in self.inputs.items()})
        await interaction.followup.send("Your ticket is open: %s" % channel.mention, ephemeral=True)


class HelpPanel(discord.ui.View):
    def __init__(self):
        super().__init__(timeout=None)
        styles = {"bug": discord.ButtonStyle.danger, "setup": discord.ButtonStyle.primary,
                  "online": discord.ButtonStyle.success, "other": discord.ButtonStyle.secondary}
        for kind, info in core.TICKET_TYPES.items():
            if info.get("private"):
                continue                # its own panel in #join-the-team
            button = discord.ui.Button(label=info["label"], emoji=info["emoji"], style=styles[kind],
                                       custom_id="ffny:ticket:" + kind)
            button.callback = self.opener(kind)
            self.add_item(button)

    @staticmethod
    def opener(kind):
        async def callback(interaction):
            mine = [c for c in bot.open_tickets(interaction.user.id) if interaction.guild.get_channel(c)]
            if kind == "apply":
                open_apps = [c for c in mine if bot.state["tickets"][str(c)]["kind"] == "apply"]
                if open_apps:
                    await interaction.response.send_message("Your application is already open: <#%d>." % open_apps[0],
                                                            ephemeral=True)
                    return
            elif len(mine) >= core.MAX_OPEN_TICKETS:
                await interaction.response.send_message(
                    "You already have open tickets: %s. Close one first." % " ".join("<#%d>" % c for c in mine),
                    ephemeral=True)
                return
            await interaction.response.send_modal(TicketForm(kind))
        return callback


class TicketControls(discord.ui.View):
    def __init__(self):
        super().__init__(timeout=None)

    @discord.ui.button(label="Claim", emoji="🙋", style=discord.ButtonStyle.secondary, custom_id="ffny:claim")
    async def claim(self, interaction, button):
        if not bot.is_team(interaction.user):
            await interaction.response.send_message("Only the FFNY Team can claim tickets.", ephemeral=True)
            return
        await interaction.response.send_message("%s is on it." % interaction.user.mention)

    @discord.ui.button(label="Close", emoji="🔒", style=discord.ButtonStyle.danger, custom_id="ffny:close")
    async def close(self, interaction, button):
        await bot.close_ticket(interaction)


class ApplyPanel(discord.ui.View):
    def __init__(self):
        super().__init__(timeout=None)
        info = core.TICKET_TYPES["apply"]
        button = discord.ui.Button(label=info["label"], emoji=info["emoji"], style=discord.ButtonStyle.success,
                                   custom_id="ffny:ticket:apply")
        button.callback = HelpPanel.opener("apply")
        self.add_item(button)


class ApplicationControls(discord.ui.View):
    """Under an application: the admins accept or decline it; either side can close it."""

    def __init__(self):
        super().__init__(timeout=None)

    async def decide(self, interaction, accept):
        ticket = bot.state["tickets"].get(str(interaction.channel.id))
        if not bot.is_admin(interaction.user):
            await interaction.response.send_message("Only the server's admins decide on applications.", ephemeral=True)
            return
        if ticket is None:
            await interaction.response.send_message("This application is already closed.", ephemeral=True)
            return
        member = await bot.member(interaction.guild, ticket["owner"])
        if member is None:
            await interaction.response.send_message("The applicant has left the server.", ephemeral=True)
            return
        if accept:
            roles = [r for r in (bot.role(interaction.guild, core.MOD_ROLE), bot.role(interaction.guild, core.TEAM_ROLE))
                     if r]
            await member.add_roles(*roles, reason="moderator application accepted by %s" % interaction.user)
        text = core.APPLY_ACCEPTED if accept else core.APPLY_DECLINED
        await interaction.response.send_message("%s %s" % (member.mention, text),
                                                allowed_mentions=discord.AllowedMentions(users=True))
        try:
            await member.send(text)
        except discord.HTTPException:
            pass        # DMs closed; the message in the channel says it too

    @discord.ui.button(label="Accept", emoji="✅", style=discord.ButtonStyle.success, custom_id="ffny:apply:accept")
    async def accept(self, interaction, button):
        await self.decide(interaction, True)

    @discord.ui.button(label="Decline", emoji="✖️", style=discord.ButtonStyle.secondary, custom_id="ffny:apply:decline")
    async def decline(self, interaction, button):
        await self.decide(interaction, False)

    @discord.ui.button(label="Close", emoji="🔒", style=discord.ButtonStyle.danger, custom_id="ffny:apply:close")
    async def close(self, interaction, button):
        await bot.close_ticket(interaction)


class PollForm(discord.ui.Modal, title="Share an idea or start a poll"):
    question = discord.ui.TextInput(label="Your idea or question", max_length=core.POLL_QUESTION_LEN,
                                    placeholder="Add a tournament mode with brackets")
    answers = discord.ui.TextInput(label="Answers (optional, empty = 👍 / 👎)", style=discord.TextStyle.paragraph,
                                   required=False, max_length=700,
                                   placeholder="Leave empty to vote yes or no, or one answer per line")

    async def on_submit(self, interaction):
        parsed, error = core.parse_poll(self.question.value, self.answers.value)
        if error:
            await interaction.response.send_message(error, ephemeral=True)
            return
        question, answers, hours = parsed
        poll = discord.Poll(question=question, duration=datetime.timedelta(hours=hours))
        for text in answers:
            poll.add_answer(text=text)
        channel = bot.text_channel(interaction.guild, core.POLLS_CHANNEL) or interaction.channel
        try:
            await channel.send(content="Poll by %s" % interaction.user.mention, poll=poll,
                               allowed_mentions=discord.AllowedMentions.none())
        except discord.HTTPException as error:
            log.warning("poll by %s failed: %s", interaction.user, error)
            await interaction.response.send_message("Discord did not take that poll, please try again.", ephemeral=True)
            return
        bot.last_poll[interaction.user.id] = datetime.datetime.now(datetime.timezone.utc)
        await interaction.response.send_message("Your poll is up! 🗳️", ephemeral=True)


class PollPanel(discord.ui.View):
    def __init__(self):
        super().__init__(timeout=None)

    @discord.ui.button(label="Start a poll", emoji="🗳️", style=discord.ButtonStyle.primary, custom_id="ffny:poll:start")
    async def start(self, interaction, button):
        last = bot.last_poll.get(interaction.user.id)
        now = datetime.datetime.now(datetime.timezone.utc)
        if last and (now - last).total_seconds() < core.POLL_COOLDOWN_S:
            wait = int(core.POLL_COOLDOWN_S - (now - last).total_seconds()) // 60 + 1
            await interaction.response.send_message("You just started a poll. Try again in %d min." % wait,
                                                    ephemeral=True)
            return
        await interaction.response.send_modal(PollForm())


class AlertToggle(discord.ui.View):
    def __init__(self):
        super().__init__(timeout=None)

    @discord.ui.button(label="Build alerts", emoji="🔔", style=discord.ButtonStyle.primary, custom_id="ffny:alerts")
    async def toggle(self, interaction, button):
        role = bot.role(interaction.guild, core.ALERT_ROLE)
        if role is None:
            await interaction.response.send_message("The alert role is missing.", ephemeral=True)
        elif role in interaction.user.roles:
            await interaction.user.remove_roles(role, reason="build alerts off")
            await interaction.response.send_message("No more build pings.", ephemeral=True)
        else:
            await interaction.user.add_roles(role, reason="build alerts on")
            await interaction.response.send_message("You get a ping for every new build. 🔔", ephemeral=True)


# ---- commands ---------------------------------------------------------------------------------

async def fetch_manifest():
    try:
        async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=15)) as s:
            async with s.get(core.UPDATE_URL) as r:
                return core.parse_manifest(await r.text()) if r.status == 200 else None
    except (aiohttp.ClientError, asyncio.TimeoutError):
        return None


async def relay_summary():
    """The relay's newest summary line from the journal (the bot runs beside the relay)."""
    try:
        proc = await asyncio.create_subprocess_exec(
            "journalctl", "-u", "defjam-relay", "-n", "30", "--no-pager", "-o", "cat",
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
        out, _ = await asyncio.wait_for(proc.communicate(), 15)
    except (OSError, asyncio.TimeoutError):
        return None
    return core.parse_relay_summary(out.decode("utf-8", "replace"))


def relay_status_blocking(host, timeout=2.0):
    """(rooms, online) of one relay by STATUS, or None when it does not answer."""
    import random
    import socket
    nonce = random.getrandbits(32)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(timeout)
            addr = (socket.gethostbyname(host), core.RELAY_PORT)
            for _ in range(2):
                sock.sendto(core.status_request(nonce), addr)
                try:
                    data, _ = sock.recvfrom(1500)
                except socket.timeout:
                    continue
                counts = core.parse_status_reply(data, nonce)
                if counts is not None:
                    return counts
    except OSError:
        return None
    return None


async def relay_totals():
    """Every relay's (label, (rooms, online) or None), asked in parallel."""
    loop = asyncio.get_running_loop()
    results = await asyncio.gather(*(loop.run_in_executor(None, relay_status_blocking, host)
                                     for _, host in core.RELAYS))
    return [(label, counts) for (label, _), counts in zip(core.RELAYS, results)]


@bot.tree.command(name="status", description="FFNY Online: the newest build and who is online")
async def status_cmd(interaction):
    await interaction.response.defer(thinking=True)
    manifest, totals = await fetch_manifest(), await relay_totals()
    embed = discord.Embed(title="FFNY Online", colour=ORANGE)
    embed.add_field(name="Newest build", value=("%d (%s)" % (manifest["build"], manifest["version"]))
                    if manifest else "unknown")
    for label, counts in totals:
        embed.add_field(name=label, value=core.status_name(counts))
    embed.add_field(name="Links", value="[Download](%s) · [Disc guide](%s) · [GitHub](%s) · [Support the servers](%s)" % (
        core.DOWNLOAD, core.DISC_GUIDE, core.GITHUB, core.KOFI), inline=False)
    await interaction.followup.send(embed=embed)


async def accounts_get(path):
    try:
        async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=10)) as s:
            async with s.get(core.ACCOUNTS_URL + path) as r:
                return r.status, (await r.json() if r.content_type == "application/json" else None)
    except (aiohttp.ClientError, asyncio.TimeoutError):
        return 0, None


@bot.tree.command(name="profile", description="FFNY Online statistics of a player (yours by default)")
@app_commands.describe(member="whose statistics")
async def profile_cmd(interaction, member: discord.Member = None):
    member = member or interaction.user
    await interaction.response.defer(thinking=True)
    status, st = await accounts_get("/internal/discord/%d" % member.id)
    if status == 404:
        who = "You have" if member == interaction.user else "%s has" % member.display_name
        await interaction.followup.send("%s no FFNY Online account yet: sign in with Discord in the launcher "
                                        "(Online page), then play online." % who)
        return
    if status != 200 or not st:
        await interaction.followup.send("The account service does not answer right now; try again in a moment.")
        return
    embed = discord.Embed(title=st["account"]["name"], colour=ORANGE)
    embed.set_thumbnail(url=member.display_avatar.url)
    embed.add_field(name="Record", value=core.record_line(st) if st["matches"] else "no online matches yet")
    embed.add_field(name="Leaderboard", value=("#%d" % st["rank"]) if st["rank"] else
                    "after %d matches" % core.LEADERBOARD_MIN)
    if st.get("best_streak"):
        embed.add_field(name="Streak", value="%d now · best %d" % (st["streak"], st["best_streak"]))
    if st.get("favorite_name"):
        embed.add_field(name="Main", value=st["favorite_name"])
    lines = core.recent_lines(st)
    if lines:
        embed.add_field(name="Last matches", value="\n".join(lines)[:1024], inline=False)
    await interaction.followup.send(embed=embed)


@bot.tree.command(name="leaderboard", description="The FFNY Online leaderboard")
async def leaderboard_cmd(interaction):
    await interaction.response.defer(thinking=True)
    status, lb = await accounts_get("/internal/leaderboard")
    if status != 200 or lb is None:
        await interaction.followup.send("The account service does not answer right now; try again in a moment.")
        return
    lines = core.leaderboard_lines(lb["entries"])
    embed = discord.Embed(title="🏆 FFNY Online leaderboard", colour=ORANGE,
                          description="\n".join(lines) if lines else
                          "Nobody yet: play %d online matches signed in to get on it." % lb["min_matches"])
    embed.set_footer(text="Most wins; signed-in players with %d+ online matches. Ranked play comes next." % lb["min_matches"])
    await interaction.followup.send(embed=embed)


@bot.tree.command(name="close", description="Close this ticket")
async def close_cmd(interaction):
    await bot.close_ticket(interaction)


@bot.tree.command(name="add", description="Add someone to this ticket (team only)")
async def add_cmd(interaction, member: discord.Member):
    if str(interaction.channel.id) not in bot.state["tickets"] or not bot.is_team(interaction.user):
        await interaction.response.send_message("Only the team can add people, in a ticket channel.", ephemeral=True)
        return
    await interaction.channel.set_permissions(member, view_channel=True, send_messages=True, attach_files=True,
                                              read_message_history=True)
    await interaction.response.send_message("Added %s." % member.mention)


@bot.tree.command(name="setup", description="Add missing channels and refresh the bot's texts")
@app_commands.default_permissions(manage_guild=True)
async def setup_cmd(interaction):
    await interaction.response.defer(ephemeral=True, thinking=True)
    await bot.build_server(interaction.guild)
    await interaction.followup.send("Layout and texts are up to date.", ephemeral=True)


# ---- background jobs --------------------------------------------------------------------------

@tasks.loop(minutes=5)
async def watch_builds(client):
    manifest = await fetch_manifest()
    if manifest is None:
        return
    last = client.state.get("last_build")
    if core.is_new_build(last, manifest):
        guild = client.guild()
        channel = client.text_channel(guild, core.ANNOUNCEMENTS)
        role = client.role(guild, core.ALERT_ROLE)
        embed = discord.Embed(title="Build %d is out" % manifest["build"], description=manifest["notes"],
                              colour=ORANGE)
        embed.add_field(name="Get it", value="The launcher offers it at its next start. New here? [Download](%s)"
                        % core.DOWNLOAD)
        if channel:
            await channel.send(role.mention if role else None, embed=embed,
                               allowed_mentions=discord.AllowedMentions(roles=True))
            log.info("announced build %d", manifest["build"])
    if last != manifest["build"]:
        client.state["last_build"] = manifest["build"]
        save_state(client.state)


async def accounts_bytes(path):
    try:
        async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=20)) as s:
            async with s.get(core.ACCOUNTS_URL + path) as r:
                return await r.read() if r.status == 200 else None
    except (aiohttp.ClientError, asyncio.TimeoutError):
        return None


@tasks.loop(minutes=1)
async def watch_reports(client):
    """New F9 reports and crash logs (the account service keeps them) go to the team's channel."""
    guild = client.guild()
    channel = client.text_channel(guild, core.REPORTS_CHANNEL) if guild else None
    if channel is None:
        return
    last = client.state.get("last_report")
    status, reports = await accounts_get("/internal/reports" + ("?after=" + last if last else ""))
    if status != 200 or not reports:
        return
    if last is None:            # first start: from now on, not the reports made before
        client.state["last_report"] = reports[-1]["folder"]
        save_state(client.state)
        return
    for info in reports:
        embed = discord.Embed(title=core.report_title(info), colour=ORANGE, description=info.get("time", ""))
        for name, value in core.report_fields(info):
            embed.add_field(name=name, value=value, inline=name not in ("Crash", "Get it"))
        picture = await accounts_bytes("/internal/reports/%s/picture.png" % info["folder"]) if info.get("picture") else None
        try:
            if picture:
                embed.set_image(url="attachment://picture.png")
                await channel.send(embed=embed, file=discord.File(io.BytesIO(picture), filename="picture.png"))
            else:
                await channel.send(embed=embed)
        except discord.HTTPException as e:
            log.warning("report %s not posted: %s", info.get("id"), e)
            return
        client.state["last_report"] = info["folder"]
        save_state(client.state)
        log.info("posted report %s", info.get("id"))


@tasks.loop(minutes=10)     # Discord allows two channel renames per ten minutes
async def update_status(client):
    guild = client.guild()
    channel = guild.get_channel(client.state.get("status_channel", 0)) if guild else None
    if channel is None:
        return
    name = core.status_name(await relay_totals())
    if channel.name != name:
        try:
            await channel.edit(name=name, reason="player count")
        except discord.HTTPException as e:
            log.warning("status rename failed: %s", e)


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    with open(TOKEN_FILE, encoding="utf-8") as f:
        token = f.read().strip()
    bot.run(token, log_handler=None)


if __name__ == "__main__":
    main()
