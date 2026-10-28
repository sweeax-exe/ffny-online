package relay

import (
	"fmt"
	"net/netip"
	"reflect"
	"testing"
	"time"
)

var (
	pF = ap("198.51.100.15:1000")
	pG = ap("198.51.100.16:1000")
	pH = ap("198.51.100.17:1000")
	pI = ap("198.51.100.18:1000")
	pJ = ap("198.51.100.19:1000")
	pK = ap("198.51.100.20:1000")
)

// kenter enters the lobby and announces King of the Hill.
func (l *lobbyHarness) kenter(addr netip.AddrPort, name string) {
	l.t.Helper()
	l.enter(addr, name)
	res := l.send(addr, Caps{Token: l.tok[addr], Caps: CapKoth})
	if len(res) != 1 || res[0].to != addr || !msgEqual(res[0].msg, RelayCapsMsg{Caps: RelayCaps}) {
		l.t.Fatalf("CAPS answered %#v", res)
	}
}

// kroom makes a King of the Hill room (venue at random) hosted by addrs[0], with the others
// joined in order, so slot i is addrs[i] and the line is addrs in order.
func (l *lobbyHarness) kroom(s RoomSettings, addrs ...netip.AddrPort) RoomCode {
	l.t.Helper()
	for i, a := range addrs {
		l.kenter(a, fmt.Sprintf("k%d", i))
	}
	code := roomOf(l.t, l.mkRoom(addrs[0], s), addrs[0]).Room
	for _, a := range addrs[1:] {
		roomOf(l.t, l.joinCode(a, code), a)
	}
	return code
}

// hillOf returns the last HILL sent to addr in res.
func hillOf(t *testing.T, res []got, addr netip.AddrPort) Hill {
	t.Helper()
	var h Hill
	found := false
	for _, g := range res {
		if m, ok := g.msg.(Hill); ok && g.to == addr {
			h, found = m, true
		}
	}
	if !found {
		t.Fatalf("no HILL to %v in %#v", addr, res)
	}
	return h
}

func watchOf(t *testing.T, res []got, addr netip.AddrPort) Watch {
	t.Helper()
	for _, g := range res {
		if w, ok := g.msg.(Watch); ok && g.to == addr {
			return w
		}
	}
	t.Fatalf("no WATCH to %v in %#v", addr, res)
	return Watch{}
}

// line is the HILL line as slots and states.
func line(h Hill) (slots, states []uint8) {
	for _, e := range h.Line {
		slots = append(slots, e.Slot)
		states = append(states, e.State)
	}
	return
}

func wantLine(t *testing.T, h Hill, slots ...uint8) {
	t.Helper()
	got, _ := line(h)
	if !reflect.DeepEqual(got, slots) {
		t.Fatalf("line %v, want %v (hill %#v)", got, slots, h)
	}
}

func (l *lobbyHarness) result(addr netip.AddrPort, seed uint32, winner uint8) []got {
	return l.send(addr, Result{Token: l.tok[addr], Seed: seed, Winner: winner})
}

// fight readies the two called players and returns the START seed; with ModeKothRandom the
// fight starts at once.
func (l *lobbyHarness) fight(a, b netip.AddrPort) uint32 {
	l.t.Helper()
	l.setReady(a, 10)
	res := l.setReady(b, 11)
	sa, sb := startOf(l.t, res, a), startOf(l.t, res, b)
	if sa.Seed != sb.Seed {
		l.t.Fatalf("two STARTs %#v %#v", sa, sb)
	}
	return sa.Seed
}

// decide has both fighters report the same winner and returns the HILL that follows.
func (l *lobbyHarness) decide(seed uint32, a, b netip.AddrPort, winner uint8) Hill {
	l.t.Helper()
	hillOf(l.t, l.result(a, seed, winner), a)
	return hillOf(l.t, l.result(b, seed, winner), b)
}

