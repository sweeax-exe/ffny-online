"""Compare HD pack memory on one built candidate, serially and with guarded saves.

python scripts/memory-pack-audit.py --fixture logs/fixtures/story --texture-root <data>/mods --output logs/memory-packs
Use --result-tail 60 --hold-menu to inspect settled memory after returning from a match.
The existing memory gate and its failures are retained; phase metrics are diagnostic.
Windows samples private bytes; macOS/Linux sample RSS. Compare runs within one host.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import time

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def phase_summary(path):
    report = json.loads(Path(path).read_text(encoding='utf-8'))
    timeline = report.get('event_timeline', [])
    start = next((e['observed_seconds'] for e in timeline if 'game.startgame(' in e['call'].lower()), None)
    result = next((e['observed_seconds'] for e in timeline if 'game.getmatchsummary(' in e['call'].lower()), None)
    returned = next((e['observed_seconds'] for e in timeline if result is not None and
                    e['observed_seconds'] > result and 'getscreeninfo(battle/cmtype' in e['call'].lower()), None)
    samples = report.get('memory', [])
    play = [s for s in samples if start is not None and s['seconds'] >= start + 20]
    baseline = max((s['private_bytes'] for s in play[:3]), default=0)
    fight = [s for s in play if result is not None and s['seconds'] < result]
    def mb(value): return round(value / 1048576, 2)
    def slope(rows):
        if len(rows) < 2: return None
        xs = [s['seconds'] for s in rows]
        ys = [s['private_bytes'] / 1048576 for s in rows]
        x, y = sum(xs)/len(xs), sum(ys)/len(ys)
        return round(60 * sum((a-x)*(b-y) for a,b in zip(xs,ys)) / sum((a-x)**2 for a in xs), 3)
    out = {'report': str(path), 'head': report.get('identity', {}).get('head'),
           'executable_sha256': report.get('identity', {}).get('run', {}).get('build_state', {}).get('exe'),
           'start_s': start, 'result_s': result, 'runtime_seconds': report.get('run_seconds'),
           'returned_menu_s': returned,
           'match_starts': sum('game.startgame(' in e['call'].lower() for e in timeline),
           'environment': report.get('identity', {}).get('run', {}).get('environment'),
           'baseline_mb': mb(baseline), 'steady_fight_slope_mb_per_min': slope(fight),
           'failed': [a for a in report['assertions'] if a['status'] != 'pass']}
    if play:
        out.update(final_mb=mb(play[-1]['private_bytes']), growth_mb=mb(play[-1]['private_bytes'] - baseline),
                   peak_mb=mb(max(s['private_bytes'] for s in play)))
    if fight:
        out.update(last_fight_mb=mb(fight[-1]['private_bytes']),
                   fight_growth_mb=mb(fight[-1]['private_bytes']-baseline),
                   transition_growth_mb=mb(play[-1]['private_bytes']-fight[-1]['private_bytes']))
    settled = [s for s in samples if returned is not None and s['seconds'] >= returned + 10]
    if len(settled) >= 6:
        settled_baseline = max(s['private_bytes'] for s in settled[:3])
        out.update(settled_menu_samples=len(settled), settled_menu_growth_mb=mb(settled[-1]['private_bytes'] - settled_baseline),
                   settled_menu_slope_mb_per_min=slope(settled))
    transition = [s for s in samples if result is not None and s['seconds'] >= result]
    if transition:
        out['transition_peak_mb'] = mb(max(s['private_bytes'] for s in transition))
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--groups', default='none,menus,fighters,both')
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--texture-root', type=Path, required=True)
    parser.add_argument('--lanczos-receipt', type=Path, help='include verified locally generated Lanczos packs; enables groups lanczos and all')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--preset')
    parser.add_argument('--seed', type=int, default=20261031)
    parser.add_argument('--result-tail', type=int, default=6)
    parser.add_argument('--hold-results', action='store_true')
    parser.add_argument('--hold-menu', action='store_true')
    args = parser.parse_args()
    if args.result_tail < 6 or args.result_tail > 120:
        parser.error('--result-tail must be 6..120 seconds')
    if set(args.groups.split(',')) - {'none', 'menus', 'fighters', 'both', 'lanczos', 'all'}:
        parser.error('--groups must select none,menus,fighters,both,lanczos,all')
    if set(args.groups.split(',')) & {'lanczos', 'all'} and not args.lanczos_receipt:
        parser.error('groups lanczos/all require --lanczos-receipt')
    if len(set(args.groups.split(','))) != len(args.groups.split(',')):
        parser.error('--groups must not repeat a label')
    if args.hold_menu and args.hold_results:
        parser.error('select only one hold mode')
    os.environ.pop('DEFJAM_REGRESSION_PROFILE', None)
    suite = load('scenario_suite')
    preset = args.preset or suite.harness.RELEASE_PRESET
    # A deliberately explicit diagnostic override, recorded in every run's
    # environment. No player settings and no inherited RECOMP diagnostics apply.
    suite.harness.BASE_ENV.update(RECOMP_ASPECT='16:9', RECOMP_TEXTURE_CACHE_MB='2048')
    suite.harness.ROUTES['fight-result']['after'] = args.result_tail
    if args.hold_results:
        suite.harness.ROUTES['fight-result']['tail'] += ',@game.getmatchsummary(,back:9999:10000'
    if args.hold_menu:
        suite.harness.ROUTES['fight-result']['tail'] += ',@getscreeninfo(battle/cmtype#2,back:9999:10000'
    fixture = args.fixture.resolve()
    packs = {'none': '', 'menus': 'menu-ui-hd-wide', 'fighters': 'hd-fighters',
             'both': 'menu-ui-hd-wide;hd-fighters'}
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=False)
    profile = suite.harness.regression_profile.prepare(args.texture_root, folder / 'verified-packs.json', args.lanczos_receipt)
    packs['all'] = profile['environment']['RECOMP_TEXTURE_PACKS']
    packs['lanczos'] = ';'.join(name for name in profile['packs'] if name not in suite.harness.regression_profile.PACKS)
    os.environ.pop('DEFJAM_REGRESSION_PROFILE', None)
    (folder / 'plan.json').write_text(json.dumps({'parameters': vars(args), 'fixture': str(fixture),
        'aspect': '16:9', 'texture_budget_mb': 2048, 'verified_packs': profile['packs'],
        'rng_seed': args.seed}, indent=2, default=str)+'\n', encoding='utf-8')
    summaries = []
    for label in args.groups.split(','):
        output = folder / label
        print('BEGIN ' + label, flush=True)
        code = suite.main(['run', 'fight-result', '--fixture', str(fixture),
            '--preset', preset, '--require-combat', '--rng-seed', str(args.seed),
            '--texture-root', str(args.texture_root.resolve()), '--texture-packs', packs[label], '--output', str(output)])
        if not (output / 'report.json').exists():
            raise RuntimeError('No report for ' + label)
        summary = phase_summary(output / 'report.json')
        summary.update(group=label, exit_code=code)
        summaries.append(summary)
        (folder / 'summary.json').write_text(json.dumps(summaries, indent=2)+'\n')
        print(json.dumps({k: v for k,v in summary.items() if k not in ('environment','failed')}) , flush=True)
        print('FAILED ' + ', '.join(a['name'] for a in summary['failed']), flush=True)
        time.sleep(1)
    return 1 if any(summary['exit_code'] for summary in summaries) else 0


if __name__ == '__main__':
    raise SystemExit(main())
