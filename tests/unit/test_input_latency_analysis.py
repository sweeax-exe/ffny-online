"""Do not mispair latency after a merged tap, or treat trace loss as game loss."""
import importlib.util
import csv
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('input_latency_analysis',ROOT/'scripts/input-latency.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

def test_matching_does_not_shift_after_a_merged_press():
    report=module.match_edges([0,70000000,1000000000,1070000000],[6000000,1006000000])
    assert report['count']==2 and report['p50_ms']==6
    assert report['unmatched_or_merged_or_later']==2
    assert report['ambiguous_source_edges']==0

def test_worker_cpu_uses_thread_ticks_and_retention_loss_is_separate():
    rows=[dict(stage='host_thread_cpu',t_ns=0,aux0=100,aux1=100),
          dict(stage='host_thread_cpu',t_ns=2000000000,aux0=100100,aux1=100100),
          dict(stage='input_edge_loss',slot=0,aux0=64,aux1=2),
          dict(stage='input_edge_loss',slot=0,aux0=64,aux1=5)]
    assert module.worker_cpu(rows)['percent_of_one_core']==1.0
    health=module.retention_health(rows)
    assert health['total_discarded']==69 and health['slots']['0']==dict(overflow=64,expired=5)

def test_ambiguous_pair_is_not_reported_as_precise_latency():
    report=module.match_edges([0,70000000],[80000000])
    assert report['count']==0 and report['ambiguous_source_edges']==2

def test_gamepad_schema_keeps_button_and_trigger_bits():
    mapped=struct.pack('<H8B4h',1,0,0,255,0,0,0,120,0,1,2,3,4)
    assert module.gamepad_bits(mapped)==1|(1<<10)|(1<<14)
    usb=b'\0\x14'+mapped
    assert module.gamepad_bits(usb,True)==module.gamepad_bits(mapped)

def write_trace(path,events,loss=0):
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=('t_ns','seq','tid','stage','slot','aux0','aux1','n','payload'))
        writer.writeheader()
        for index,event in enumerate(events,1):
            time,stage,data=event[:3]
            slot=event[3] if len(event)>3 else 0
            writer.writerow(dict(t_ns=time,seq=index,tid=1,stage=stage,slot=slot,aux0=0,aux1=0,n=len(data),payload=data.hex()))
    Path(str(path)+'.json').write_text(json.dumps(dict(last_ns=1000000000,closed=1,drop_lock=loss)))

def test_censored_loss_never_becomes_a_complete_measurement(tmp_path):
    path=tmp_path/'trace.csv'
    write_trace(path,[(1,'host_mapped',bytes(18))],loss=1)
    result=module.analyze(path)
    assert not result['quality']['trace_complete']
    assert not result['quality']['checkpointed_windows_without_reported_loss']

def test_isolated_button_calibration_ignores_constant_modifiers(tmp_path):
    path=tmp_path/'trace.csv'
    held=bytearray(24);held[10]=1
    neutral=struct.pack('<12I',0x3000,*([0]*11))
    pressed=struct.pack('<12I',0x3040,*([0]*11))
    write_trace(path,[(100000000,'script_virtual',bytes(held)),(102000000,'guest_pad_read',neutral),
        (105000000,'guest_pad_read',pressed),(120000000,'guest_pad_read',neutral)])
    result=module.analyze(path)
    assert result['phases'][0]['guest_bit']=='0x40'
    assert result['phases'][0]['observed_rising_edges']['guest_pad_read']==1

def test_partial_final_record_is_explicit(tmp_path):
    path=tmp_path/'trace.csv';write_trace(path,[(1,'host_mapped',bytes(18))])
    with path.open('a') as f:f.write('123,2,1,host_mapped,0,0,0,18,aa\n')
    result=module.analyze(path)
    assert result['malformed_rows']==1 and not result['quality']['trace_complete']

def test_guest_port_mapping_splits_usb_cache_and_fighter_delays(tmp_path):
    path=tmp_path/'trace.csv'
    held=bytearray(24);held[10]=1
    body=struct.pack('<H8B4h',0,0,0,255,0,0,0,0,0,0,0,0,0)
    cache=struct.pack('<8I',0x40,*([0]*7))
    fighter=struct.pack('<12I',0x40,*([0]*11))
    write_trace(path,[(100000000,'script_virtual',bytes(held)),
        (101000000,'usb_complete',b'\0\x14'+body),
        (110000000,'xdk_read',struct.pack('<I',7)+body,2),
        (120000000,'pad_cache_after',cache),(130000000,'guest_pad_read',fighter)])
    result=module.analyze(path,guest_bit=0x40)
    assert result['xdk_slot']==2
    latency=result['phases'][0]['latency']
    assert latency['usb_complete -> xdk_read']['p50_ms']==9
    assert latency['xdk_read -> pad_cache_after']['p50_ms']==10
    assert latency['pad_cache_after -> guest_pad_read']['p50_ms']==10

def test_late_report_uses_guest_port_and_separate_timing_boundary(tmp_path):
    path=tmp_path/'late.csv'
    held=bytearray(24);held[10]=1
    body=struct.pack('<H8B4h',0,0,0,255,0,0,0,0,0,0,0,0,0)
    cache=struct.pack('<8I',0x40,*([0]*7))
    fighter=struct.pack('<12I',0x40,*([0]*11))
    write_trace(path,[(100000000,'script_virtual',bytes(held)),
        (101000000,'host_mapped',body),
        (110000000,'xdk_read',struct.pack('<I',7)+body,2),
        (119000000,'late_report',struct.pack('<I',7)+body,2),
        (120000000,'pad_cache_after',cache),(121000000,'guest_pad_read',fighter),
        (122000000,'late_shadow',bytes()),(123000000,'late_skip_busy',bytes())])
    result=module.analyze(path,guest_bit=0x40)
    assert result['xdk_slot']==2
    latency=result['phases'][0]['latency']
    assert latency['xdk_read -> pad_cache_after']['p50_ms']==10
    assert latency['late_report -> pad_cache_after']['p50_ms']==1
    assert latency['late_report -> guest_pad_read']['p50_ms']==2
    assert result['late_refresh']==dict(applied_reports=1,shadow_comparisons=1,
        shadow_mismatches=0,busy_fallbacks=1,dispatch_fallbacks=0,read_errors=0,
        entire_refresh={'count':0},unpaired_refresh_boundaries=0)

def test_entire_refresh_includes_pre_api_wait_and_fallback():
    stages=('late_refresh_begin','late_refresh_end','late_refresh_begin','late_skip_dispatch','late_refresh_end')
    rows=[dict(stage=s,t_ns=t,slot=0,tid=4,aux0=0) for s,t in zip(stages,(0,477000000,500000000,501000000,502000000))]
    health=module.late_health(rows)
    assert health['entire_refresh']['max_ms']==477
    assert health['entire_refresh']['count']==2 and health['dispatch_fallbacks']==1
    assert health['unpaired_refresh_boundaries']==0
