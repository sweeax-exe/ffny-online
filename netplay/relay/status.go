package relay

import (
	"net/netip"
	"time"
)

// status answers STATUS (protocol_status.go): the relay's version, its load and the players a
// quick match of the asker's build would meet. It registers nothing and never answers with more
// bytes than it was sent; a source gets at most StatusPerSecond answers a second, further ones
// are dropped silently.
func (r *Relay) status(now time.Time, from netip.AddrPort, src *source, m Status, reqLen int) []Packet {
	if !src.allowStatus(now, r.cfg.StatusPerSecond) {
		return nil
	}
	waiting, idle := r.statusCounts(m.Build)
	reply := StatusReply{
		Nonce:   m.Nonce,
		Version: Version,
		Flags:   StatusKoth,
		Online:  clamp16(len(r.clients)),
		Rooms:   clamp16(len(r.rooms)),
		Waiting: clamp16(waiting),
		Idle:    clamp16(idle),
	}
	if r.cfg.Ranked {
		reply.Flags |= StatusRanked
	}
	if r.cfg.RollbackAllowed != nil && r.cfg.RollbackAllowed() {
		reply.Flags |= StatusRollback
	}
	if r.cfg.PublishedBuild != nil {
		reply.Published = r.cfg.PublishedBuild()
	}
	data := mustEncode(reply)
	if len(data) > reqLen {
		return nil // cannot happen with an exact-length request; kept as the anti-amplification guard
	}
	r.stats.Status++
	return []Packet{{To: from, Data: data}}
}

// statusCounts counts, for build b (zeros: every build), the players in public rooms that wait in
// the lobby phase with a free place (rooms a quick match fills; not King of the Hill, ranked or
// code-only rooms), and the players in the lobby outside any room.
func (r *Relay) statusCounts(b Build) (waiting, idle int) {
	every := b == Build{}
	for _, rm := range r.rooms {
		if rm.legacy || rm.ranked || rm.koth != nil || rm.settings.Public != 1 || rm.phase != PhaseLobby ||
			(!every && rm.build != b) {
			continue
		}
		if n := rm.count(); n < int(rm.settings.Max) {
			waiting += n
		}
	}
	for _, c := range r.clients {
		if c.lobby && c.room == nil && c.queue == nil && (every || c.build == b) {
			idle++
		}
	}
	return waiting, idle
}

// allowStatus counts one STATUS answer in a fixed one-second window.
func (s *source) allowStatus(now time.Time, perSecond int) bool {
	if now.Sub(s.statusWin) >= time.Second || now.Before(s.statusWin) {
		s.statusWin = now
		s.statusCount = 0
	}
	s.statusCount++
	return s.statusCount <= perSecond
}

func clamp16(n int) uint16 {
	if n > 0xFFFF {
		return 0xFFFF
	}
	return uint16(n)
}
