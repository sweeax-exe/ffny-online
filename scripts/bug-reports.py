"""Lists and fetches the players' bug reports from the account service's server.

F9 in the game, or "Send crash report" in the launcher after a crash, sends the run's log and
(F9) a picture to POST /api/report (netplay/relay/accounts/reports.go). The service keeps each
report as a folder <reports>/<YYYYMMDD-HHMMSS>-<id>/ with info.txt, log.txt and picture.png.

    python scripts/bug-reports.py list [--last 20]     # newest last: time, id, kind, build, player
    python scripts/bug-reports.py get ID [ID ...]      # copies them to logs/bug-reports/<folder>/
    python scripts/bug-reports.py new                  # fetches every report not fetched yet

Needs ssh access to the server (host alias ffny-online, --host to change). The reports hold
players' logs: they stay under logs/, which is never committed.
"""
import argparse
import os
import subprocess
import sys

REMOTE = "/var/lib/ffny-accounts/reports"
LOCAL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "logs", "bug-reports")


def remote_list(host):
    """[(folder, info dict)], oldest first."""
    script = ("cd %s 2>/dev/null || exit 0; for d in $(ls -1 | sort); do echo \"@@ $d\"; cat \"$d/info.txt\" 2>/dev/null; "
              "ls -1 \"$d\" | sed 's/^/file: /'; done" % REMOTE)
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
    a = ap.parse_args()

    reports = remote_list(a.host)
    if a.cmd == "list":
        for folder, info in reports[-a.last:]:
            pic = " +picture" if "picture.png" in info["files"] else ""
            print("%s  %-7s build %-4s %s%s" % (folder, info.get("kind", "?"), info.get("build", "?"),
                                                info.get("player", "?"), pic))
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
    if not want:
        print("nothing to fetch")


if __name__ == "__main__":
    main()
