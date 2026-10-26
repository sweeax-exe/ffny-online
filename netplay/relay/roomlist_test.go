package relay

import (
	"bytes"
	"fmt"
	"testing"
	"time"
)

func TestRoomListEncoding(t *testing.T) {
	req, err := Encode(RoomList{Nonce: 0x01020304, Build: build, Mode: ModeAny, Caps: CapKoth, Pad: 3})
	if err != nil {
		t.Fatal(err)
	}
	want := cat(hdr(TypeRoomList), []byte{4, 3, 2, 1}, build[:], []byte{0xFF, 1, 0, 0, 0}, []byte{0, 0, 0})
	if !bytes.Equal(req, want) {
		t.Fatalf("ROOMLIST request % x, want % x", req, want)
	}
	c, err := DecodeClient(req)
	if err != nil {
		t.Fatal(err)
	}
	if got := c.(RoomList); got.Nonce != 0x01020304 || got.Build != build || got.Mode != ModeAny || got.Caps != CapKoth || got.Pad != 3 {
		t.Fatalf("client round trip: %#v", got)
	}
	code := RoomCode{'A', 'B', 'C', 'D', 'E', 'F'}
	rep := RoomListReply{Nonce: 9, Total: 2, Rooms: []ListedRoom{
		{RoomInfo: RoomInfo{Room: code, Name: "x", Mode: 0, Players: 1, Max: 2, Phase: 0}, Age: 300}}}
	b, err := Encode(rep)
	if err != nil {
		t.Fatal(err)
	}
	if want := cat(hdr(TypeRoomList), []byte{9, 0, 0, 0, 2, 0, 1}, code[:], []byte{1, 'x', 0, 1, 2, 0, 0x2C, 1}); !bytes.Equal(b, want) {
		t.Fatalf("ROOMLIST answer % x, want % x", b, want)
	}
	m, err := DecodeServer(b)
	if err != nil || !msgEqual(m, rep) {
		t.Fatalf("server round trip: %#v %v", m, err)
	}
	// malformed: short, padding that is not zero; a truncated answer, a phase out of range
	bad := [][]byte{req[:HeaderLen+RoomListBodyMin-1], append(append([]byte(nil), req...), 1)}
	for _, x := range bad {
		if _, err := DecodeClient(x); err == nil {
			t.Errorf("DecodeClient accepted % x", x)
		}
	}
	badServer := [][]byte{b[:len(b)-1], cat(hdr(TypeRoomList), []byte{9, 0, 0, 0, 1, 0, 1}, code[:], []byte{1, 'x', 0, 1, 2, 3, 0, 0}),
		cat(hdr(TypeRoomList), []byte{9, 0, 0, 0, 0, 0, 31})}
	for _, x := range badServer {
		if _, err := DecodeServer(x); err == nil {
			t.Errorf("DecodeServer accepted % x", x)
		}
	}
}

// roomList asks with a full-size request (as the game does) and returns the answer.
func roomList(t *testing.T, h *harness, from string, b Build, mode uint8, caps uint32) (RoomListReply, bool) {
	t.Helper()
	res := h.send(ap(from), RoomList{Nonce: 77, Build: b, Mode: mode, Caps: caps, Pad: MaxDatagram - HeaderLen - RoomListBodyMin})
	if len(res) == 0 {
		return RoomListReply{}, false
	}
	if len(res) != 1 || res[0].to != ap(from) {
		t.Fatalf("ROOMLIST: %#v", res)
	}
	r, ok := res[0].msg.(RoomListReply)
	if !ok || r.Nonce != 77 {
		t.Fatalf("ROOMLIST answer %#v", res[0].msg)
	}
	return r, true
}

