#!/usr/bin/env python3
"""Analyze RECOMP_INPUT_TRACE CSV; observed software timing, never button-to-photon.

python scripts/input-latency.py logs/input-trace-123.csv --button x
An isolated scripted button can calibrate its guest bit; otherwise pass --guest-bit.
Dropped records, unfinished tails, censored matches and ambiguous matches are explicit.
"""
import argparse
from bisect import bisect_left
from collections import Counter, defaultdict, deque
import csv
import json
from pathlib import Path
import struct

BUTTONS = dict(up=0,down=1,left=2,right=3,start=4,back=5,ls=6,rs=7,a=8,b=9,x=10,y=11,
               black=12,white=13,lt=14,rt=15)
SDL_BUTTONS = dict(a=0,b=1,x=2,y=3,back=4,start=6,ls=7,rs=8,black=9,white=10,up=11,down=12,left=13,right=14)

def distribution(values):
    if not values:
        return dict(count=0)
    ordered=sorted(values)
    def percentile(p):
        at=(len(ordered)-1)*p
        lo=int(at); hi=min(lo+1,len(ordered)-1)
        return round((ordered[lo]+(ordered[hi]-ordered[lo])*(at-lo))/1e6,4)
    return dict(count=len(ordered),min_ms=round(ordered[0]/1e6,4),p50_ms=percentile(.5),
                p95_ms=percentile(.95),p99_ms=percentile(.99),max_ms=round(ordered[-1]/1e6,4))

def worker_cpu(rows):
    samples=[r for r in rows if r['stage']=='host_thread_cpu']
    if len(samples)<2: return dict(samples=len(samples))
    elapsed=samples[-1]['t_ns']-samples[0]['t_ns']
    ticks=sum(samples[-1][k]-samples[0][k] for k in ('aux0','aux1'))
    if elapsed<=0 or ticks<0: return dict(samples=len(samples),invalid_counter=True)
    return dict(samples=len(samples),window_s=round(elapsed/1e9,3),cpu_s=round(ticks/1e7,6),
        percent_of_one_core=round(ticks*100/elapsed*100,4))

def retention_health(rows):
    losses={}
    for row in rows:
        if row['stage']!='input_edge_loss': continue
        item=losses.setdefault(str(row['slot']),dict(overflow=0,expired=0))
        item['overflow']=max(item['overflow'],row['aux0'])
        item['expired']=max(item['expired'],row['aux1'])
    return dict(slots=losses,total_discarded=sum(sum(v.values()) for v in losses.values()),
        cancellation_events=sum(r['stage']=='input_edge_cancel' for r in rows),
        note='Retention discards are game-path events, distinct from lost trace records. Focus/UI cancellation is intentional.')

def late_health(rows):
    pending={};durations=[];unpaired=0
    for row in rows:
        key=(row.get('tid',0),row['slot'])
        if row['stage']=='late_refresh_begin':
            if key in pending: unpaired+=1
            pending[key]=row['t_ns']
        elif row['stage']=='late_refresh_end':
            begin=pending.pop(key,None)
            if begin is None: unpaired+=1
            else: durations.append(row['t_ns']-begin)
    return dict(applied_reports=sum(r['stage']=='late_report' for r in rows),
        shadow_comparisons=sum(r['stage']=='late_shadow' for r in rows),
        shadow_mismatches=sum(r['stage']=='late_shadow' and r['aux0']!=0 for r in rows),
        busy_fallbacks=sum(r['stage']=='late_skip_busy' for r in rows),
        dispatch_fallbacks=sum(r['stage']=='late_skip_dispatch' for r in rows),
        read_errors=sum(r['stage']=='late_read_error' for r in rows),
        entire_refresh=distribution(durations),unpaired_refresh_boundaries=unpaired+len(pending))

def match_edges(sources,targets,maximum_ns=250000000):
    """Match only unambiguous same-button rises; do not shift after a merged tap."""
    pending=deque(); i=0; delays=[]; censored=0; ambiguous=0; orphan=0
    for target in targets:
        while i<len(sources) and sources[i]<=target:
            pending.append(sources[i]); i+=1
        while pending and target-pending[0]>maximum_ns:
            pending.popleft(); censored+=1
        if len(pending)==1:
            delays.append(target-pending.popleft())
        elif len(pending)>1:
            ambiguous+=len(pending); pending.clear()
        else:
            orphan+=1
    return dict(**distribution(delays),unmatched_or_merged_or_later=censored+len(pending)+len(sources)-i,
                ambiguous_source_edges=ambiguous,target_without_source=orphan,matching_window_ms=maximum_ns/1e6)

