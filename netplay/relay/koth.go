package relay

import (
	"encoding/binary"
	"io"
	"time"
)

// King of the Hill (docs/research/king-of-the-hill.md): a room of up to MaxKothSlots players
// where two fight One on One and the others wait in a line and watch. The winner stays on,
// the loser goes to the back of the line and everyone else moves up one place. A draw, or a
// fight whose two RESULTs disagree, sends both fighters to the back (the challenger first). A
// fighter who leaves during the fight loses it. The two players called next must READY within
// KothReadyTime or are skipped to the back; after KothAwayAfter skips in a row a player is
// away and not called until it sends READY 1 again.
//
// The room's ROOM phase follows the hill: waiting and calling are phase 0, voting is phase 1,
// fighting is phase 2. Only the two fighters get START; every other member present when the
// fight starts gets WATCH (the same body) and the fighters' DATA, one way. A member who joins
// during a fight watches from the next one.

// hill is the King of the Hill state of a room.
type hill struct {
	phase  uint8     // HillWaiting..HillFighting
	line   []*client // every member, in line order; line[0] is the king when there is one
	king   *client
	streak int

	pair     [2]*client // calling, voting, fighting: the two called, pair[0] ahead in the line
	deadline time.Time  // calling: the ready deadline; fighting: the wait for the second RESULT

	seed     uint32            // fighting: the START seed
	watchers []*client         // fighting: the members a WATCH went to
	watch    []byte            // fighting: the encoded WATCH, re-sent on a VOTE
	reports  map[*client]uint8 // fighting: each fighter's RESULT winner

	fights                         uint16 // fights finished
	lastWinner, lastLoser, lastHow uint8
}

func newHill() *hill { return &hill{lastWinner: WinnerDraw, lastLoser: WinnerDraw} }

// join puts a new member at the back of the line.
func (h *hill) join(c *client) {
	h.line = append(h.line, c)
	c.wins, c.skips, c.away = 0, 0, false
}

func (h *hill) inPair(c *client) bool { return c != nil && (h.pair[0] == c || h.pair[1] == c) }

// fighting reports whether c is one of the two in a running fight.
func (h *hill) fighting(c *client) bool { return h.phase == HillFighting && h.inPair(c) }

func (h *hill) other(c *client) *client {
	if h.pair[0] == c {
		return h.pair[1]
	}
	return h.pair[0]
}

func (h *hill) watching(c *client) bool {
	for _, w := range h.watchers {
		if w == c {
			return true
		}
	}
	return false
}

// without returns list without the given clients (a fresh slice).
func without(list []*client, drop ...*client) []*client {
	out := make([]*client, 0, len(list))
outer:
	for _, c := range list {
		for _, d := range drop {
			if c == d {
				continue outer
			}
		}
		out = append(out, c)
	}
	return out
}

// next is the pair to call: the first two in the line who are not away.
func (h *hill) next() (a, b *client) {
	for _, c := range h.line {
		if c.away {
			continue
		}
		if a == nil {
			a = c
		} else {
			return a, c
		}
	}
	return a, nil
}

// recall makes the next hillAdvance call the pair again with a fresh ready time.
func (h *hill) recall() {
	if h.phase == HillCalling {
		h.phase = HillWaiting
	}
}

// toBack moves c to the back of the line.
func (h *hill) toBack(c *client) { h.line = append(without(h.line, c), c) }

// ---- state changes (no packets; the callers send ROOM and HILL with changed) ----

