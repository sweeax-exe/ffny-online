// Command relayprobe is a small test client for the netplay relay. It speaks
// protocol version 1 with the lobby extension and is meant for checking a
// deployed server from a PC.
//
// Modes:
//
//	relayprobe -server relay.example.net:27777                 # pair (default): two clients in one process
//	relayprobe -server HOST:27777 -mode create                 # create a room, print its code, wait for a peer
//	relayprobe -server HOST:27777 -mode join -room ABCDEF      # join that room from another machine
//	relayprobe -server HOST:27777 -mode lobby                  # lobby flow: two clients, create, list, join, ready, vote, START
//	relayprobe -server HOST:27777 -mode status                 # three STATUS probes: round trip, version, players (no HELLO)
//	relayprobe -server HOST:27777 -mode roomlist               # one ROOMLIST: the public rooms of every build (no HELLO)
//
// In every mode it pings once a second and prints the round-trip time from
// PING/PONG, sends -n DATA packets once a peer is present, and prints every
// message that arrives. Pair mode checks the whole flow (create, join, PEER,
// DATA both ways, PING/PONG, BYE, peer-left, ERROR 7 for a dead token) and
// exits non-zero on any failure. Lobby mode checks the lobby extension the
// same way: HELLO mode 2 twice, a public CREATE, LIST, JOIN, READY from both,
// VOTE from both, START on both (same seed and venue), OVER back to the lobby.
package main

import (
	"encoding/binary"
	"encoding/hex"
	"errors"
	"flag"
	"fmt"
	"net"
	"os"
	"os/signal"
	"strings"
	"time"

	"defjam/netplay/relay"
)

func main() {
	server := flag.String("server", fmt.Sprintf("127.0.0.1:%d", relay.DefaultPort), "relay address host:port")
	mode := flag.String("mode", "pair", "pair | create | join | lobby | status | roomlist")
	roomFlag := flag.String("room", "", "room code to join (-mode join)")
	name := flag.String("name", "probe", "player name sent in HELLO (1..24 bytes)")
	buildHex := flag.String("build", "5052-4f42-4500-0000", "build fingerprint, 8 bytes as hex (dashes ignored)")
	n := flag.Int("n", 10, "number of DATA packets to send once a peer is present")
	interval := flag.Duration("interval", 100*time.Millisecond, "time between DATA packets")
	wait := flag.Duration("wait", 5*time.Minute, "create/join: how long to wait for a peer")
	timeout := flag.Duration("timeout", 5*time.Second, "time to wait for WELCOME and other replies")
	flag.Parse()

	var build relay.Build
	raw, err := hex.DecodeString(strings.ReplaceAll(*buildHex, "-", ""))
	if err != nil || len(raw) != relay.BuildLen {
		fatalf("-build must be %d bytes of hex", relay.BuildLen)
	}
	copy(build[:], raw)
	if *n < 0 || *n > 100000 {
		fatalf("-n out of range")
	}

	var ok bool
	switch *mode {
	case "pair":
		ok = runPair(*server, build, *name, *n, *interval, *timeout)
	case "lobby":
		ok = runLobby(*server, build, *name, *timeout)
	case "status":
		ok = runStatus(*server, build, *timeout)
	case "roomlist":
		any := relay.Build{} // every build, unless -build names one
		flag.Visit(func(f *flag.Flag) {
			if f.Name == "build" {
				any = build
			}
		})
		ok = runRoomList(*server, any, *timeout)
	case "create", "join":
		var code relay.RoomCode
		if *mode == "join" {
			code, err = relay.ParseRoomCode(strings.ToUpper(*roomFlag))
			if err != nil || !code.Valid() {
				fatalf("-room must be a 6-character code from %s", relay.RoomAlphabet)
			}
		}
		ok = runSingle(*server, *mode == "join", code, build, *name, *n, *interval, *wait, *timeout)
	default:
		fatalf("unknown -mode %q", *mode)
	}
	if !ok {
		os.Exit(1)
	}
}

func fatalf(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "relayprobe: "+format+"\n", args...)
	os.Exit(2)
}

// ---- one protocol client ----

type event struct {
	msg relay.Message
	at  time.Time
}

type client struct {
	label   string
	conn    *net.UDPConn
	events  chan event
	pending []event // events read while waiting for something else

	token relay.Token
	slot  uint8
	room  relay.RoomCode

	nonce uint32
	pings map[uint32]time.Time
	rtts  []time.Duration
}

