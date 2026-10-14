"""scripts/triage.py: the digest of a run log, and the bounded ways into the original.

Every log here is synthetic and tiny. The shapes follow what src/main.c and the runtime print
(`[CRASH] ... fault addr=`, `in sub_XXXXXXXX+0x..`, `[ICALL] Failed to resolve VA`, `[D3D] 2.0s:`), so a
change to either side shows up here.
"""
import importlib.util
import json
import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', '..', 'scripts', 'triage.py')
spec = importlib.util.spec_from_file_location('triage', SCRIPT)
triage = importlib.util.module_from_spec(spec)
sys.modules['triage'] = triage
spec.loader.exec_module(triage)

FRAME = '  [D3D] 2.0s: 120 present (60.0 fps), 4 begin\n'
CALL = '[FUNCCALL] Control.GetScreenInfo(intMain/mainMenu.swf)\n'
CRASH = (
    '[CRASH] Access violation (SIGSEGV) at PC=0x7F001234, fault addr=0x7F000010\n'
    '  Xbox regs: eax=0x00000001 ecx=0x00000002 edx=0x00000003 esp=0x00F7F874\n'
    '  Xbox regs: ebx=0x00000004 esi=0x00000005 edi=0x00000006\n'
    '  Xbox VA of fault: 0x00000010\n'
    '  in sub_0021EB90+0x44\n')
WATCHDOG = ('[WATCHDOG] no exit after 10s; guest esp=0x00F7F874\n'
            '  GS 00F7F87C 0021EF70\n  GS 00F7F880 00234770\n')


def write(tmp_path, text, name='run-20261014-101500.log.err'):
    p = tmp_path / name
    p.write_text(text, encoding='utf-8')
    return str(p)


def digest(tmp_path, text):
    return triage.digest(triage.scan(write(tmp_path, text), triage.load_ignores()))


def healthy(n=5):
    return CALL + FRAME * n


def test_a_healthy_run_is_clean(tmp_path):
    d = digest(tmp_path, healthy())
    assert d['verdict'] == 'clean'
    assert d['presents']['median'] == 120 and d['lines'] == 6
    assert d['last_calls'][0]['call'].startswith('Control.GetScreenInfo')


def test_a_crash_with_a_function_is_explained(tmp_path):
    d = digest(tmp_path, healthy() + CRASH)
    assert d['verdict'] == 'explained'
    c = d['crash']
    assert c['function'] == 'sub_0021EB90+0x44' and c['guest_va'] == '0x00000010'
    assert c['line'] == 7 and c['pc'] == '0x7F001234'
    assert any('--around 7' in step for step in d['next'])
    assert any('callers.py 0x0021EB90 --depth 2' in step for step in d['next'])


def test_a_windows_style_crash_is_read_too(tmp_path):
    text = ('[CRASH] Access violation at RIP=0x140012345, fault addr=0x1000 (write)\n'
            '  Xbox regs: eax=0x1 ecx=0x2 edx=0x3 esp=0x4\n  in sub_00100000+0x10\n')
    d = digest(tmp_path, healthy() + text)
    assert d['verdict'] == 'explained' and d['crash']['pc'] == '0x140012345'


def test_a_crash_without_a_function_is_only_partial(tmp_path):
    d = digest(tmp_path, healthy() + '[CRASH] Fatal signal (SIGABRT) at PC=0x1, fault addr=0x0\n')
    assert d['verdict'] == 'partial'
    assert 'function' in ' '.join(d['reasons'])


def test_unclassified_lines_before_the_crash_keep_it_from_being_explained(tmp_path):
    text = CALL + 'Heap corruption detected in pool 3\n' + FRAME + CRASH
    d = digest(tmp_path, text)
    assert d['verdict'] == 'partial'
    assert d['unclassified'][0]['line'] == 2 and 'before the crash' in d['reasons'][0]
    assert any('--around 2' in step for step in d['next'])


def test_unclassified_lines_after_the_crash_do_not_block_explained(tmp_path):
    after = FRAME * (triage.CRASH_BLOCK + 2) + 'shutdown failed to flush cache\n'     # past the crash block
    d = digest(tmp_path, healthy() + CRASH + after)
    assert d['verdict'] == 'explained'
    assert d['unclassified_distinct'] == 1


