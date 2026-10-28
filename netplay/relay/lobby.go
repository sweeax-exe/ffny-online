package relay

import (
	"crypto/subtle"
	"encoding/binary"
	"io"
	"sort"
	"time"
	"unicode/utf8"
)

// This file holds the lobby extension of the state machine: generalised rooms
// of up to four, room phases (lobby, voting, playing) and the lobby requests.
// The v1 paths (HELLO mode 0/1, PEER, DATA, PING, BYE) in relay.go use the same
// room helpers, so a legacy room of two behaves exactly as before.

// ---- room helpers ----

// members returns the clients present, by slot.
func (rm *room) members() []*client {
	var out []*client
	for _, c := range rm.slots {
		if c != nil {
			out = append(out, c)
		}
	}
	return out
}

func (rm *room) count() int {
	n := 0
	for _, c := range rm.slots {
		if c != nil {
			n++
		}
	}
	return n
}

// host is the member in the lowest slot (nil for an empty room).
func (rm *room) host() *client {
	for _, c := range rm.slots {
		if c != nil {
			return c
		}
	}
	return nil
}

// freeSlot is the slot a new member gets: in a legacy room slot 1 only (as in
// protocol v1), otherwise the lowest free slot below Settings.max.
func (rm *room) freeSlot() (uint8, bool) {
	if rm.legacy {
		return SlotGuest, rm.slots[SlotGuest] == nil
	}
	for i := 0; i < int(rm.settings.Max) && i < len(rm.slots); i++ {
		if rm.slots[i] == nil {
			return uint8(i), true
		}
	}
	return 0, false
}

// joinable checks whether a client with build b (koth: it announced CapKoth) may join now. It
// returns the slot, or the ERROR code: 2 room full, 3 build mismatch, 8 not in phase 0 or a
// King of the Hill room for a client without CapKoth. A King of the Hill room takes players in
// every phase (they join the back of the line and watch).
func (rm *room) joinable(b Build, koth bool) (uint8, uint8) {
	slot, ok := rm.freeSlot()
	if !ok {
		return 0, ErrRoomFull
	}
	if subtle.ConstantTimeCompare(rm.build[:], b[:]) != 1 {
		return 0, ErrBuildMismatch
	}
	if rm.koth != nil {
		if !koth {
			return 0, ErrNotAllowed
		}
		return slot, 0
	}
	if rm.phase != PhaseLobby {
		return 0, ErrNotAllowed
	}
	return slot, 0
}

// place puts c into slot with fresh per-room state.
func (rm *room) place(c *client, slot uint8) {
	rm.slots[slot] = c
	c.room, c.slot = rm, slot
	c.ready, c.vote, c.over = false, VoteNone, false
	if rm.koth != nil {
		rm.koth.join(c)
	}
}

// pairPeer is the other member of a full room of two (PEER is sent in rooms
// whose max is 2, as in protocol v1), or nil.
func (c *client) pairPeer() *client {
	rm := c.room
	if rm == nil || rm.settings.Max != 2 || rm.koth != nil {
		return nil
	}
	for _, o := range rm.slots {
		if o != nil && o != c {
			return o
		}
	}
	return nil
}

func (rm *room) all(pred func(*client) bool) bool {
	for _, c := range rm.slots {
		if c != nil && !pred(c) {
			return false
		}
	}
	return true
}

// ---- room events ----

// joined announces a new member c of rm: PEER both ways in a room of two
// (joiner first, as in protocol v1), then ROOM to every lobby member.
func (r *Relay) joined(now time.Time, rm *room, c *client) []Packet {
	if rm.count() >= 2 {
		rm.everFull = true
	}
	r.cfg.Logf("room %s joined by c=%s slot=%d", rm.code, c.id, c.slot)
	var out []Packet
	if o := c.pairPeer(); o != nil {
		out = append(out,
			Packet{To: c.addr, Data: mustEncode(Peer{Slot: o.slot, Name: o.name})},
			Packet{To: o.addr, Data: mustEncode(Peer{Slot: c.slot, Name: c.name})},
		)
	}
	return append(out, r.changed(now, rm)...)
}