func dial(server, label string) (*client, error) {
	raddr, err := net.ResolveUDPAddr("udp", server)
	if err != nil {
		return nil, err
	}
	conn, err := net.DialUDP("udp", nil, raddr)
	if err != nil {
		return nil, err
	}
	c := &client{label: label, conn: conn, events: make(chan event, 1024), pings: map[uint32]time.Time{}}
	go c.readLoop()
	return c, nil
}

func (c *client) readLoop() {
	buf := make([]byte, 2048)
	for {
		n, err := c.conn.Read(buf)
		if err != nil {
			if errors.Is(err, net.ErrClosed) {
				close(c.events)
				return
			}
			continue // e.g. ICMP port unreachable while the server is down
		}
		data := append([]byte(nil), buf[:n]...)
		msg, err := relay.DecodeServer(data)
		if err != nil {
			c.logf("malformed datagram from server (%d bytes)", n)
			continue
		}
		c.events <- event{msg: msg, at: time.Now()}
	}
}

func (c *client) logf(format string, args ...any) {
	fmt.Printf("%s [%s] %s\n", time.Now().Format("15:04:05.000"), c.label, fmt.Sprintf(format, args...))
}

func (c *client) send(m relay.Message) {
	b, err := relay.Encode(m)
	if err != nil {
		c.logf("encode: %v", err)
		return
	}
	_, _ = c.conn.Write(b)
}

func (c *client) ping() {
	c.nonce++
	c.pings[c.nonce] = time.Now()
	c.send(relay.Ping{Token: c.token, Nonce: c.nonce})
}

// next returns the next event, or false after d.
func (c *client) next(d time.Duration) (event, bool) {
	if len(c.pending) > 0 {
		e := c.pending[0]
		c.pending = c.pending[1:]
		return e, true
	}
	select {
	case e, ok := <-c.events:
		return e, ok
	case <-time.After(d):
		return event{}, false
	}
}

// hello sends HELLO every 500 ms until WELCOME or ERROR arrives.
func (c *client) hello(h relay.Hello, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		c.send(h)
		retry := time.Now().Add(500 * time.Millisecond)
		for time.Now().Before(retry) {
			e, ok := c.next(time.Until(retry))
			if !ok {
				break
			}
			switch m := e.msg.(type) {
			case relay.Welcome:
				c.token, c.slot, c.room = m.Token, m.Slot, m.Room
				if m.Room == (relay.RoomCode{}) {
					c.logf("WELCOME slot=%d (lobby)", m.Slot)
				} else {
					c.logf("WELCOME slot=%d room=%s", m.Slot, m.Room)
				}
				return nil
			case relay.Error:
				return fmt.Errorf("ERROR %d %q", m.Code, m.Text)
			default:
				c.pending = append(c.pending, e)
			}
		}
	}
	return errors.New("no WELCOME (server unreachable, or UDP blocked by a firewall?)")
}

// handle prints and accounts for one event. It returns the message for
// callers that need to react to it.
func (c *client) handle(e event) relay.Message {
	switch m := e.msg.(type) {
	case relay.Pong:
		if t, ok := c.pings[m.Nonce]; ok {
			rtt := e.at.Sub(t)
			delete(c.pings, m.Nonce)
			c.rtts = append(c.rtts, rtt)
			c.logf("PONG nonce=%d rtt=%s", m.Nonce, rtt.Round(10*time.Microsecond))
		} else {
			c.logf("PONG nonce=%d (unknown nonce)", m.Nonce)
		}
	case relay.Peer:
		if m.Name == "" {
			c.logf("PEER slot=%d left", m.Slot)
		} else {
			c.logf("PEER slot=%d name=%q", m.Slot, m.Name)
		}
	case relay.ServerData:
		extra := ""
		if seq, sent, ok := parseProbePayload(m.Payload); ok {
			extra = fmt.Sprintf(" seq=%d", seq)
			if !sent.IsZero() {
				// Only meaningful when sender and receiver share a clock (pair mode).
				extra += fmt.Sprintf(" via-relay=%s", e.at.Sub(sent).Round(10*time.Microsecond))
			}
		}
		c.logf("DATA from=%d %d bytes%s", m.From, len(m.Payload), extra)
	case relay.Error:
		c.logf("ERROR %d %q", m.Code, m.Text)
	case relay.Welcome:
		c.logf("WELCOME slot=%d room=%s (repeat)", m.Slot, m.Room)
	case relay.Rooms:
		c.logf("ROOMS count=%d", len(m.Rooms))
		for _, r := range m.Rooms {
			c.logf("  room=%s name=%q mode=%d players=%d/%d phase=%d", r.Room, r.Name, r.Mode, r.Players, r.Max, r.Phase)
		}
	case relay.RoomState:
		var players []string
		for _, p := range m.Players {
			players = append(players, fmt.Sprintf("%d:%q ready=%d fighter=%d voted=%d", p.Slot, p.Name, p.Ready, p.Fighter, p.Voted))
		}
		c.logf("ROOM %s you=%d host=%d phase=%d max=%d candidates=%v votes=%v ms_left=%d players=[%s]",
			m.Room, m.You, m.Host, m.Phase, m.Settings.Max, m.Candidates, m.Votes, m.MsLeft, strings.Join(players, ", "))
	case relay.Start:
		c.logf("START seed=%#08x mode=%d rounds=%d venue=%d players=%v", m.Seed, m.Mode, m.Rounds, m.Venue, m.Players)
	}
	return e.msg
}