def load_rows(path):
    rows=[]; malformed=0
    with Path(path).open(newline='',encoding='utf-8') as stream:
        for row in csv.DictReader(stream):
            try:
                item={k:int(row[k]) for k in ('t_ns','seq','tid','slot','aux0','aux1','n')}
                item['stage']=row['stage']; item['data']=bytes.fromhex(row['payload'])
                if len(item['data'])!=item['n']: raise ValueError('payload size')
                rows.append(item)
            except (TypeError,ValueError,KeyError): malformed+=1
    rows.sort(key=lambda r:(r['t_ns'],r['seq']))
    return rows,malformed

def gamepad_bits(data,usb=False):
    offset=2 if usb else 0
    digital=struct.unpack_from('<H',data,offset)[0]&255
    analog=data[offset+2:offset+10]
    if len(analog)!=8: raise ValueError('short gamepad')
    return digital|sum((1<<(8+i)) for i,v in enumerate(analog) if v)

def edges(rows,button,slot,guest_bit=None,xdk_slot=None):
    rises=defaultdict(list); previous={}; raw_guest=Counter()
    bit=1<<BUTTONS[button]
    for row in rows:
        stage=row['stage']; data=row['data']; stamp=row['t_ns']
        selected = xdk_slot if (stage.startswith('xdk_read') or stage=='late_report') and xdk_slot is not None else slot
        if row['slot']!=selected: continue
        mask=None
        if stage=='script_virtual': mask=sum(1<<i for i,v in enumerate(data[:16]) if v)
        elif stage in ('host_mapped','usb_host_read','input_edge_consumed'): mask=gamepad_bits(data)
        elif stage in ('usb_sample','usb_latched','usb_built','usb_complete'): mask=gamepad_bits(data,True)
        elif stage in ('xdk_read','late_report') and len(data)==22: mask=gamepad_bits(data[4:])
        elif stage in ('pad_cache_before','pad_cache_after') and len(data)==32 and guest_bit:
            value=struct.unpack_from('<I',data)[0]
            mask=bit if value&guest_bit else 0
        elif stage=='fighter_input_ack' and guest_bit:
            mask=bit if row['aux0']&guest_bit else 0
        elif stage in ('guest_pad_read','fighter_after') and len(data)==48:
            words=struct.unpack('<12I',data)
            if words[11]&1: continue  # CPU flag bit0; mapped pad index is in bits8+.
            raw_guest[words[0]&0x30f0]+=1
            if guest_bit:
                value=words[0] if stage=='guest_pad_read' else words[8]
                mask=bit if value&guest_bit else 0
        elif stage=='sdl_button' and len(data)==2 and data[0]==SDL_BUTTONS.get(button):
            mask=bit if data[1] else 0
        if mask is None: continue
        down=bool(mask&bit)
        if down and not previous.get(stage,False): rises[stage].append(stamp)
        previous[stage]=down
    return rises,raw_guest