def test_a_watchdog_is_never_explained(tmp_path):
    d = digest(tmp_path, healthy() + WATCHDOG)
    assert d['verdict'] == 'partial'
    assert d['watchdog']['stack_words'] == 2
    assert 'scan' in d['reasons'][0]


def test_unresolved_indirect_calls_are_listed_once_each(tmp_path):
    text = healthy() + ''.join('[ICALL] Failed to resolve VA 0x00201770 (total calls: %d)\n' % i for i in range(3))
    text += '[ICALL] Failed to resolve VA 0x00201954\n'
    d = digest(tmp_path, text)
    assert d['verdict'] == 'explained'
    assert [i['va'] for i in d['unresolved_icall']] == ['0x00201770', '0x00201954']
    assert d['unresolved_icall'][0]['count'] == 3 and d['unresolved_icall'][0]['line'] == 7


def test_new_untranslated_instructions_are_faults_known_ones_are_not(tmp_path):
    known = '[UNIMPL-KNOWN] untranslated instruction REACHED: `cli` at 0x00100000\n'
    assert digest(tmp_path, healthy() + known)['verdict'] == 'clean'
    new = '[UNIMPL] untranslated instruction REACHED: `unknown` at 0x00100004\n'
    d = digest(tmp_path, healthy() + known + new)
    assert d['verdict'] == 'explained' and d['untranslated_known'] == 1 and len(d['untranslated']) == 1


def test_frames_that_stop_without_a_watchdog_are_a_partial_hang(tmp_path):
    d = digest(tmp_path, healthy(3) + 'idle\n' * 600)
    assert d['verdict'] == 'partial' and 'hang' in d['reasons'][0]


def test_a_gpu_coverage_note_is_not_a_failure(tmp_path):
    d = digest(tmp_path, healthy() + '[GPU] 12 unhandled methods\n')
    assert d['verdict'] == 'clean'


def test_a_failure_nothing_recognises_is_unexplained_and_pointed_at(tmp_path):
    d = digest(tmp_path, healthy() + 'Sound device failed to open (error 5)\nSound device failed to open (error 7)\n')
    assert d['verdict'] == 'unexplained'
    assert d['unclassified_distinct'] == 1 and d['unclassified'][0]['count'] == 2    # the number is normalised away
    assert any('--around 7' in step for step in d['next'])


def test_the_ignore_file_silences_a_known_harmless_line(tmp_path, monkeypatch):
    ignore = tmp_path / 'ignore.txt'
    ignore.write_text('# note\nSound device failed\n')
    monkeypatch.setattr(triage, 'IGNORE_FILE', str(ignore))
    d = digest(tmp_path, healthy() + 'Sound device failed to open\n')
    assert d['verdict'] == 'clean'


def test_file_failures_are_reported(tmp_path):
    d = digest(tmp_path, healthy() + '[PATH] open \\Device\\CdRom0\\x.big -> not found\n')
    assert d['file_path_failures'][0]['line'] == 7
    assert d['verdict'] == 'unexplained'


def test_a_log_with_almost_nothing_in_it_is_unexplained(tmp_path):
    assert digest(tmp_path, 'XBE loaded\n')['verdict'] == 'unexplained'


def test_a_very_long_line_is_cut_and_says_so(tmp_path):
    out = triage.clip(b'x' * 1000 + b'\n')
    assert out.startswith('x' * 220) and out.endswith('...(+780 chars)')


# --- the way into the original ------------------------------------------------------------

def run_cli(capsys, *argv):
    assert triage.main(list(argv)) == 0
    return capsys.readouterr().out


def test_around_prints_numbered_lines_of_the_original(tmp_path, capsys):
    path = write(tmp_path, ''.join('line %d\n' % i for i in range(1, 201)))
    out = run_cli(capsys, path, '--around', '100', '--context', '2')
    assert out.splitlines() == ['98: line 98', '99: line 99', '100: line 100', '101: line 101', '102: line 102']