// request sends m (unless nil) every 500 ms until a message arrives for which
// accept returns true, or until timeout. Every message is printed.
func (c *client) request(m relay.Message, timeout time.Duration, accept func(relay.Message) bool) (relay.Message, bool) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if m != nil {
			c.send(m)
		}
		retry := time.Now().Add(500 * time.Millisecond)
		for time.Now().Before(retry) {
			e, ok := c.next(time.Until(retry))
			if !ok {
				break
			}
			if got := c.handle(e); accept(got) {
				return got, true
			}
		}
	}
	return nil, false
}

func (c *client) rttSummary() string {
	if len(c.rtts) == 0 {
		return "no PONG received"
	}
	minR, maxR, sum := c.rtts[0], c.rtts[0], time.Duration(0)
	for _, r := range c.rtts {
		minR, maxR, sum = min(minR, r), max(maxR, r), sum+r
	}
	avg := sum / time.Duration(len(c.rtts))
	return fmt.Sprintf("rtt min/avg/max = %s/%s/%s over %d pongs (%d pings unanswered)",
		minR.Round(10*time.Microsecond), avg.Round(10*time.Microsecond), maxR.Round(10*time.Microsecond),
		len(c.rtts), len(c.pings))
}

// Probe payload: "DJPROBE" + seq u32 + send time (unix ns, i64), little-endian.
const probeTag = "DJPROBE"

