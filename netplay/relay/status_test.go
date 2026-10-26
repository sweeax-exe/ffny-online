package relay

import (
	"bytes"
	"encoding/binary"
	"testing"
	"time"
)

func TestStatusEncoding(t *testing.T) {
	req, err := Encode(Status{Nonce: 0x01020304, Build: build})
	if err != nil {
		t.Fatal(err)
	}
	want := cat(hdr(TypeStatus), []byte{4, 3, 2, 1}, build[:], make([]byte, StatusReserved))
	if !bytes.Equal(req, want) {
		t.Fatalf("STATUS request % x, want % x", req, want)
	}
	rep, err := Encode(StatusReply{Nonce: 7, Version: Version, Flags: StatusKoth, Online: 12, Rooms: 3, Waiting: 1, Idle: 4, Published: 29})
	if err != nil {
		t.Fatal(err)
	}
	if len(rep) > len(req) {
		t.Fatalf("the answer (%d bytes) is larger than the request (%d)", len(rep), len(req))
	}
	m, err := DecodeServer(rep)
	if err != nil {
		t.Fatal(err)
	}
	if got := m.(StatusReply); got.Online != 12 || got.Rooms != 3 || got.Waiting != 1 || got.Idle != 4 || got.Published != 29 || got.Nonce != 7 {
		t.Fatalf("round trip: %#v", got)
	}
	c, err := DecodeClient(req)
	if err != nil || c.(Status).Nonce != 0x01020304 || c.(Status).Build != build {
		t.Fatalf("client round trip: %#v %v", c, err)
	}
	// malformed: short, long, reserved bytes set; the server's answer is not a client message
	bad := [][]byte{
		req[:len(req)-1],
		append(append([]byte(nil), req...), 0),
		append(append([]byte(nil), req[:len(req)-1]...), 1),
		rep,
	}
	for _, b := range bad {
		if _, err := DecodeClient(b); err == nil {
			t.Errorf("DecodeClient accepted % x", b)
		}
	}
	if _, err := DecodeServer(rep[:len(rep)-1]); err == nil {
		t.Error("DecodeServer accepted a short STATUS answer")
	}
}

func status(t *testing.T, h *harness, from string, b Build) (StatusReply, bool) {
	t.Helper()
	res := h.send(ap(from), Status{Nonce: 99, Build: b})
	if len(res) == 0 {
		return StatusReply{}, false
	}
	if len(res) != 1 || res[0].to != ap(from) {
		t.Fatalf("STATUS: %#v", res)
	}
	s, ok := res[0].msg.(StatusReply)
	if !ok || s.Nonce != 99 || s.Version != Version {
		t.Fatalf("STATUS answer %#v", res[0].msg)
	}
	return s, true
}

func TestStatusCounts(t *testing.T) {
	h := newHarness(t, Config{PublishedBuild: func() uint32 { return 30 }})
	s, ok := status(t, h, "10.0.0.1:5000", build)
	if !ok || s.Online != 0 || s.Rooms != 0 || s.Waiting != 0 || s.Idle != 0 || s.Published != 30 || s.Flags&StatusKoth == 0 ||
		s.Flags&StatusRanked != 0 {
		t.Fatalf("empty relay: %#v", s)
	}
	if h.r.Clients() != 0 {
		t.Fatal("STATUS registered a client")
	}
	// two lobby players; one quick-matches into a new public room and waits there
	a, b := ap("10.0.1.1:1000"), ap("10.0.1.2:1000")
	wa := h.send(a, Hello{Mode: ModeLobby, Build: build, Name: "A", BuildNo: 30})[0].msg.(Welcome)
	h.send(b, Hello{Mode: ModeLobby, Build: build, Name: "B", BuildNo: 30})
	h.send(a, Quick{Token: wa.Token, Mode: 0, Max: 2})
	// a code-only legacy room and a player of another build count as online only
	h.send(ap("10.0.1.3:1000"), Hello{Mode: ModeCreate, Build: build, Name: "legacy", BuildNo: 30})
	other := Build{1}
	h.send(ap("10.0.1.4:1000"), Hello{Mode: ModeLobby, Build: other, Name: "D", BuildNo: 30})

	s, _ = status(t, h, "10.0.0.1:5001", build)
	if s.Online != 4 || s.Rooms != 2 || s.Waiting != 1 || s.Idle != 1 {
		t.Fatalf("counts for this build: %#v", s)
	}
	s, _ = status(t, h, "10.0.0.2:5001", other)
	if s.Waiting != 0 || s.Idle != 1 {
		t.Fatalf("counts for the other build: %#v", s)
	}
	s, _ = status(t, h, "10.0.0.3:5001", Build{})
	if s.Waiting != 1 || s.Idle != 2 {
		t.Fatalf("counts for any build: %#v", s)
	}
	// the room filled: nobody waits any more
	wb := h.r.clients[b].token
	h.send(b, Quick{Token: wb, Mode: 0, Max: 2})
	s, _ = status(t, h, "10.0.0.4:5001", build)
	if s.Waiting != 0 || s.Idle != 0 {
		t.Fatalf("after the room filled: %#v", s)
	}
	if got := h.r.TakeStats().Status; got != 5 {
		t.Fatalf("stats counted %d STATUS answers, want 5", got)
	}
}

func TestStatusRateLimit(t *testing.T) {
	h := newHarness(t, Config{StatusPerSecond: 3, Ranked: true})
	for i := 0; i < 3; i++ {
		s, ok := status(t, h, "10.0.0.1:5000", build)
		if !ok || s.Flags&StatusRanked == 0 {
			t.Fatalf("probe %d not answered (%#v)", i, s)
		}
	}
	if _, ok := status(t, h, "10.0.0.1:5001", build); ok {
		t.Fatal("the fourth STATUS in a second from one IP was answered")
	}
	// another IP is not affected; a second later the first is answered again
	if _, ok := status(t, h, "10.0.0.2:5000", build); !ok {
		t.Fatal("another source was limited")
	}
	h.now = h.now.Add(time.Second)
	if _, ok := status(t, h, "10.0.0.1:5000", build); !ok {
		t.Fatal("not answered after the window")
	}
}

// A client in a room probing from its own address keeps its session, and STATUS does not count
// as a sign of life (only messages with its token do).
func TestStatusLeavesClientsAlone(t *testing.T) {
	h := newHarness(t, Config{})
	host, guest := ap("10.0.0.1:1000"), ap("10.0.0.2:1000")
	h.pair(host, guest)
	seen := h.r.clients[host].lastSeen
	h.now = h.now.Add(5 * time.Second)
	if _, ok := status(t, h, "10.0.0.1:1000", build); !ok {
		t.Fatal("no answer")
	}
	if c := h.r.clients[host]; c == nil || !c.lastSeen.Equal(seen) || c.room == nil {
		t.Fatalf("STATUS changed the client: %#v", c)
	}
	// an unknown type a later client might send (0x25) is still dropped silently
	b := cat(hdr(0x25), make([]byte, 20))
	expectNone(t, h.sendRaw(host, b))
	// a reply is never larger than the request
	req, _ := Encode(Status{Nonce: 1, Build: build})
	out := h.r.Handle(h.now, ap("10.0.0.9:9"), req)
	if len(out) != 1 || len(out[0].Data) > len(req) || binary.LittleEndian.Uint32(out[0].Data[HeaderLen:]) != 1 {
		t.Fatalf("answer %#v", out)
	}
}
