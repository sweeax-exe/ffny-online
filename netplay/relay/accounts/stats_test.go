package accounts

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func oneOnOne(key string, you int, winner int) MatchReport {
	return MatchReport{Key: key, Mode: 0, ModeName: "One on One", Venue: 19, VenueName: "Club", You: you,
		Players: []MatchPlayer{{Slot: 0, Name: "Masa", Fighter: 55, FName: "Fighter A"},
			{Slot: 1, Name: "DMX", Fighter: 22, FName: "Fighter B"}},
		Winners: []int{winner}}
}

func twoAccounts(t *testing.T) (*Store, Account, Account, time.Time) {
	store, err := Open(filepath.Join(t.TempDir(), "a.json"))
	if err != nil {
		t.Fatal(err)
	}
	now := time.Now()
	return store, store.Upsert("1", "masa", "Masa", "", now), store.Upsert("2", "dmx", "DMX", "", now), now
}

func TestAgreeingReportsCount(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	if !store.Report(a.ID, oneOnOne("ROOM-1-1", 0, 0), now) || !store.Report(b.ID, oneOnOne("ROOM-1-1", 1, 0), now) {
		t.Fatal("reports refused")
	}
	if c, d := store.Settle(now.Add(ReportWindow / 2)); c+d != 0 {
		t.Fatal("settled before the window ended")
	}
	if c, d := store.Settle(now.Add(ReportWindow)); c != 1 || d != 0 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	sa, _ := store.StatsOf(a.ID)
	sb, _ := store.StatsOf(b.ID)
	if sa.Wins != 1 || sa.Losses != 0 || sa.Streak != 1 || sa.FavoriteName != "Fighter A" || sa.WinRate != 100 {
		t.Fatalf("winner %+v", sa)
	}
	if sb.Wins != 0 || sb.Losses != 1 || sb.Recent[0].Result != "loss" || sb.Recent[0].Opponents[0] != "Masa" ||
		sb.Recent[0].Fighter != "Fighter B" || sb.Recent[0].Venue != "Club" {
		t.Fatalf("loser %+v", sb)
	}
}

func TestDisagreeingReportsAreDropped(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	store.Report(a.ID, oneOnOne("ROOM-1-2", 0, 0), now)
	store.Report(b.ID, oneOnOne("ROOM-1-2", 1, 1), now) // both claim the win
	if c, d := store.Settle(now.Add(ReportWindow)); c != 0 || d != 1 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	if st, _ := store.StatsOf(a.ID); st.Matches != 0 {
		t.Fatal("a disputed match counted")
	}
}

func event(r MatchReport, kind string, left ...int) MatchReport {
	r.Event, r.Left = kind, left
	if kind != eventResult {
		r.Winners = nil
	}
	return r
}

func TestOneSidedResultDoesNotCount(t *testing.T) {
	// the opponent not signed in (or its game never reported): no free win
	store, a, _, now := twoAccounts(t)
	store.Report(a.ID, oneOnOne("ROOM-3-1", 0, 0), now)
	if c, d := store.Settle(now.Add(ReportWindow)); c != 0 || d != 1 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	if st, _ := store.StatsOf(a.ID); st.Matches != 0 {
		t.Fatal("a one-sided result counted")
	}
}

func TestStartThenResultCounts(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	store.Report(a.ID, event(oneOnOne("ROOM-3-2", 0, 0), eventStart), now)
	store.Report(b.ID, event(oneOnOne("ROOM-3-2", 1, 0), eventStart), now)
	if c, d := store.Settle(now.Add(10 * time.Minute)); c+d != 0 {
		t.Fatal("settled on the starts alone")
	}
	later := now.Add(3 * time.Minute)
	store.Report(a.ID, event(oneOnOne("ROOM-3-2", 0, 1), eventResult), later)
	store.Report(b.ID, oneOnOne("ROOM-3-2", 1, 1), later) // a build-22 style result
	if c, d := store.Settle(later.Add(ReportWindow)); c != 1 || d != 0 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	if sb, _ := store.StatsOf(b.ID); sb.Wins != 1 {
		t.Fatalf("winner %+v", sb)
	}
}

