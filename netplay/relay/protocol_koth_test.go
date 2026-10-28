package relay

import (
	"reflect"
	"testing"
)

func kothSettings(mode uint8, pool ...uint8) RoomSettings {
	if len(pool) == 0 {
		pool = []uint8{1}
	}
	return RoomSettings{Name: "hill", Public: 1, Mode: mode, Max: MaxKothSlots, Rounds: 2, Pool: pool}
}

func TestKothCodec(t *testing.T) {
	var tok Token
	copy(tok[:], "TOKEN123")
	rc := RoomCode{'A', 'B', 'C', 'D', 'E', 'F'}
	h := Hill{Room: rc, Phase: HillFighting, King: 3, Streak: 0x0102, MsLeft: 0x0304, Seed: 0x05060708, Fights: 0x090A,
		LastWinner: 3, LastLoser: 9, LastHow: HowWin, Line: []HillEntry{{3, LineFighting, 0x0B0C}, {9, LineAway, 1}}}
	want := cat(hdr(TypeHill), []byte("ABCDEF"), []byte{3, 3, 2, 1, 4, 3, 8, 7, 6, 5, 0x0A, 9, 3, 9, 1, 2,
		3, 3, 0x0C, 0x0B, 9, 4, 1, 0})
	if b, err := Encode(h); err != nil || string(b) != string(want) {
		t.Fatalf("HILL % x, want % x (%v)", b, want, err)
	}
	for _, m := range []Message{Caps{Token: tok, Caps: CapKoth}, Result{Token: tok, Seed: 7, Winner: WinnerDraw}, Yield{Token: tok}} {
		b, _ := Encode(m)
		d, err := DecodeClient(b)
		if err != nil || !reflect.DeepEqual(d, m) {
			t.Fatalf("client %#v -> %#v (%v)", m, d, err)
		}
	}
	w := Watch{Seed: 1, Mode: 0, Rounds: 2, Venue: 6, Players: []StartPlayer{{4, 10}, {7, 11}}}
	for _, m := range []Message{RelayCapsMsg{Caps: CapKoth}, h, w, Hill{Room: rc, King: WinnerDraw}} {
		b, _ := Encode(m)
		d, err := DecodeServer(b)
		if err != nil || !msgEqual(d, m) || d.Type() != m.Type() {
			t.Fatalf("server %#v -> %#v (%v)", m, d, err)
		}
	}
	// Malformed: wrong lengths, out-of-range states and slots.
	for _, b := range [][]byte{
		cat(hdr(TypeCaps), tok[:], []byte{1, 0, 0}),
		cat(hdr(TypeResult), tok[:], []byte{1, 0, 0, 0}),
		cat(hdr(TypeYield), tok[:], []byte{0}),
	} {
		if _, err := DecodeClient(b); err == nil {
			t.Fatalf("client accepted % x", b)
		}
	}
	for _, m := range []Hill{
		{Room: rc, Phase: 4},
		{Room: rc, LastHow: 5},
		{Room: rc, Line: []HillEntry{{10, 0, 0}}},
		{Room: rc, Line: []HillEntry{{1, 5, 0}}},
	} {
		b, _ := Encode(m)
		if _, err := DecodeServer(b); err == nil {
			t.Fatalf("server accepted %#v", m)
		}
	}
	// ROOM holds ten players only in a King of the Hill room.
	st := RoomState{Room: rc, Settings: kothSettings(ModeKoth)}
	for i := 0; i < 10; i++ {
		st.Players = append(st.Players, RoomPlayer{Slot: uint8(i), Name: "p", Voted: VoteNone})
	}
	b, _ := Encode(st)
	if _, err := DecodeServer(b); err != nil {
		t.Fatalf("ten-player ROOM: %v", err)
	}
	st.Settings = settings4("plain", 0)
	b, _ = Encode(st)
	if _, err := DecodeServer(b); err == nil {
		t.Fatal("ten players in a normal ROOM accepted")
	}
}