// hillAdvance moves a waiting or calling hill on: it calls the next pair when it changed, and
// once both are ready starts the vote, or with ModeKothRandom (or a single venue) the fight. It
// returns the START and WATCH packets when a fight starts.
func (r *Relay) hillAdvance(now time.Time, rm *room) []Packet {
	h := rm.koth
	if h.phase == HillVoting || h.phase == HillFighting {
		return nil
	}
	rm.phase = PhaseLobby
	a, b := h.next()
	if b == nil {
		for _, c := range rm.members() {
			c.ready = false
		}
		h.phase, h.pair = HillWaiting, [2]*client{}
		return nil
	}
	if h.phase != HillCalling || h.pair != [2]*client{a, b} {
		for _, o := range h.pair {
			if o != nil && o != a && o != b {
				o.ready = false // no longer called
			}
		}
		h.phase, h.pair = HillCalling, [2]*client{a, b}
		h.deadline = now.Add(r.cfg.KothReadyTime)
		r.cfg.Logf("room %s koth: calling c=%s and c=%s", rm.code, a.id, b.id)
	}
	if !a.ready || !b.ready {
		return nil
	}
	candidates := r.drawCandidates(rm.settings.Pool)
	if rm.settings.Mode == ModeKothRandom || len(candidates) <= 1 {
		return r.startFight(now, rm, candidates[0])
	}
	rm.candidates = candidates
	for _, c := range rm.members() {
		c.vote = VoteNone
	}
	h.phase, rm.phase = HillVoting, PhaseVoting
	rm.voteEnd = now.Add(r.cfg.VoteTime)
	r.cfg.Logf("room %s koth: voting (%d candidates)", rm.code, len(candidates))
	return nil
}

// startFight sends START to the two fighters and WATCH to every other member, and enters the
// fight. The caller broadcasts the room afterwards.
func (r *Relay) startFight(now time.Time, rm *room, venue uint8) []Packet {
	h := rm.koth
	var seed [4]byte
	_, _ = io.ReadFull(r.cfg.Rand, seed[:]) // on failure the seed is 0; never happens with crypto/rand
	st := Start{Seed: binary.LittleEndian.Uint32(seed[:]), Mode: KothFightMode, Rounds: rm.settings.Rounds, Venue: venue}
	a, b := h.pair[0], h.pair[1]
	if b.slot < a.slot {
		a, b = b, a
	}
	st.Players = []StartPlayer{{Slot: a.slot, Fighter: a.fighter}, {Slot: b.slot, Fighter: b.fighter}}
	rm.start, h.watch = mustEncode(st), mustEncode(Watch(st))
	h.phase, rm.phase = HillFighting, PhasePlaying
	rm.playStart, h.seed, h.deadline = now, st.Seed, time.Time{}
	h.reports = map[*client]uint8{}
	h.watchers = nil

	var out []Packet
	for _, c := range rm.members() {
		if !c.lobby {
			continue
		}
		if h.inPair(c) {
			out = append(out, Packet{To: c.addr, Data: rm.start})
		} else {
			h.watchers = append(h.watchers, c)
			out = append(out, Packet{To: c.addr, Data: h.watch})
		}
	}
	r.cfg.Logf("room %s koth: fight venue=%d slots=%d,%d watchers=%d", rm.code, venue, a.slot, b.slot, len(h.watchers))
	return out
}

// hillFinish ends the fight (or a call that never became one): w beat l (l, in slot lSlot,
// may have left already), or with w nil a draw or a void fight. It rotates the line and
// clears the fight; the caller sends changed, which calls the next pair.
func (r *Relay) hillFinish(rm *room, w, l *client, lSlot uint8, how uint8) {
	h := rm.koth
	f0, f1 := h.pair[0], h.pair[1]
	rest := without(h.line, f0, f1)
	if w != nil {
		if h.king == w {
			h.streak++
		} else {
			h.king, h.streak = w, 1
		}
		w.wins++
		h.line = append([]*client{w}, rest...)
		if l != nil && l.room == rm {
			h.line = append(h.line, l)
		}
		h.lastWinner, h.lastLoser = w.slot, lSlot
	} else {
		h.line = rest
		h.lastWinner, h.lastLoser = WinnerDraw, WinnerDraw
		for i, f := range []*client{f0, f1} {
			if f != nil && f.room == rm {
				if i == 0 {
					h.lastWinner = f.slot
				} else {
					h.lastLoser = f.slot
				}
			}
		}
		for _, f := range []*client{f1, f0} { // both to the back, the challenger first
			if f != nil && f.room == rm {
				h.line = append(h.line, f)
			}
		}
		h.king, h.streak = nil, 0
	}
	for _, f := range []*client{f0, f1} {
		if f != nil {
			f.skips = 0
		}
	}
	h.fights++
	h.lastHow = how
	h.phase, h.pair = HillWaiting, [2]*client{}
	h.seed, h.watchers, h.watch, h.reports, h.deadline = 0, nil, nil, nil, time.Time{}
	rm.phase, rm.candidates, rm.start = PhaseLobby, nil, nil
	for _, c := range rm.members() {
		c.ready, c.vote = false, VoteNone
	}
	r.cfg.Logf("room %s koth: fight over how=%d winner=%d loser=%d streak=%d", rm.code, how, h.lastWinner, h.lastLoser, h.streak)
}