func TestLeaverTakesTheLoss(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	store.Report(a.ID, event(oneOnOne("ROOM-3-3", 0, 0), eventStart), now)
	store.Report(b.ID, event(oneOnOne("ROOM-3-3", 1, 0), eventStart), now)
	store.Report(a.ID, event(oneOnOne("ROOM-3-3", 0, 0), eventLeft, 1), now.Add(time.Minute))
	if c, d := store.Settle(now.Add(time.Minute + ReportWindow)); c != 1 || d != 0 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	sa, _ := store.StatsOf(a.ID)
	sb, _ := store.StatsOf(b.ID)
	if sa.Wins != 1 || sb.Losses != 1 || sb.Recent[0].Result != "loss" || sb.Recent[0].Opponents[0] != "Masa" {
		t.Fatalf("stayed %+v, left %+v", sa, sb)
	}
}

func TestLeaverWithoutStartOrBothClaimingCountsNothing(t *testing.T) {
	// the one who left never said it started (not signed in): nothing
	store, a, b, now := twoAccounts(t)
	store.Report(a.ID, event(oneOnOne("ROOM-3-4", 0, 0), eventStart), now)
	store.Report(a.ID, event(oneOnOne("ROOM-3-4", 0, 0), eventLeft, 1), now)
	// each game says the other one left (the connection between them broke): nothing
	store.Report(a.ID, event(oneOnOne("ROOM-3-5", 0, 0), eventStart), now)
	store.Report(b.ID, event(oneOnOne("ROOM-3-5", 1, 0), eventStart), now)
	store.Report(a.ID, event(oneOnOne("ROOM-3-5", 0, 0), eventLeft, 1), now)
	store.Report(b.ID, event(oneOnOne("ROOM-3-5", 1, 0), eventLeft, 0), now)
	// a result and a "left" for one match disagree: nothing
	store.Report(a.ID, event(oneOnOne("ROOM-3-6", 0, 0), eventStart), now)
	store.Report(b.ID, event(oneOnOne("ROOM-3-6", 1, 0), eventStart), now)
	store.Report(a.ID, event(oneOnOne("ROOM-3-6", 0, 0), eventLeft, 1), now)
	store.Report(b.ID, oneOnOne("ROOM-3-6", 1, 1), now)
	if c, d := store.Settle(now.Add(ReportWindow)); c != 0 || d != 3 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	for _, id := range []int64{a.ID, b.ID} {
		if st, _ := store.StatsOf(id); st.Matches != 0 {
			t.Fatalf("account %d: %+v", id, st)
		}
	}
	if store.Report(a.ID, event(oneOnOne("ROOM-3-7", 0, 0), eventLeft, 0), now) {
		t.Fatal("took a report that says its own player left")
	}
}

// started reports the start of a one on one from both accounts.
func started(store *Store, a, b Account, key string, now time.Time) {
	store.Report(a.ID, event(oneOnOne(key, 0, 0), eventStart), now)
	store.Report(b.ID, event(oneOnOne(key, 1, 0), eventStart), now)
}

func TestCrashedLeaverIsNoContest(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	started(store, a, b, "ROOM-4-1", now)
	// b's game crashes mid-fight and says so; a's game reports b gone a few seconds later
	if !store.Report(b.ID, event(oneOnOne("ROOM-4-1", 1, 0), eventCrashed), now.Add(time.Minute)) {
		t.Fatal("crash report refused")
	}
	store.Report(a.ID, event(oneOnOne("ROOM-4-1", 0, 0), eventLeft, 1), now.Add(time.Minute+10*time.Second))
	if c, d := store.Settle(now.Add(time.Minute + ReportWindow)); c+d != 0 {
		t.Fatal("settled before the window after the left report ended")
	}
	if c, d := store.Settle(now.Add(time.Minute + 10*time.Second + ReportWindow)); c != 0 || d != 1 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	for _, id := range []int64{a.ID, b.ID} {
		if st, _ := store.StatsOf(id); st.Matches != 0 || st.Wins != 0 || st.Losses != 0 || len(st.Recent) != 0 {
			t.Fatalf("account %d: %+v", id, st)
		}
	}
	drops := store.TakeDrops()
	if len(drops) != 1 || !strings.HasPrefix(drops[0], "no contest: slot 1 (DMX)'s game crashed") {
		t.Fatalf("drops %q", drops)
	}
}

