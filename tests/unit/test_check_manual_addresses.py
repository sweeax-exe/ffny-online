"""scripts/check-manual-addresses.py: the addresses recomp_manual.c names, checked against a lift."""
import importlib.util
import json
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/check-manual-addresses.py'
spec = importlib.util.spec_from_file_location('check_manual_addresses', SCRIPT)
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)

SOURCE = '''
/* An example in a comment is not a use:
 *   extern void sub_00012345(void);
 */
void sub_001A4A30_enter(void) { hook(); }
static void sub_00087700(void)
{
    extern void sub_000AA300(void);   // a callee
    sub_000AA300();
}
// void sub_0000DEAD(void) is commented out
'''


def test_definitions_hooks_and_externs_outside_comments():
    assert check.manual_addresses(SOURCE) == {
        0x001A4A30: 'entry hook', 0x00087700: 'replacement', 0x000AA300: 'extern'}


def test_a_missing_start_fails_and_is_named(tmp_path, capsys):
    toolkit = tmp_path / 'tk'
    out = toolkit / 'tools/disasm/output'
    out.mkdir(parents=True)
    (out / 'functions.json').write_text(json.dumps(
        [{'start': '0x001A4A30'}, {'start': '0x00087700'}]), encoding='utf-8')
    manual = tmp_path / 'recomp_manual.c'
    manual.write_text(SOURCE, encoding='utf-8')
    assert check.main(['--toolkit', str(toolkit), '--manual', str(manual)]) == 1
    assert 'sub_000AA300 (extern)' in capsys.readouterr().out
    (out / 'functions.json').write_text(json.dumps(
        [{'start': '0x001A4A30'}, {'start': '0x00087700'}, {'start': '0x000AA300'}]), encoding='utf-8')
    assert check.main(['--toolkit', str(toolkit), '--manual', str(manual)]) == 0