// leaveRoom takes c out of its room (c stays registered, in the lobby). The
// remaining member of a room of two gets PEER with an empty name; a vote in
// progress is called off; a match ends if every remaining member has sent
// OVER; an empty room is deleted at once.
func (r *Relay) leaveRoom(now time.Time, c *client, reason string) []Packet {
	rm, slot := c.room, c.slot
	twoRoom := rm.settings.Max == 2 && rm.koth == nil
	rm.slots[slot] = nil
	c.room, c.slot = nil, SlotNone
	c.ready, c.vote, c.over = false, VoteNone, false
	r.cfg.Logf("room %s peer left c=%s slot=%d reason=%s", rm.code, c.id, slot, reason)
	if rm.count() == 0 {
		delete(r.rooms, rm.code)
		r.cfg.Logf("room %s closed (empty)", rm.code)
		return nil
	}
	r.renameQuick(rm, c)
	if rm.koth != nil {
		r.hillLeft(now, rm, c, slot)
		return r.changed(now, rm)
	}
	var out []Packet
	if twoRoom {
		for _, o := range rm.members() {
			out = append(out, Packet{To: o.addr, Data: mustEncode(Peer{Slot: slot, Name: ""})})
		}
	}
	switch rm.phase {
	case PhaseVoting:
		r.toLobby(rm, "player left during the vote")
	case PhasePlaying:
		if rm.all(func(o *client) bool { return o.over }) {
			r.toLobby(rm, "match over")
		}
	}
	return append(out, r.changed(now, rm)...)
}

// changed is called after anything in rm changed: in phase 0 it starts the
// vote once at least two players are present and all are ready, then it sends
// ROOM to every lobby member.
func (r *Relay) changed(now time.Time, rm *room) []Packet {
	if rm.koth != nil {
		return append(r.hillAdvance(now, rm), r.broadcast(now, rm)...)
	}
	if rm.phase == PhaseLobby && rm.count() >= MinPlayers && rm.all(func(c *client) bool { return c.ready }) {
		rm.candidates = r.drawCandidates(rm.settings.Pool)
		rm.phase = PhaseVoting
		rm.voteEnd = now.Add(r.cfg.VoteTime)
		for _, c := range rm.members() {
			c.vote = VoteNone
		}
		r.cfg.Logf("room %s voting (%d candidates)", rm.code, len(rm.candidates))
	}
	return r.broadcast(now, rm)
}

// broadcast sends ROOM to every lobby member of rm (in a King of the Hill room ROOM and HILL).
func (r *Relay) broadcast(now time.Time, rm *room) []Packet {
	var out []Packet
	var hill []byte
	if rm.koth != nil {
		hill = mustEncode(r.hillState(now, rm))
	}
	for _, c := range rm.members() {
		if c.lobby {
			out = append(out, r.roomTo(now, c))
			if hill != nil {
				out = append(out, Packet{To: c.addr, Data: hill})
			}
		}
	}
	return out
}

// stateTo is ROOM for c alone, and in a King of the Hill room HILL after it.
func (r *Relay) stateTo(now time.Time, c *client) []Packet {
	out := []Packet{r.roomTo(now, c)}
	if c.room.koth != nil {
		out = append(out, Packet{To: c.addr, Data: mustEncode(r.hillState(now, c.room))})
	}
	return out
}

// recipients are the members a DATA from c goes to: every other member, except in a King of
// the Hill room (hillRecipients).
func (r *Relay) recipients(c *client) []*client {
	if c.room.koth != nil {
		return r.hillRecipients(c)
	}
	var out []*client
	for _, o := range c.room.members() {
		if o != c {
			out = append(out, o)
		}
	}
	return out
}

