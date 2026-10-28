package relay

import "encoding/binary"

// King of the Hill extension (docs/research/king-of-the-hill.md, "Protocol"). Same header and
// version byte; message types from 0x20. A relay without the extension drops CAPS silently, so
// a client that gets no CAPS answer treats King of the Hill as unavailable. Only a client that
// announced CapKoth is let into a King of the Hill room; every other room behaves as before.

// King of the Hill constants.
const (
	// ModeKoth and ModeKothRandom are the Settings.mode of a King of the Hill room: the venue of
	// each fight is voted on by the two fighters (ModeKoth) or drawn at random (ModeKothRandom).
	// They are not front-end match types: every fight is One on One (KothFightMode).
	ModeKoth       uint8 = 0x40
	ModeKothRandom uint8 = 0x41
	KothFightMode  uint8 = 0 // START.mode of every King of the Hill fight: One on One

	MaxKothSlots = 10 // players in a King of the Hill room, upper bound of its Settings.max

	// CapKoth is the CAPS bit for King of the Hill.
	CapKoth uint32 = 1 << 0
	// CapRollback in the relay's answer lets clients roll back (their beta setting still decides);
	// without it every match plays in lockstep. Config.RollbackAllowed switches it.
	CapRollback uint32 = 1 << 1
	// CapYield in the relay's answer says it takes YIELD ("give up my spot"); a client shows the
	// choice only then (relays before build 36 drop the message).
	CapYield uint32 = 1 << 2
	// RelayCaps is what this relay always answers to CAPS.
	RelayCaps = CapKoth | CapYield

	WinnerDraw uint8 = 0xFF // RESULT winner and HILL last_winner: a draw / nobody
)

// King of the Hill message types.
const (
	TypeCaps   MsgType = 0x20 // both directions
	TypeHill   MsgType = 0x21 // server -> client
	TypeResult MsgType = 0x22 // client -> server
	TypeWatch  MsgType = 0x23 // server -> client
	TypeYield  MsgType = 0x26 // client -> server: give up my spot (to the back of the line)
)

// HILL phases.
const (
	HillWaiting  uint8 = 0 // fewer than two players can fight
	HillCalling  uint8 = 1 // the next two must READY within the ready time
	HillVoting   uint8 = 2 // the two fighters vote on the venue
	HillFighting uint8 = 3 // the fight runs; the others watch
)

// HILL per-player line states.
const (
	LineWaiting  uint8 = 0 // in the line, not called
	LineCalled   uint8 = 1 // called, not ready yet
	LineReady    uint8 = 2 // called and ready
	LineFighting uint8 = 3 // in the fight (voting or fighting)
	LineAway     uint8 = 4 // skipped too often; not called until it sends READY 1
)

// HILL last_how: how the last fight ended.
const (
	HowNone uint8 = 0 // no fight yet
	HowWin  uint8 = 1 // decided in the fight
	HowDraw uint8 = 2 // a draw: both to the back
	HowLeft uint8 = 3 // the loser left during the fight
	HowVoid uint8 = 4 // the reports disagreed, or none came: as a draw
)

// IsKoth reports whether a Settings.mode is a King of the Hill mode.
func IsKoth(mode uint8) bool { return mode == ModeKoth || mode == ModeKothRandom }

// maxPlayers is the upper bound of Settings.max for a mode.
func maxPlayers(mode uint8) int {
	if IsKoth(mode) {
		return MaxKothSlots
	}
	return MaxSlots
}

// modeMatches is the LIST and QUICK filter: a King of the Hill filter matches both venue choices.
func modeMatches(filter, mode uint8) bool {
	return filter == ModeAny || filter == mode || (IsKoth(filter) && IsKoth(mode))
}

// ---- client -> server ----

// Caps announces what the client supports (CapKoth). Answered with RelayCapsMsg.
type Caps struct {
	Token Token
	Caps  uint32
}

// Result is a fighter's outcome of the fight its START began: the winner's slot or WinnerDraw.
// Clients repeat it until HILL shows the fight is over.
type Result struct {
	Token  Token
	Seed   uint32
	Winner uint8
}

// Yield gives up the sender's place in the King of the Hill line: it goes to the back (a king
// gives up the crown). Not while the sender votes or fights. Answered with ROOM and HILL.
type Yield struct {
	Token Token
}

// ---- server -> client ----

// RelayCapsMsg answers CAPS with what the relay supports.
type RelayCapsMsg struct {
	Caps uint32
}

// HillEntry is one player of HILL, in line order.
type HillEntry struct {
	Slot  uint8
	State uint8  // LineWaiting..LineAway
	Wins  uint16 // fights won in this room
}