// A client that never announced King of the Hill is kept out of it entirely; normal rooms are
// as before.
func TestKothNeedsCaps(t *testing.T) {
	l := newLobby(t, Config{})
	code := l.kroom(kothSettings(ModeKothRandom), pA)
	l.enter(pB, "old")

	expectError(t, l.joinCode(pB, code), pB, ErrNotAllowed)
	expectError(t, l.mkRoom(pB, kothSettings(ModeKoth)), pB, ErrNotAllowed)
	expectError(t, l.quickM(pB, ModeKoth, 4), pB, ErrNotAllowed)
	if rs := l.listM(pB, ModeAny); len(rs.Rooms) != 0 {
		t.Fatalf("an old client sees %#v", rs)
	}
	expectError(t, l.send(pC, Hello{Mode: ModeJoin, Room: code, Build: build, Name: "v1"}), pC, ErrNotAllowed)

	// A King of the Hill client sees it under either King of the Hill filter, and under "any".
	l.kenter(pD, "new")
	for _, f := range []uint8{ModeAny, ModeKoth, ModeKothRandom} {
		if rs := l.listM(pD, f); len(rs.Rooms) != 1 || rs.Rooms[0].Max != MaxKothSlots {
			t.Fatalf("filter %#x: %#v", f, rs)
		}
	}
	if rs := l.listM(pD, 1); len(rs.Rooms) != 0 {
		t.Fatalf("mode 1 lists %#v", rs)
	}

	// A normal room cannot become King of the Hill or have more than four.
	l.enter(pE, "e")
	roomOf(t, l.mkRoom(pE, settings4("plain", 0)), pE)
	expectError(t, l.setSet(pE, kothSettings(ModeKoth)), pE, ErrNotAllowed)
	big := settings4("plain", 0)
	big.Max = 5
	expectNone(t, l.setSet(pE, big)) // malformed for a normal mode, as before
	expectNone(t, l.quickM(pE, 1, 5))
	// ...and a King of the Hill room cannot stop being one.
	expectError(t, l.setSet(pA, settings4("plain", 1)), pA, ErrNotAllowed)
}

func TestKothTenPlayers(t *testing.T) {
	l := newLobby(t, Config{})
	addrs := []netip.AddrPort{pA, pB, pC, pD, pE, pF, pG, pH, pI, pJ}
	code := l.kroom(kothSettings(ModeKothRandom), addrs...)
	l.kenter(pK, "eleven")
	expectError(t, l.joinCode(pK, code), pK, ErrRoomFull)

	res := l.send(pA, Ready{Token: l.tok[pA], Ready: 0, Fighter: 3}) // a change: broadcast to all ten
	if countType(res, TypeRoom) != 10 || countType(res, TypeHill) != 10 {
		t.Fatalf("broadcast %d ROOM, %d HILL", countType(res, TypeRoom), countType(res, TypeHill))
	}
	st := roomOf(t, res, pJ)
	if len(st.Players) != 10 || st.You != 9 {
		t.Fatalf("ROOM %#v", st)
	}
	h := hillOf(t, res, pJ)
	wantLine(t, h, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9)
	if _, states := line(h); states[0] != LineCalled || states[1] != LineCalled || states[2] != LineWaiting {
		t.Fatalf("states %v", states)
	}
	if h.Phase != HillCalling || h.King != WinnerDraw || h.MsLeft == 0 {
		t.Fatalf("hill %#v", h)
	}
}

