"""Where the relay's Go source is for the protocol cross-checks: the private server repository
(sweeax-exe/ffny-server) cloned as <checkout>/server, or the folder FFNY_SERVER_DIR names; a worktree
uses the main checkout's. RELAY is None without it, and the cross-checks skip."""
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def _relay():
    roots = [Path(os.environ['FFNY_SERVER_DIR'])] if os.environ.get('FFNY_SERVER_DIR') else []
    roots.append(ROOT / 'server')
    try:
        common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=ROOT,
                                capture_output=True, text=True, check=True).stdout.strip()
        roots.append(Path(common).parent / 'server')
    except (OSError, subprocess.CalledProcessError):
        pass
    for root in roots:
        if (root / 'relay' / 'go.mod').is_file():
            return root / 'relay'
    return None


RELAY = _relay()
SKIP = 'the relay source (private server repository) is not checked out'
