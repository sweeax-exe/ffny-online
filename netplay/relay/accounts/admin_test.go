package accounts

import (
	"strings"
	"testing"
	"time"
)

// played settles a one on one between a (slot 0) and b (slot 1) decided at `at`.
func played(t *testing.T, store *Store, a, b Account, key string, winner int, at time.Time) {
	t.Helper()
	store.Report(a.ID, oneOnOne(key, 0, winner), at)
	store.Report(b.ID, oneOnOne(key, 1, winner), at)
	if c, _ := store.Settle(at.Add(ReportWindow)); c != 1 {
		t.Fatal("not counted")
	}
}

func TestVoidLoss(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	t0 := now.Add(-time.Hour)
	// b: win, win, loss (the crash), win; a the reverse
	played(t, store, a, b, "ROOM-6-1", 1, t0)
	played(t, store, a, b, "ROOM-6-2", 1, t0.Add(time.Minute))
	crash := t0.Add(2 * time.Minute)
	played(t, store, a, b, "ROOM-6-3", 0, crash)
	played(t, store, a, b, "ROOM-6-4", 1, t0.Add(3*time.Minute))
	if sb, _ := store.StatsOf(b.ID); sb.Streak != 1 || sb.BestStreak != 2 || sb.Losses != 1 {
		t.Fatalf("before: %+v", sb)
	}
	store.Save()
	again, err := Open(store.path) // the tool works on the file
	if err != nil {
		t.Fatal(err)
	}
	if _, err := again.VoidLoss(b.ID, crash.Add(10*time.Second)); err == nil {
		t.Fatal("voided a loss 10 s away")
	}
	if _, err := again.VoidLoss(b.ID, t0); err == nil {
		t.Fatal("voided a win")
	}
	changed, err := again.VoidLoss(b.ID, crash.Add(3*time.Second).Truncate(time.Second))
	if err != nil {
		t.Fatal(err)
	}
	if len(changed) != 2 || !strings.Contains(changed[0], "streak restored") || !strings.Contains(changed[1], "Masa") {
		t.Fatalf("changed %q", changed)
	}
	if err := again.Save(); err != nil {
		t.Fatal(err)
	}
	after, _ := Open(store.path)
	sb, _ := after.StatsOf(b.ID)
	sa, _ := after.StatsOf(a.ID)
	if sb.Matches != 3 || sb.Wins != 3 || sb.Losses != 0 || sb.Streak != 3 || sb.BestStreak != 3 || len(sb.Recent) != 3 {
		t.Fatalf("loser after: %+v", sb)
	}
	for _, l := range sb.Recent {
		if l.Result != "win" {
			t.Fatalf("loser's history: %+v", sb.Recent)
		}
	}
	// a's win at the crash was the last of its run (a lost the match after): no streak to cut
	if sa.Matches != 3 || sa.Wins != 0 || sa.Losses != 3 || sa.Streak != 0 || sa.BestStreak != 1 || len(sa.Recent) != 3 {
		t.Fatalf("opponent after: %+v", sa)
	}
	if after.accounts[b.ID].Stats.Fighters["22"] != 3 || after.accounts[a.ID].Stats.Fighters["55"] != 3 {
		t.Fatalf("fighters %v / %v", after.accounts[b.ID].Stats.Fighters, after.accounts[a.ID].Stats.Fighters)
	}
}

func TestVoidLossWinnersStreakAndOldHistory(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	t0 := now.Add(-time.Hour)
	crash := t0
	played(t, store, a, b, "ROOM-7-1", 0, crash) // a wins (b crashed), then wins again
	played(t, store, a, b, "ROOM-7-2", 0, t0.Add(time.Minute))
	// b's history: loss, loss. The voided one is the older: it did not end b's current run (0)
	changed, err := store.VoidLoss(b.ID, crash)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(changed[0], "streak left as it was") {
		t.Fatalf("changed %q", changed)
	}
	sa, _ := store.StatsOf(a.ID)
	sb, _ := store.StatsOf(b.ID)
	if sa.Wins != 1 || sa.Streak != 1 || sa.BestStreak != 2 || sb.Losses != 1 || sb.Streak != 0 {
		t.Fatalf("a %+v, b %+v", sa, sb)
	}
	if _, err := store.VoidLoss(999, crash); err == nil {
		t.Fatal("voided on a missing account")
	}
	if lines, _ := store.RecentLines(b.ID); len(lines) != 1 || !strings.Contains(lines[0], "loss: One on One at Club") {
		t.Fatalf("lines %q", lines)
	}
}