func TestQuitWithoutCrashStillLoses(t *testing.T) {
	// the crash report names the other match: this one is an ordinary leave
	store, a, b, now := twoAccounts(t)
	started(store, a, b, "ROOM-4-2", now)
	started(store, a, b, "ROOM-4-3", now)
	store.Report(b.ID, event(oneOnOne("ROOM-4-3", 1, 0), eventCrashed), now)
	store.Report(a.ID, event(oneOnOne("ROOM-4-2", 0, 0), eventLeft, 1), now)
	if c, _ := store.Settle(now.Add(ReportWindow)); c != 1 {
		t.Fatalf("settle: %d counted", c)
	}
	sa, _ := store.StatsOf(a.ID)
	sb, _ := store.StatsOf(b.ID)
	if sa.Wins != 1 || sa.Matches != 1 || sb.Losses != 1 || sb.Matches != 1 {
		t.Fatalf("stayed %+v, left %+v", sa, sb)
	}
}

func TestCrashReportFromAPlayerWhoStayedChangesNothing(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	started(store, a, b, "ROOM-4-4", now)
	store.Report(a.ID, event(oneOnOne("ROOM-4-4", 0, 0), eventLeft, 1), now)
	store.Report(a.ID, event(oneOnOne("ROOM-4-4", 0, 0), eventCrashed), now.Add(time.Second)) // the stayer, later
	if c, d := store.Settle(now.Add(ReportWindow)); c != 1 || d != 0 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	sa, _ := store.StatsOf(a.ID)
	sb, _ := store.StatsOf(b.ID)
	if sa.Wins != 1 || sb.Losses != 1 {
		t.Fatalf("stayed %+v, left %+v", sa, sb)
	}
}

func TestCrashAfterTheResultsChangesNothing(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	started(store, a, b, "ROOM-4-5", now)
	store.Report(a.ID, event(oneOnOne("ROOM-4-5", 0, 0), eventResult), now)
	store.Report(b.ID, event(oneOnOne("ROOM-4-5", 1, 0), eventResult), now)
	store.Report(b.ID, event(oneOnOne("ROOM-4-5", 1, 0), eventCrashed), now.Add(time.Second)) // within the window
	if c, d := store.Settle(now.Add(ReportWindow)); c != 1 || d != 0 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	// and one after the match was settled opens nothing that counts
	store.Report(b.ID, event(oneOnOne("ROOM-4-5", 1, 0), eventCrashed), now.Add(ReportWindow+time.Second))
	if c, d := store.Settle(now.Add(StartLifetime + time.Hour)); c != 0 || d != 0 {
		t.Fatalf("late crash report: %d counted, %d dropped", c, d)
	}
	sa, _ := store.StatsOf(a.ID)
	sb, _ := store.StatsOf(b.ID)
	if sa.Wins != 1 || sa.Matches != 1 || sb.Losses != 1 || sb.Matches != 1 {
		t.Fatalf("winner %+v, loser %+v", sa, sb)
	}
}

func TestCrashReportShape(t *testing.T) {
	store, a, _, now := twoAccounts(t)
	r := event(oneOnOne("ROOM-4-6", 0, 0), eventCrashed)
	r.Winners = []int{0}
	if store.Report(a.ID, r, now) {
		t.Fatal("took a crash report with a winner")
	}
	if store.Report(a.ID, event(oneOnOne("ROOM-4-6", 0, 0), eventCrashed, 1), now) {
		t.Fatal("took a crash report naming a leaver")
	}
}

// ffa is a free for all of the given humans (slots 0..) and CPU fighters up to four.
func ffa(key string, you, humans int) MatchReport {
	names := []string{"Masa", "DMX", "Redman", "Ice-T"}
	r := MatchReport{Key: key, Mode: 3, ModeName: "Free for All", Venue: 19, VenueName: "Club", You: you}
	for i := 0; i < 4; i++ {
		r.Players = append(r.Players, MatchPlayer{Slot: i, Name: names[i], Fighter: 10 + i, CPU: i >= humans})
	}
	return r
}