// The winner stays, the loser goes to the back and everyone moves up; a draw sends both back.
func TestKothRotation(t *testing.T) {
	l := newLobby(t, Config{})
	l.kroom(kothSettings(ModeKothRandom), pA, pB, pC, pD)

	// A (0) vs B (1); C and D watch.
	l.setReady(pA, 10)
	res := l.setReady(pB, 11)
	s := startOf(t, res, pA)
	if s.Mode != KothFightMode || s.Rounds != 2 || s.Venue != 1 || len(s.Players) != 2 ||
		s.Players[0] != (StartPlayer{0, 10}) || s.Players[1] != (StartPlayer{1, 11}) {
		t.Fatalf("START %#v", s)
	}
	if !msgEqual(startOf(t, res, pB), s) || countType(res, TypeStart) != 2 {
		t.Fatalf("STARTs %#v", res)
	}
	for _, w := range []netip.AddrPort{pC, pD} {
		if wt := watchOf(t, res, w); !msgEqual(Start(wt), s) {
			t.Fatalf("WATCH %#v, START %#v", wt, s)
		}
	}
	if countType(res, TypeWatch) != 2 {
		t.Fatalf("WATCHes %#v", res)
	}
	h := hillOf(t, res, pC)
	if h.Phase != HillFighting || h.Seed != s.Seed || roomOf(t, res, pC).Phase != PhasePlaying {
		t.Fatalf("hill %#v", h)
	}

	h = l.decide(s.Seed, pA, pB, 0) // A wins
	wantLine(t, h, 0, 2, 3, 1)
	if h.King != 0 || h.Streak != 1 || h.Phase != HillCalling || h.LastWinner != 0 || h.LastLoser != 1 ||
		h.LastHow != HowWin || h.Fights != 1 || h.Line[0].Wins != 1 {
		t.Fatalf("after A beat B %#v", h)
	}

	h = l.decide(l.fight(pA, pC), pA, pC, 2) // C beats the king
	wantLine(t, h, 2, 3, 1, 0)
	if h.King != 2 || h.Streak != 1 {
		t.Fatalf("after C beat A %#v", h)
	}

	h = l.decide(l.fight(pC, pD), pC, pD, 2) // C defends
	wantLine(t, h, 2, 1, 0, 3)
	if h.King != 2 || h.Streak != 2 {
		t.Fatalf("after C beat D %#v", h)
	}

	h = l.decide(l.fight(pC, pB), pC, pB, WinnerDraw) // a draw: both back, the challenger first
	wantLine(t, h, 0, 3, 1, 2)
	if h.King != WinnerDraw || h.Streak != 0 || h.LastHow != HowDraw || h.LastWinner != 2 || h.LastLoser != 1 {
		t.Fatalf("after the draw %#v", h)
	}
	if h.Phase != HillCalling {
		t.Fatalf("not calling A and D: %#v", h)
	}
	l.fight(pA, pD)
}

func TestKothSpectatorForwarding(t *testing.T) {
	l := newLobby(t, Config{})
	code := l.kroom(kothSettings(ModeKothRandom), pA, pB, pC, pD)

	// Between fights DATA goes to everyone (room voice).
	if res := l.send(pC, ClientData{Token: l.tok[pC], Payload: []byte("hi")}); len(res) != 3 {
		t.Fatalf("room DATA %#v", res)
	}
	seed := l.fight(pA, pB)
	l.kenter(pE, "late")
	roomOf(t, l.joinCode(pE, code), pE) // joins during the fight: no WATCH, at the back

	// A fighter's DATA: to the other fighter and the watchers, not to the late joiner.
	res := l.send(pA, ClientData{Token: l.tok[pA], Payload: []byte("steps")})
	seen := map[netip.AddrPort]bool{}
	for _, g := range res {
		if !msgEqual(g.msg, ServerData{From: 0, Payload: []byte("steps")}) {
			t.Fatalf("forwarded %#v", g.msg)
		}
		seen[g.to] = true
	}
	if len(res) != 3 || !seen[pB] || !seen[pC] || !seen[pD] {
		t.Fatalf("fighter DATA to %v", seen)
	}
	// A spectator's DATA never reaches a fighter.
	res = l.send(pC, ClientData{Token: l.tok[pC], Payload: []byte("voice")})
	seen = map[netip.AddrPort]bool{}
	for _, g := range res {
		seen[g.to] = true
	}
	if len(res) != 2 || !seen[pD] || !seen[pE] {
		t.Fatalf("spectator DATA to %v", seen)
	}

	// A lost START or WATCH comes back on VOTE; the late joiner has nothing to watch.
	if s := startOf(t, l.castVote(pB, 0), pB); s.Seed != seed {
		t.Fatalf("START again %#v", s)
	}
	if w := watchOf(t, l.castVote(pD, 0), pD); w.Seed != seed {
		t.Fatalf("WATCH again %#v", w)
	}
	if res := l.castVote(pE, 0); countType(res, TypeWatch) != 0 || countType(res, TypeHill) != 1 {
		t.Fatalf("late VOTE %#v", res)
	}
	// A spectator's RESULT, or one for another fight, changes nothing.
	if h := hillOf(t, l.result(pC, seed, 2), pC); h.Phase != HillFighting {
		t.Fatalf("spectator result %#v", h)
	}
	if h := hillOf(t, l.result(pA, seed+1, 0), pA); h.Phase != HillFighting {
		t.Fatalf("stale result %#v", h)
	}
	// The next fight is watched by the late joiner too.
	l.decide(seed, pA, pB, 1)
	l.setReady(pB, 10)
	res = l.setReady(pC, 11)
	watchOf(t, res, pE)
	watchOf(t, res, pA)
	if countType(res, TypeWatch) != 3 {
		t.Fatalf("WATCHes %#v", res)
	}
}

