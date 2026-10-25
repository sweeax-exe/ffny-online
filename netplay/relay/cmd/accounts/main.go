// Command accounts is FFNY Online's account service: sign-in with Discord for the launcher.
// It listens on a local port behind the HTTPS proxy (deploy/Caddyfile) and keeps its data in
// one JSON file. A second local listener (-internal-listen) serves the Discord bot.
//
//	accounts -listen 127.0.0.1:8090 -data /var/lib/ffny-accounts/accounts.json \
//	         -public-url https://play.defjam.online -client-id ID -client-secret-file FILE
//
// Offline, with the service stopped: -reset-stats (a new season), -void-loss ID -at TIME (one
// loss becomes a no contest; void.go).
package main

import (
	"context"
	"errors"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"defjam/netplay/relay/accounts"
)

func main() {
	listen := flag.String("listen", "127.0.0.1:8090", "HTTP address (the HTTPS proxy forwards to it)")
	data := flag.String("data", "accounts.json", "the accounts file")
	publicURL := flag.String("public-url", "https://play.defjam.online", "the address players reach the service at")
	clientID := flag.String("client-id", "", "the Discord application's client id")
	secretFile := flag.String("client-secret-file", "", "file holding the Discord client secret")
	internal := flag.String("internal-listen", "127.0.0.1:8091", "local address for the Discord bot (empty: off)")
	reportDir := flag.String("reports", "", "folder for the players' bug reports (empty: off)")
	botTokenFile := flag.String("bot-token-file", "", "file holding the FFNY Online bot's token: server nicknames become player names (empty: off)")
	guildID := flag.String("guild-id", "", "our Discord server's id, for the nicknames")
	resetStats := flag.Bool("reset-stats", false, "clear every account's casual statistics in -data (a new leaderboard season), save and exit; stop the service first")
	voidLoss := flag.Int64("void-loss", 0, "account id: turn its loss at -at into a no contest (and the one on one opponent's win) in -data, save and exit; without -at, list its history; stop the service first")
	voidAt := flag.String("at", "", "with -void-loss: the loss's time as its history shows it (RFC 3339, within 5 s)")
	dryRun := flag.Bool("dry-run", false, "with -void-loss: print what would change, write nothing")
	flag.Parse()

	if *voidLoss != 0 {
		os.Exit(runVoidLoss(*data, *voidLoss, *voidAt, *dryRun))
	}

	if *resetStats {
		store, err := accounts.Open(*data)
		if err != nil {
			log.Fatalf("accounts: %v", err)
		}
		n := store.ResetStats()
		if err := store.Save(); err != nil {
			log.Fatalf("accounts: save: %v", err)
		}
		log.Printf("accounts: statistics of %d account(s) cleared in %s", n, *data)
		return
	}

	secret, err := os.ReadFile(*secretFile)
	if err != nil || *clientID == "" {
		log.Fatalf("accounts: need -client-id and a readable -client-secret-file (%v)", err)
	}
	store, err := accounts.Open(*data)
	if err != nil {
		log.Fatalf("accounts: %v", err)
	}
	var reports *accounts.Reports
	if *reportDir != "" {
		if reports, err = accounts.OpenReports(*reportDir); err != nil {
			log.Fatalf("accounts: reports: %v", err)
		}
	}
	var botToken []byte
	if *botTokenFile != "" {
		if botToken, err = os.ReadFile(*botTokenFile); err != nil {
			log.Fatalf("accounts: bot token: %v", err)
		}
	}
	srv := accounts.NewServer(accounts.Config{PublicURL: strings.TrimRight(*publicURL, "/"), ClientID: *clientID,
		ClientSecret: strings.TrimSpace(string(secret)), Reports: reports,
		BotToken: strings.TrimSpace(string(botToken)), GuildID: strings.TrimSpace(*guildID)}, store)
	httpSrv := &http.Server{Addr: *listen, Handler: srv.Handler(), ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout: 2 * time.Minute, WriteTimeout: 30 * time.Second, MaxHeaderBytes: 16 << 10}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	go srv.RefreshNicks(ctx, 30*time.Minute) // server nicknames: renames reach the leaderboard
	go func() {
		t := time.NewTicker(2 * time.Second)
		defer t.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-t.C:
				srv.Settle()
				if err := store.Save(); err != nil {
					log.Printf("accounts: save: %v", err)
				}
			}
		}
	}()
	go func() {
		<-ctx.Done()
		shut, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		httpSrv.Shutdown(shut)
	}()
	if *internal != "" {
		in := &http.Server{Addr: *internal, Handler: srv.Internal(), ReadHeaderTimeout: 10 * time.Second}
		go func() {
			if err := in.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
				log.Printf("accounts: internal listener: %v", err)
			}
		}()
		go func() { <-ctx.Done(); in.Close() }()
	}
	log.Printf("accounts: %d accounts, listening on %s for %s", store.Count(), *listen, *publicURL)
	if err := httpSrv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatalf("accounts: %v", err)
	}
	if err := store.Save(); err != nil {
		log.Printf("accounts: save: %v", err)
	}
}
