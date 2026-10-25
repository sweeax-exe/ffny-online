package accounts

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestTicketsAreOneTime(t *testing.T) {
	store, a, _, now := twoAccounts(t)
	tk := store.NewTicket(a.ID, now)
	if len(tk) != 32 {
		t.Fatalf("ticket %q", tk)
	}
	got, r, ok := store.RedeemTicket(strings.ToUpper(tk), now.Add(time.Second))
	if !ok || got.ID != a.ID || r != NewRating() {
		t.Fatalf("redeem: %v %+v %+v", ok, got, r)
	}
	if _, _, ok := store.RedeemTicket(tk, now); ok {
		t.Fatal("a ticket worked twice")
	}
	late := store.NewTicket(a.ID, now)
	if _, _, ok := store.RedeemTicket(late, now.Add(TicketLifetime+time.Second)); ok {
		t.Fatal("an expired ticket worked")
	}
}

// rankedGame pairs a and b (a in slot 0) under key, has both report a win of winner's slot and
// settles it.
func rankedGame(t *testing.T, store *Store, a, b Account, key string, winner int, at time.Time) {
	t.Helper()
	if !store.Pair(key[:15], map[int]int64{0: a.ID, 1: b.ID}, at) {
		t.Fatal("pairing refused")
	}
	store.Report(a.ID, oneOnOne(key, 0, winner), at)
	store.Report(b.ID, oneOnOne(key, 1, winner), at)
	if c, _ := store.Settle(at.Add(ReportWindow)); c != 1 {
		t.Fatal("not counted")
	}
}

func TestRankedGameMovesRatings(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	rankedGame(t, store, a, b, "ABC123-0000BEEF-1", 0, now)
	sa, _ := store.StatsOf(a.ID)
	sb, _ := store.StatsOf(b.ID)
	if sa.Ranked == nil || sb.Ranked == nil || sa.Ranked.Rating <= 1500 || sb.Ranked.Rating >= 1500 ||
		sa.Ranked.Wins != 1 || sb.Ranked.Losses != 1 || sa.Matches != 1 {
		t.Fatalf("after a ranked win: %+v / %+v", sa.Ranked, sb.Ranked)
	}
	// A casual match (no pairing) leaves the ratings alone.
	before := sa.Ranked.Rating
	store.Report(a.ID, oneOnOne("XYZ789-00000001-1", 0, 0), now)
	store.Report(b.ID, oneOnOne("XYZ789-00000001-1", 1, 0), now)
	store.Settle(now.Add(ReportWindow))
	if sa, _ = store.StatsOf(a.ID); sa.Ranked.Rating != before || sa.Ranked.Games != 1 || sa.Matches != 2 {
		t.Fatalf("a casual match moved the rating: %+v", sa.Ranked)
	}
}

func TestRankedNeedsBothPairedAccounts(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	c := store.Upsert("3", "third", "Third", "", now)
	// Seats swapped against the reports: not the pairing the relay made.
	store.Pair("ABC123-00000002", map[int]int64{0: b.ID, 1: a.ID}, now)
	store.Report(a.ID, oneOnOne("ABC123-00000002-1", 0, 0), now)
	store.Report(b.ID, oneOnOne("ABC123-00000002-1", 1, 0), now)
	// Only one of the two reported.
	store.Pair("ABC123-00000003", map[int]int64{0: a.ID, 1: c.ID}, now)
	store.Report(a.ID, oneOnOne("ABC123-00000003-1", 0, 0), now)
	store.Settle(now.Add(ReportWindow))
	if sa, _ := store.StatsOf(a.ID); sa.Ranked != nil {
		t.Fatalf("ranked without the pairing: %+v", sa.Ranked)
	}
	if store.Pair("ABC123-00000004", map[int]int64{0: a.ID, 1: a.ID}, now) || store.Pair("short", map[int]int64{0: a.ID, 1: b.ID}, now) {
		t.Fatal("bad pairings taken")
	}
}