def analyze(path,button='x',slot=0,guest_bit=None,xdk_slot=None):
    path=Path(path); rows,malformed=load_rows(path)
    meta_path=Path(str(path)+'.json')
    meta=json.loads(meta_path.read_text()) if meta_path.exists() else {}
    checkpoint=meta.get('last_ns',rows[-1]['t_ns'] if rows else 0)
    # The harness may kill the process; exclude its uncheckpointed final tail.
    tail=sum(r['t_ns']>checkpoint for r in rows)
    rows=[r for r in rows if r['t_ns']<=checkpoint]
    observed,_=edges(rows,button,slot,xdk_slot=xdk_slot)
    xdk_method='explicit' if xdk_slot is not None else 'same slot; physical mapping unverified'
    if xdk_slot is None and observed.get('script_virtual'):
        candidates=[]
        for port in range(4):
            probe,_=edges(rows,button,slot,xdk_slot=port)
            if probe.get('xdk_read'): candidates.append(port)
        if len(candidates)==1:
            xdk_slot=candidates[0]
            xdk_method='unique XDK port with selected button rises in isolated scripted capture'
    groups=[]
    for stamp in observed.get('script_virtual',[]):
        if not groups or stamp-groups[-1][-1]>3000000000: groups.append([])
        groups[-1].append(stamp)
    if not groups and rows: groups=[[rows[0]['t_ns'],rows[-1]['t_ns']]]
    phases=[]
    for group in groups:
        start=max(0,group[0]-50000000); end=min(checkpoint,group[-1]+500000000)
        window=[r for r in rows if start<=r['t_ns']<=end]
        _,guest_values=edges(window,button,slot,xdk_slot=xdk_slot)
        calibration=guest_bit
        if calibration is None and observed.get('script_virtual'):
            combined=0; constant=0x30f0
            for value in guest_values: combined|=value; constant&=value
            combined&=~constant
            if combined and not combined&(combined-1): calibration=combined
        rising,_=edges(window,button,slot,calibration,xdk_slot)
        # Never turn an already-held state at the start of a window into a new edge.
        whole_rises,_=edges(rows,button,slot,calibration,xdk_slot)
        rising={stage:[t for t in stamps if start<=t<=end] for stage,stamps in whole_rises.items()}
        cadence={}
        for stage in ('host_poll_begin','sampler_tick','game_step','xdk_read','pad_cache_after','guest_pad_read','usb_latched','present_end'):
            selected=xdk_slot if stage=='xdk_read' and xdk_slot is not None else slot
            stamps=[r['t_ns'] for r in window if r['stage']==stage and (r['slot'] in (-1,selected))]
            cadence[stage]=distribution([b-a for a,b in zip(stamps,stamps[1:])])
        pairs={}
        for first,last in (('script_virtual','host_mapped'),('sdl_button','host_mapped'),
            ('script_virtual','guest_pad_read'),('host_mapped','guest_pad_read'),('host_mapped','usb_sample'),
            ('host_mapped','usb_complete'),('usb_complete','guest_pad_read'),
            ('usb_complete','xdk_read'),('xdk_read','pad_cache_after'),('pad_cache_after','guest_pad_read'),
            ('host_mapped','late_report'),('late_report','pad_cache_after'),('late_report','guest_pad_read'),
            ('guest_pad_read','fighter_after')):
            if first in rising and last in rising:
                pairs[first+' -> '+last]=match_edges(rising[first],rising[last])
        present=[r['t_ns'] for r in window if r['stage']=='present_end']
        waits=[]
        for t in rising.get('fighter_after',[]):
            index=bisect_left(present,t)
            if index<len(present): waits.append(present[index]-t)
        pairs['fighter_after -> next Present end (boundary only)']=distribution(waits)
        phases.append(dict(start_s=round(start/1e9,4),end_s=round(end/1e9,4),
            guest_bit=hex(calibration) if calibration else None,
            guest_bit_calibration='explicit' if guest_bit else 'inferred only if isolated script varies one action bit; constant modifiers excluded',
            observed_rising_edges={k:len(v) for k,v in rising.items()},cadence=cadence,latency=pairs,
            input_worker_cpu=worker_cpu(window),
            guest_pad_action_values={hex(k):v for k,v in guest_values.items()}))
    losses=sum(meta.get(k,0) for k in ('drop_lock','drop_full','drop_budget','bad_payload','io_errors'))
    return dict(schema=1,trace=str(path),button=button,slot=slot,xdk_slot=xdk_slot,xdk_slot_method=xdk_method,
        rows=len(rows),malformed_rows=malformed,input_retention=retention_health(rows),
        pad_cache_writers={hex(writer):sum(r['stage']=='pad_cache_after' and r['aux1']==writer for r in rows)
            for writer in sorted({r['aux1'] for r in rows if r['stage']=='pad_cache_after'})},
        input_worker_cpu=worker_cpu(rows),
        late_refresh=late_health(rows),
        quality=dict(metadata=meta,loss_reported=losses,uncheckpointed_tail_rows_excluded=tail,
            trace_complete=bool(meta) and not losses and not malformed and bool(meta.get('closed')),
            checkpointed_windows_without_reported_loss=bool(meta) and not losses and not malformed),phases=phases,
        limits=['Software observations only; neither physical actuation nor display scanout is measured.',
            'SDL/raw/host/report/guest stages are separate. Poll cadence is not hardware report rate.',
            'Edge matching is excluded when ambiguous and censored beyond 250ms; unmatched means merged, missing, or later.',
            'Next Present is a timing boundary and does not prove when an attack animation or pixel responded.',
            'Reported trace loss prevents a claim that unobserved input edges were lost by the game.'])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace');parser.add_argument('--button',choices=BUTTONS,default='x')
    parser.add_argument('--slot',type=int,default=0)
    parser.add_argument('--xdk-slot',type=int,choices=range(4),help='guest XDK port ordinal; can differ from host/fighter slot')
    parser.add_argument('--guest-bit',type=lambda s:int(s,0))
    parser.add_argument('--output',help='JSON result; default <trace>.analysis.json')
    args=parser.parse_args(); result=analyze(args.trace,args.button,args.slot,args.guest_bit,args.xdk_slot)
    output=Path(args.output or args.trace+'.analysis.json')
    output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('trace','quality','phases')},indent=2))
    print('Analysis: '+str(output))
    return 0 if result['quality']['checkpointed_windows_without_reported_loss'] else 2

if __name__=='__main__': raise SystemExit(main())