// Hill is the King of the Hill state of the room, sent after every ROOM of a King of the Hill
// room. Line[0] is the king (when King is not WinnerDraw), Line[1] the challenger.
type Hill struct {
	Room       RoomCode
	Phase      uint8  // HillWaiting..HillFighting
	King       uint8  // slot, or WinnerDraw for none
	Streak     uint16 // the king's wins in a row
	MsLeft     uint16 // calling: ready countdown; voting: vote countdown
	Seed       uint32 // fighting: the fight's START seed; otherwise 0
	Fights     uint16 // fights finished in this room (wraps)
	LastWinner uint8  // slot, or WinnerDraw
	LastLoser  uint8  // slot, or WinnerDraw
	LastHow    uint8  // HowNone..HowVoid
	Line       []HillEntry
}

// Watch is the spectator's START: the same body, for a fight this client only watches.
type Watch Start

func (Caps) Type() MsgType         { return TypeCaps }
func (Result) Type() MsgType       { return TypeResult }
func (Yield) Type() MsgType        { return TypeYield }
func (RelayCapsMsg) Type() MsgType { return TypeCaps }
func (Hill) Type() MsgType         { return TypeHill }
func (Watch) Type() MsgType        { return TypeWatch }

func (m Caps) token() Token   { return m.Token }
func (m Result) token() Token { return m.Token }
func (m Yield) token() Token  { return m.Token }

func (m Caps) appendBody(b []byte) ([]byte, error) {
	return binary.LittleEndian.AppendUint32(append(b, m.Token[:]...), m.Caps), nil
}

func (m Result) appendBody(b []byte) ([]byte, error) {
	b = binary.LittleEndian.AppendUint32(append(b, m.Token[:]...), m.Seed)
	return append(b, m.Winner), nil
}

func (m Yield) appendBody(b []byte) ([]byte, error) { return append(b, m.Token[:]...), nil }

func (m RelayCapsMsg) appendBody(b []byte) ([]byte, error) {
	return binary.LittleEndian.AppendUint32(b, m.Caps), nil
}

func (m Watch) appendBody(b []byte) ([]byte, error) { return Start(m).appendBody(b) }

func (m Hill) appendBody(b []byte) ([]byte, error) {
	if len(m.Line) > 255 {
		return b, ErrListTooLong
	}
	b = append(b, m.Room[:]...)
	b = append(b, m.Phase, m.King)
	b = binary.LittleEndian.AppendUint16(b, m.Streak)
	b = binary.LittleEndian.AppendUint16(b, m.MsLeft)
	b = binary.LittleEndian.AppendUint32(b, m.Seed)
	b = binary.LittleEndian.AppendUint16(b, m.Fights)
	b = append(b, m.LastWinner, m.LastLoser, m.LastHow, byte(len(m.Line)))
	for _, e := range m.Line {
		b = append(b, e.Slot, e.State)
		b = binary.LittleEndian.AppendUint16(b, e.Wins)
	}
	return b, nil
}

// decodeKothClient parses CAPS, RESULT and YIELD (the token is already read).
func decodeKothClient(t MsgType, tok Token, c *cursor) (Message, bool) {
	switch t {
	case TypeCaps:
		return Caps{Token: tok, Caps: c.u32()}, true
	case TypeResult:
		return Result{Token: tok, Seed: c.u32(), Winner: c.u8()}, true
	case TypeYield:
		return Yield{Token: tok}, true
	}
	return nil, false
}

// decodeKothServer parses CAPS, HILL and WATCH.
func decodeKothServer(t MsgType, c *cursor) (Message, bool) {
	switch t {
	case TypeCaps:
		return RelayCapsMsg{Caps: c.u32()}, true
	case TypeWatch:
		s := Start{Seed: c.u32(), Mode: c.u8(), Rounds: c.u8(), Venue: c.u8()}
		np := int(c.u8())
		if np > MaxSlots {
			c.ok = false
		}
		for i := 0; i < np && c.ok; i++ {
			s.Players = append(s.Players, StartPlayer{Slot: c.u8(), Fighter: c.u8()})
		}
		return Watch(s), true
	case TypeHill:
		h := Hill{Room: c.room(), Phase: c.u8(), King: c.u8(), Streak: c.u16(), MsLeft: c.u16(), Seed: c.u32(),
			Fights: c.u16(), LastWinner: c.u8(), LastLoser: c.u8(), LastHow: c.u8()}
		n := int(c.u8())
		if n > MaxKothSlots || h.Phase > HillFighting || h.LastHow > HowVoid {
			c.ok = false
		}
		for i := 0; i < n && c.ok; i++ {
			e := HillEntry{Slot: c.u8(), State: c.u8(), Wins: c.u16()}
			if e.Slot >= MaxKothSlots || e.State > LineAway {
				c.ok = false
			}
			h.Line = append(h.Line, e)
		}
		return h, true
	}
	return nil, false
}