func TestRankedDailyLimitPerPair(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	now = time.Date(2026, 10, 21, 12, 0, 0, 0, time.UTC)
	var last float64
	for i := 0; i < RankedPairDaily+2; i++ {
		at := now.Add(time.Duration(i) * time.Minute)
		rankedGame(t, store, a, b, "ABC123-0000000A-"+string(rune('1'+i)), 0, at)
		sa, _ := store.StatsOf(a.ID)
		if i >= RankedPairDaily && float64(sa.Ranked.Rating) != last {
			t.Fatalf("game %d moved the rating past the daily limit", i+1)
		}
		last = float64(sa.Ranked.Rating)
	}
	if sa, _ := store.StatsOf(a.ID); sa.Ranked.Games != RankedPairDaily+2 {
		t.Fatalf("games %d", sa.Ranked.Games)
	}
	// The next day it counts again.
	rankedGame(t, store, a, b, "ABC123-0000000B-1", 0, now.Add(24*time.Hour))
	if sa, _ := store.StatsOf(a.ID); float64(sa.Ranked.Rating) == last {
		t.Fatal("the limit did not reset the next day")
	}
}

func TestRankedEndpoints(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	srv := NewServer(Config{PublicURL: "https://example.test"}, store)
	srv.now = func() time.Time { return now }
	token := store.NewSession(a.ID, now)

	req := httptest.NewRequest("POST", "/api/ranked/ticket", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	rec := httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, req)
	var tk struct {
		Ticket string `json:"ticket"`
	}
	if rec.Code != 200 || json.Unmarshal(rec.Body.Bytes(), &tk) != nil || len(tk.Ticket) != 32 {
		t.Fatalf("ticket: %d %s", rec.Code, rec.Body)
	}
	rec = httptest.NewRecorder()
	srv.Handler().ServeHTTP(rec, httptest.NewRequest("POST", "/api/ranked/ticket", nil))
	if rec.Code != http.StatusUnauthorized {
		t.Fatalf("ticket without a session: %d", rec.Code)
	}

	in := srv.Internal()
	rec = httptest.NewRecorder()
	in.ServeHTTP(rec, httptest.NewRequest("POST", "/internal/ranked/redeem", strings.NewReader(`{"ticket":"`+tk.Ticket+`"}`)))
	var red struct {
		Account int64   `json:"account"`
		Rating  float64 `json:"rating"`
	}
	if rec.Code != 200 || json.Unmarshal(rec.Body.Bytes(), &red) != nil || red.Account != a.ID || red.Rating != RatingStart {
		t.Fatalf("redeem: %d %s", rec.Code, rec.Body)
	}
	rec = httptest.NewRecorder()
	in.ServeHTTP(rec, httptest.NewRequest("POST", "/internal/ranked/redeem", strings.NewReader(`{"ticket":"`+tk.Ticket+`"}`)))
	if rec.Code != http.StatusNotFound {
		t.Fatalf("second redeem: %d", rec.Code)
	}

	body := `{"key":"ABC123-0000CAFE","seats":[{"slot":0,"account":` + jsonInt(a.ID) + `},{"slot":1,"account":` + jsonInt(b.ID) + `}]}`
	rec = httptest.NewRecorder()
	in.ServeHTTP(rec, httptest.NewRequest("POST", "/internal/ranked/pairing", strings.NewReader(body)))
	if rec.Code != http.StatusNoContent {
		t.Fatalf("pairing: %d %s", rec.Code, rec.Body)
	}
	store.Report(a.ID, oneOnOne("ABC123-0000CAFE-1", 0, 1), now)
	store.Report(b.ID, oneOnOne("ABC123-0000CAFE-1", 1, 1), now)
	store.Settle(now.Add(ReportWindow))
	if sb, _ := store.StatsOf(b.ID); sb.Ranked == nil || sb.Ranked.Wins != 1 {
		t.Fatalf("ranked win through the endpoints: %+v", sb.Ranked)
	}
}

func jsonInt(n int64) string { b, _ := json.Marshal(n); return string(b) }

func TestRankedNoContestMovesNoRating(t *testing.T) {
	store, a, b, now := twoAccounts(t)
	key := "ABC123-0000CAFE-1"
	if !store.Pair(key[:15], map[int]int64{0: a.ID, 1: b.ID}, now) {
		t.Fatal("pairing refused")
	}
	started(store, a, b, key, now)
	store.Report(b.ID, event(oneOnOne(key, 1, 0), eventCrashed), now)
	store.Report(a.ID, event(oneOnOne(key, 0, 0), eventLeft, 1), now)
	if c, d := store.Settle(now.Add(ReportWindow)); c != 0 || d != 1 {
		t.Fatalf("settle: %d counted, %d dropped", c, d)
	}
	for _, id := range []int64{a.ID, b.ID} {
		if st, _ := store.StatsOf(id); st.Ranked != nil || st.Matches != 0 {
			t.Fatalf("account %d after a no contest: %+v", id, st)
		}
	}
}
