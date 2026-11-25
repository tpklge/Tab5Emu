#!/usr/bin/env python3
"""Measure guest Timer0 cycles using the licensed mGBA assembly subset.

Temporarily replaces ONLY the verified, original T5RV diagnostic ROM, keeping
the user's catalog unchanged. Restores and reads back that ROM before exit.
Nonzero exit on timing mismatches; results are not an emulator-speed claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
from emulator_usb import prepare

ROOT=Path(__file__).resolve().parents[1]
ALIAS='Tab5-Emu/roms/gba/tab5-rv32-bench.gba'
MODES=['arm_rom_'+w for w in ('0000','4000','0004','4004','0010','4010','0014','4014')]+[
    'arm_ewram','arm_iwram']+['thumb_rom_'+w for w in ('0000','4000','0004','4004','0010','4010','0014','4014')]+[
    'thumb_ewram','thumb_iwram']

def digest(data):return hashlib.sha256(data).hexdigest()

def decode(raw,fixture):
    count=len(fixture['cases'])
    if raw[:4]!=b'T5TM' or len(raw)<256+count*240 or struct.unpack_from('<4I',raw,4)!=(1,count,20,count):
        raise ValueError('Guest timing suite did not finish; no conformance result')
    cases=[];passed=total=0
    for i,case in enumerate(fixture['cases']):
        baseline=struct.unpack_from('<20i',raw,256+i*240)
        observed=struct.unpack_from('<20i',raw,336+i*240)
        relative=struct.unpack_from('<20i',raw,416+i*240)
        results=[]
        for mode,base,value,got,expected in zip(MODES,baseline,observed,relative,case['expected']):
            total+=1;passed+=got==expected
            results.append(dict(mode=mode,baseline=base,observed=value,relative=got,
                                expected=expected,passed=got==expected,error=got-expected))
        cases.append(dict(name=case['name'],function=case['function'],results=results))
    # A core that updates Timer0 only at scheduler boundaries cannot resolve
    # these tiny instruction sequences. Do not classify zero/quantized reads
    # as hundreds of independently established CPU timing defects.
    nop=next(case for case in cases if case['function']=='testNop2')
    resolved=sum(result['relative']>0 for result in nop['results'])
    usable=resolved>=15
    return dict(cases=cases,execution_completed=True,comparisons=total,passed=passed,
                failed=total-passed,conformance_passed=passed==total,
                timer_reads_usable=usable,
                assessment='compared with suite expectations' if usable else
                'Timer0 resolution is insufficient for short instruction sequences; mismatch count is not a conformance diagnosis')

def run(c,report,expected_elf,cpu,raw_output):
    report['info']=c.info()
    if report['info']['elf_sha256']!=expected_elf:raise ValueError('Unexpected firmware; no ROM changed')
    if c.status()['state']!='menu':raise ValueError('Return to HOME before probing; no ROM changed')
    directory=ROOT/'artifacts/test-roms'
    fixture=json.loads((directory/'tab5-gba-timing-manifest.json').read_text())
    original_manifest=json.loads((directory/'tab5-rv32-bench-manifest.json').read_text())
    data=(directory/'tab5-gba-timing.gba').read_bytes()
    original=c.read_file(ALIAS)
    if digest(data)!=fixture['sha256']:raise ValueError('Timing image does not match its manifest')
    if digest(original)!=original_manifest['sha256'] or original[0xac:0xb0]!=b'T5RV':
        raise ValueError('Template differs from our original T5RV fixture; preserved')
    report.update(rom_sha256=fixture['sha256'],reference_commit=fixture['reference_commit'],
        original_template_sha256=digest(original),template_restored=False)
    (ROOT/'artifacts/gba-timing-template-backup.local.bin').write_bytes(original)
    changed=False
    previous=c.settings();settings=previous['settings']
    try:
        c.settings('volume=5');c.settings('resume=0')
        if cpu is not None:c.settings('gba_cpu='+str(cpu))
        changed=True;c.write_file(ALIAS,data)
        if digest(c.read_file(ALIAS))!=digest(data):raise ValueError('Timing ROM SD readback differs')
        c.launch(ALIAS);c.settings('input_test=1');time.sleep(2)
        report['active_status']=c.status();report['active_metrics']=c.settings().get('gba_metrics',{})
        c.quit()
        raw=c.read_file('Tab5-Emu/saves/gba/'+digest(data)+'/battery.sav')
        raw_output.write_bytes(raw)
        report.update(decode(raw,fixture))
    finally:
        if changed:
            state=c.status()
            if state['state']=='game' and state.get('rom','').endswith('/tab5-rv32-bench.gba'):
                if state['paused']:c.press(['B']);time.sleep(.2)
                c.quit()
            current=c.read_file(ALIAS)
            if current!=original:
                if current!=data and not (len(current)<=len(data) and data.startswith(current)):
                    raise ValueError('Template changed unexpectedly; host backup retained, unknown file preserved')
                c.write_file(ALIAS,original)
            if c.read_file(ALIAS)!=original:raise ValueError('Original template restoration failed')
            report['template_restored']=True
        c.settings('resume='+str(settings['resume']))
        c.settings('input_test='+str(settings['input_test']))
        if cpu is not None:c.settings('gba_cpu='+str(previous['gba_cpu_requested']))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--expected-elf',required=True)
    p.add_argument('--report',type=Path,required=True)
    p.add_argument('--cpu',type=int,choices=(0,1),help='Select interpreter (0) or experimental RV32 (1)')
    a=p.parse_args()
    if a.report.exists():raise FileExistsError(a.report)
    prepare();from emulink.link import Link;from emulink.control import Control
    report={'execution_completed':False,'conformance_passed':False,'input_source':'guest Timer0; no RAM injection'}
    try:
        with Link('tab5-emu').open(timeout_s=20) as link:
            run(Control(link),report,a.expected_elf,a.cpu,a.report.with_suffix('.bin'))
    except Exception as error:
        report['error']=repr(error);raise
    finally:
        a.report.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({k:v for k,v in report.items() if k not in ('cases','info','active_status','active_metrics')},indent=2))
    return 0 if report['conformance_passed'] else 2
if __name__=='__main__':raise SystemExit(main())
