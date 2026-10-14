#!/usr/bin/env python3
"""Two small helpers for .github/workflows/ci.yml, kept here so they can be tested.

    python scripts/ci-digest.py changes --base SHA --head SHA     prints `code=true` or `code=false`
    python scripts/ci-digest.py summary                           reads the `needs` context from $NEEDS (JSON)

`changes` decides whether the expensive jobs (three setup builds, the MSVC runtime, the POSIX runtime and the
native fixtures) have anything to check: a change that only touches Markdown files cannot alter them. The
cheap jobs (source hygiene and the unit tests, which do read some Markdown) always run. Whenever it cannot be
sure, it says `code=true`: no base commit (a new branch, a tag, a force-push), a git failure, an empty diff.

`summary` turns the results of every job into one short table (appended to $GITHUB_STEP_SUMMARY when set) and
exits 1 if any job failed or was cancelled, or if the `changes` job did not succeed. It lists only the jobs that
are not plain successes first, so whoever (or whatever) reads a failed run starts at the failing job and does
not have to list every job to find it.

Standard library only.
"""
import argparse
import json
import os
import subprocess
import sys

NO_COMMIT = {'', '0' * 40}


def docs_only(paths):
    """True when every changed path is a Markdown file (and there is at least one)."""
    paths = [p for p in paths if p.strip()]
    return bool(paths) and all(p.lower().endswith('.md') for p in paths)


def changed_paths(base, head, run=subprocess.run):
    r = run(['git', 'diff', '--name-only', '-z', base, head], capture_output=True)
    if r.returncode != 0:
        raise RuntimeError((r.stderr or b'git diff failed').decode('utf-8', 'replace').strip())
    return [p.decode('utf-8', 'replace') for p in r.stdout.split(b'\0') if p]


def decide(base, head, run=subprocess.run):
    """(code, reason)"""
    base, head = (base or '').strip(), (head or '').strip()
    if base in NO_COMMIT or not head:
        return True, 'no base commit to compare with'
    try:
        paths = changed_paths(base, head, run)
    except (RuntimeError, OSError) as ex:
        return True, 'git could not compare the commits: %s' % ex
    if not paths:
        return True, 'the diff is empty'
    if docs_only(paths):
        return False, '%d changed file(s), all Markdown' % len(paths)
    other = [p for p in paths if not p.lower().endswith('.md')]
    return True, '%d changed file(s); first non-Markdown: %s' % (len(paths), other[0])


def cmd_changes(args):
    code, reason = decide(args.base, args.head)
    print('code=%s' % ('true' if code else 'false'))
    print('docs-only check: %s -> code=%s' % (reason, str(code).lower()), file=sys.stderr)
    return 0


def summarise(needs):
    """(markdown, failed job names, problems)"""
    bad = {'failure', 'cancelled'}
    failed = sorted(k for k, v in needs.items() if v.get('result') in bad)
    problems = list(failed)
    if needs.get('changes', {}).get('result') not in (None, 'success'):
        if 'changes' not in problems:
            problems.append('changes')
    order = sorted(needs.items(), key=lambda kv: (kv[0] not in problems, kv[1].get('result') == 'success', kv[0]))
    lines = ['| Job | Result |', '|---|---|']
    lines += ['| %s | %s |' % (k, v.get('result', '?')) for k, v in order]
    head = ('**CI failed:** ' + ', '.join(problems)) if problems else 'CI passed'
    skipped = [k for k, v in needs.items() if v.get('result') == 'skipped']
    note = ('\n\n%d job(s) skipped because only Markdown changed.' % len(skipped)
            if skipped and needs.get('changes', {}).get('outputs', {}).get('code') == 'false' else '')
    return head + '\n\n' + '\n'.join(lines) + note + '\n', failed, problems


def cmd_summary(args):
    try:
        needs = json.loads(os.environ.get('NEEDS', '{}'))
    except json.JSONDecodeError as ex:
        print('NEEDS is not JSON: %s' % ex, file=sys.stderr)
        return 1
    text, _, problems = summarise(needs)
    out = os.environ.get('GITHUB_STEP_SUMMARY')
    if out:
        with open(out, 'a', encoding='utf-8') as f:
            f.write(text)
    print(text.splitlines()[0])
    if problems:
        print('::error::CI failed: %s' % ', '.join(problems))
    return 1 if problems else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('changes')
    p.add_argument('--base', default='')
    p.add_argument('--head', default='')
    p.set_defaults(fn=cmd_changes)
    sub.add_parser('summary').set_defaults(fn=cmd_summary)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == '__main__':
    sys.exit(main())