// roomTo is a ROOM about c's room for c alone.
func (r *Relay) roomTo(now time.Time, c *client) Packet {
	return Packet{To: c.addr, Data: mustEncode(r.roomState(now, c.room, c.slot))}
}

func (r *Relay) roomState(now time.Time, rm *room, you uint8) RoomState {
	st := RoomState{
		Room:       rm.code,
		You:        you,
		Phase:      rm.phase,
		Settings:   rm.settings,
		Candidates: rm.candidates,
	}
	if h := rm.host(); h != nil {
		st.Host = h.slot
	}
	if n := len(rm.candidates); n > 0 {
		st.Votes = make([]uint8, n)
	}
	for _, c := range rm.members() {
		if c.vote != VoteNone && int(c.vote) < len(st.Votes) {
			st.Votes[c.vote]++
		}
		ready := uint8(0)
		if c.ready {
			ready = 1
		}
		st.Players = append(st.Players, RoomPlayer{Slot: c.slot, Name: c.name, Ready: ready, Fighter: c.fighter, Voted: c.vote})
	}
	if rm.phase == PhaseVoting {
		ms := rm.voteEnd.Sub(now).Milliseconds()
		st.MsLeft = uint16(max(0, min(ms, 0xFFFF)))
	}
	return st
}

// endVote picks the venue (most votes; ties at random), sends START with a
// fresh seed to every lobby member and enters phase 2.
func (r *Relay) endVote(now time.Time, rm *room) []Packet {
	venue := r.votedVenue(rm)

	var seed [4]byte
	_, _ = io.ReadFull(r.cfg.Rand, seed[:]) // on failure the seed is 0; never happens with crypto/rand
	st := Start{Seed: binary.LittleEndian.Uint32(seed[:]), Mode: rm.settings.Mode, Rounds: rm.settings.Rounds, Venue: venue}
	for _, c := range rm.members() {
		st.Players = append(st.Players, StartPlayer{Slot: c.slot, Fighter: c.fighter})
		c.over = false
	}
	rm.start = mustEncode(st)
	r.rankedStart(rm, st.Seed)
	rm.phase = PhasePlaying
	rm.playStart = now
	r.cfg.Logf("room %s started venue=%d players=%d", rm.code, venue, len(st.Players))

	var out []Packet
	for _, c := range rm.members() {
		if c.lobby {
			out = append(out, Packet{To: c.addr, Data: rm.start})
		}
	}
	return append(out, r.broadcast(now, rm)...)
}

// votedVenue is the candidate with the most votes among the members' votes (ties at random).
func (r *Relay) votedVenue(rm *room) uint8 {
	tally := make([]int, len(rm.candidates))
	for _, c := range rm.members() {
		if int(c.vote) < len(tally) {
			tally[c.vote]++
		}
	}
	best := 0
	for _, n := range tally {
		best = max(best, n)
	}
	var tied []int
	for i, n := range tally {
		if n == best {
			tied = append(tied, i)
		}
	}
	venue := uint8(0)
	if len(tied) > 0 {
		venue = rm.candidates[tied[r.intn(len(tied))]]
	}
	return venue
}

// toLobby returns rm to phase 0 with every ready flag and vote cleared.
func (r *Relay) toLobby(rm *room, why string) {
	rm.phase = PhaseLobby
	rm.candidates, rm.start = nil, nil
	for _, c := range rm.members() {
		c.ready, c.vote, c.over = false, VoteNone, false
	}
	r.cfg.Logf("room %s back to lobby (%s)", rm.code, why)
}

// tickRoom runs the timers of one room: the vote ends after VoteTime (ROOM
// goes out on every tick until then, for the countdown), and a match returns
// to the lobby after PlayTimeout.
func (r *Relay) tickRoom(now time.Time, rm *room) []Packet {
	if rm.koth != nil {
		return r.tickHill(now, rm)
	}
	switch rm.phase {
	case PhaseVoting:
		if !now.Before(rm.voteEnd) {
			return r.endVote(now, rm)
		}
		return r.broadcast(now, rm)
	case PhasePlaying:
		if now.Sub(rm.playStart) >= r.cfg.PlayTimeout {
			r.toLobby(rm, "play timeout")
			return r.changed(now, rm)
		}
	}
	return nil
}

