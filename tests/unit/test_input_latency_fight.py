"""The reusable latency fixture stays inside the native script bounds."""
import importlib.util
from pathlib import Path
import sys
import pytest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
spec=importlib.util.spec_from_file_location('input_latency_fight',ROOT/'scripts/input-latency-fight.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

def test_maximum_pulses_fit_native_script_and_have_distinct_releases():
    tail,after=module.pulse_script(40)
    script=module.harness.build_pad_script(module.harness.ROUTES['versus']['stages'],tail)
    entries=tail.split(',')[1:]
    assert len(entries)==160 and len(script.split(','))<256
    assert after>float(entries[-1].split(':')[2])+3
    ranges=[tuple(map(float,entry.split(':')[1:])) for entry in entries]
    assert all(b>a and abs(b-a-.020)<1e-6 for a,b in ranges)
    assert all(a>previous for (_,previous),(a,_) in zip(ranges,ranges[1:]))

@pytest.mark.parametrize('pairs',[0,41])
def test_invalid_pulse_count_is_rejected(pairs):
    with pytest.raises(ValueError): module.pulse_script(pairs)

def test_calibration_does_not_press_pause_or_back():
    tail,_=module.calibration_script()
    names=[entry.split(':')[0] for entry in tail.split(',')[1:]]
    assert len(names)==36 and len(set(names))==12
    assert 'start' not in names and 'back' not in names

def test_phase_sweep_stays_bounded_and_moves_each_pair():
    tail,after=module.pulse_script(40,7)
    entries=tail.split(',')[1:]
    assert len(entries)==160
    assert abs(float(entries[2].split(':')[1])-float(entries[0].split(':')[1])-1.007)<1e-6
    assert after>float(entries[-1].split(':')[2])+3

def test_axis_sweep_has_both_signs_without_button_presses():
    tail,after=module.axes_script()
    entries=tail.split(',')[1:]
    assert [e.split(':')[0] for e in entries]==['lleft','lright','lup','ldown','rleft','rright','rup','rdown']
    assert after>float(entries[-1].split(':')[2])+3

def valid_analysis():
    counts=dict.fromkeys(('script_virtual','host_mapped','usb_complete','xdk_read','pad_cache_after',
        'input_edge_consumed','fighter_input_ack','guest_pad_read','fighter_after'),20)
    return dict(phases=[dict(observed_rising_edges=dict(counts),cadence={'game_step':{'max_ms':34}}) for _ in range(2)],
        quality=dict(checkpointed_windows_without_reported_loss=True),input_retention=dict(total_discarded=0))

def test_missing_fighter_press_cannot_pass_a_healthy_pacing_run():
    report=valid_analysis();assert module.pulse_checks(report,10)['valid']
    report['phases'][1]['observed_rising_edges']['guest_pad_read']=19
    assert not module.pulse_checks(report,10)['valid']

def test_long_game_stall_cannot_hide_behind_full_rate_windows():
    report=valid_analysis();report['phases'][1]['cadence']['game_step']['max_ms']=477
    assert not module.pulse_checks(report,10)['valid']

@pytest.mark.parametrize('quality_loss',[True,False])
def test_trace_loss_or_expired_input_cannot_pass_registration(quality_loss):
    report=valid_analysis()
    if quality_loss: report['quality']['checkpointed_windows_without_reported_loss']=False
    else: report['input_retention']['total_discarded']=1
    assert not module.pulse_checks(report,10)['valid']
