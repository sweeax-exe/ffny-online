package relay

import "encoding/binary"

// ROOMLIST (0x25, both directions): the relay's public rooms for a client that is not registered
// there (build 31, "Both servers": the game is in the lobby of one relay and shows the rooms of
// the other beside its own). Like STATUS it needs no token, registers nothing and touches no
// client; the answer is never larger than the request, so a client pads its request with zero
// bytes to the size of the answer it can take (a full datagram for the whole list), and each
// source IP gets at most Config.RoomListPerSecond answers a second. A relay without it drops
// ROOMLIST silently (an unknown type), so a client that gets no answer shows that relay's rooms
// as unknown.

// TypeRoomList is the ROOMLIST message type, in both directions.
const TypeRoomList MsgType = 0x25

// ROOMLIST sizes.
const (
	RoomListBodyMin  = 4 + BuildLen + 1 + 4    // nonce, build, mode, caps; zero padding may follow
	RoomListReplyMin = 4 + 2 + 1               // nonce, total, count
	roomListEntryFix = RoomCodeLen + 1 + 4 + 2 // code, name length, mode/players/max/phase, age
	RoomListMaxAge   = 0xFFFF                  // seconds; older rooms say this
)

// RoomList asks for the public rooms of build Build (zeros: every build) with match type Mode
// (ModeAny: every type). King of the Hill rooms are listed only when Caps has CapKoth, as for LIST.
// Pad is the number of zero bytes after the fields: the answer may use HeaderLen+RoomListBodyMin+Pad
// bytes at most.
type RoomList struct {
	Nonce uint32
	Build Build
	Mode  uint8
	Caps  uint32
	Pad   int
}

// ListedRoom is one entry of a ROOMLIST answer: a ROOMS entry and the room's age.
type ListedRoom struct {
	RoomInfo
	Age uint16 // seconds since the room was opened (RoomListMaxAge: that long or longer)
}

// RoomListReply answers ROOMLIST: how many rooms matched, and as many of them as fit (newest first).
type RoomListReply struct {
	Nonce uint32
	Total uint16
	Rooms []ListedRoom // at most MaxListed
}

func (RoomList) Type() MsgType      { return TypeRoomList }
func (RoomListReply) Type() MsgType { return TypeRoomList }

func (m RoomList) appendBody(b []byte) ([]byte, error) {
	b = binary.LittleEndian.AppendUint32(b, m.Nonce)
	b = append(b, m.Build[:]...)
	b = append(b, m.Mode)
	b = binary.LittleEndian.AppendUint32(b, m.Caps)
	if m.Pad > 0 {
		b = append(b, make([]byte, m.Pad)...)
	}
	return b, nil
}

func (m RoomListReply) appendBody(b []byte) ([]byte, error) {
	if len(m.Rooms) > 255 {
		return b, ErrListTooLong
	}
	b = binary.LittleEndian.AppendUint32(b, m.Nonce)
	b = binary.LittleEndian.AppendUint16(b, m.Total)
	b = append(b, byte(len(m.Rooms)))
	for _, r := range m.Rooms {
		var err error
		b = append(b, r.Room[:]...)
		if b, err = appendString(b, r.Name); err != nil {
			return b, err
		}
		b = append(b, r.Mode, r.Players, r.Max, r.Phase)
		b = binary.LittleEndian.AppendUint16(b, r.Age)
	}
	return b, nil
}

// decodeRoomListClient parses a ROOMLIST request body: the fields, then only zero bytes.
func decodeRoomListClient(body []byte) (Message, error) {
	if len(body) < RoomListBodyMin {
		return nil, ErrMalformed
	}
	for _, x := range body[RoomListBodyMin:] {
		if x != 0 {
			return nil, ErrMalformed
		}
	}
	var m RoomList
	m.Nonce = binary.LittleEndian.Uint32(body)
	copy(m.Build[:], body[4:4+BuildLen])
	m.Mode = body[4+BuildLen]
	m.Caps = binary.LittleEndian.Uint32(body[4+BuildLen+1:])
	m.Pad = len(body) - RoomListBodyMin
	return m, nil
}

// decodeRoomListServer parses a ROOMLIST answer body.
func decodeRoomListServer(body []byte) (Message, error) {
	c := newCursor(body)
	m := RoomListReply{Nonce: c.u32(), Total: c.u16()}
	n := int(c.u8())
	if n > MaxListed {
		return nil, ErrMalformed
	}
	for i := 0; i < n && c.ok; i++ {
		r := ListedRoom{RoomInfo: RoomInfo{Room: c.room(), Name: c.str(), Mode: c.u8(), Players: c.u8(), Max: c.u8(), Phase: c.u8()}}
		r.Age = c.u16()
		if r.Phase > PhasePlaying {
			return nil, ErrMalformed
		}
		m.Rooms = append(m.Rooms, r)
	}
	if !c.end() {
		return nil, ErrMalformed
	}
	return m, nil
}