func probePayload(seq uint32) []byte {
	b := append([]byte(probeTag), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
	binary.LittleEndian.PutUint32(b[len(probeTag):], seq)
	binary.LittleEndian.PutUint64(b[len(probeTag)+4:], uint64(time.Now().UnixNano()))
	return b
}

func parseProbePayload(p []byte) (uint32, time.Time, bool) {
	if len(p) != len(probeTag)+12 || string(p[:len(probeTag)]) != probeTag {
		return 0, time.Time{}, false
	}
	seq := binary.LittleEndian.Uint32(p[len(probeTag):])
	ns := int64(binary.LittleEndian.Uint64(p[len(probeTag)+4:]))
	return seq, time.Unix(0, ns), true
}

// ---- create / join: one client, a real peer elsewhere ----

func runSingle(server string, join bool, code relay.RoomCode, build relay.Build, name string,
	n int, interval, wait, timeout time.Duration) bool {
	c, err := dial(server, name)
	if err != nil {
		fmt.Fprintln(os.Stderr, "relayprobe:", err)
		return false
	}
	defer c.conn.Close()

	h := relay.Hello{Mode: relay.ModeCreate, Build: build, Name: name}
	if join {
		h.Mode, h.Room = relay.ModeJoin, code
	}
	if err := c.hello(h, timeout); err != nil {
		c.logf("HELLO failed: %v", err)
		return false
	}
	if !join {
		c.logf("room code: %s (join with: relayprobe -server %s -mode join -room %s)", c.room, server, c.room)
	}

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt)
	pingT := time.NewTicker(time.Second)
	defer pingT.Stop()
	c.ping()

	var dataC <-chan time.Time
	sent, got := 0, 0
	peerDeadline := time.Now().Add(wait)
	var finishAt time.Time
	haveFinish := false
	housekeeping := time.NewTicker(100 * time.Millisecond)
	defer housekeeping.Stop()
	startData := func() {
		if dataC == nil && n > 0 {
			t := time.NewTicker(interval)
			dataC = t.C
		} else if n == 0 && !haveFinish {
			finishAt, haveFinish = time.Now().Add(3*time.Second), true
		}
	}
	for _, e := range c.pending {
		if p, ok := c.handle(e).(relay.Peer); ok && p.Name != "" {
			startData()
		}
	}
	c.pending = nil

	result := true
loop:
	for {
		select {
		case e, ok := <-c.events:
			if !ok {
				break loop
			}
			switch m := c.handle(e).(type) {
			case relay.Peer:
				if m.Name != "" {
					startData()
				} else {
					break loop
				}
			case relay.ServerData:
				got++
			case relay.Error:
				if m.Code == relay.ErrUnknownToken {
					result = false
					break loop
				}
			}
		case <-pingT.C:
			c.ping()
		case <-dataC:
			if sent < n {
				sent++
				c.send(relay.ClientData{Token: c.token, Payload: probePayload(uint32(sent))})
			}
			if sent >= n && !haveFinish {
				dataC = nil
				finishAt, haveFinish = time.Now().Add(3*time.Second), true
			}
		case <-housekeeping.C:
			if dataC == nil && !haveFinish && time.Now().After(peerDeadline) {
				c.logf("no peer within %s", wait)
				result = false
				break loop
			}
			if haveFinish && time.Now().After(finishAt) {
				break loop
			}
		case <-sig:
			c.logf("interrupted")
			break loop
		}
	}
	c.send(relay.Bye{Token: c.token})
	c.logf("BYE sent; DATA sent=%d received=%d; %s", sent, got, c.rttSummary())
	return result
}

// ---- pair: two clients in this process, full self-check ----

