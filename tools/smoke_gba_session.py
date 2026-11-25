#!/usr/bin/env python3
"""Observe the owned GBA tone, paused producer and persistence; no acoustic claim."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from emulator_usb import prepare
from smoke_gba import SHA256, smoke

def run(c, expected, seconds, report, require_zero=False):
    from emulink.control import ControlError
    rom='Tab5-Emu/roms/gba/tab5-diagnostic-ab627e01.gba'
    if c.info()['elf_sha256']!=expected or c.status()['state']!='menu':
        raise ValueError('expected ELF and library required')
    if hashlib.sha256(c.read_file(rom)).hexdigest()!=SHA256:
        raise ValueError('owned ROM differs; nothing modified')
    old=c.settings()['settings']
    report.update(complete=False,elf_sha256=expected,fixture_sha256=SHA256)
    try:
        try:c.settings('gba_cpu=1')
        except ControlError:report['jit_activation_rejected']=True
        else:raise AssertionError('stable accepted JIT')
        for setting in ('resume=0','input_test=1','volume=5'):c.settings(setting)
        c.launch(rom);time.sleep(.4)
        before=c.settings()['audio_sink'];start=time.monotonic();time.sleep(seconds)
        after=c.settings()['audio_sink'];elapsed=time.monotonic()-start
        keys=('blocks','partial_blocks','empty_blocks','missing_frames','write_errors')
        delta={key:after[key]-before[key] for key in keys}
        report['tone_window']={'backend':'gpsp-interpreter','source':'owned 0BSD ~440 Hz tone',
            'seconds':elapsed,'sink_delta':delta,'zero_missing_frames':delta['missing_frames']==0,
            'acoustic_measurement':False}
        assert delta['blocks']>seconds*40 and delta['write_errors']==0,delta
        if require_zero:assert delta['missing_frames']==0,delta
        c.tap(1170,60);time.sleep(.2);assert c.status()['paused']
        frames=c.settings()['audio']['source_frames'];time.sleep(2)
        assert c.settings()['audio']['source_frames']==frames
        report['pause_stops_producer']=True
        c.press(['B']);time.sleep(.4)
        report['max_submit_gap_after_resume_us']=c.settings()['audio']['max_submit_gap_us']
        assert report['max_submit_gap_after_resume_us']<1000000
        report['persistence']={};smoke(c,report['persistence'])
        report['complete']=True
    except Exception as error:report['error']=repr(error);raise
    finally:
        if c.status()['state']=='game':
            if c.status()['paused']:c.press(['B']);time.sleep(.2)
            c.quit()
        for key in ('input_test','resume','volume'):c.settings(f'{key}={old[key]}')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf-sha256',required=True);parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--seconds',type=float,default=20)
    parser.add_argument('--require-zero',action='store_true',help='fail also on any inserted silence')
    args=parser.parse_args()
    if not 3<=args.seconds<=120:parser.error('seconds must be 3..120')
    if args.report.exists():raise FileExistsError(args.report)
    prepare();from emulink.link import Link;from emulink.control import Control
    report={'complete':False}
    try:
        with Link('tab5-emu').open(timeout_s=20) as link:run(Control(link),args.elf_sha256,args.seconds,report,args.require_zero)
    finally:args.report.write_text(json.dumps(report,indent=2)+'\n')