// hillDecide applies the agreed RESULT winner (a slot of the pair, or WinnerDraw).
func (r *Relay) hillDecide(rm *room, winner uint8) {
	h := rm.koth
	for _, f := range h.pair {
		if f != nil && f.slot == winner {
			l := h.other(f)
			lSlot := WinnerDraw
			if l != nil {
				lSlot = l.slot
			}
			r.hillFinish(rm, f, l, lSlot, HowWin)
			return
		}
	}
	r.hillFinish(rm, nil, nil, WinnerDraw, HowDraw)
}

// hillLeft takes c (which was in slot) out of the hill. A fighter who leaves during the fight
// loses it; one who leaves while called or voting is replaced by the next in the line.
func (r *Relay) hillLeft(now time.Time, rm *room, c *client, slot uint8) {
	h := rm.koth
	wasPair := h.inPair(c)
	h.line = without(h.line, c)
	h.watchers = without(h.watchers, c)
	delete(h.reports, c)
	if h.king == c {
		h.king, h.streak = nil, 0
	}
	if !wasPair {
		return
	}
	switch h.phase {
	case HillFighting:
		r.hillFinish(rm, h.other(c), c, slot, HowLeft)
	case HillVoting, HillCalling:
		if h.pair[0] == c {
			h.pair[0] = nil
		} else {
			h.pair[1] = nil
		}
		rm.candidates = nil
		for _, o := range rm.members() {
			o.vote = VoteNone
		}
		h.phase = HillWaiting // hillAdvance calls the next pair; the one still called keeps its ready
	}
}

// skip moves a called player who did not ready in time to the back of the line.
func (r *Relay) skip(rm *room, c *client) {
	h := rm.koth
	if h.king == c {
		h.king, h.streak = nil, 0
	}
	h.toBack(c)
	c.ready = false
	c.skips++
	if c.skips >= r.cfg.KothAwayAfter {
		c.away = true
	}
	r.cfg.Logf("room %s koth: c=%s skipped (not ready, %d in a row, away=%v)", rm.code, c.id, c.skips, c.away)
}

// ---- requests ----

// hillReady handles READY in a King of the Hill room. A called player's READY sets its ready
// flag; anyone's READY 1 ends being away; the fighter can change except while the sender is
// in the vote or the fight.
func (r *Relay) hillReady(now time.Time, c *client, m Ready) []Packet {
	rm := c.room
	h := rm.koth
	changed := false
	if m.Ready == 1 && (c.away || c.skips > 0) {
		c.away, c.skips = false, 0
		changed = true
	}
	locked := (h.phase == HillVoting || h.phase == HillFighting) && h.inPair(c)
	if f := safeFighter(m.Fighter); !locked && c.fighter != f {
		c.fighter = f
		changed = true
	}
	if h.phase == HillCalling && h.inPair(c) && c.ready != (m.Ready == 1) {
		c.ready = m.Ready == 1
		changed = true
	}
	if !changed {
		return r.stateTo(now, c)
	}
	return r.changed(now, rm)
}