func runPair(server string, build relay.Build, name string, n int, interval, timeout time.Duration) bool {
	failures := 0
	check := func(cond bool, format string, args ...any) {
		status := "ok  "
		if !cond {
			status = "FAIL"
			failures++
		}
		fmt.Printf("%s %s\n", status, fmt.Sprintf(format, args...))
	}

	a, err := dial(server, "A")
	if err != nil {
		fmt.Fprintln(os.Stderr, "relayprobe:", err)
		return false
	}
	defer a.conn.Close()
	b, err := dial(server, "B")
	if err != nil {
		fmt.Fprintln(os.Stderr, "relayprobe:", err)
		return false
	}
	defer b.conn.Close()

	if err := a.hello(relay.Hello{Mode: relay.ModeCreate, Build: build, Name: name + "-A"}, timeout); err != nil {
		check(false, "create room: %v", err)
		return false
	}
	check(a.slot == relay.SlotHost && a.room.Valid(), "create: slot=%d room=%s", a.slot, a.room)

	if err := b.hello(relay.Hello{Mode: relay.ModeJoin, Room: a.room, Build: build, Name: name + "-B"}, timeout); err != nil {
		check(false, "join room: %v", err)
		a.send(relay.Bye{Token: a.token})
		return false
	}
	check(b.slot == relay.SlotGuest && b.room == a.room, "join: slot=%d room=%s", b.slot, b.room)

	// Both sides must hear about each other.
	waitPeer := func(c *client, wantSlot uint8, wantName string) bool {
		deadline := time.Now().Add(timeout)
		for time.Now().Before(deadline) {
			e, ok := c.next(time.Until(deadline))
			if !ok {
				break
			}
			if p, isPeer := c.handle(e).(relay.Peer); isPeer {
				return p.Slot == wantSlot && p.Name == wantName
			}
		}
		return false
	}
	check(waitPeer(b, relay.SlotHost, name+"-A"), "B got PEER about A")
	check(waitPeer(a, relay.SlotGuest, name+"-B"), "A got PEER about B")

	// DATA both ways plus pings, for n*interval and a grace period.
	gotA, gotB := 0, 0
	badFrom := 0
	pingT := time.NewTicker(250 * time.Millisecond)
	defer pingT.Stop()
	dataT := time.NewTicker(interval)
	defer dataT.Stop()
	a.ping()
	b.ping()
	sent := 0
	aEv, bEv := a.events, b.events
	end := time.Now().Add(time.Duration(n)*interval + 2*time.Second)
	for time.Now().Before(end) && !(sent >= n && gotA >= n && gotB >= n && len(a.rtts) >= 3 && len(b.rtts) >= 3) {
		select {
		case e, ok := <-aEv:
			if !ok {
				aEv = nil // socket closed; stop selecting on it
				continue
			}
			if d, isData := a.handle(e).(relay.ServerData); isData {
				gotA++
				if d.From != relay.SlotGuest {
					badFrom++
				}
			}
		case e, ok := <-bEv:
			if !ok {
				bEv = nil
				continue
			}
			if d, isData := b.handle(e).(relay.ServerData); isData {
				gotB++
				if d.From != relay.SlotHost {
					badFrom++
				}
			}
		case <-dataT.C:
			if sent < n {
				sent++
				a.send(relay.ClientData{Token: a.token, Payload: probePayload(uint32(sent))})
				b.send(relay.ClientData{Token: b.token, Payload: probePayload(uint32(sent))})
			}
		case <-pingT.C:
			a.ping()
			b.ping()
		case <-time.After(100 * time.Millisecond):
		}
	}
	check(gotB == n, "A->B DATA: %d/%d delivered", gotB, n)
	check(gotA == n, "B->A DATA: %d/%d delivered", gotA, n)
	check(badFrom == 0, "DATA 'from' slots correct (%d wrong)", badFrom)
	check(len(a.rtts) > 0, "A %s", a.rttSummary())
	check(len(b.rtts) > 0, "B %s", b.rttSummary())

	// B leaves; A must be told with an empty-name PEER.
	b.send(relay.Bye{Token: b.token})
	check(waitPeer(a, relay.SlotGuest, ""), "A got peer-left after B's BYE")

	// B's token is dead now: a PING must be answered with ERROR 7.
	b.send(relay.Ping{Token: b.token, Nonce: 0xDEAD})
	got7 := false
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) && !got7 {
		e, ok := b.next(time.Until(deadline))
		if !ok {
			break
		}
		if m, isErr := b.handle(e).(relay.Error); isErr && m.Code == relay.ErrUnknownToken {
			got7 = true
		}
	}
	check(got7, "dead token answered with ERROR 7")

	a.send(relay.Bye{Token: a.token})
	if failures > 0 {
		fmt.Printf("FAIL: %d check(s) failed against %s\n", failures, server)
		return false
	}
	fmt.Printf("PASS: relay at %s works (room %s)\n", server, a.room)
	return true
}

// ---- lobby: two clients through the lobby extension ----

func isRoom(m relay.Message) bool { _, ok := m.(relay.RoomState); return ok }

func isStart(m relay.Message) bool { _, ok := m.(relay.Start); return ok }

