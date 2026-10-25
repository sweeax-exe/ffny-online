package accounts

import (
	"fmt"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Match results and the statistics made from them.
//
// Every signed-in player's game reports the match it plays (POST /api/matches). The reports of
// one match share a key (room, seed and match id). Since build 23 a game reports three events:
// "start" once the fight is loaded, then "result" when it is decided, or "left" naming the
// players who stopped answering before that (builds before 23 send results only). Since build 27
// a game whose process crashes during the fight sends a fourth, "crashed", for its own slot from
// the crash handler, before it dies.
//
// A match counts once the reports have had ReportWindow to arrive after the first result or
// "left", and:
//   - results: every human player reported one, and all of them agree on who played what and who
//     won. A match against a player who is not signed in, or whose game did not report, does not
//     count (a second copy of the game, not signed in, gave free wins);
//   - a player left: everyone who stayed says so and names the same players; each of those left
//     after reporting the start (so they were signed in) and sent nothing else but, perhaps,
//     "crashed". They take a loss; in a one on one the one who stayed takes the win. Leaving
//     before a loss kept records clean (players' reports, 2026-10-24). When the two games each say
//     the other left, nothing counts;
//   - no contest: a leaver whose own game reported "crashed" for the match takes nothing, and in a
//     one on one the one who stayed takes nothing either (the owner's decision, 2026-10-25: players
//     lost records to crashes of the rollback beta). When every leaver crashed the match counts for
//     nobody and is logged as a no contest; a leaver who quit, was killed or lost the connection
//     beside one who crashed still takes the loss. A "crashed" report from a player nobody named as
//     a leaver, or for a match whose results are all in, changes nothing (the match was decided).
// Anything else is dropped. Casual statistics trust the games; ranked adds the relay's pairing and
// moves ratings on agreed results only, so neither a leaver nor a no contest changes a rating.

// ReportWindow is how long the other players' reports may take after the first one.
const ReportWindow = 45 * time.Second

// StartLifetime is how long a match's start reports wait for a result or a "left".
const StartLifetime = 2 * time.Hour

// RecentMatches is how many matches an account keeps in its history.
const RecentMatches = 20

const maxReportsPerHour = 120 // a start and a result per match since build 23

// MatchPlayer is one fighter of a reported match.
type MatchPlayer struct {
	Slot    int    `json:"slot"`
	Name    string `json:"name"`
	Fighter int    `json:"fighter"`
	FName   string `json:"fighter_name"` // from the player's own dump; the project ships no names
	CPU     bool   `json:"cpu"`
}

// MatchReport is what a game sends after a match.
type MatchReport struct {
	Key       string        `json:"key"`
	Mode      int           `json:"mode"`
	ModeName  string        `json:"mode_name"`
	Venue     int           `json:"venue"`
	VenueName string        `json:"venue_name"`
	You       int           `json:"you"` // the reporter's slot
	Players   []MatchPlayer `json:"players"`
	Winners   []int         `json:"winners"`
	Draw      bool          `json:"draw"`
	Event     string        `json:"event,omitempty"` // "start", "left", "crashed" or "result" ("" is a result)
	Left      []int         `json:"left,omitempty"`  // "left": the slots that stopped answering
}

const (
	eventStart   = "start"
	eventLeft    = "left"
	eventResult  = "result"
	eventCrashed = "crashed" // the reporter's own game crashed during the fight
)

func (r *MatchReport) event() string {
	if r.Event == "" {
		return eventResult
	}
	return r.Event
}

// MatchLine is one match in an account's history.
type MatchLine struct {
	At        time.Time `json:"at"`
	Result    string    `json:"result"` // win, loss, draw
	Mode      string    `json:"mode"`
	Venue     string    `json:"venue"`
	Fighter   string    `json:"fighter"`
	Opponents []string  `json:"opponents"`
}

// Stats are an account's numbers.
type Stats struct {
	Matches    int               `json:"matches"`
	Wins       int               `json:"wins"`
	Losses     int               `json:"losses"`
	Draws      int               `json:"draws"`
	Streak     int               `json:"streak"` // wins in a row now
	BestStreak int               `json:"best_streak"`
	Fighters   map[string]int    `json:"fighters,omitempty"` // fighter id -> matches
	FNames     map[string]string `json:"fighter_names,omitempty"`
	Recent     []MatchLine       `json:"recent,omitempty"`
	reports    []time.Time
}

// valid checks a report's shape; the outcome itself is checked against the other reports.
func (r *MatchReport) valid() bool {
	if len(r.Key) < 8 || len(r.Key) > 80 || len(r.Players) < 2 || len(r.Players) > 4 || r.Mode < 0 || r.Mode > 255 ||
		r.Venue < 0 || r.Venue > 255 || len(r.ModeName) > 40 || len(r.VenueName) > 60 {
		return false
	}
	slots := map[int]bool{}
	for _, p := range r.Players {
		if p.Slot < 0 || p.Slot > 3 || slots[p.Slot] || p.Fighter < 0 || p.Fighter > 255 || len(p.Name) > 32 || len(p.FName) > 40 {
			return false
		}
		slots[p.Slot] = true
	}
	if !slots[r.You] {
		return false
	}
	for _, w := range r.Winners {
		if !slots[w] {
			return false
		}
	}
	switch r.event() {
	case eventStart, eventCrashed:
		return len(r.Winners) == 0 && !r.Draw && len(r.Left) == 0
	case eventLeft:
		if len(r.Winners) != 0 || r.Draw || len(r.Left) == 0 {
			return false
		}
		for _, l := range r.Left {
			if !slots[l] || l == r.You {
				return false
			}
		}
		return true
	case eventResult:
		return len(r.Left) == 0 && (r.Draw || len(r.Winners) > 0)
	}
	return false
}

// lineup is what every report of one match must agree on before its outcome: the mode, the venue
// and who fought as what.
func (r *MatchReport) lineup() string {
	var b strings.Builder
	b.WriteString(strconv.Itoa(r.Mode) + "/" + strconv.Itoa(r.Venue) + "/")
	ps := append([]MatchPlayer(nil), r.Players...)
	sort.Slice(ps, func(i, j int) bool { return ps[i].Slot < ps[j].Slot })
	for _, p := range ps {
		b.WriteString(strconv.Itoa(p.Slot) + ":" + strconv.Itoa(p.Fighter) + ":" + strconv.FormatBool(p.CPU) + ",")
	}
	return b.String()
}

func (r *MatchReport) humans() []int {
	var out []int
	for _, p := range r.Players {
		if !p.CPU {
			out = append(out, p.Slot)
		}
	}
	return out
}

func slotSet(slots []int) string {
	s := append([]int(nil), slots...)
	sort.Ints(s)
	var b strings.Builder
	for _, x := range s {
		b.WriteString(strconv.Itoa(x) + ",")
	}
	return b.String()
}

// outcome is what every report of one match must agree on.
func (r *MatchReport) outcome() string {
	var b strings.Builder
	b.WriteString(strconv.Itoa(r.Mode) + "/" + strconv.Itoa(r.Venue) + "/" + strconv.FormatBool(r.Draw) + "/")
	ps := append([]MatchPlayer(nil), r.Players...)
	sort.Slice(ps, func(i, j int) bool { return ps[i].Slot < ps[j].Slot })
	for _, p := range ps {
		b.WriteString(strconv.Itoa(p.Slot) + ":" + strconv.Itoa(p.Fighter) + ":" + strconv.FormatBool(p.CPU) + ",")
	}
	ws := append([]int(nil), r.Winners...)
	sort.Ints(ws)
	for _, w := range ws {
		b.WriteString("w" + strconv.Itoa(w))
	}
	return b.String()
}

type pendingMatch struct {
	created time.Time             // the first report of any kind
	first   time.Time             // the first result or "left": the window runs from here
	starts  map[int64]MatchReport // by account
	reports map[int64]MatchReport // the results, by account
	lefts   map[int64]MatchReport // "left", by account
	crashed map[int64]MatchReport // "crashed", by account: that player's own game crashed
}

// Report takes a game's report. It returns false when the report is malformed or the account
// sends too many.
func (s *Store) Report(account int64, r MatchReport, now time.Time) bool {
	if !r.valid() {
		return false
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	a, ok := s.accounts[account]
	if !ok {
		return false
	}
	st := a.stats()
	recent := st.reports[:0]
	for _, t := range st.reports {
		if now.Sub(t) < time.Hour {
			recent = append(recent, t)
		}
	}
	st.reports = recent
	if len(recent) >= maxReportsPerHour {
		return false
	}
	st.reports = append(st.reports, now)
	if s.pending == nil {
		s.pending = map[string]*pendingMatch{}
	}
	p, ok := s.pending[r.Key]
	if !ok {
		p = &pendingMatch{created: now, starts: map[int64]MatchReport{}, reports: map[int64]MatchReport{},
			lefts: map[int64]MatchReport{}, crashed: map[int64]MatchReport{}}
		s.pending[r.Key] = p
	}
	into := p.reports
	switch r.event() {
	case eventStart:
		into = p.starts
	case eventLeft:
		into = p.lefts
	case eventCrashed:
		into = p.crashed
	}
	if _, dup := into[account]; !dup {
		into[account] = r
	}
	// A crash alone does not start the window: the other games say who left a few seconds later
	// (a match where every game crashed expires like one that only started).
	if r.event() != eventStart && r.event() != eventCrashed && p.first.IsZero() {
		p.first = now
	}
	return true
}

// Settle counts the matches whose report window is over. It returns how many counted and how
// many were dropped because their reports disagreed.
func (s *Store) Settle(now time.Time) (counted, dropped int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for key, p := range s.pending {
		if p.first.IsZero() {
			if now.Sub(p.created) > StartLifetime { // started, never decided nor left
				delete(s.pending, key)
			}
			continue
		}
		if now.Sub(p.first) < ReportWindow {
			continue
		}
		delete(s.pending, key)
		if why := s.settle(key, p, now); why != "" {
			dropped++
			if len(s.drops) < 100 {
				s.drops = append(s.drops, fmt.Sprintf("%s (%d start, %d result, %d left, %d crashed)", why,
					len(p.starts), len(p.reports), len(p.lefts), len(p.crashed)))
			}
			continue
		}
		counted++
		s.dirty = true
	}
	s.expirePairings(now)
	return counted, dropped
}

// settle counts one match whose window is over, by the rules at the top of this file, and returns
// "" or why it does not count. Called with s.mu held.
func (s *Store) settle(key string, p *pendingMatch, now time.Time) string {
	var lineup string
	var any MatchReport
	seat := map[int]int64{}   // slot -> account
	seatOf := map[int64]int{} // account -> slot
	for _, group := range []map[int64]MatchReport{p.starts, p.reports, p.lefts, p.crashed} {
		for id, r := range group {
			if lineup == "" {
				lineup, any = r.lineup(), r
			} else if r.lineup() != lineup {
				return "the games disagree on the mode, venue or fighters"
			}
			if prev, ok := seat[r.You]; ok && prev != id {
				return "two accounts reported the same slot"
			}
			if prev, ok := seatOf[id]; ok && prev != r.You {
				return "one account reported two slots"
			}
			seat[r.You], seatOf[id] = id, r.You
		}
	}
	humans := any.humans()
	switch {
	case len(p.lefts) == 0:
		if len(p.reports) != len(humans) {
			var crashed []int
			for id, r := range p.crashed {
				if _, reported := p.reports[id]; !reported {
					crashed = append(crashed, r.You)
				}
			}
			if len(crashed) > 0 { // it would not count anyway; the log says why
				return "no contest: " + slotNames(any, crashed) + " game crashed"
			}
			return fmt.Sprintf("%d of %d players reported a result (not signed in, or the game did not report)",
				len(p.reports), len(humans))
		}
		var want string
		for _, r := range p.reports {
			if want == "" {
				want = r.outcome()
			} else if r.outcome() != want {
				return "the results disagree on who won"
			}
		}
		for id, r := range p.reports {
			if a, ok := s.accounts[id]; ok {
				a.stats().add(r, p.first)
			}
		}
		s.rankedResult(key, p, now)
		return ""
	case len(p.reports) == 0:
		var claim string
		var leavers, stayed []int
		for _, r := range p.lefts {
			if claim == "" {
				claim, leavers = slotSet(r.Left), r.Left
			} else if slotSet(r.Left) != claim {
				return "the games disagree on who left"
			}
			stayed = append(stayed, r.You)
		}
		var lost []int64
		var crashed []int
		for _, l := range leavers {
			id, ok := seat[l]
			if !ok {
				return "a player who left was not signed in"
			}
			if _, said := p.lefts[id]; said {
				return "each game says the other left"
			}
			if _, started := p.starts[id]; !started {
				return "a player who left never reported the start"
			}
			if _, crash := p.crashed[id]; crash {
				crashed = append(crashed, l) // no contest for this one: no loss
				continue
			}
			lost = append(lost, id)
		}
		if len(stayed)+len(leavers) != len(humans) {
			return "not every player who stayed reported the leaver"
		}
		if len(lost) == 0 {
			return "no contest: " + slotNames(any, crashed) + " game crashed"
		}
		for _, id := range lost {
			r := p.starts[id]
			r.Winners, r.Draw = stayed, false
			if a, ok := s.accounts[id]; ok {
				a.stats().add(r, p.first)
			}
		}
		if len(humans) == 2 && len(any.Players) == 2 { // the one leaver, and its game did not crash
			for id, r := range p.lefts {
				r.Winners = []int{r.You}
				if a, ok := s.accounts[id]; ok {
					a.stats().add(r, p.first)
				}
			}
		}
		return ""
	}
	return "both results and a player leaving were reported"
}

// slotNames names the players in slots for the service's log: "slot 1 (DMX)'s".
func slotNames(r MatchReport, slots []int) string {
	sort.Ints(slots)
	var parts []string
	for _, slot := range slots {
		name := ""
		for _, p := range r.Players {
			if p.Slot == slot {
				name = p.Name
			}
		}
		parts = append(parts, fmt.Sprintf("slot %d (%s)'s", slot, name))
	}
	return strings.Join(parts, ", ")
}

// TakeDrops returns why matches were dropped since the last call, no contests among them.
func (s *Store) TakeDrops() []string {
	s.mu.Lock()
	defer s.mu.Unlock()
	d := s.drops
	s.drops = nil
	return d
}

// ResetStats clears every account's casual statistics (a new leaderboard season) and returns how
// many accounts had any. Accounts, sessions and ranked records stay.
func (s *Store) ResetStats() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	n := 0
	for _, a := range s.accounts {
		if a.Stats != nil {
			a.Stats = nil
			n++
		}
	}
	s.pending = nil
	s.dirty = true
	return n
}

func (a *Account) stats() *Stats {
	if a.Stats == nil {
		a.Stats = &Stats{}
	}
	return a.Stats
}

func (st *Stats) add(r MatchReport, at time.Time) {
	line := MatchLine{At: at, Mode: r.ModeName, Venue: r.VenueName}
	won := false
	for _, w := range r.Winners {
		if w == r.You {
			won = true
		}
	}
	for _, p := range r.Players {
		if p.Slot == r.You {
			line.Fighter = p.FName
			if st.Fighters == nil {
				st.Fighters, st.FNames = map[string]int{}, map[string]string{}
			}
			id := strconv.Itoa(p.Fighter)
			st.Fighters[id]++
			if p.FName != "" {
				st.FNames[id] = p.FName
			}
		} else {
			name := p.Name
			if p.CPU {
				name = "CPU"
			}
			line.Opponents = append(line.Opponents, name)
		}
	}
	st.Matches++
	switch {
	case r.Draw:
		st.Draws++
		line.Result = "draw"
		st.Streak = 0
	case won:
		st.Wins++
		line.Result = "win"
		st.Streak++
		if st.Streak > st.BestStreak {
			st.BestStreak = st.Streak
		}
	default:
		st.Losses++
		line.Result = "loss"
		st.Streak = 0
	}
	st.Recent = append([]MatchLine{line}, st.Recent...)
	if len(st.Recent) > RecentMatches {
		st.Recent = st.Recent[:RecentMatches]
	}
}

// PublicStats is an account's statistics as the game and the bot show them.
type PublicStats struct {
	Account      PublicAccount `json:"account"`
	Matches      int           `json:"matches"`
	Wins         int           `json:"wins"`
	Losses       int           `json:"losses"`
	Draws        int           `json:"draws"`
	WinRate      int           `json:"win_rate"` // percent of decided matches
	Streak       int           `json:"streak"`
	BestStreak   int           `json:"best_streak"`
	Favorite     int           `json:"favorite"` // fighter id, -1 none
	FavoriteName string        `json:"favorite_name"`
	Rank         int           `json:"rank"` // place on the leaderboard, 0 not on it
	Ranked       *PublicRanked `json:"ranked,omitempty"`
	Recent       []MatchLine   `json:"recent"`
}

// LeaderboardPageSize is how many places one page of the game's leaderboard shows.
const LeaderboardPageSize = 10

// LeaderboardMin is how many matches put an account on the leaderboard.
const LeaderboardMin = 3

func (s *Store) publicStats(a *Account, rank int) PublicStats {
	st := a.stats()
	p := PublicStats{Account: public(*a), Matches: st.Matches, Wins: st.Wins, Losses: st.Losses, Draws: st.Draws,
		Streak: st.Streak, BestStreak: st.BestStreak, Favorite: -1, Rank: rank, Recent: st.Recent, Ranked: a.publicRanked()}
	if p.Recent == nil {
		p.Recent = []MatchLine{}
	}
	if d := st.Wins + st.Losses; d > 0 {
		p.WinRate = (st.Wins*100 + d/2) / d
	}
	best := 0
	for id, n := range st.Fighters {
		f, _ := strconv.Atoi(id)
		if n > best || (n == best && f < p.Favorite) {
			best, p.Favorite, p.FavoriteName = n, f, st.FNames[id]
		}
	}
	return p
}

// ranked is the leaderboard's order: most wins, then the better win rate, then fewer matches.
func (s *Store) ranked() []*Account {
	var list []*Account
	for _, a := range s.accounts {
		if a.Stats != nil && a.Stats.Matches >= LeaderboardMin {
			list = append(list, a)
		}
	}
	sort.Slice(list, func(i, j int) bool {
		x, y := list[i].Stats, list[j].Stats
		if x.Wins != y.Wins {
			return x.Wins > y.Wins
		}
		rx, ry := x.Wins*1000/max(1, x.Wins+x.Losses), y.Wins*1000/max(1, y.Wins+y.Losses)
		if rx != ry {
			return rx > ry
		}
		if x.Matches != y.Matches {
			return x.Matches < y.Matches
		}
		return list[i].ID < list[j].ID
	})
	return list
}

// StatsOf is an account's statistics with its place on the leaderboard.
func (s *Store) StatsOf(id int64) (PublicStats, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	a, ok := s.accounts[id]
	if !ok {
		return PublicStats{}, false
	}
	rank := 0
	for i, r := range s.ranked() {
		if r.ID == id {
			rank = i + 1
		}
	}
	return s.publicStats(a, rank), true
}

// StatsOfDiscord is StatsOf for a Discord user id.
func (s *Store) StatsOfDiscord(discordID string) (PublicStats, bool) {
	s.mu.Lock()
	id, ok := s.byDiscord[discordID]
	s.mu.Unlock()
	if !ok {
		return PublicStats{}, false
	}
	return s.StatsOf(id)
}

// LeaderboardEntry is one line of the leaderboard.
type LeaderboardEntry struct {
	Rank    int    `json:"rank"`
	Name    string `json:"name"`
	Wins    int    `json:"wins"`
	Losses  int    `json:"losses"`
	Matches int    `json:"matches"`
	WinRate int    `json:"win_rate"`
	Discord string `json:"discord_id,omitempty"` // internal listing only
}

// Leaderboard is the first n places.
func (s *Store) Leaderboard(n int, withDiscord bool) []LeaderboardEntry {
	page, _ := s.LeaderboardPage(0, n, withDiscord)
	return page
}

// LeaderboardPage is n places from place offset+1, and how many places the leaderboard has.
func (s *Store) LeaderboardPage(offset, n int, withDiscord bool) ([]LeaderboardEntry, int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := []LeaderboardEntry{}
	list := s.ranked()
	for i, a := range list {
		if i < offset {
			continue
		}
		if i >= offset+n {
			break
		}
		p := s.publicStats(a, i+1)
		e := LeaderboardEntry{Rank: i + 1, Name: a.Name, Wins: p.Wins, Losses: p.Losses, Matches: p.Matches, WinRate: p.WinRate}
		if withDiscord {
			e.Discord = a.DiscordID
		}
		out = append(out, e)
	}
	return out, len(list)
}

// StatsText is the game's form of an account's statistics and the leaderboard: one
// "key=value" per line, list entries as fields split by '|'. Names lose '|' and line breaks.
//
//	name=Masa
//	matches=12 / wins / losses / draws / win_rate / streak / best_streak / rank / min=3
//	favorite=55
//	favorite_name=...
//	recent=win|fighter|opponent, opponent|venue        (newest first)
//	board=1|Masa|9|2|82                                 (rank|name|wins|losses|win rate)
func StatsText(st PublicStats, board []LeaderboardEntry) string {
	clean := func(s string) string {
		return strings.Map(func(r rune) rune {
			if r == '|' || r == '\n' || r == '\r' {
				return ' '
			}
			return r
		}, s)
	}
	var b strings.Builder
	line := func(k, v string) { b.WriteString(k + "=" + v + "\n") }
	num := func(k string, v int) { line(k, strconv.Itoa(v)) }
	line("name", clean(st.Account.Name))
	num("matches", st.Matches)
	num("wins", st.Wins)
	num("losses", st.Losses)
	num("draws", st.Draws)
	num("win_rate", st.WinRate)
	num("streak", st.Streak)
	num("best_streak", st.BestStreak)
	num("rank", st.Rank)
	num("min", LeaderboardMin)
	num("favorite", st.Favorite)
	line("favorite_name", clean(st.FavoriteName))
	if r := st.Ranked; r != nil {
		line("ranked", fmt.Sprintf("%d|%d|%d|%d|%d", r.Rating, r.Games, r.Wins, r.Losses, r.Draws))
	}
	for _, m := range st.Recent {
		opp := make([]string, len(m.Opponents))
		for i, o := range m.Opponents {
			opp[i] = clean(o)
		}
		line("recent", m.Result+"|"+clean(m.Fighter)+"|"+strings.Join(opp, ", ")+"|"+clean(m.Venue))
	}
	for _, e := range board {
		line("board", fmt.Sprintf("%d|%s|%d|%d|%d", e.Rank, clean(e.Name), e.Wins, e.Losses, e.WinRate))
	}
	return b.String()
}
