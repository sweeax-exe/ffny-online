"""Lists and fetches the players' bug reports from the account service's server.

F9 in the game, or "Send crash report" in the launcher after a crash, sends the run's log and
(F9) a picture to POST /api/report (server/relay/accounts/reports.go). The service keeps each
report as a folder <reports>/<YYYYMMDD-HHMMSS>-<id>/ with info.txt, log.txt and picture.png.
info.txt has a "note:" line when the player wrote what is wrong in F9's box. A report sent from an
online match (no box there) may get its note after the match: note.txt in the same folder (build 68,
POST /api/report/note), shown here as "note (after the match)".

    python scripts/bug-reports.py list [--last 20]     # newest last: time, id, kind, build, player, note
    python scripts/bug-reports.py get ID [ID ...]      # copies them to logs/bug-reports/<folder>/
    python scripts/bug-reports.py new                  # fetches every report not fetched yet
    python scripts/bug-reports.py triage [--days 14]   # the reports grouped by signature (start of a session)

triage reads the account service's GET /internal/reports/triage (server/relay/accounts/triage.go) over ssh: each
signature (the crash's guest function+offset or native place, F9 reports by mode), its status (new, known,
fixed-in-build-N, regressed, ignored), reports in the span, players, builds, mode, examples and notes. Mark one
with the bot's /report-fixed <sig> <build> or /report-mark, or in <reports>/triage-marks.json on the server.

Needs ssh access to the server (host alias ffny-online, --host to change). The reports hold
players' logs: they stay under logs/, which is never committed.
"""
import argparse
import json
import os
import subprocess
import sys

REMOTE = "/var/lib/ffny-accounts/reports"
LOCAL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "logs", "bug-reports")


def remote_list(host):
    """[(folder, info dict)], oldest first."""
    script = ("cd %s 2>/dev/null || exit 0; for d in $(ls -1 | sort); do echo \"@@ $d\"; cat \"$d/info.txt\" 2>/dev/null; "
              "ls -1 \"$d\" | sed 's/^/file: /'; "
              "if [ -f \"$d/note.txt\" ]; then echo \"late_note: $(head -c 600 \"$d/note.txt\")\"; fi; "
              "done; true" % REMOTE)
    out = subprocess.run(["ssh", host, script], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if out.returncode != 0:
        sys.exit("ssh %s failed: %s" % (host, out.stderr.strip()))
    reports, cur = [], None
    for line in out.stdout.splitlines():
        if line.startswith("@@ "):
            cur = {"files": []}
            reports.append((line[3:].strip(), cur))
        elif cur is not None and line.startswith("file: "):
            cur["files"].append(line[6:])
        elif cur is not None and ": " in line:
            k, v = line.split(": ", 1)
            cur[k] = v
    return reports


TRIAGE_URL = "http://127.0.0.1:8091/internal/reports/triage?days=%d"   # the service's internal listener


def triage(host, days, show_all, raw):
    out = subprocess.run(["ssh", host, "curl -fsS '%s'" % (TRIAGE_URL % days)], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if out.returncode != 0:
        sys.exit("ssh %s failed: %s" % (host, (out.stderr or out.stdout).strip()))
    data = json.loads(out.stdout)
    if raw:
        print(json.dumps(data, indent=1))
        return
    sigs = data.get("signatures") or []
    print("%d report(s) in %d days, %d signature(s) (generated %s)" % (data.get("reports", 0), data.get("days", 0),
                                                                         len(sigs), data.get("generated", "?")))
    for v in sigs:
        if not show_all and v.get("class") in ("f9", "studio-report") and not v.get("notes"):
            continue
        builds = v.get("builds") or ["?"]
        span = builds[0] if len(builds) == 1 else "%s..%s" % (builds[0], builds[-1])
        print("%s %-18s %4d in span (%d ever, %d players)  build %-14s %-12s %s" % (
            v["id"], v.get("status", "?"), v.get("in_window", 0), v.get("count", 0), v.get("players", 0), span,
            v.get("class", "?"), v.get("where", "?")))
        extra = []
        if v.get("modes"):
            extra.append("mode " + ", ".join(v["modes"]))
        if v.get("faults"):
            extra.append("fault " + ", ".join(v["faults"]))
        if v.get("examples"):
            extra.append("e.g. " + " ".join(v["examples"][-2:]))
        if extra:
            print("    " + "; ".join(extra))
        for n in (v.get("notes") or [])[:2]:
            print("    note %s (build %s): %s" % (n.get("id"), n.get("build"), n.get("note")))
    events = data.get("events") or []
    if events:
        print("latest events:")
        for e in events[-8:]:
            print("  #%d %s %s %s report %s build %s: %s" % (e["seq"], e.get("time", "")[:16], e["type"], e["sig"],
                                                            e.get("report"), e.get("build"), e.get("why")))


def fetch(host, folder):
    dest = os.path.join(LOCAL, folder)
    os.makedirs(dest, exist_ok=True)
    r = subprocess.run(["scp", "-q", "-r", "%s:%s/%s/." % (host, REMOTE, folder), dest])
    if r.returncode != 0:
        print("could not fetch", folder)
        return None
    return dest


def main():
    # players' names can hold any character; a Windows console in cp1252 would stop the run halfway
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="ffny-online")
    sub = ap.add_subparsers(dest="cmd", required=True)
    ls = sub.add_parser("list")
    ls.add_argument("--last", type=int, default=20)
    get = sub.add_parser("get")
    get.add_argument("ids", nargs="+")
    sub.add_parser("new")
    tr = sub.add_parser("triage")
    tr.add_argument("--days", type=int, default=14)
    tr.add_argument("--all", action="store_true", help="also F9 and Studio groups without notes")
    tr.add_argument("--json", action="store_true", help="the service's JSON as it is")
    a = ap.parse_args()

    if a.cmd == "triage":
        triage(a.host, a.days, a.all, a.json)
        return

    reports = remote_list(a.host)
    if a.cmd == "list":
        for folder, info in reports[-a.last:]:
            pic = " +picture" if "picture.png" in info["files"] else ""
            print("%s  %-7s build %-4s %s%s" % (folder, info.get("kind", "?"), info.get("build", "?"),
                                                info.get("player", "?"), pic))
            if info.get("note"):
                print("    note: %s" % info["note"])
            if info.get("late_note"):
                print("    note (after the match): %s" % info["late_note"])
        print("%d report(s)" % len(reports))
        return
    if a.cmd == "get":
        want = [(f, i) for f, i in reports if any(f.endswith("-" + x) for x in a.ids)]
    else:
        want = [(f, i) for f, i in reports if not os.path.isdir(os.path.join(LOCAL, f))]
    for folder, info in want:
        dest = fetch(a.host, folder)
        if dest:
            print(dest, "|", info.get("kind"), "build", info.get("build"), "|", info.get("player"))
            if info.get("note"):
                print("    note:", info["note"])
            if info.get("late_note"):
                print("    note (after the match):", info["late_note"])
    if not want:
        print("nothing to fetch")


if __name__ == "__main__":
    main()