// hillYield handles YIELD: the sender gives up its place and goes to the back of the line (a
// king gives up the crown; a called player is replaced by the next in the line). Not while it
// votes or fights (ERROR 8). Already last (and not called): nothing changes.
func (r *Relay) hillYield(now time.Time, c *client, reqLen int) []Packet {
	rm := c.room
	h := rm.koth
	if (h.phase == HillVoting || h.phase == HillFighting) && h.inPair(c) {
		return r.errorTo(c.addr, ErrNotAllowed, reqLen)
	}
	called := h.phase == HillCalling && h.inPair(c)
	last := len(h.line) > 0 && h.line[len(h.line)-1] == c
	if last && !called && h.king != c {
		return r.stateTo(now, c)
	}
	if h.king == c {
		h.king, h.streak = nil, 0
	}
	h.toBack(c)
	c.ready = false
	if called {
		h.recall() // hillAdvance calls the next pair, with a fresh ready time
	}
	r.cfg.Logf("room %s koth: c=%s gave up its spot (called=%v)", rm.code, c.id, called)
	return r.changed(now, rm)
}

// hillVote handles VOTE: the two fighters vote on the venue; during the fight a VOTE re-sends
// the sender's START or WATCH (a lost one is recovered by voting again, as in other rooms).
func (r *Relay) hillVote(now time.Time, c *client, choice uint8, reqLen int) []Packet {
	rm := c.room
	h := rm.koth
	switch h.phase {
	case HillFighting:
		switch {
		case h.inPair(c):
			return append([]Packet{{To: c.addr, Data: rm.start}}, r.stateTo(now, c)...)
		case h.watching(c):
			return append([]Packet{{To: c.addr, Data: h.watch}}, r.stateTo(now, c)...)
		}
		return r.stateTo(now, c) // joined during the fight: it watches the next one
	case HillVoting:
		if !h.inPair(c) || int(choice) >= len(rm.candidates) {
			return r.errorTo(c.addr, ErrNotAllowed, reqLen)
		}
		if c.vote == choice {
			return r.stateTo(now, c)
		}
		c.vote = choice
		if h.pair[0].vote != VoteNone && h.pair[1].vote != VoteNone {
			out := r.startFight(now, rm, r.votedVenue(rm))
			return append(out, r.broadcast(now, rm)...)
		}
		return r.broadcast(now, rm)
	}
	return r.errorTo(c.addr, ErrNotAllowed, reqLen)
}

// hillResult handles a fighter's RESULT. Two that agree decide the fight; two that disagree
// void it (both to the back); a single one decides it after KothResultWait. A RESULT for
// another fight, or from a spectator, changes nothing.
func (r *Relay) hillResult(now time.Time, c *client, m Result) []Packet {
	rm := c.room
	h := rm.koth
	if h.phase != HillFighting || m.Seed != h.seed || !h.inPair(c) {
		return r.stateTo(now, c)
	}
	if m.Winner != WinnerDraw && m.Winner != h.pair[0].slot && m.Winner != h.pair[1].slot {
		return r.errorTo(c.addr, ErrNotAllowed, 0)
	}
	if prev, ok := h.reports[c]; ok && prev == m.Winner {
		return r.stateTo(now, c)
	}
	h.reports[c] = m.Winner
	if len(h.reports) < 2 {
		if h.deadline.IsZero() {
			h.deadline = now.Add(r.cfg.KothResultWait)
		}
		return r.stateTo(now, c)
	}
	if a, b := h.reports[h.pair[0]], h.reports[h.pair[1]]; a == b {
		r.hillDecide(rm, a)
	} else {
		r.hillFinish(rm, nil, nil, WinnerDraw, HowVoid)
	}
	return r.changed(now, rm)
}

