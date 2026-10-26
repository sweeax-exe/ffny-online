package relay

import (
	"net/netip"
	"sort"
	"time"
)

// roomList answers ROOMLIST (protocol_roomlist.go): the public rooms of the asker's build, as LIST
// gives them to a registered client, with each room's age. It registers nothing, and the answer
// holds as many rooms as fit in the request's own size (never more than MaxListed); a source gets
// at most RoomListPerSecond answers a second, further requests are dropped silently.
func (r *Relay) roomList(now time.Time, from netip.AddrPort, src *source, m RoomList, reqLen int) []Packet {
	if !src.allowRoomList(now, r.cfg.RoomListPerSecond) {
		return nil
	}
	every := m.Build == Build{}
	var rooms []*room
	for _, rm := range r.rooms {
		if rm.koth != nil && m.Caps&CapKoth == 0 {
			continue // as LIST: only a client that can enter a hill sees one
		}
		if rm.settings.Public == 1 && (every || rm.build == m.Build) && modeMatches(m.Mode, rm.settings.Mode) {
			rooms = append(rooms, rm)
		}
	}
	sort.Slice(rooms, func(i, j int) bool { return rooms[i].seq > rooms[j].seq })
	res := RoomListReply{Nonce: m.Nonce, Total: clamp16(len(rooms))}
	size := HeaderLen + RoomListReplyMin
	for _, rm := range rooms {
		entry := roomListEntryFix + len(rm.settings.Name)
		if len(res.Rooms) == MaxListed || size+entry > reqLen {
			break
		}
		size += entry
		age := now.Sub(rm.created) / time.Second
		if age < 0 {
			age = 0
		}
		if age > RoomListMaxAge {
			age = RoomListMaxAge
		}
		res.Rooms = append(res.Rooms, ListedRoom{
			RoomInfo: RoomInfo{Room: rm.code, Name: rm.settings.Name, Mode: rm.settings.Mode,
				Players: uint8(rm.count()), Max: rm.settings.Max, Phase: rm.phase},
			Age: uint16(age),
		})
	}
	data := mustEncode(res)
	if len(data) > reqLen {
		return nil // cannot happen (the entries are counted against reqLen); the anti-amplification guard
	}
	r.stats.RoomList++
	return []Packet{{To: from, Data: data}}
}

// allowRoomList counts one ROOMLIST answer in a fixed one-second window.
func (s *source) allowRoomList(now time.Time, perSecond int) bool {
	if now.Sub(s.roomListWin) >= time.Second || now.Before(s.roomListWin) {
		s.roomListWin = now
		s.roomListCount = 0
	}
	s.roomListCount++
	return s.roomListCount <= perSecond
}
