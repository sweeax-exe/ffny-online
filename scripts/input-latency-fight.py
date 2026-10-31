#!/usr/bin/env python3
"""Run one serial, save-guarded SDL virtual-pad latency fight.

Uses the real host/USB/guest path; never RECOMP_TEST_INPUT. Example:
python scripts/input-latency-fight.py --name baseline --pairs 10 --hold-ms 120 --edges 0
Run from a configured project with DEFJAM_DATA pointing to disposable test data.
Reports and CSVs stay under ignored logs/input-audit/. Do not run beside another game.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import harness
import test_evidence

def pulse_checks(analysis,pairs):
    """An apparently healthy fight is insufficient if a registration stage lost taps."""
    required=('script_virtual','host_mapped','usb_complete','xdk_read','pad_cache_after',
              'input_edge_consumed','fighter_input_ack','guest_pad_read','fighter_after')
    phases=analysis.get('phases',[])
    counts=[p.get('observed_rising_edges',{}) for p in phases]
    valid=len(phases)==2 and all(c.get(stage)==pairs*2 for c in counts for stage in required)
    valid=valid and analysis.get('quality',{}).get('checkpointed_windows_without_reported_loss',False)
    valid=valid and analysis.get('input_retention',{}).get('total_discarded')==0
    gaps=[p.get('cadence',{}).get('game_step',{}).get('max_ms') for p in phases]
    valid=valid and all(gap is not None and gap<=100 for gap in gaps)
    return dict(valid=bool(valid),expected_per_phase=pairs*2,required_stages=required,counts=counts,
        maximum_step_gaps_ms=gaps,maximum_allowed_step_gap_ms=100)

def pulse_analysis(path):
    spec=importlib.util.spec_from_file_location('fight_input_analysis',Path(__file__).with_name('input-latency.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    result=module.analyze(path,guest_bit=0x40)
    Path(str(path)+'.analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


def pulse_script(pairs,phase_step_ms=0):
    if not 1 <= pairs <= 40:
        raise ValueError('pairs must be 1..40 to stay within script limits')
    parts = ['@game.startgame(']
    for start, gap in ((25, .070), (35 + pairs, .170)):
        for n in range(pairs):
            base=start+n+n*phase_step_ms/1000
            for t in (base, base + gap):
                parts.append(f'x:{t:.3f}:{t + .020:.3f}')
    return ','.join(parts), 40 + 2 * pairs

def calibration_script():
    controls=('a','b','x','y','black','white','lt','rt','up','down','left','right')
    parts=['@game.startgame(']
    for n,control in enumerate(controls):
        for offset in (0,.5,1):
            start=25+n*3+offset
            parts.append(f'{control}:{start:.3f}:{start+.080:.3f}')
    return ','.join(parts), 66

def axes_script():
    controls=('lleft','lright','lup','ldown','rleft','rright','rup','rdown')
    parts=['@game.startgame(']
    for n,control in enumerate(controls):
        start=25+n*1.5
        parts.append(f'{control}:{start:.3f}:{start+.5:.3f}')
    return ','.join(parts), 48


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True)
    parser.add_argument('--pairs', type=int, default=10)
    parser.add_argument('--hold-ms', type=int, default=120)
    parser.add_argument('--poll-ms', type=int, choices=(1, 2), default=2)
    parser.add_argument('--edges', choices=('0', '1'), default='1')
    parser.add_argument('--late-refresh', choices=('0', '1', 'shadow'), default='0')
    parser.add_argument('--phase-step-ms', type=int, choices=range(0,10), default=0,
        help='add this offset per pair to sample different positions within a game frame')
    parser.add_argument('--ai-opponent', action='store_true', help='legacy fight route; default uses two neutral human players')
    parser.add_argument('--calibrate-buttons', action='store_true', help='three isolated presses of each face/shoulder/d-pad control')
    parser.add_argument('--calibrate-axes', action='store_true', help='both signs of each stick axis through the actual virtual-pad path')
    parser.add_argument('--scripted-combat', action='store_true', help='exercise movement, attacks and damage with the normal fight tail')
    args = parser.parse_args()
    if not args.name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in args.name):
        parser.error('name must contain only letters, numbers, dash or underscore')
    script, after = calibration_script() if args.calibrate_buttons else pulse_script(args.pairs,args.phase_step_ms)
    if args.calibrate_axes: script,after=axes_script()
    if args.scripted_combat:
        script=harness.ROUTES['fight' if args.ai_opponent else 'versus']['tail']
        after=120
    folder = Path(harness.REPO) / 'logs/input-audit'
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f'{args.name}.csv'
    env = {'RECOMP_INPUT_HOST': '1', 'RECOMP_INPUT_IGNORE_FOCUS': '1', 'RECOMP_PAD_HOST': '1',
        'RECOMP_INPUT_NO_PADS': '1', 'RECOMP_INPUT_VIRTUAL_PADS': '1' if args.ai_opponent else '2',
        'RECOMP_USB_PADS': '1' if args.ai_opponent else '2', 'RECOMP_PAD_SCRIPT_VIRTUAL': '1',
        'RECOMP_KEYBOARD_PLAYER': 'off', 'RECOMP_TEST_OBSERVATIONS': '1', 'RECOMP_TEST_RNG_SEED': '314159',
        'RECOMP_PAD_MIN_HOLD_MS': str(args.hold_ms), 'RECOMP_RECORD': '1', 'RECOMP_RECORD_FPS': '0',
        'RECOMP_PRESENT_PACING': '1', 'RECOMP_INPUT_TRACE': str(path),
        'RECOMP_INPUT_TRACE_LIMIT': '2000000',
        'RECOMP_INPUT_POLL_MS': str(args.poll_ms), 'RECOMP_INPUT_EDGES': args.edges,
        'RECOMP_INPUT_LATE_REFRESH': args.late_refresh}
    route = 'fight' if args.ai_opponent else 'versus'
    run = harness.run_game('input-' + args.name, stages=harness.ROUTES[route]['stages'], tail=script,
        secs=400, until='game.startgame(', after=after, shots='', env=env, quiet=True)
    events,bad=test_evidence.telemetry_events(run.text)
    starts=[e for e in events if e.get('event')=='match_start']
    roster=starts[0].get('fighters',[]) if starts else []
    human_slots=sorted(f['slot'] for f in roster if not f.get('cpu'))
    ended=sum(e.get('event') in ('result','match_over') for e in events)
    fixture_ok=bool(starts) and bad==0 and (args.ai_opponent or (len(roster)==2 and human_slots==[0,1] and ended==0))
    report = dict(name=args.name, parameters=vars(args), trace=str(path), log=run.log,
        identity=run.identity, save_ok=run.save_ok,
        reached=harness.reached(run.summary, 'game.startgame('), faults=harness.run_faults(run),
        fixture=dict(valid=fixture_ok,human_slots=human_slots,match_end_events=ended,malformed_events=bad),
        pacing=test_evidence.pacing_checks(run.text))
    if args.scripted_combat:
        # A combat fixture may legitimately finish; the isolated pulse fixture may not.
        fixture_ok=bool(starts) and bad==0 and human_slots==([0] if args.ai_opponent else [0,1])
        report['fixture']['valid']=fixture_ok
        report['combat']=test_evidence.combat_checks(run.text,require_result=False,humans=1 if args.ai_opponent else 2)
    if args.edges=='1' and not args.scripted_combat and not args.calibrate_buttons and not args.calibrate_axes:
        report['registration']=pulse_checks(pulse_analysis(path),args.pairs)
    (folder / f'{args.name}.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('name', 'save_ok', 'reached', 'faults', 'fixture', 'pacing','registration','combat') if k in report}, indent=2))
    return 0 if fixture_ok and report.get('registration',{}).get('valid',True) and report['reached'] and report['save_ok'] and not report['faults'] and all(
        item['status'] == 'pass' for item in report['pacing']+report.get('combat',[])) else 1


if __name__ == '__main__':
    raise SystemExit(main())