func TestKothLeaving(t *testing.T) {
	l := newLobby(t, Config{})
	l.kroom(kothSettings(ModeKothRandom), pA, pB, pC, pD)

	// A fighter who leaves mid-fight loses it.
	l.fight(pA, pB)
	h := hillOf(t, l.leave(pA), pB)
	wantLine(t, h, 1, 2, 3)
	if h.King != 1 || h.Streak != 1 || h.LastHow != HowLeft || h.LastWinner != 1 || h.LastLoser != 0 || h.Phase != HillCalling {
		t.Fatalf("after A left %#v", h)
	}
	// A called player who leaves is replaced by the next; the other stays ready.
	l.setReady(pB, 10)
	h = hillOf(t, l.leave(pC), pB)
	wantLine(t, h, 1, 3)
	if _, st := line(h); st[0] != LineReady || st[1] != LineCalled {
		t.Fatalf("after C left %v", st)
	}
	// Alone, the king waits.
	l.leave(pD)
	h = hillOf(t, l.send(pB, Over{Token: l.tok[pB]}), pB)
	if h.Phase != HillWaiting || h.King != 1 || h.Streak != 1 {
		t.Fatalf("alone %#v", h)
	}
	// The last one out closes the room.
	l.leave(pB)
	if l.r.Rooms() != 0 {
		t.Fatalf("%d rooms", l.r.Rooms())
	}
}

// A fighter who leaves during the vote is replaced; the fight never started, so nobody wins.
func TestKothLeaveDuringVote(t *testing.T) {
	l := newLobby(t, Config{})
	l.kroom(kothSettings(ModeKoth, 1, 6, 11), pA, pB, pC)
	l.setReady(pA, 10)
	if h := hillOf(t, l.setReady(pB, 11), pC); h.Phase != HillVoting {
		t.Fatalf("not voting %#v", h)
	}
	l.castVote(pA, 1)
	res := l.leave(pB)
	h := hillOf(t, res, pA)
	wantLine(t, h, 0, 2)
	if st := roomOf(t, res, pA); h.Phase != HillCalling || h.Fights != 0 || st.Phase != PhaseLobby ||
		len(st.Candidates) != 0 || st.Players[0].Voted != VoteNone {
		t.Fatalf("after B left the vote %#v %#v", h, st)
	}
	if _, st := line(h); st[0] != LineReady || st[1] != LineCalled {
		t.Fatalf("states %v", st)
	}
}