// ---- lobby requests ----

// lobbyRequest handles a lobby message from the registered client c.
// Requests are idempotent, since clients retry over UDP: every accepted
// request except LIST and LEAVE is answered with at least a ROOM to c (a
// broadcast if the room changed, otherwise a ROOM to c alone); LIST always
// answers ROOMS; LEAVE has no reply, and a LEAVE from the lobby is ignored.
func (r *Relay) lobbyRequest(now time.Time, c *client, m lobbyRequest, reqLen int) []Packet {
	switch m := m.(type) {
	case List:
		return r.list(c, m)
	case Who:
		return r.who(c)
	case Ranked:
		if r.outdated(c.buildNo) {
			return r.errorTo(c.addr, ErrUpdateRequired, reqLen)
		}
		return r.ranked(now, c, m, reqLen)
	case Caps:
		c.caps = m.Caps
		return []Packet{{To: c.addr, Data: mustEncode(RelayCapsMsg{Caps: r.caps()})}}
	case Result:
		if c.room == nil {
			return r.errorTo(c.addr, ErrRoomState, reqLen)
		}
		if c.room.koth == nil {
			return r.errorTo(c.addr, ErrNotAllowed, reqLen)
		}
		return r.hillResult(now, c, m)
	case Yield:
		if c.room == nil {
			return r.errorTo(c.addr, ErrRoomState, reqLen)
		}
		if c.room.koth == nil {
			return r.errorTo(c.addr, ErrNotAllowed, reqLen)
		}
		return r.hillYield(now, c, reqLen)
	case Create:
		r.dequeue(c)
		if r.outdated(c.buildNo) { // a new build came out while this client was in the lobby
			return r.errorTo(c.addr, ErrUpdateRequired, reqLen)
		}
		return r.create(now, c, m.Settings, reqLen)
	case Join:
		r.dequeue(c)
		if r.outdated(c.buildNo) {
			return r.errorTo(c.addr, ErrUpdateRequired, reqLen)
		}
		if c.room != nil {
			if c.room.code != m.Room {
				return r.errorTo(c.addr, ErrRoomState, reqLen) // already in another room
			}
			return r.stateTo(now, c) // a retry: its ROOM may have been lost
		}
		rm := r.rooms[m.Room]
		if rm == nil {
			return r.errorTo(c.addr, ErrRoomNotFound, reqLen)
		}
		slot, code := rm.joinable(c.build, c.caps&CapKoth != 0)
		if code != 0 {
			return r.errorTo(c.addr, code, reqLen)
		}
		rm.place(c, slot)
		return r.joined(now, rm, c)
	case Quick:
		r.dequeue(c)
		if r.outdated(c.buildNo) {
			return r.errorTo(c.addr, ErrUpdateRequired, reqLen)
		}
		return r.quick(now, c, m, reqLen)
	case SetSettings:
		return r.setSettings(now, c, m.Settings, reqLen)
	case Ready:
		rm := c.room
		if rm == nil {
			return r.errorTo(c.addr, ErrRoomState, reqLen)
		}
		if rm.koth != nil {
			return r.hillReady(now, c, m)
		}
		if rm.phase != PhaseLobby {
			return r.errorTo(c.addr, ErrNotAllowed, reqLen)
		}
		ready, fighter := m.Ready == 1, safeFighter(m.Fighter)
		if c.ready == ready && c.fighter == fighter {
			return []Packet{r.roomTo(now, c)}
		}
		c.ready, c.fighter = ready, fighter
		return r.changed(now, rm)
	case Vote:
		return r.vote(now, c, m.Choice, reqLen)
	case Leave:
		if c.queue != nil {
			r.dequeue(c)
			r.cfg.Logf("ranked: c=%s left the queue", c.id)
			return []Packet{r.queueTo(now, c, QueueOff)}
		}
		if c.room == nil {
			return nil // a retry after the LEAVE took effect: nothing to do
		}
		return r.leaveRoom(now, c, "leave")
	case Over:
		rm := c.room
		if rm == nil {
			return r.errorTo(c.addr, ErrRoomState, reqLen)
		}
		if rm.koth != nil {
			return r.stateTo(now, c) // a King of the Hill fight ends with RESULT
		}
		if rm.phase != PhasePlaying {
			return []Packet{r.roomTo(now, c)} // a late or repeated OVER: nothing to do
		}
		c.over = true
		if rm.all(func(o *client) bool { return o.over }) {
			r.toLobby(rm, "match over")
			return r.changed(now, rm)
		}
		return []Packet{r.roomTo(now, c)}
	}
	return nil
}

