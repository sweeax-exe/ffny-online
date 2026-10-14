"""scripts/ci-digest.py: which CI jobs a change needs, and the one-table result of a run."""
import importlib.util
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ci_digest', os.path.join(HERE, '..', '..', 'scripts', 'ci-digest.py'))
ci = importlib.util.module_from_spec(spec)
sys.modules['ci_digest'] = ci
spec.loader.exec_module(ci)


def test_only_markdown_is_docs_only():
    assert ci.docs_only(['README.md', 'docs/agent/triage.md', 'PROGRESS.MD'])
    assert not ci.docs_only(['README.md', 'scripts/triage.py'])
    assert not ci.docs_only(['docs/media/launcher.png'])
    assert not ci.docs_only([])                                   # nothing to go on is not docs-only
    assert not ci.docs_only(['.github/workflows/ci.yml'])


def git(cwd, *args):
    subprocess.run(['git', *args], cwd=cwd, check=True, capture_output=True)


def make_repo(tmp_path):
    git(tmp_path, 'init', '-q')
    git(tmp_path, 'config', 'user.email', 'a@b')
    git(tmp_path, 'config', 'user.name', 'n')
    (tmp_path / 'a.py').write_text('x = 1\n')
    (tmp_path / 'README.md').write_text('one\n')
    git(tmp_path, 'add', '-A')
    git(tmp_path, 'commit', '-qm', 'one')
    return subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=tmp_path, capture_output=True, text=True).stdout.strip()


def head_of(tmp_path):
    return subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=tmp_path, capture_output=True, text=True).stdout.strip()


def test_decide_on_a_real_repository(tmp_path, monkeypatch):
    base = make_repo(tmp_path)
    monkeypatch.chdir(tmp_path)
    (tmp_path / 'README.md').write_text('two\n')
    git(tmp_path, 'commit', '-qam', 'docs')
    code, reason = ci.decide(base, head_of(tmp_path))
    assert code is False and 'all Markdown' in reason
    (tmp_path / 'a.py').write_text('x = 2\n')
    (tmp_path / 'README.md').write_text('three\n')
    git(tmp_path, 'commit', '-qam', 'code and docs')
    code, reason = ci.decide(base, head_of(tmp_path))
    assert code is True and 'a.py' in reason


def test_decide_is_conservative_when_unsure(tmp_path, monkeypatch):
    base = make_repo(tmp_path)
    monkeypatch.chdir(tmp_path)
    head = head_of(tmp_path)
    assert ci.decide('', head)[0] is True                        # a new branch has no "before"
    assert ci.decide('0' * 40, head)[0] is True                  # nor does a tag push
    assert ci.decide(base, head)[0] is True                      # an empty diff
    assert ci.decide('deadbeef' * 5, head)[0] is True            # a commit git does not have (force-push)
    assert ci.decide(base, '')[0] is True


def test_changes_prints_the_job_output_line(tmp_path, monkeypatch, capsys):
    base = make_repo(tmp_path)
    monkeypatch.chdir(tmp_path)
    (tmp_path / 'README.md').write_text('two\n')
    git(tmp_path, 'commit', '-qam', 'docs')
    assert ci.main(['changes', '--base', base, '--head', head_of(tmp_path)]) == 0
    assert capsys.readouterr().out.strip() == 'code=false'


NEEDS = {
    'changes': {'result': 'success', 'outputs': {'code': 'true'}},
    'hygiene': {'result': 'success', 'outputs': {}},
    'python-tools': {'result': 'failure', 'outputs': {}},
    'windows-setup': {'result': 'success', 'outputs': {}},
}


def test_summary_puts_the_failing_job_first(monkeypatch, tmp_path, capsys):
    out = tmp_path / 'summary.md'
    monkeypatch.setenv('NEEDS', json.dumps(NEEDS))
    monkeypatch.setenv('GITHUB_STEP_SUMMARY', str(out))
    assert ci.main(['summary']) == 1
    text = out.read_text()
    assert text.startswith('**CI failed:** python-tools')
    assert text.index('| python-tools | failure |') < text.index('| hygiene | success |')
    assert '::error::CI failed: python-tools' in capsys.readouterr().out


def test_summary_passes_when_heavy_jobs_were_skipped_for_a_docs_change(monkeypatch, tmp_path):
    needs = {k: dict(v) for k, v in NEEDS.items()}
    needs['changes']['outputs'] = {'code': 'false'}
    needs['python-tools']['result'] = 'success'
    needs['windows-setup']['result'] = 'skipped'
    monkeypatch.setenv('NEEDS', json.dumps(needs))
    monkeypatch.setenv('GITHUB_STEP_SUMMARY', str(tmp_path / 's.md'))
    assert ci.main(['summary']) == 0
    assert 'skipped because only Markdown changed' in (tmp_path / 's.md').read_text()


def test_summary_fails_if_the_changes_job_did_not_succeed(monkeypatch):
    needs = {'changes': {'result': 'skipped', 'outputs': {}}, 'hygiene': {'result': 'success', 'outputs': {}}}
    monkeypatch.setenv('NEEDS', json.dumps(needs))
    monkeypatch.delenv('GITHUB_STEP_SUMMARY', raising=False)
    assert ci.main(['summary']) == 1


def test_summary_cancelled_counts_as_failed(monkeypatch):
    monkeypatch.setenv('NEEDS', json.dumps({'a': {'result': 'cancelled'}}))
    assert ci.main(['summary']) == 1


def test_bad_needs_json_fails_cleanly(monkeypatch):
    monkeypatch.setenv('NEEDS', '{not json')
    assert ci.main(['summary']) == 1