// quickHill joins the public King of the Hill room of the client's build with a free slot and
// the most players (then the oldest), in any phase and whatever its venue choice, or creates one
// with the asked venue choice (m.Mode).
func (r *Relay) quickHill(now time.Time, c *client, m Quick, reqLen int) []Packet {
	if c.caps&CapKoth == 0 {
		return r.errorTo(c.addr, ErrNotAllowed, reqLen)
	}
	var best *room
	for _, rm := range r.rooms {
		if rm.koth == nil || rm.settings.Public != 1 || rm.build != c.build {
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
	return r.create(now, c, RoomSettings{Name: quickName(c.name), Public: 1, Mode: m.Mode, Max: m.Max, Pool: quickPool(m.Mode)}, reqLen)
}

// ---- timers and forwarding ----

// tickHill runs a King of the Hill room's timers: the ready time (then the skip), the vote,
// the wait for the second RESULT and the play timeout. While calling or voting HILL goes out
// every tick for the countdown.
func (r *Relay) tickHill(now time.Time, rm *room) []Packet {
	h := rm.koth
	switch h.phase {
	case HillCalling:
		if now.Before(h.deadline) {
			return r.hillBroadcast(now, rm)
		}
		for _, p := range h.pair {
			if p != nil && !p.ready {
				r.skip(rm, p)
			}
		}
		h.phase = HillWaiting // a fresh call, with a fresh ready time
		return r.changed(now, rm)
	case HillVoting:
		if now.Before(rm.voteEnd) {
			return r.hillBroadcast(now, rm)
		}
		out := r.startFight(now, rm, r.votedVenue(rm))
		return append(out, r.broadcast(now, rm)...)
	case HillFighting:
		if len(h.reports) == 1 && !now.Before(h.deadline) {
			for _, w := range h.reports {
				r.hillDecide(rm, w)
			}
			return r.changed(now, rm)
		}
		if now.Sub(rm.playStart) >= r.cfg.PlayTimeout {
			r.hillFinish(rm, nil, nil, WinnerDraw, HowVoid)
			return r.changed(now, rm)
		}
	}
	return nil
}

// hillBroadcast sends HILL alone to every lobby member (the countdowns).
func (r *Relay) hillBroadcast(now time.Time, rm *room) []Packet {
	data := mustEncode(r.hillState(now, rm))
	var out []Packet
	for _, c := range rm.members() {
		if c.lobby {
			out = append(out, Packet{To: c.addr, Data: data})
		}
	}
	return out
}

// hillState is the HILL message of rm.
func (r *Relay) hillState(now time.Time, rm *room) Hill {
	h := rm.koth
	st := Hill{Room: rm.code, Phase: h.phase, King: WinnerDraw, Streak: uint16(min(h.streak, 0xFFFF)),
		Seed: h.seed, Fights: h.fights, LastWinner: h.lastWinner, LastLoser: h.lastLoser, LastHow: h.lastHow}
	if h.king != nil {
		st.King = h.king.slot
	}
	var end time.Time
	switch h.phase {
	case HillCalling:
		end = h.deadline
	case HillVoting:
		end = rm.voteEnd
	}
	if !end.IsZero() {
		st.MsLeft = uint16(max(0, min(end.Sub(now).Milliseconds(), 0xFFFF)))
	}
	for _, c := range h.line {
		e := HillEntry{Slot: c.slot, State: LineWaiting, Wins: uint16(min(c.wins, 0xFFFF))}
		switch {
		case h.inPair(c) && (h.phase == HillVoting || h.phase == HillFighting):
			e.State = LineFighting
		case h.inPair(c) && c.ready:
			e.State = LineReady
		case h.inPair(c):
			e.State = LineCalled
		case c.away:
			e.State = LineAway
		}
		st.Line = append(st.Line, e)
	}
	return st
}

// hillRecipients are the members a DATA from c goes to. During a fight a fighter's DATA goes to
// the other fighter and the watchers (one way: the fighters never get a spectator's DATA, so
// nothing a spectator sends can reach the lockstep), and a spectator's DATA goes to the other
// spectators only. Between fights it goes to every other member, as in other rooms.
func (r *Relay) hillRecipients(c *client) []*client {
	rm := c.room
	h := rm.koth
	var out []*client
	switch {
	case h.phase != HillFighting:
		out = without(rm.members(), c)
	case h.inPair(c):
		if o := h.other(c); o != nil && o.room == rm {
			out = append(out, o)
		}
		out = append(out, without(h.watchers, c)...)
	default:
		out = without(rm.members(), c, h.pair[0], h.pair[1])
	}
	return out
}