// alreadyInRoom answers CREATE or QUICK from a client that is in a room with
// a fresh ROOM about that room and changes nothing: requests are idempotent,
// and this is a retry whose first ROOM was lost.
func (r *Relay) alreadyInRoom(now time.Time, c *client) []Packet {
	return r.stateTo(now, c)
}

// list answers LIST with the public rooms of the client's build, newest
// first, at most MaxListed and as many as fit in one datagram.
func (r *Relay) list(c *client, m List) []Packet {
	var rooms []*room
	for _, rm := range r.rooms {
		if rm.koth != nil && c.caps&CapKoth == 0 {
			continue // a client without King of the Hill never sees such a room
		}
		if rm.settings.Public == 1 && rm.build == c.build && modeMatches(m.Mode, rm.settings.Mode) {
			rooms = append(rooms, rm)
		}
	}
	sort.Slice(rooms, func(i, j int) bool { return rooms[i].seq > rooms[j].seq })
	var res Rooms
	size := HeaderLen + 1
	for _, rm := range rooms {
		entry := RoomCodeLen + 1 + len(rm.settings.Name) + 4
		if len(res.Rooms) == MaxListed || size+entry > MaxDatagram {
			break
		}
		size += entry
		res.Rooms = append(res.Rooms, RoomInfo{
			Room: rm.code, Name: rm.settings.Name, Mode: rm.settings.Mode,
			Players: uint8(rm.count()), Max: rm.settings.Max, Phase: rm.phase,
		})
	}
	return []Packet{{To: c.addr, Data: mustEncode(res)}}
}

// who answers WHO: how many are connected, the rooms, and the lobby players of the asker's
// build (those in the hub first, then in rooms, then playing; by name), as many as fit.
func (r *Relay) who(c *client) []Packet {
	res := Players{Online: uint16(min(len(r.clients), 0xFFFF)), Rooms: uint16(min(len(r.rooms), 0xFFFF))}
	var list []PlayerInfo
	for _, o := range r.clients {
		if !o.lobby || o.build != c.build || o.name == "" {
			continue
		}
		state := PlayerLobby
		if o.room != nil {
			state = PlayerRoom
			if o.room.phase == PhasePlaying && (o.room.koth == nil || o.room.koth.fighting(o)) {
				state = PlayerPlaying
			}
		}
		list = append(list, PlayerInfo{Name: o.name, State: state})
	}
	sort.Slice(list, func(i, j int) bool {
		if list[i].State != list[j].State {
			return list[i].State < list[j].State
		}
		return list[i].Name < list[j].Name
	})
	size := HeaderLen + 5
	for _, p := range list {
		entry := 1 + len(p.Name) + 1
		if len(res.List) == MaxWho || size+entry > MaxDatagram {
			break
		}
		size += entry
		res.List = append(res.List, p)
	}
	return []Packet{{To: c.addr, Data: mustEncode(res)}}
}