def test_around_near_the_start_does_not_go_negative(tmp_path, capsys):
    path = write(tmp_path, ''.join('line %d\n' % i for i in range(1, 20)))
    assert run_cli(capsys, path, '--around', '2', '--context', '5').splitlines()[0] == '1: line 1'


def test_slices_are_capped_and_say_how_to_continue(tmp_path, capsys):
    path = write(tmp_path, ''.join('line %d\n' % i for i in range(1, 1001)))
    out = run_cli(capsys, path, '--lines', '10:900', '--max', '5').splitlines()
    assert out[0] == '10: line 10' and out[4] == '14: line 14'
    assert out[5] == '-- cut at 5 lines; continue with --lines 15:900'


def test_the_ceiling_cannot_be_raised_by_max(tmp_path, capsys):
    path = write(tmp_path, ''.join('line %d\n' % i for i in range(1, 1001)))
    out = run_cli(capsys, path, '--lines', '1:1000', '--max', '99999').splitlines()
    assert len([l for l in out if not l.startswith('--')]) == triage.HARD_MAX


def test_grep_counts_all_matches_and_shows_the_first_or_last(tmp_path, capsys):
    path = write(tmp_path, ''.join('%s %d\n' % ('hit' if i % 2 else 'miss', i) for i in range(1, 21)))
    first = run_cli(capsys, path, '--grep', 'HIT', '--max', '3').splitlines()
    assert first[:3] == ['1: hit 1', '3: hit 3', '5: hit 5']
    assert first[3] == '-- 10 matching lines; 7 more not shown (--max, or --last for the end)'
    last = run_cli(capsys, path, '--grep', 'hit', '--max', '2', '--last').splitlines()
    assert last == ['17: hit 17', '19: hit 19', '-- 10 matching lines; showing the last 2']


def test_head_and_tail(tmp_path, capsys):
    path = write(tmp_path, ''.join('line %d\n' % i for i in range(1, 51)))
    assert run_cli(capsys, path, '--head', '2').splitlines() == ['1: line 1', '2: line 2']
    assert run_cli(capsys, path, '--tail', '2').splitlines() == ['49: line 49', '50: line 50']


def test_bad_arguments_exit_with_a_message(tmp_path):
    path = write(tmp_path, 'x\n')
    with pytest.raises(SystemExit):
        triage.main([path, '--lines', '9:3'])
    with pytest.raises(SystemExit):
        triage.main([path, '--grep', '('])


def test_the_digest_never_modifies_the_log(tmp_path, capsys):
    path = write(tmp_path, healthy() + CRASH)
    before = open(path, 'rb').read()
    run_cli(capsys, path)
    run_cli(capsys, path, '--json')
    assert open(path, 'rb').read() == before


def test_json_output_parses_and_matches_the_digest(tmp_path, capsys):
    path = write(tmp_path, healthy() + CRASH)
    d = json.loads(run_cli(capsys, path, '--json'))
    assert d['verdict'] == 'explained' and d['crash']['function'] == 'sub_0021EB90+0x44'


def test_human_digest_is_one_screen(tmp_path, capsys):
    noisy = ''.join('problem number %d failed in module %d\n' % (i, i % 7) for i in range(500))
    out = run_cli(capsys, write(tmp_path, healthy() + noisy + CRASH))
    assert len(out.splitlines()) < 40
    assert 'UNCLASSIFIED' in out


def test_a_stem_or_the_stdout_half_finds_the_stderr_file(tmp_path):
    err = write(tmp_path, healthy())
    (tmp_path / 'run-20261014-101500.log').write_text('stdout\n')
    stem = str(tmp_path / 'run-20261014-101500')
    assert triage.resolve(stem) == err
    assert triage.resolve(stem + '.log') == err
    assert triage.resolve(err) == err


def test_no_argument_picks_the_newest_run(tmp_path):
    old = write(tmp_path, healthy(), 'run-20261013-000000.log.err')
    new = write(tmp_path, healthy(), 'run-20261014-000000.log.err')
    os.utime(old, (1, 1))
    assert triage.resolve(None, str(tmp_path)) == new
    with pytest.raises(SystemExit):
        triage.resolve(None, str(tmp_path / 'nowhere'))
