package relay

import "encoding/binary"

// STATUS (0x24, both directions): what a client asks a relay before it registers, to choose
// between relays (Europe, North America, ...): the round trip, the relay's protocol version, how
// many players it has and how many of them wait for a quick match. It needs no token and
// registers nothing; the answer is never larger than the request, and each source IP gets at
// most Config.StatusPerSecond answers a second. A relay without it drops STATUS silently (an
// unknown type is a malformed message), so a client that gets no answer treats that relay as
// reachable only by HELLO.

// TypeStatus is the STATUS message type, in both directions.
const TypeStatus MsgType = 0x24

// STATUS sizes: the request carries StatusReserved zero bytes at its end so that the answer,
// which may not be larger, has room for fields a later version adds.
const (
	StatusReserved  = 8
	StatusBodyLen   = 4 + BuildLen + StatusReserved // nonce, build, reserved
	StatusReplyBody = 4 + 1 + 1 + 2 + 2 + 2 + 2 + 4 + 2
)

// STATUS answer flags: what the relay offers.
const (
	StatusRanked   uint8 = 1 << 0 // the ranked queue (it has the account service's internal listener)
	StatusKoth     uint8 = 1 << 1 // King of the Hill rooms (CAPS)
	StatusRollback uint8 = 1 << 2 // its rollback switch is on
)

// Status asks for the relay's StatusReply. Build is the asker's build fingerprint (zeros: any
// build); Waiting and Idle in the answer count only players of that build.
type Status struct {
	Nonce uint32
	Build Build
}

// StatusReply answers STATUS.
type StatusReply struct {
	Nonce     uint32 // the request's
	Version   uint8  // the protocol version byte the relay speaks (Version)
	Flags     uint8  // StatusRanked | StatusKoth | StatusRollback
	Online    uint16 // registered clients, every build
	Rooms     uint16 // open rooms
	Waiting   uint16 // players of the asker's build in public One on One / Free for All rooms that wait for players (quick match finds them)
	Idle      uint16 // players of the asker's build in the lobby, not in a room
	Published uint32 // the newest published build number, 0 unknown
}

func (Status) Type() MsgType      { return TypeStatus }
func (StatusReply) Type() MsgType { return TypeStatus }

func (m Status) appendBody(b []byte) ([]byte, error) {
	b = binary.LittleEndian.AppendUint32(b, m.Nonce)
	b = append(b, m.Build[:]...)
	return append(b, make([]byte, StatusReserved)...), nil
}

func (m StatusReply) appendBody(b []byte) ([]byte, error) {
	b = binary.LittleEndian.AppendUint32(b, m.Nonce)
	b = append(b, m.Version, m.Flags)
	b = binary.LittleEndian.AppendUint16(b, m.Online)
	b = binary.LittleEndian.AppendUint16(b, m.Rooms)
	b = binary.LittleEndian.AppendUint16(b, m.Waiting)
	b = binary.LittleEndian.AppendUint16(b, m.Idle)
	b = binary.LittleEndian.AppendUint32(b, m.Published)
	return append(b, 0, 0), nil
}

// decodeStatusClient parses a STATUS request body: exactly StatusBodyLen bytes, the reserved
// ones zero.
func decodeStatusClient(body []byte) (Message, error) {
	if len(body) != StatusBodyLen {
		return nil, ErrMalformed
	}
	for _, x := range body[4+BuildLen:] {
		if x != 0 {
			return nil, ErrMalformed
		}
	}
	var s Status
	s.Nonce = binary.LittleEndian.Uint32(body)
	copy(s.Build[:], body[4:4+BuildLen])
	return s, nil
}

// decodeStatusServer parses a STATUS answer body.
func decodeStatusServer(body []byte) (Message, error) {
	if len(body) != StatusReplyBody || body[StatusReplyBody-2] != 0 || body[StatusReplyBody-1] != 0 {
		return nil, ErrMalformed
	}
	c := newCursor(body)
	return StatusReply{Nonce: c.u32(), Version: c.u8(), Flags: c.u8(), Online: c.u16(), Rooms: c.u16(),
		Waiting: c.u16(), Idle: c.u16(), Published: c.u32()}, nil
}