func TestFreeForAllCrashAndQuit(t *testing.T) {
	store, err := Open(filepath.Join(t.TempDir(), "a.json"))
	if err != nil {
		t.Fatal(err)
	}
	now := time.Now()
	var ids []int64
	for i := 0; i < 3; i++ {
		ids = append(ids, store.Upsert(string(rune('1'+i)), "p", "P", "", now).ID)
	}
	for i, id := range ids {
		store.Report(id, event(ffa("ROOM-5-1", i, 3), eventStart), now)
		store.Report(id, event(ffa("ROOM-5-2", i, 3), eventStart), now)
	}
	// ROOM-5-1: slot 2 crashes, the other two stay: nobody counts
	store.Report(ids[2], event(ffa("ROOM-5-1", 2, 3), eventCrashed), now)
	store.Report(ids[0], event(ffa("ROOM-5-1", 0, 3), eventLeft, 2), now)
	store.Report(ids[1], event(ffa("ROOM-5-1", 1, 3), eventLeft, 2), now)
	// ROOM-5-2: slot 2 crashes and slot 1 quits: slot 1 takes the loss, nobody else counts
	store.Report(ids[2], event(ffa("ROOM-5-2", 2, 3), eventCrashed), now)
	store.Report(ids[0], event(ffa("ROOM-5-2", 0, 3), eventLeft, 1, 2), now)
	if c, d := store.Settle(now.Add(ReportWindow)); c != 1 || d != 1 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	want := []struct{ matches, losses int }{{0, 0}, {1, 1}, {0, 0}}
	for i, id := range ids {
		st, _ := store.StatsOf(id)
		if st.Matches != want[i].matches || st.Losses != want[i].losses || st.Wins != 0 {
			t.Fatalf("slot %d: %+v", i, st)
		}
	}
}

func TestBadReportsAndFlooding(t *testing.T) {
	store, a, _, now := twoAccounts(t)
	bad := oneOnOne("ROOM-1-3", 3, 0) // reporter not in the match
	if store.Report(a.ID, bad, now) {
		t.Fatal("took a report from outside the match")
	}
	bad = oneOnOne("ROOM-1-3", 0, 2) // winner not in the match
	if store.Report(a.ID, bad, now) {
		t.Fatal("took an impossible winner")
	}
	n := 0
	for i := 0; i < maxReportsPerHour+5; i++ {
		if store.Report(a.ID, oneOnOne("FLOOD-"+string(rune('A'+i%26))+string(rune('a'+i/26)), 0, 0), now) {
			n++
		}
	}
	if n != maxReportsPerHour {
		t.Fatalf("took %d reports in an hour", n)
	}
}

func TestLeaderboardAndStatsRoutes(t *testing.T) {
	s, store, h := newTestServer(t, discordUser{ID: "42", Username: "masa"})
	now := time.Now()
	s.now = func() time.Time { return now }
	a := store.Upsert("42", "masa", "Masa", "", now)
	b := store.Upsert("43", "dmx", "DMX", "", now)
	tokA := store.NewSession(a.ID, now)
	for i := 0; i < LeaderboardMin; i++ {
		key := "ROOM-2-" + string(rune('a'+i))
		body, _ := json.Marshal(oneOnOne(key, 0, 0))
		if r := do(h, "POST", "/api/matches", string(body), tokA); r.Code != http.StatusAccepted {
			t.Fatalf("report: %d %s", r.Code, r.Body.String())
		}
		store.Report(b.ID, oneOnOne(key, 1, 0), now)
	}
	now = now.Add(ReportWindow)
	s.Settle()
	r := do(h, "GET", "/api/me/stats", "", tokA)
	var st PublicStats
	json.Unmarshal(r.Body.Bytes(), &st)
	if r.Code != http.StatusOK || st.Wins != LeaderboardMin || st.Rank != 1 || st.BestStreak != LeaderboardMin {
		t.Fatalf("my stats: %d %+v", r.Code, st)
	}
	r = do(h, "GET", "/api/leaderboard", "", "")
	var lb struct {
		Entries []LeaderboardEntry `json:"entries"`
	}
	json.Unmarshal(r.Body.Bytes(), &lb)
	if len(lb.Entries) != 2 || lb.Entries[0].Name != "Masa" || lb.Entries[1].Name != "DMX" || lb.Entries[0].Discord != "" {
		t.Fatalf("leaderboard %+v", lb.Entries)
	}
	if r := do(h, "POST", "/api/matches", "{}", ""); r.Code != http.StatusUnauthorized {
		t.Fatalf("report without sign-in: %d", r.Code)
	}
	// the bot's listener names the Discord ids and finds a player by theirs
	in := s.Internal()
	rec := httptest.NewRecorder()
	in.ServeHTTP(rec, httptest.NewRequest("GET", "/internal/discord/43", nil))
	json.Unmarshal(rec.Body.Bytes(), &st)
	if rec.Code != http.StatusOK || st.Losses != LeaderboardMin || st.Rank != 2 {
		t.Fatalf("internal profile: %d %+v", rec.Code, st)
	}
	rec = httptest.NewRecorder()
	in.ServeHTTP(rec, httptest.NewRequest("GET", "/internal/leaderboard", nil))
	json.Unmarshal(rec.Body.Bytes(), &lb)
	if lb.Entries[0].Discord != "42" {
		t.Fatalf("internal leaderboard %+v", lb.Entries)
	}
	// statistics survive a restart
	store.Save()
	again, _ := Open(store.path)
	if st, _ := again.StatsOf(a.ID); st.Wins != LeaderboardMin || len(st.Recent) != LeaderboardMin {
		t.Fatalf("after reload %+v", st)
	}
}