// create opens a room with settings s and c as its host.
func (r *Relay) create(now time.Time, c *client, s RoomSettings, reqLen int) []Packet {
	if c.room != nil {
		return r.alreadyInRoom(now, c)
	}
	if !validName(s.Name) {
		return r.errorTo(c.addr, ErrBadName, reqLen)
	}
	if IsKoth(s.Mode) && c.caps&CapKoth == 0 {
		return r.errorTo(c.addr, ErrNotAllowed, reqLen)
	}
	if len(r.rooms) >= r.cfg.MaxRooms {
		return r.errorTo(c.addr, ErrServerFull, reqLen)
	}
	rm, err := r.newRoom(now, c.build, s, false)
	if err != nil {
		return nil
	}
	rm.place(c, SlotHost)
	r.cfg.Logf("room %s created by c=%s public=%d mode=%d max=%d", rm.code, c.id, s.Public, s.Mode, s.Max)
	return r.changed(now, rm)
}

// quick joins the public phase-0 quick room with that mode, the client's build and a
// free slot that has the most players (then the oldest), whatever its size, or
// creates one named "Quick: <player>" with quickPool (every venue), so the room list shows it
// as a quick match. Rooms players made themselves are never quick-joined.
func (r *Relay) quick(now time.Time, c *client, m Quick, reqLen int) []Packet {
	if c.room != nil {
		return r.alreadyInRoom(now, c)
	}
	if IsKoth(m.Mode) {
		return r.quickHill(now, c, m, reqLen)
	}
	var best *room
	for _, rm := range r.rooms {
		if !rm.quick || rm.legacy || rm.settings.Public != 1 || rm.phase != PhaseLobby || rm.build != c.build ||
			rm.settings.Mode != m.Mode {
			continue
		}
		if _, ok := rm.freeSlot(); !ok {
			continue
		}
		if best == nil || rm.count() > best.count() || (rm.count() == best.count() && rm.seq < best.seq) {
			best = rm
		}
	}
	if best != nil {
		slot, _ := best.freeSlot()
		best.place(c, slot)
		return r.joined(now, best, c)
	}
	out := r.create(now, c, RoomSettings{Name: quickName(c.name), Public: 1, Mode: m.Mode, Max: m.Max, Pool: quickPool(m.Mode)}, reqLen)
	if c.room != nil {
		c.room.quick = true
	}
	return out
}

// quickName is a quick-match room's name: "Quick: " and the player's name, cut to
// MaxNameLen bytes on a character boundary.
func quickName(player string) string {
	name := "Quick: " + player
	for len(name) > MaxNameLen {
		_, size := utf8.DecodeLastRuneInString(name)
		name = name[:len(name)-size]
	}
	return name
}

// renameQuick renames a quick-match room ("Quick: <player>", made by QUICK) after the player it
// is named after left or timed out: it becomes "Quick: <host>", so the room list never shows a
// player who is gone. The settings are otherwise unchanged and nobody's ready flag is cleared.
func (r *Relay) renameQuick(rm *room, gone *client) {
	if !rm.quick && rm.koth == nil {
		return
	}
	h := rm.host()
	if h == nil || rm.settings.Name != quickName(gone.name) {
		return
	}
	name := quickName(h.name)
	if name == rm.settings.Name || !validName(name) {
		return
	}
	rm.settings.Name = name
	r.cfg.Logf("room %s renamed (quick room of a player who left)", rm.code)
}

