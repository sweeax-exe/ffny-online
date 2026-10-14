#!/usr/bin/env python3
"""A one-screen digest of a run log, with exact ways to read deeper into the original.

    python scripts/triage.py                       newest logs/run-*.log.err
    python scripts/triage.py logs/run-20261014-101500          (or the .log / .log.err path)
    python scripts/triage.py LOG --json            the same digest for a script
    python scripts/triage.py LOG --around 4146     lines 4106-4186 of the log, numbered
    python scripts/triage.py LOG --around 4146 --context 15
    python scripts/triage.py LOG --lines 4100:4180
    python scripts/triage.py LOG --grep "ICALL|sub_0021" --max 40      (--last: the final matches)
    python scripts/triage.py LOG --head 50 | --tail 50

The digest is the first step, not a replacement for the log. A run log is easily hundreds of
megabytes, and most of it is one line a frame; the digest says what stopped the run, what is
unusual, and the line numbers to look at. It ends with a verdict:

    clean        no fault signature, nothing unusual, frames presented to the end
    explained    one known stop signature with a resolved detail (the function or the address),
                 and nothing unclassified before it
    partial      a known signature, but the detail is missing, or unclassified lines come first,
                 or it is a hang (the watchdog's stack is a scan, not a walk)
    unexplained  the run looks wrong and no known signature says why

Only `clean` and `explained` mean the digest is enough. For `partial` and `unexplained`, or when a
fix based on an `explained` verdict does not change the outcome, read the original: every number
this prints is a line number of the file, and the options above slice it, bounded, with the total
and how to continue. Nothing here modifies a log.

Lines the project knows to be harmless are listed in config/triage-ignore.txt (one regular
expression per line); add to it when a benign line keeps showing up as unclassified.

Standard library only, so it runs wherever the repo does. Output is capped on purpose.
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IGNORE_FILE = os.path.join(REPO, 'config', 'triage-ignore.txt')
WIDTH = 220                  # a longer line is cut, with the count of what was cut
DEFAULT_MAX = 80             # lines a drill-down prints before saying "more"
HARD_MAX = 400               # ... and the most it will ever print
CRASH_BLOCK = 40             # lines read after a [CRASH] or [WATCHDOG] line
SHOW_ANOMALIES = 8           # distinct unclassified lines listed in the digest

CRASH_RE = re.compile(rb'\[CRASH\] (.*)')
FAULT_RE = re.compile(rb'fault addr=0x([0-9A-Fa-f]+)')
PC_RE = re.compile(rb'(?:RIP|PC)=0x([0-9A-Fa-f]+)')
GUEST_VA_RE = re.compile(rb'Xbox VA of fault: 0x([0-9A-Fa-f]+)')
IN_FUNC_RE = re.compile(rb'^\s+in (\S+?)\+0x([0-9A-Fa-f]+)')
ICALL_RE = re.compile(rb'\[ICALL\] Failed to resolve VA (0x[0-9A-Fa-f]{8})')
ICALL_NONCODE_RE = re.compile(rb'\[ICALL\] target (0x[0-9A-Fa-f]+) is not code')
FILEPATH_FAIL_RE = re.compile(rb'^\s*\[(?:FILE|PATH)\].*(?:fail|error|not found|denied|invalid)', re.I)
# What a log line has to look like to be worth a person's eye when no signature claims it.
ANOMALY_RE = re.compile(
    rb'\b(?:error|fail|fatal|assert|abort|exception|unhandled|panic|not implemented|unimplemented|'
    rb'corrupt|overflow|underflow|deadlock|time ?out)'
    rb'|0xC0{2,}[0-9A-Fa-f]{4}|\[(?:CRASH|WATCHDOG|ICALL|UNIMPL|ABI|D3D11-DEBUG)\]', re.I)
# Lines that carry a signature are classified, so they are not "unclassified" as well.
CLASSIFIED_RE = re.compile(
    rb'\[(?:CRASH|WATCHDOG|ICALL|UNIMPL|UNIMPL-KNOWN|ABI|D3D11-DEBUG)\]|indices truncated')
# One search per ordinary line: everything the scan wants to look at twice starts here.
INTEREST_RE = re.compile(
    ANOMALY_RE.pattern + rb'|\[UNIMPL-KNOWN\]|indices truncated|\[(?:FILE|PATH)\].*(?:not found|denied|invalid)', re.I)
BUILTIN_IGNORE = [
    rb'\d+ unhandled methods',           # the GPU executor's coverage note, not a fault
]
DIGITS_RE = re.compile(rb'0x[0-9A-Fa-f]+|\b[0-9A-Fa-f]{6,}\b|\d+')


def clip(raw):
    """A bytes line as short text; a very long line is cut and says by how much."""
    text = raw.rstrip(b'\r\n').decode('utf-8', 'replace')
    if len(text) > WIDTH:
        return text[:WIDTH] + ' ...(+%d chars)' % (len(text) - WIDTH)
    return text


def load_ignores():
    out = [re.compile(p) for p in BUILTIN_IGNORE]
    try:
        with open(IGNORE_FILE, 'rb') as f:
            for line in f.read().splitlines():
                line = line.strip()
                if line and not line.startswith(b'#'):
                    out.append(re.compile(line))
    except OSError:
        pass
    return out


def resolve(arg, logs_dir=None):
    """The .log.err to read: a path with or without the extension, or the newest run."""
    logs_dir = logs_dir or os.path.join(REPO, 'logs')
    if not arg:
        found = sorted(glob.glob(os.path.join(logs_dir, 'run-*.log.err')), key=os.path.getmtime)
        if not found:
            raise SystemExit('no logs/run-*.log.err here; pass a log path')
        return found[-1]
    candidates = [arg, arg + '.err', arg + '.log.err']
    if not os.path.isabs(arg) and not os.path.exists(arg):
        candidates += [os.path.join(logs_dir, arg), os.path.join(logs_dir, arg + '.log.err'),
                       os.path.join(logs_dir, arg + '.err')]
    for c in candidates:
        if os.path.isfile(c):
            # `run-X.log` is the stdout half; what a run reports is on stderr.
            if c.endswith('.log') and os.path.isfile(c + '.err'):
                return c + '.err'
            return c
    raise SystemExit('log not found: ' + arg)


def normalise(raw):
    """A line with its numbers removed, so repeats of one message count as one."""
    return DIGITS_RE.sub(b'#', raw.strip())[:160]


def scan(path, ignores):
    """One streaming pass. Returns the findings; never holds the log in memory."""
    r = {
        'file': path, 'size': os.path.getsize(path), 'lines': 0,
        'presents': [], 'last_present_line': 0, 'first_present_line': 0,
        'funccalls': [], 'funccall_count': 0,
        'crash': None, 'watchdog': None,
        'icall': collections.OrderedDict(), 'icall_noncode': collections.OrderedDict(),
        'unimpl': collections.OrderedDict(), 'unimpl_known': 0,
        'abi': [], 'truncated_batches': 0, 'debug_layer': 0, 'filepath_fail': [],
        'anomalies': collections.OrderedDict(),
    }
    pending = None            # ('crash'|'watchdog', start line, [lines]) while a block is read
    with open(path, 'rb') as f:
        for n, raw in enumerate(f, 1):
            r['lines'] = n
            if pending:
                pending[2].append(raw)
                if len(pending[2]) >= CRASH_BLOCK:
                    _close_block(r, pending)
                    pending = None
            if b'[D3D] 2.0s:' in raw:
                i = raw.find(b'2.0s: ') + 6
                j = raw.find(b' ', i)
                if j > i and raw[i:j].isdigit():
                    r['presents'].append(int(raw[i:j]))
                    r['last_present_line'] = n
                    r['first_present_line'] = r['first_present_line'] or n
                continue
            if raw.startswith(b'[FUNCCALL] '):
                r['funccall_count'] += 1
                r['funccalls'].append((n, raw[11:111].strip().decode('utf-8', 'replace')))
                if len(r['funccalls']) > 6:
                    del r['funccalls'][0]
                continue
            hit = INTEREST_RE.search(raw)
            if not hit:
                continue
            if b'[CRASH]' in raw and not r['crash'] and not pending:
                pending = ('crash', n, [raw])
                continue
            if b'[WATCHDOG]' in raw and not r['watchdog'] and not pending:
                pending = ('watchdog', n, [raw])
                continue
            if pending:
                continue
            if b'[ICALL]' in raw:
                m = ICALL_RE.search(raw)
                if m:
                    r['icall'].setdefault(m.group(1).decode(), [n, 0])[1] += 1
                    continue
                m = ICALL_NONCODE_RE.search(raw)
                if m:
                    r['icall_noncode'].setdefault(m.group(1).decode(), [n, 0])[1] += 1
                    continue
            if b'[UNIMPL-KNOWN]' in raw:
                r['unimpl_known'] += 1
                continue
            if b'[UNIMPL]' in raw:
                key = clip(raw)
                r['unimpl'].setdefault(key, [n, 0])[1] += 1
                continue
            if b'[ABI]' in raw:
                if len(r['abi']) < 5:
                    r['abi'].append((n, clip(raw)))
                continue
            if b'indices truncated' in raw:
                r['truncated_batches'] += 1
                continue
            if b'[D3D11-DEBUG]' in raw:
                r['debug_layer'] += 1
                continue
            if (b'[FILE]' in raw or b'[PATH]' in raw) and FILEPATH_FAIL_RE.search(raw):
                if len(r['filepath_fail']) < 5:
                    r['filepath_fail'].append((n, clip(raw)))
                # not `continue`: a failure also belongs in the anomaly counts below
            if ANOMALY_RE.search(raw) and not CLASSIFIED_RE.search(raw):
                if any(p.search(raw) for p in ignores):
                    continue
                key = normalise(raw)
                slot = r['anomalies'].get(key)
                if slot is None:
                    if len(r['anomalies']) < 200:
                        r['anomalies'][key] = [n, 1, clip(raw)]
                else:
                    slot[1] += 1
    if pending:
        _close_block(r, pending)
    return r


def _close_block(r, pending):
    kind, start, lines = pending
    block = b''.join(lines)
    if kind == 'crash':
        head = lines[0]
        m_pc, m_fault = PC_RE.search(head), FAULT_RE.search(head)
        func = None
        for ln in lines:
            m = IN_FUNC_RE.match(ln)
            if m:
                func = '%s+0x%s' % (m.group(1).decode(), m.group(2).decode())
                break
        m_va = GUEST_VA_RE.search(block)
        what = CRASH_RE.search(head)
        r['crash'] = {
            'line': start, 'what': what.group(1).decode('utf-8', 'replace')[:160] if what else '',
            'pc': ('0x' + m_pc.group(1).decode()) if m_pc else None,
            'fault_addr': ('0x' + m_fault.group(1).decode()) if m_fault else None,
            'guest_va': ('0x' + m_va.group(1).decode()) if m_va else None,
            'function': func,
            'regs': [clip(ln).strip() for ln in lines if b'Xbox regs:' in ln][:2],
        }
    else:
        stack = [ln for ln in lines if ln.lstrip().startswith(b'GS ')]
        r['watchdog'] = {'line': start, 'head': clip(lines[0]).strip(),
                         'stack_words': len(stack)}


def decide(r):
    """verdict, reasons. The rules are deliberately conservative: `explained` has to be earned."""
    reasons = []
    first_anomaly = min((v[0] for v in r['anomalies'].values()), default=None)
    p = r['presents']
    stopped_presenting = bool(p) and r['lines'] - r['last_present_line'] > 400 and not r['crash']

    if r['watchdog']:
        reasons.append('watchdog fired: its guest stack is a scan of stale words, not a walk; '
                       'find the live thread with sample-threads.py / native-stacks.py')
        return 'partial', reasons
    if r['crash']:
        c = r['crash']
        detail = c['function'] or c['guest_va']
        if not detail:
            reasons.append('crash without a function or guest address in its block')
            return 'partial', reasons
        if first_anomaly is not None and first_anomaly < c['line']:
            reasons.append('unclassified lines come before the crash (first at line %d); '
                           'the crash may be a consequence' % first_anomaly)
            return 'partial', reasons
        return 'explained', ['crash at line %d in %s' % (c['line'], detail)]
    if r['icall']:
        if first_anomaly is not None and first_anomaly < min(v[0] for v in r['icall'].values()):
            reasons.append('unclassified lines come before the first unresolved call')
            return 'partial', reasons
        return 'explained', ['%d unresolved indirect-call target(s)' % len(r['icall'])]
    if r['unimpl']:
        return 'explained', ['%d untranslated instruction(s) reached' % len(r['unimpl'])]
    if r['abi']:
        return 'partial', ['[ABI] stack mismatch reported; the first one names the function']
    if r['icall_noncode'] or r['truncated_batches'] or r['debug_layer']:
        return 'partial', ['non-code indirect calls, truncated GPU batches or D3D11 debug-layer '
                           'messages are present; they are symptoms, not a cause']
    if stopped_presenting:
        return 'partial', ['frames stopped %d lines before the end of the log (last present at '
                           'line %d): a hang without a watchdog' % (r['lines'] - r['last_present_line'],
                                                                   r['last_present_line'])]
    if r['anomalies']:
        return 'unexplained', ['%d distinct unclassified line(s); the first is at line %d'
                               % (len(r['anomalies']), first_anomaly)]
    if r['filepath_fail']:
        return 'unexplained', ['file/path failures only (line %d); see below' % r['filepath_fail'][0][0]]
    if not p and r['lines'] < 50:
        return 'unexplained', ['almost nothing was logged (%d lines): did the process start?' % r['lines']]
    return 'clean', ['no fault signature; %d frames-per-2s lines' % len(p)]


def next_steps(r, verdict, path):
    """Exact commands, with this log's own line numbers."""
    rel = os.path.relpath(path, REPO) if path.startswith(REPO) else path
    steps = []
    win = os.name == 'nt'
    if r['crash']:
        c = r['crash']
        steps.append('python scripts/triage.py %s --around %d --context 30      (the crash block and what led to it)'
                     % (rel, c['line']))
        if c['function']:
            fn = c['function'].split('+')[0]
            va = '0x' + fn[4:] if fn.startswith('sub_') else fn
            steps.append('python scripts/callers.py %s --depth 2      (who calls %s)' % (va, fn))
        steps.append('python scripts/guest-stack.py --log %s      (call sites on the stack)' % rel)
    if r['watchdog']:
        steps.append('python scripts/triage.py %s --around %d --context 40' % (rel, r['watchdog']['line']))
        steps.append('python scripts/sample-threads.py      (live threads, process still running)' if win else
                     'sample the live process (macOS `sample`, Linux gdb): docs/build-macos-linux.md; '
                     'sample-threads.py is Windows-only')
    if r['icall']:
        steps.append('seed the targets (scripts/seed-from-log.ps1, or `python -m tools.seed_from_log %s`), '
                     'then analyze -> recomp -> build' % rel)
    if verdict in ('partial', 'unexplained'):
        first = min((v[0] for v in r['anomalies'].values()), default=None)
        if first:
            steps.append('python scripts/triage.py %s --around %d      (the first unclassified line)' % (rel, first))
        steps.append('python scripts/triage.py %s --tail 80      (how the log ends)' % rel)
        steps.append('python scripts/triage.py %s --grep "<word from a line above>" --max 40' % rel)
    if verdict == 'explained':
        steps.append('if the fix based on this does not change the outcome, go deeper: --around / --grep above '
                     '(do not re-run the digest)')
    return steps


