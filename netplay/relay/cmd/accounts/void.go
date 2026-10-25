package main

import (
	"fmt"
	"os"
	"time"

	"defjam/netplay/relay/accounts"
)

// runVoidLoss is -void-loss: an offline repair of one match's statistics (accounts.VoidLoss),
// for a loss the owner decided should not count (a player's game crashed before build 27 made
// that a no contest by itself). It copies the data file aside before it writes it.
//
//	accounts -data accounts.json -void-loss 12                                # the history, to pick a line
//	accounts -data accounts.json -void-loss 12 -at 2026-10-25T14:03:07Z -dry-run
//	accounts -data accounts.json -void-loss 12 -at 2026-10-25T14:03:07Z
func runVoidLoss(data string, id int64, at string, dryRun bool) int {
	store, err := accounts.Open(data)
	if err != nil {
		fmt.Fprintf(os.Stderr, "accounts: %v\n", err)
		return 1
	}
	if at == "" {
		lines, err := store.RecentLines(id)
		if err != nil {
			fmt.Fprintf(os.Stderr, "accounts: %v\n", err)
			return 1
		}
		fmt.Printf("account %d, newest first (pass a loss's time as -at):\n", id)
		for _, l := range lines {
			fmt.Println("  " + l)
		}
		return 0
	}
	when, err := time.Parse(time.RFC3339, at)
	if err != nil {
		fmt.Fprintf(os.Stderr, "accounts: -at: %v\n", err)
		return 2
	}
	changed, err := store.VoidLoss(id, when)
	if err != nil {
		fmt.Fprintf(os.Stderr, "accounts: %v\n", err)
		if lines, lerr := store.RecentLines(id); lerr == nil {
			fmt.Fprintf(os.Stderr, "its history, newest first:\n")
			for _, l := range lines {
				fmt.Fprintln(os.Stderr, "  "+l)
			}
		}
		return 1
	}
	for _, c := range changed {
		fmt.Println(c)
	}
	if dryRun {
		fmt.Println("dry run: nothing written")
		return 0
	}
	raw, err := os.ReadFile(data)
	if err != nil {
		fmt.Fprintf(os.Stderr, "accounts: backup: %v\n", err)
		return 1
	}
	backup := data + ".before-void-" + time.Now().UTC().Format("20060102-150405")
	if err := os.WriteFile(backup, raw, 0o600); err != nil {
		fmt.Fprintf(os.Stderr, "accounts: backup: %v\n", err)
		return 1
	}
	if err := store.Save(); err != nil {
		fmt.Fprintf(os.Stderr, "accounts: save: %v (the file is unchanged; backup %s)\n", err, backup)
		return 1
	}
	fmt.Printf("saved %s (backup %s)\n", data, backup)
	return 0
}
