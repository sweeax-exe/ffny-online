package accounts

import (
	"fmt"
	"strings"
	"time"
)

// Offline repairs for the owner (cmd/accounts -void-loss), run against the data file while the
// service is stopped. The statistics keep counters and the last RecentMatches lines, not the
// matches' keys, so a repair works on a history line: its time is the moment the match was
// decided, the same on every account the match counted for.

// VoidSlack is how far apart a history line's time and the time asked for may be.
const VoidSlack = 5 * time.Second

// RecentLines is an account's history, newest first, one line each, for choosing a line to void.
func (s *Store) RecentLines(id int64) ([]string, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	a, ok := s.accounts[id]
	if !ok {
		return nil, fmt.Errorf("no account %d", id)
	}
	var out []string
	st := a.stats()
	for _, l := range st.Recent {
		out = append(out, describeLine(l))
	}
	return out, nil
}

func describeLine(l MatchLine) string {
	return fmt.Sprintf("%s %s: %s at %s, as %s against %s", l.At.UTC().Format(time.RFC3339Nano), l.Result,
		l.Mode, l.Venue, l.Fighter, strings.Join(l.Opponents, ", "))
}

// VoidLoss turns account id's "loss" at about `at` (within VoidSlack) into a no contest: the
// match comes off its counters and its history, and, in a one on one, off the opponent's (the
// "win" line of the same moment, mode and venue on another account). It returns what it changed,
// one line each. Streaks: the loser's current streak is restored when the loss was what ended
// it and the wins before it are all in the history; the winner's current streak loses that win
// when the win is part of it. Best streaks stay as they are, except that a restored streak
// longer than the best becomes the best.
func (s *Store) VoidLoss(id int64, at time.Time) ([]string, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	a, ok := s.accounts[id]
	if !ok {
		return nil, fmt.Errorf("no account %d", id)
	}
	st := a.stats()
	i := nearestLine(st.Recent, at, "loss", "", "")
	if i < 0 {
		return nil, fmt.Errorf("account %d (%s) has no loss within %s of %s in its last %d matches", id, a.Name,
			VoidSlack, at.UTC().Format(time.RFC3339), len(st.Recent))
	}
	line := st.Recent[i]
	var changed []string
	before := fmt.Sprintf("matches %d, losses %d, streak %d, best %d", st.Matches, st.Losses, st.Streak, st.BestStreak)
	ended := firstNonWin(st.Recent) == i // the loss ended the current streak
	st.Recent = append(st.Recent[:i:i], st.Recent[i+1:]...)
	st.Matches = max(0, st.Matches-1)
	st.Losses = max(0, st.Losses-1)
	uncountFighter(st, line.Fighter)
	streakNote := "streak left as it was"
	if ended {
		j := firstNonWin(st.Recent)
		if j >= 0 || len(st.Recent) == st.Matches { // the run before the loss is all in the history
			if j < 0 {
				j = len(st.Recent)
			}
			st.Streak = j
			st.BestStreak = max(st.BestStreak, st.Streak)
			streakNote = "streak restored"
		} else {
			streakNote = "streak left as it was (the wins before the loss go past the history)"
		}
	}
	changed = append(changed, fmt.Sprintf("account %d (%s): removed %q; %s -> matches %d, losses %d, streak %d, best %d (%s)",
		id, a.Name, describeLine(line), before, st.Matches, st.Losses, st.Streak, st.BestStreak, streakNote))

	if len(line.Opponents) != 1 {
		changed = append(changed, "not a one on one: no opponent took a win for it, the other accounts stay as they are")
		s.dirty = true
		return changed, nil
	}
	var found []*Account
	var at2 []int
	for _, o := range s.accounts {
		if o.ID == id || o.Stats == nil {
			continue
		}
		if k := nearestLine(o.Stats.Recent, line.At, "win", line.Mode, line.Venue); k >= 0 {
			found, at2 = append(found, o), append(at2, k)
		}
	}
	if len(found) > 1 { // prefer the one the loser's line names
		for n, o := range found {
			if o.Name == line.Opponents[0] {
				found, at2 = []*Account{o}, []int{at2[n]}
				break
			}
		}
	}
	switch {
	case len(found) == 0:
		changed = append(changed, fmt.Sprintf("no account has the matching win (%s, within %s): the opponent %q stays as it is",
			line.At.UTC().Format(time.RFC3339Nano), VoidSlack, line.Opponents[0]))
	case len(found) > 1:
		var names []string
		for _, o := range found {
			names = append(names, fmt.Sprintf("%d (%s)", o.ID, o.Name))
		}
		changed = append(changed, "more than one account has a matching win ("+strings.Join(names, ", ")+"): none changed")
	default:
		o, k := found[0], at2[0]
		ost := o.Stats
		win := ost.Recent[k]
		before := fmt.Sprintf("matches %d, wins %d, streak %d, best %d", ost.Matches, ost.Wins, ost.Streak, ost.BestStreak)
		inRun := firstNonWin(ost.Recent) < 0 || k < firstNonWin(ost.Recent)
		ost.Recent = append(ost.Recent[:k:k], ost.Recent[k+1:]...)
		ost.Matches = max(0, ost.Matches-1)
		ost.Wins = max(0, ost.Wins-1)
		uncountFighter(ost, win.Fighter)
		if inRun {
			ost.Streak = max(0, ost.Streak-1)
		}
		changed = append(changed, fmt.Sprintf("account %d (%s): removed %q; %s -> matches %d, wins %d, streak %d, best %d",
			o.ID, o.Name, describeLine(win), before, ost.Matches, ost.Wins, ost.Streak, ost.BestStreak))
	}
	s.dirty = true
	return changed, nil
}

// nearestLine is the index of the line with that result (and mode and venue, if given) nearest
// to at within VoidSlack, or -1.
func nearestLine(lines []MatchLine, at time.Time, result, mode, venue string) int {
	best, bestD := -1, VoidSlack+1
	for i, l := range lines {
		if l.Result != result || (mode != "" && l.Mode != mode) || (venue != "" && l.Venue != venue) {
			continue
		}
		d := l.At.Sub(at)
		if d < 0 {
			d = -d
		}
		if d <= VoidSlack && d < bestD {
			best, bestD = i, d
		}
	}
	return best
}

// firstNonWin is the index of the newest line that is not a win, -1 if all are wins.
func firstNonWin(lines []MatchLine) int {
	for i, l := range lines {
		if l.Result != "win" {
			return i
		}
	}
	return -1
}

// uncountFighter takes one match off the fighter of that name, when the name says which one.
func uncountFighter(st *Stats, name string) {
	if name == "" {
		return
	}
	var ids []string
	for id, n := range st.FNames {
		if n == name {
			ids = append(ids, id)
		}
	}
	if len(ids) != 1 {
		return
	}
	if st.Fighters[ids[0]]--; st.Fighters[ids[0]] <= 0 {
		delete(st.Fighters, ids[0])
	}
}