def digest(r):
    verdict, reasons = decide(r)
    p = sorted(r['presents'])
    out = {
        'file': r['file'], 'size_bytes': r['size'], 'lines': r['lines'], 'verdict': verdict,
        'reasons': reasons,
        'presents': ({'lines_of_2s': len(p), 'min': p[0], 'median': p[len(p) // 2], 'max': p[-1],
                      'first_line': r['first_present_line'], 'last_line': r['last_present_line']} if p else None),
        'last_calls': [{'line': n, 'call': c} for n, c in r['funccalls'][-3:]],
        'crash': r['crash'], 'watchdog': r['watchdog'],
        'unresolved_icall': [{'va': k, 'line': v[0], 'count': v[1]} for k, v in r['icall'].items()][:12],
        'icall_noncode': [{'target': k, 'line': v[0], 'count': v[1]} for k, v in r['icall_noncode'].items()][:6],
        'untranslated': [{'text': k, 'line': v[0], 'count': v[1]} for k, v in r['unimpl'].items()][:6],
        'untranslated_known': r['unimpl_known'],
        'abi': [{'line': n, 'text': t} for n, t in r['abi']],
        'truncated_batches': r['truncated_batches'], 'debug_layer': r['debug_layer'],
        'file_path_failures': [{'line': n, 'text': t} for n, t in r['filepath_fail']],
        'unclassified': [{'line': v[0], 'count': v[1], 'text': v[2]}
                         for v in sorted(r['anomalies'].values())][:SHOW_ANOMALIES],
        'unclassified_distinct': len(r['anomalies']),
    }
    out['next'] = next_steps(r, verdict, r['file'])
    return out


def human(d):
    L = []
    mb = d['size_bytes'] / 1e6
    L.append('%s   %.1f MB, %d lines' % (os.path.relpath(d['file'], REPO) if d['file'].startswith(REPO) else d['file'],
                                        mb, d['lines']))
    L.append('verdict  %s' % d['verdict'].upper() + ('   ' + '; '.join(d['reasons']) if d['reasons'] else ''))
    p = d['presents']
    if p:
        L.append('frames   %d lines of 2 s: min %d, median %d, max %d   (lines %d..%d)'
                 % (p['lines_of_2s'], p['min'], p['median'], p['max'], p['first_line'], p['last_line']))
    else:
        L.append('frames   no "[D3D] 2.0s" lines: nothing was presented')
    if d['last_calls']:
        L.append('screen   last front-end calls: ' + ' | '.join('%d:%s' % (c['line'], c['call']) for c in d['last_calls']))
    c = d['crash']
    if c:
        L.append('CRASH    line %d: %s' % (c['line'], c['what']))
        L.append('         function %s   guest VA %s   fault addr %s   pc %s'
                 % (c['function'] or '(none printed)', c['guest_va'] or '-', c['fault_addr'] or '-', c['pc'] or '-'))
        for rg in c['regs']:
            L.append('         ' + rg)
    w = d['watchdog']
    if w:
        L.append('WATCHDOG line %d: %s   (%d stack words follow; a scan, not a walk)' % (w['line'], w['head'], w['stack_words']))
    if d['unresolved_icall']:
        L.append('ICALL    %d unresolved target(s): ' % len(d['unresolved_icall']) +
                 ', '.join('%s x%d @%d' % (i['va'], i['count'], i['line']) for i in d['unresolved_icall'][:6]))
    if d['icall_noncode']:
        L.append('ICALL    calls to non-code: ' + ', '.join('%s x%d @%d' % (i['target'], i['count'], i['line'])
                                                           for i in d['icall_noncode'][:4]))
    for u in d['untranslated']:
        L.append('UNIMPL   %s x%d @%d' % (u['text'], u['count'], u['line']))
    if d['untranslated_known']:
        L.append('         (%d inherited [UNIMPL-KNOWN] omissions, not faults)' % d['untranslated_known'])
    for a in d['abi']:
        L.append('ABI      @%d %s' % (a['line'], a['text']))
    extra = []
    if d['truncated_batches']:
        extra.append('%d truncated GPU batches' % d['truncated_batches'])
    if d['debug_layer']:
        extra.append('%d D3D11 debug-layer messages' % d['debug_layer'])
    if extra:
        L.append('GPU      ' + ', '.join(extra))
    for fp in d['file_path_failures']:
        L.append('FILE     @%d %s' % (fp['line'], fp['text']))
    if d['unclassified']:
        L.append('UNCLASSIFIED (%d distinct; first occurrence, count):' % d['unclassified_distinct'])
        for a in d['unclassified']:
            L.append('  @%d x%d  %s' % (a['line'], a['count'], a['text']))
        if d['unclassified_distinct'] > len(d['unclassified']):
            L.append('  ... %d more distinct; --grep to list them' % (d['unclassified_distinct'] - len(d['unclassified'])))
    if d['next']:
        L.append('next:')
        L.extend('  ' + s for s in d['next'])
    return '\n'.join(L)


# --- drill-down: bounded slices of the original --------------------------------------------

def numbered(path, want, limit):
    """Lines selected by want(n, raw) -> bool, with line numbers, at most `limit` of them."""
    got, total, more = [], 0, 0
    with open(path, 'rb') as f:
        for n, raw in enumerate(f, 1):
            if want(n, raw):
                total += 1
                if len(got) < limit:
                    got.append('%d: %s' % (n, clip(raw)))
                else:
                    more += 1
    return got, total, more


def line_count(path):
    with open(path, 'rb') as f:
        return sum(1 for _ in f)


def tail_lines(path, count):
    dq = collections.deque(maxlen=count)
    with open(path, 'rb') as f:
        for n, raw in enumerate(f, 1):
            dq.append((n, raw))
    return ['%d: %s' % (n, clip(raw)) for n, raw in dq]


def slice_lines(path, a, b, limit):
    if b - a + 1 > limit:
        b = a + limit - 1
    got = []
    with open(path, 'rb') as f:
        for n, raw in enumerate(f, 1):
            if n < a:
                continue
            if n > b:
                break
            got.append('%d: %s' % (n, clip(raw)))
    return got, b


def drill(args, path):
    limit = max(1, min(args.max, HARD_MAX))
    if args.around is not None:
        c = max(0, args.context)
        a, b = max(1, args.around - c), args.around + c
        lines, end = slice_lines(path, a, b, limit)
        print('\n'.join(lines))
        if end < b:
            print('-- cut at %d lines; continue with --lines %d:%d' % (limit, end + 1, b))
    elif args.lines:
        m = re.fullmatch(r'(\d+):(\d+)', args.lines)
        if not m or int(m.group(1)) > int(m.group(2)):
            raise SystemExit('--lines A:B with A <= B, for example --lines 4100:4180')
        a, b = int(m.group(1)), int(m.group(2))
        lines, end = slice_lines(path, a, b, limit)
        print('\n'.join(lines))
        if end < b:
            print('-- cut at %d lines; continue with --lines %d:%d' % (limit, end + 1, b))
    elif args.grep:
        try:
            rx = re.compile(args.grep.encode(), re.I)
        except re.error as ex:
            raise SystemExit('bad --grep pattern: %s' % ex)
        if args.last:
            got, total, _ = numbered(path, lambda n, raw: bool(rx.search(raw)), 10 ** 9)
            shown = got[-limit:]
            print('\n'.join(shown))
            print('-- %d matching lines; showing the last %d' % (total, len(shown)))
        else:
            got, total, more = numbered(path, lambda n, raw: bool(rx.search(raw)), limit)
            print('\n'.join(got))
            print('-- %d matching lines%s' % (total, '; %d more not shown (--max, or --last for the end)' % more if more else ''))
    elif args.head:
        n = min(args.head, limit)
        lines, _ = slice_lines(path, 1, n, limit)
        print('\n'.join(lines))
    elif args.tail:
        print('\n'.join(tail_lines(path, min(args.tail, limit))))
    else:
        return False
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log', nargs='?', help='a run log (path, stem or .log.err); default: the newest run')
    ap.add_argument('--json', action='store_true', help='print the digest as JSON')
    ap.add_argument('--around', type=int, metavar='LINE', help='lines around LINE')
    ap.add_argument('--context', type=int, default=40, help='lines either side for --around (default 40)')
    ap.add_argument('--lines', metavar='A:B', help='lines A through B')
    ap.add_argument('--grep', metavar='REGEX', help='matching lines, numbered (case-insensitive)')
    ap.add_argument('--last', action='store_true', help='with --grep: the final matches, not the first')
    ap.add_argument('--head', type=int, metavar='N')
    ap.add_argument('--tail', type=int, metavar='N')
    ap.add_argument('--max', type=int, default=DEFAULT_MAX,
                    help='most lines a slice prints (default %d, ceiling %d)' % (DEFAULT_MAX, HARD_MAX))
    args = ap.parse_args(argv)

    path = resolve(args.log)
    if drill(args, path):
        return 0
    d = digest(scan(path, load_ignores()))
    if args.json:
        print(json.dumps(d, indent=2))
    else:
        print(human(d))
    return 0


if __name__ == '__main__':
    sys.exit(main())