// A called player who is not ready in time goes to the back; twice in a row, it is away.
func TestKothAFK(t *testing.T) {
	l := newLobby(t, Config{KothReadyTime: 10 * time.Second, IdleTimeout: time.Hour})
	l.kroom(kothSettings(ModeKothRandom), pA, pB, pC)

	h := hillOf(t, l.setReady(pA, 10), pA)
	if _, st := line(h); st[0] != LineReady || st[1] != LineCalled {
		t.Fatalf("states %v", st)
	}
	if res := l.advance(9 * time.Second); hillOf(t, res, pA).Phase != HillCalling || countType(res, TypeRoom) != 0 {
		t.Fatalf("countdown %#v", res)
	}
	res := l.advance(time.Second) // B skipped; A stays called and ready, C is called
	h = hillOf(t, res, pA)
	wantLine(t, h, 0, 2, 1)
	if _, st := line(h); st[0] != LineReady || st[1] != LineCalled || h.MsLeft != 10000 {
		t.Fatalf("after the skip %#v", h)
	}
	h = hillOf(t, l.advance(10*time.Second), pA) // C skipped
	wantLine(t, h, 0, 1, 2)
	h = hillOf(t, l.advance(10*time.Second), pA) // B skipped a second time: away
	wantLine(t, h, 0, 2, 1)
	if _, st := line(h); st[2] != LineAway {
		t.Fatalf("B not away %v", st)
	}
	h = hillOf(t, l.advance(10*time.Second), pA) // C away too: nobody left to call
	if _, st := line(h); h.Phase != HillWaiting || st[0] != LineWaiting || st[1] != LineAway || st[2] != LineAway {
		t.Fatalf("waiting %#v", h)
	}
	if res := l.advance(30 * time.Second); len(res) != 0 {
		t.Fatalf("a waiting room ticks %#v", res)
	}
	// B comes back: called at once with A.
	h = hillOf(t, l.setReady(pB, 12), pB)
	wantLine(t, h, 0, 1, 2)
	if _, st := line(h); h.Phase != HillCalling || st[0] != LineCalled || st[1] != LineCalled || st[2] != LineAway {
		t.Fatalf("B back %#v", h)
	}
	l.fight(pA, pB)
}

// A king who does not ready in time loses the crown.
func TestKothKingSkipped(t *testing.T) {
	l := newLobby(t, Config{KothReadyTime: 10 * time.Second, IdleTimeout: time.Hour})
	l.kroom(kothSettings(ModeKothRandom), pA, pB, pC)
	l.decide(l.fight(pA, pB), pA, pB, 0)
	l.setReady(pC, 10)
	h := hillOf(t, l.advance(10*time.Second), pC)
	wantLine(t, h, 2, 1, 0)
	if h.King != WinnerDraw || h.Streak != 0 {
		t.Fatalf("king kept %#v", h)
	}
}

func TestKothResultRules(t *testing.T) {
	l := newLobby(t, Config{KothResultWait: 5 * time.Second, IdleTimeout: time.Hour})
	l.kroom(kothSettings(ModeKothRandom), pA, pB, pC)

	// Reports that disagree void the fight: both to the back.
	seed := l.fight(pA, pB)
	hillOf(t, l.result(pA, seed, 0), pA)
	hillOf(t, l.result(pA, seed, 0), pA) // a repeat changes nothing
	h := hillOf(t, l.result(pB, seed, 1), pB)
	wantLine(t, h, 2, 1, 0)
	if h.LastHow != HowVoid || h.King != WinnerDraw {
		t.Fatalf("void %#v", h)
	}
	// A single report decides after the wait; a winner who is not a fighter is refused.
	seed = l.fight(pC, pB)
	expectError(t, l.result(pC, seed, 0), pC, ErrNotAllowed)
	if h = hillOf(t, l.result(pC, seed, 2), pC); h.Phase != HillFighting {
		t.Fatalf("decided on one report %#v", h)
	}
	if res := l.advance(4 * time.Second); len(res) != 0 {
		t.Fatalf("early %#v", res)
	}
	h = hillOf(t, l.advance(time.Second), pA)
	wantLine(t, h, 2, 0, 1)
	if h.King != 2 || h.Streak != 1 || h.LastHow != HowWin {
		t.Fatalf("single report %#v", h)
	}
	// A late RESULT for the finished fight changes nothing.
	if h = hillOf(t, l.result(pB, seed, 1), pB); h.Fights != 2 || h.King != 2 {
		t.Fatalf("late result %#v", h)
	}
	// RESULT outside a King of the Hill room is refused.
	l.enter(pD, "d")
	expectError(t, l.result(pD, 1, 0), pD, ErrRoomState)
	roomOf(t, l.mkRoom(pD, settings4("plain", 0)), pD)
	expectError(t, l.result(pD, 1, 0), pD, ErrNotAllowed)
}