// setSettings replaces the room settings (the host only, in phase 0, not in a
// legacy room) and clears every ready flag. Identical settings change nothing.
func (r *Relay) setSettings(now time.Time, c *client, s RoomSettings, reqLen int) []Packet {
	rm := c.room
	if rm == nil {
		return r.errorTo(c.addr, ErrRoomState, reqLen)
	}
	if rm.host() != c || rm.phase != PhaseLobby || rm.legacy || rm.ranked {
		return r.errorTo(c.addr, ErrNotAllowed, reqLen)
	}
	if !validName(s.Name) {
		return r.errorTo(c.addr, ErrBadName, reqLen)
	}
	if IsKoth(s.Mode) != (rm.koth != nil) {
		return r.errorTo(c.addr, ErrNotAllowed, reqLen) // a room cannot become or stop being King of the Hill
	}
	for _, o := range rm.members() {
		if o.slot >= s.Max {
			return r.errorTo(c.addr, ErrNotAllowed, reqLen) // max below an occupied slot
		}
	}
	if rm.settings.equal(s) {
		return r.stateTo(now, c)
	}
	rm.settings = s.clone()
	for _, o := range rm.members() {
		o.ready = false
	}
	if rm.koth != nil {
		rm.koth.recall() // the called pair is called again under the new rules
	}
	r.cfg.Logf("room %s settings changed public=%d mode=%d max=%d", rm.code, s.Public, s.Mode, s.Max)
	return r.changed(now, rm)
}

// vote records a vote in phase 1 (ending the vote once everyone has voted).
// In phase 2 it re-sends the START, so a client whose START was lost recovers
// by voting again.
func (r *Relay) vote(now time.Time, c *client, choice uint8, reqLen int) []Packet {
	rm := c.room
	if rm == nil {
		return r.errorTo(c.addr, ErrRoomState, reqLen)
	}
	if rm.koth != nil {
		return r.hillVote(now, c, choice, reqLen)
	}
	switch rm.phase {
	case PhasePlaying:
		return []Packet{{To: c.addr, Data: rm.start}, r.roomTo(now, c)}
	case PhaseVoting:
		if int(choice) >= len(rm.candidates) {
			return r.errorTo(c.addr, ErrNotAllowed, reqLen)
		}
		if c.vote == choice {
			return []Packet{r.roomTo(now, c)}
		}
		c.vote = choice
		if rm.all(func(o *client) bool { return o.vote != VoteNone }) {
			return r.endVote(now, rm)
		}
		return r.broadcast(now, rm)
	}
	return r.errorTo(c.addr, ErrNotAllowed, reqLen)
}

// ---- randomness ----

// drawCandidates picks up to MaxCandidates different venues from pool at
// random (a partial Fisher-Yates shuffle of its distinct values).
func (r *Relay) drawCandidates(pool []uint8) []uint8 {
	if n := len(r.cfg.TestVenues); n > 0 {
		v := r.cfg.TestVenues[r.testVenue%n]
		r.testVenue++
		return []uint8{v}
	}
	var seen [256]bool
	var distinct []uint8
	safe := false
	for _, v := range pool {
		if !UnsafeVenues[v] {
			safe = true
		}
	}
	if !safe {
		pool = QuickPool
	}
	for _, v := range pool {
		if !seen[v] && !UnsafeVenues[v] {
			seen[v] = true
			distinct = append(distinct, v)
		}
	}
	k := min(MaxCandidates, len(distinct))
	for i := 0; i < k; i++ {
		j := i + r.intn(len(distinct)-i)
		distinct[i], distinct[j] = distinct[j], distinct[i]
	}
	return distinct[:k:k]
}

// intn is cfg.Intn guarded: 0 for n <= 1, and never out of range.
func (r *Relay) intn(n int) int {
	if n <= 1 {
		return 0
	}
	if v := r.cfg.Intn(n); v >= 0 && v < n {
		return v
	}
	return 0
}

// readerIntn makes an unbiased Intn from a random byte stream (rejection
// sampling over 32-bit draws).
func readerIntn(rd io.Reader) func(int) int {
	return func(n int) int {
		if n <= 1 {
			return 0
		}
		limit := (uint64(1) << 32) / uint64(n) * uint64(n)
		var b [4]byte
		for i := 0; i < 64; i++ {
			if _, err := io.ReadFull(rd, b[:]); err != nil {
				return 0
			}
			if v := uint64(binary.LittleEndian.Uint32(b[:])); v < limit {
				return int(v % uint64(n))
			}
		}
		return 0
	}
}