func runLobby(server string, build relay.Build, name string, timeout time.Duration) bool {
	failures := 0
	check := func(cond bool, format string, args ...any) bool {
		status := "ok  "
		if !cond {
			status = "FAIL"
			failures++
		}
		fmt.Printf("%s %s\n", status, fmt.Sprintf(format, args...))
		return cond
	}
	finish := func(clients ...*client) bool {
		for _, c := range clients {
			c.send(relay.Bye{Token: c.token})
		}
		if failures > 0 {
			fmt.Printf("FAIL: %d check(s) failed against %s (lobby)\n", failures, server)
			return false
		}
		fmt.Printf("PASS: lobby at %s works\n", server)
		return true
	}

	a, err := dial(server, "A")
	if err != nil {
		fmt.Fprintln(os.Stderr, "relayprobe:", err)
		return false
	}
	defer a.conn.Close()
	b, err := dial(server, "B")
	if err != nil {
		fmt.Fprintln(os.Stderr, "relayprobe:", err)
		return false
	}
	defer b.conn.Close()

	// 1. Both enter the lobby. A relay without the extension drops HELLO mode 2.
	for _, c := range []*client{a, b} {
		if err := c.hello(relay.Hello{Mode: relay.ModeLobby, Build: build, Name: name + "-" + c.label}, timeout); err != nil {
			check(false, "%s lobby HELLO: %v (a relay without the lobby extension drops HELLO mode 2)", c.label, err)
			return finish()
		}
		check(c.slot == relay.SlotNone && c.room == relay.RoomCode{}, "%s in lobby: slot=%#x", c.label, c.slot)
	}

	// 2. A creates a public room with a recognisable name.
	roomName := fmt.Sprintf("probe %04x", time.Now().UnixNano()&0xFFFF)
	settings := relay.RoomSettings{Name: roomName, Public: 1, Mode: 0, Max: 2, Rounds: 0, Pool: []uint8{1, 6, 19}}
	m, ok := a.request(relay.Create{Token: a.token, Settings: settings}, timeout, isRoom)
	if !check(ok, "A CREATE answered with ROOM") {
		return finish(a, b)
	}
	room := m.(relay.RoomState)
	check(room.You == 0 && room.Host == 0 && room.Room.Valid(), "A is host of room %s", room.Room)

	// 3. B finds it with LIST.
	_, ok = b.request(relay.List{Token: b.token, Mode: relay.ModeAny}, timeout, func(m relay.Message) bool {
		rs, isRooms := m.(relay.Rooms)
		if !isRooms {
			return false
		}
		for _, r := range rs.Rooms {
			if r.Room == room.Room && r.Name == roomName {
				return true
			}
		}
		return false
	})
	if !check(ok, "B LIST shows room %s", room.Room) {
		return finish(a, b)
	}

	// 4. B joins by code.
	m, ok = b.request(relay.Join{Token: b.token, Room: room.Room}, timeout, func(m relay.Message) bool {
		st, isRoom := m.(relay.RoomState)
		return isRoom && len(st.Players) == 2
	})
	if !check(ok, "B JOIN answered with ROOM of 2") {
		return finish(a, b)
	}
	check(m.(relay.RoomState).You == 1, "B is in slot 1")

	// 5. Both ready; the vote starts.
	inVote := func(m relay.Message) bool {
		st, isRoom := m.(relay.RoomState)
		return isRoom && st.Phase == relay.PhaseVoting
	}
	readyAck := func(m relay.Message) bool {
		st, isRoom := m.(relay.RoomState)
		if !isRoom {
			return false
		}
		for _, p := range st.Players {
			if p.Slot == st.You && p.Ready == 1 {
				return true
			}
		}
		return st.Phase == relay.PhaseVoting
	}
	_, okA := a.request(relay.Ready{Token: a.token, Ready: 1, Fighter: 1}, timeout, readyAck)
	m, okB := b.request(relay.Ready{Token: b.token, Ready: 1, Fighter: 2}, timeout, readyAck)
	check(okA && okB, "READY acknowledged on both")
	if !okB || !inVote(m) {
		m, okB = b.request(nil, timeout, inVote) // ROOM comes every second while voting
	}
	if !check(okB, "room entered voting") {
		return finish(a, b)
	}
	vote := m.(relay.RoomState)
	if !check(len(vote.Candidates) >= 1 && len(vote.Candidates) <= 3, "candidates %v", vote.Candidates) {
		return finish(a, b)
	}

	// 6. Both vote for candidate 0; START reaches both. A VOTE after the vote
	// ended is answered with the START again, so retrying is safe.
	_, ok = a.request(relay.Vote{Token: a.token, Choice: 0}, timeout, func(m relay.Message) bool {
		st, isRoom := m.(relay.RoomState)
		return isStart(m) || (isRoom && len(st.Votes) > 0 && st.Votes[0] >= 1)
	})
	check(ok, "A VOTE acknowledged")
	mb, okB := b.request(relay.Vote{Token: b.token, Choice: 0}, timeout, isStart)
	ma, okA := a.request(relay.Vote{Token: a.token, Choice: 0}, timeout, isStart)
	if !check(okA && okB, "START received on both") {
		return finish(a, b)
	}
	sa, sb := ma.(relay.Start), mb.(relay.Start)
	check(sa.Seed == sb.Seed && sa.Venue == sb.Venue && len(sa.Players) == 2,
		"same START on both (seed %#08x venue %d)", sa.Seed, sa.Venue)
	check(sa.Venue == vote.Candidates[0], "venue %d is the voted candidate %d", sa.Venue, vote.Candidates[0])

	// 7. OVER from both returns the room to the lobby.
	inLobby := func(m relay.Message) bool {
		st, isRoom := m.(relay.RoomState)
		return isRoom && st.Phase == relay.PhaseLobby
	}
	a.request(relay.Over{Token: a.token}, timeout, isRoom)
	_, okB = b.request(relay.Over{Token: b.token}, timeout, inLobby)
	_, okA = a.request(relay.Over{Token: a.token}, timeout, inLobby)
	check(okA && okB, "OVER from both: back to phase 0")

	return finish(a, b)
}