func TestKothPlayTimeout(t *testing.T) {
	l := newLobby(t, Config{PlayTimeout: time.Minute, IdleTimeout: time.Hour})
	l.kroom(kothSettings(ModeKothRandom), pA, pB)
	l.fight(pA, pB)
	h := hillOf(t, l.advance(time.Minute), pA)
	if h.LastHow != HowVoid || h.Phase != HillCalling {
		t.Fatalf("timeout %#v", h)
	}
	wantLine(t, h, 1, 0)
}

func TestKothVenueVote(t *testing.T) {
	l := newLobby(t, Config{VoteTime: 15 * time.Second, IdleTimeout: time.Hour})
	l.kroom(kothSettings(ModeKoth, 1, 6, 11), pA, pB, pC)
	l.setReady(pA, 10)
	res := l.setReady(pB, 11)
	h := hillOf(t, res, pC)
	if st := roomOf(t, res, pC); h.Phase != HillVoting || st.Phase != PhaseVoting || len(st.Candidates) != 3 || h.MsLeft != 15000 {
		t.Fatalf("vote %#v %#v", h, st)
	}
	expectError(t, l.castVote(pC, 0), pC, ErrNotAllowed) // spectators do not vote
	// A fighter cannot change its fighter once the vote runs; a spectator can.
	if st := roomOf(t, l.setReady(pA, 12), pA); st.Players[0].Fighter != 10 {
		t.Fatalf("fighter changed %#v", st)
	}
	if st := roomOf(t, l.setReady(pC, 13), pC); st.Players[2].Fighter != 13 {
		t.Fatalf("spectator fighter %#v", st)
	}
	l.castVote(pA, 2)
	res = l.castVote(pB, 2)
	if s := startOf(t, res, pA); s.Venue != 11 || s.Players[0].Fighter != 10 || s.Players[1].Fighter != 11 {
		t.Fatalf("START %#v", s)
	}
	seed := watchOf(t, res, pC).Seed

	// The vote ends after VoteTime with whatever votes there are.
	l.decide(seed, pA, pB, 0)
	l.setReady(pA, 10)
	l.setReady(pC, 13)
	l.castVote(pA, 1)
	if res := l.advance(14 * time.Second); countType(res, TypeStart) != 0 || countType(res, TypeHill) != 3 {
		t.Fatalf("vote countdown %#v", res)
	}
	res = l.advance(time.Second)
	if s := startOf(t, res, pC); s.Venue != 6 {
		t.Fatalf("START %#v", s)
	}
	watchOf(t, res, pB)
}

func TestKothQuick(t *testing.T) {
	l := newLobby(t, Config{})
	l.kenter(pA, "alice")
	st := roomOf(t, l.quickM(pA, ModeKothRandom, 8), pA)
	if st.Settings.Mode != ModeKothRandom || st.Settings.Max != 8 || st.Settings.Public != 1 {
		t.Fatalf("quick room %#v", st)
	}
	// A normal quick match never lands in it, and a King of the Hill one joins it in any phase.
	l.enter(pB, "bob")
	if st2 := roomOf(t, l.quickM(pB, 0, 2), pB); st2.Room == st.Room {
		t.Fatal("normal quick joined the hill")
	}
	l.kenter(pC, "carol")
	l.kenter(pD, "dave")
	roomOf(t, l.quickM(pC, ModeKoth, 4), pC)
	l.fight(pA, pC)
	if st3 := roomOf(t, l.quickM(pD, ModeKoth, 4), pD); st3.Room != st.Room || st3.Phase != PhasePlaying {
		t.Fatalf("quick during the fight %#v", st3)
	}
}

