"""Scene changes must be distinguishable from sustained memory growth in audit evidence."""
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('memory_pack_audit', ROOT / 'scripts/memory-pack-audit.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def report(tmp_path, tail):
    samples = [{'seconds': t, 'private_bytes': mb * 1048576}
               for t, mb in [(t, 300) for t in range(20, 100, 10)] + tail]
    failure = {'name': 'performance.memory_growth', 'status': 'fail', 'metrics': {'growth_mb': 280}}
    path = tmp_path / 'report.json'
    path.write_text(json.dumps({'identity': {}, 'event_timeline': [
        {'observed_seconds': 0, 'call': 'Game.StartGame()'},
        {'observed_seconds': 100, 'call': 'Game.GetMatchSummary(0)'},
        {'observed_seconds': 104, 'call': 'Control.GetScreenInfo(battle/cmtype.swf)'}],
        'memory': samples, 'assertions': [failure]}))
    return path, failure


def test_audit_keeps_failed_gate_and_reports_transition_separately(tmp_path):
    path, failure = report(tmp_path, [(110, 580)])
    result = audit.phase_summary(path)
    assert result['failed'] == [failure]
    assert result['fight_growth_mb'] == 0
    assert result['steady_fight_slope_mb_per_min'] == 0
    assert result['transition_growth_mb'] == 280
    assert result['transition_peak_mb'] == 580


def test_audit_detects_settling_without_erasing_the_observed_peak(tmp_path):
    path, _ = report(tmp_path, [(110, 580)] + [(t, 310) for t in range(120, 190, 10)])
    result = audit.phase_summary(path)
    assert result['transition_peak_mb'] == 580
    assert result['final_mb'] == 310
    assert result['settled_menu_growth_mb'] == 0
    assert result['settled_menu_slope_mb_per_min'] == 0
    assert result['settled_menu_samples'] == 7