// runStatus sends three STATUS requests (no HELLO, nothing registered) and prints each answer's
// round trip and contents. A relay older than STATUS never answers.
func runStatus(server string, build relay.Build, timeout time.Duration) bool {
	addr, err := net.ResolveUDPAddr("udp", server)
	if err != nil {
		fmt.Printf("FAIL: resolve %s: %v\n", server, err)
		return false
	}
	conn, err := net.DialUDP("udp", nil, addr)
	if err != nil {
		fmt.Printf("FAIL: %v\n", err)
		return false
	}
	defer conn.Close()
	answered := 0
	buf := make([]byte, 2048)
	for i := 0; i < 3; i++ {
		nonce := uint32(time.Now().UnixNano()) ^ uint32(i)
		req, _ := relay.Encode(relay.Status{Nonce: nonce, Build: build})
		sent := time.Now()
		if _, err := conn.Write(req); err != nil {
			fmt.Printf("send: %v\n", err)
			continue
		}
		_ = conn.SetReadDeadline(sent.Add(timeout))
		for {
			n, err := conn.Read(buf)
			if err != nil {
				fmt.Printf("probe %d: no answer within %v (an older relay without STATUS, or unreachable)\n", i+1, timeout)
				break
			}
			m, err := relay.DecodeServer(buf[:n])
			st, ok := m.(relay.StatusReply)
			if err != nil || !ok || st.Nonce != nonce {
				continue
			}
			answered++
			fmt.Printf("probe %d: %d ms  protocol v%d  flags %#02x  online %d  rooms %d  waiting %d  idle %d  published build %d\n",
				i+1, time.Since(sent).Milliseconds(), st.Version, st.Flags, st.Online, st.Rooms, st.Waiting, st.Idle, st.Published)
			break
		}
		time.Sleep(300 * time.Millisecond)
	}
	if answered == 0 {
		fmt.Printf("FAIL: %s did not answer STATUS\n", server)
		return false
	}
	fmt.Printf("PASS: %s answers STATUS (%d of 3)\n", server, answered)
	return true
}

// runRoomList sends one ROOMLIST (padded to a full datagram, so the whole list fits) and prints
// the rooms. It registers nothing on the relay.
func runRoomList(server string, build relay.Build, timeout time.Duration) bool {
	addr, err := net.ResolveUDPAddr("udp", server)
	if err != nil {
		fmt.Printf("FAIL: resolve %s: %v\n", server, err)
		return false
	}
	conn, err := net.DialUDP("udp", nil, addr)
	if err != nil {
		fmt.Printf("FAIL: %v\n", err)
		return false
	}
	defer conn.Close()
	nonce := uint32(time.Now().UnixNano())
	req, _ := relay.Encode(relay.RoomList{Nonce: nonce, Build: build, Mode: relay.ModeAny, Caps: relay.CapKoth,
		Pad: relay.MaxDatagram - relay.HeaderLen - relay.RoomListBodyMin})
	sent := time.Now()
	if _, err := conn.Write(req); err != nil {
		fmt.Printf("FAIL: send: %v\n", err)
		return false
	}
	_ = conn.SetReadDeadline(sent.Add(timeout))
	buf := make([]byte, 2048)
	for {
		n, err := conn.Read(buf)
		if err != nil {
			fmt.Printf("FAIL: %s did not answer ROOMLIST within %v (a relay before build 31, or unreachable)\n", server, timeout)
			return false
		}
		m, err := relay.DecodeServer(buf[:n])
		rl, ok := m.(relay.RoomListReply)
		if err != nil || !ok || rl.Nonce != nonce {
			continue
		}
		fmt.Printf("%d ms  %d public rooms, %d listed (%d bytes for a %d-byte request)\n", time.Since(sent).Milliseconds(),
			rl.Total, len(rl.Rooms), n, len(req))
		for _, r := range rl.Rooms {
			fmt.Printf("  room=%s name=%q mode=%d players=%d/%d phase=%d age=%ds\n", r.Room, r.Name, r.Mode, r.Players, r.Max, r.Phase, r.Age)
		}
		fmt.Printf("PASS: %s answers ROOMLIST\n", server)
		return true
	}
}