func (l *lobbyHarness) yield(addr netip.AddrPort) []got {
	return l.send(addr, Yield{Token: l.tok[addr]})
}

// YIELD ("give up my spot"): to the back of the line; a king gives up the crown; a called player
// is replaced by the next in the line; not while voting or fighting; the fighter changes for the
// next fight while waiting or watching.
func TestKothYield(t *testing.T) {
	l := newLobby(t, Config{IdleTimeout: time.Hour})
	l.kroom(kothSettings(ModeKothRandom), pA, pB, pC, pD)

	// A and B are called; C (waiting) gives up its spot: behind D.
	h := hillOf(t, l.yield(pC), pC)
	wantLine(t, h, 0, 1, 3, 2)
	if h.Phase != HillCalling {
		t.Fatalf("calling %#v", h)
	}
	// Already last: nothing changes (ROOM and HILL to C alone).
	res := l.yield(pC)
	if len(res) != 2 {
		t.Fatalf("last yields %#v", res)
	}
	roomOf(t, res, pC)
	wantLine(t, hillOf(t, res, pC), 0, 1, 3, 2)

	// B is called and ready, then gives up: D is called with A; B goes to the back, not ready.
	l.setReady(pB, 11)
	h = hillOf(t, l.yield(pB), pB)
	wantLine(t, h, 0, 3, 2, 1)
	if _, st := line(h); h.Phase != HillCalling || st[0] != LineCalled || st[1] != LineCalled || st[3] != LineWaiting {
		t.Fatalf("after B gave up %#v", h)
	}

	// A vs D. While they fight, neither may give up; C (watching) changes its fighter and gives up.
	seed := l.fight(pA, pD)
	expectError(t, l.yield(pA), pA, ErrNotAllowed)
	if st := roomOf(t, l.send(pC, Ready{Token: l.tok[pC], Ready: 0, Fighter: 22}), pC); st.Players[2].Fighter != 22 {
		t.Fatalf("watcher fighter %#v", st)
	}
	wantLine(t, hillOf(t, l.yield(pC), pC), 0, 3, 1, 2)
	h = l.decide(seed, pA, pD, 0) // A wins: A, then the waiting B and C, then D
	wantLine(t, h, 0, 1, 2, 3)
	if h.King != 0 || h.Streak != 1 {
		t.Fatalf("after the fight %#v", h)
	}

	// The king, called, gives up the hill: no king, B and C are called; C's new fighter fights.
	h = hillOf(t, l.yield(pA), pA)
	wantLine(t, h, 1, 2, 3, 0)
	if h.King != WinnerDraw || h.Streak != 0 || h.Phase != HillCalling {
		t.Fatalf("after the king gave up %#v", h)
	}
	l.setReady(pB, 11)
	s := startOf(t, l.send(pC, Ready{Token: l.tok[pC], Ready: 1, Fighter: 22}), pC)
	if s.Players[1] != (StartPlayer{2, 22}) {
		t.Fatalf("START %#v", s)
	}

	// Not in a King of the Hill room, or not in a room: refused.
	l.enter(pE, "e")
	expectError(t, l.yield(pE), pE, ErrRoomState)
	roomOf(t, l.mkRoom(pE, settings4("plain", 0)), pE)
	expectError(t, l.yield(pE), pE, ErrNotAllowed)
	if !containsLog(l.logs, "gave up its spot") {
		t.Errorf("logs %q", l.logs)
	}
}