func TestStatsText(t *testing.T) {
	st := PublicStats{Account: PublicAccount{Name: "Ma|sa"}, Matches: 2, Wins: 1, Losses: 1, WinRate: 50, Favorite: 55,
		FavoriteName: "Fighter A", Rank: 3,
		Recent: []MatchLine{{Result: "win", Fighter: "Fighter A", Opponents: []string{"DMX", "CPU"}, Venue: "Club"}}}
	got := StatsText(st, []LeaderboardEntry{{Rank: 1, Name: "Top", Wins: 9, Losses: 2, WinRate: 82}})
	for _, want := range []string{"name=Ma sa\n", "wins=1\n", "rank=3\n", "favorite=55\n", "favorite_name=Fighter A\n",
		"recent=win|Fighter A|DMX, CPU|Club\n", "board=1|Top|9|2|82\n"} {
		if !strings.Contains(got, want) {
			t.Errorf("missing %q in\n%s", want, got)
		}
	}
}

func TestResetStats(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	store.Report(a.ID, oneOnOne("ROOM-9-1", 0, 0), now)
	store.Report(b.ID, oneOnOne("ROOM-9-1", 1, 0), now)
	store.Settle(now.Add(ReportWindow))
	token := store.NewSession(a.ID, now)
	if n := store.ResetStats(); n != 2 {
		t.Fatalf("reset %d accounts", n)
	}
	if err := store.Save(); err != nil {
		t.Fatal(err)
	}
	again, err := Open(store.path)
	if err != nil {
		t.Fatal(err)
	}
	if st, _ := again.StatsOf(a.ID); st.Matches != 0 || st.Wins != 0 || len(st.Recent) != 0 {
		t.Fatalf("stats survived the reset: %+v", st)
	}
	if _, ok := again.Session(token, now); !ok {
		t.Fatal("the reset signed the player out")
	}
	if len(again.Leaderboard(10, false)) != 0 {
		t.Fatal("leaderboard not empty")
	}
}

func TestLeaderboardPages(t *testing.T) {
	store, err := Open(filepath.Join(t.TempDir(), "a.json"))
	if err != nil {
		t.Fatal(err)
	}
	now := time.Now()
	var ids []int64
	for i := 0; i < 23; i++ {
		ids = append(ids, store.Upsert(string(rune('a'+i)), "p", "P", "", now).ID)
	}
	// player i wins i+3 matches against the next one
	for i, id := range ids {
		for k := 0; k < i+3; k++ {
			key := "ROOM" + string(rune('A'+i)) + "-" + string(rune('a'+k)) + "-1"
			store.Report(id, oneOnOne(key, 0, 0), now)
			store.Report(ids[(i+1)%len(ids)], oneOnOne(key, 1, 0), now)
		}
	}
	store.Settle(now.Add(ReportWindow))
	first, total := store.LeaderboardPage(0, LeaderboardPageSize, false)
	third, _ := store.LeaderboardPage(20, LeaderboardPageSize, false)
	if total != 23 || len(first) != 10 || first[0].Rank != 1 || len(third) != 3 || third[0].Rank != 21 {
		t.Fatalf("total %d, first %d (rank %d), third %d", total, len(first), first[0].Rank, len(third))
	}
	if first[0].Wins < first[9].Wins {
		t.Fatal("not sorted")
	}
}