func TestRoomListContents(t *testing.T) {
	l := newLobby(t, Config{})
	if r, ok := roomList(t, l.harness, "10.0.0.1:5000", build, ModeAny, 0); !ok || r.Total != 0 || len(r.Rooms) != 0 {
		t.Fatalf("empty relay: %#v %v", r, ok)
	}
	// a public One on One quick room (A waits), a private room (B), a 3-Way room (C), a legacy room,
	// a hill (D, F), and a room of another build (E)
	l.enter(pA, "A")
	quick := roomOf(t, l.quickM(pA, 0, 2), pA).Room
	l.harness.now = l.harness.now.Add(42 * time.Second)
	l.enter(pB, "B")
	l.mkRoom(pB, RoomSettings{Name: "secret", Public: 0, Mode: 0, Max: 2, Pool: []uint8{1}})
	l.enter(pC, "C")
	three := roomOf(t, l.mkRoom(pC, RoomSettings{Name: "three", Public: 1, Mode: 3, Max: 3, Pool: []uint8{1}}), pC).Room
	l.send(ap("10.0.1.3:1000"), Hello{Mode: ModeCreate, Build: build, Name: "legacy"})
	hill := l.kroom(RoomSettings{Name: "hill", Public: 1, Mode: ModeKothRandom, Max: 8, Pool: []uint8{1}}, pD, pF)
	other := Build{1}
	l.send(pE, Hello{Mode: ModeLobby, Build: other, Name: "E"})
	l.send(pE, Create{Token: l.r.clients[pE].token, Settings: RoomSettings{Name: "other", Public: 1, Mode: 0, Max: 2, Pool: []uint8{1}}})
	clients := l.r.Clients()

	r, _ := roomList(t, l.harness, "10.0.0.1:5001", build, ModeAny, 0)
	if r.Total != 2 || len(r.Rooms) != 2 || r.Rooms[0].Room != three || r.Rooms[1].Room != quick {
		t.Fatalf("without King of the Hill: %#v", r)
	}
	if q := r.Rooms[1]; q.Players != 1 || q.Max != 2 || q.Phase != PhaseLobby || q.Mode != 0 || q.Age != 42 || q.Name == "" {
		t.Fatalf("the quick room: %#v", q)
	}
	if r.Rooms[0].Age != 0 {
		t.Fatalf("the new room's age: %d", r.Rooms[0].Age)
	}
	r, _ = roomList(t, l.harness, "10.0.0.1:5002", build, ModeAny, CapKoth)
	if r.Total != 3 || r.Rooms[0].Room != hill || r.Rooms[0].Players != 2 {
		t.Fatalf("with King of the Hill: %#v", r)
	}
	r, _ = roomList(t, l.harness, "10.0.0.1:5003", build, 0, CapKoth)
	if r.Total != 1 || r.Rooms[0].Room != quick {
		t.Fatalf("One on One only: %#v", r)
	}
	r, _ = roomList(t, l.harness, "10.0.0.2:5000", other, ModeAny, 0)
	if r.Total != 1 || r.Rooms[0].Name != "other" {
		t.Fatalf("the other build: %#v", r)
	}
	r, _ = roomList(t, l.harness, "10.0.0.3:5000", Build{}, ModeAny, CapKoth)
	if r.Total != 4 {
		t.Fatalf("every build: %#v", r)
	}
	// nothing registered, nobody's idle timer touched; the counter counts the answers
	if l.r.Clients() != clients {
		t.Fatalf("ROOMLIST registered a client: %d, was %d", l.r.Clients(), clients)
	}
	if got := l.r.TakeStats().RoomList; got != 6 {
		t.Fatalf("stats counted %d ROOMLIST answers, want 6", got)
	}
}

// The answer never outgrows the request: a short request gets fewer rooms (Total still says how
// many there are), a full one at most MaxListed, and either fits one datagram.
func TestRoomListBounded(t *testing.T) {
	l := newLobby(t, Config{MaxClients: 100})
	for i := 0; i < 40; i++ {
		a := ap(fmt.Sprintf("10.1.%d.1:1000", i))
		l.enter(a, fmt.Sprintf("P%02d", i))
		l.mkRoom(a, RoomSettings{Name: fmt.Sprintf("a long room name, %02d...", i), Public: 1, Mode: 0, Max: 2, Pool: []uint8{1}})
	}
	r, _ := roomList(t, l.harness, "10.0.0.1:5000", build, ModeAny, 0)
	if r.Total != 40 || len(r.Rooms) == 0 || len(r.Rooms) > MaxListed {
		t.Fatalf("full request: total %d, %d rooms", r.Total, len(r.Rooms))
	}
	for _, pad := range []int{0, 40, 200} {
		req, _ := Encode(RoomList{Nonce: 1, Build: build, Mode: ModeAny, Pad: pad})
		out := l.r.Handle(l.now, ap(fmt.Sprintf("10.0.9.%d:1", pad)), req)
		if len(out) != 1 || len(out[0].Data) > len(req) {
			t.Fatalf("pad %d: answer %#v", pad, out)
		}
		m, err := DecodeServer(out[0].Data)
		if err != nil {
			t.Fatal(err)
		}
		if got := m.(RoomListReply); got.Total != 40 || len(got.Rooms) >= len(r.Rooms) {
			t.Fatalf("pad %d: total %d, %d rooms", pad, got.Total, len(got.Rooms))
		}
	}
}

func TestRoomListRateLimit(t *testing.T) {
	h := newHarness(t, Config{RoomListPerSecond: 2})
	for i := 0; i < 2; i++ {
		if _, ok := roomList(t, h, "10.0.0.1:5000", build, ModeAny, 0); !ok {
			t.Fatalf("request %d not answered", i)
		}
	}
	if _, ok := roomList(t, h, "10.0.0.1:5001", build, ModeAny, 0); ok {
		t.Fatal("the third ROOMLIST in a second from one IP was answered")
	}
	// STATUS has its own allowance; another IP is not affected; a second later it is answered again
	if _, ok := status(t, h, "10.0.0.1:5002", build); !ok {
		t.Fatal("STATUS was limited by ROOMLIST")
	}
	if _, ok := roomList(t, h, "10.0.0.2:5000", build, ModeAny, 0); !ok {
		t.Fatal("another source was limited")
	}
	h.now = h.now.Add(time.Second)
	if _, ok := roomList(t, h, "10.0.0.1:5000", build, ModeAny, 0); !ok {
		t.Fatal("not answered after the window")
	}
}
