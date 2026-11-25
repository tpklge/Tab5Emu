#!/usr/bin/env python3
"""Exercise save/load/exit refusals using ONLY the exact owned GBA fixture."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from emulator_usb import prepare
from smoke_gba import SHA256

def run(c, expected_elf, report, capture=False):
    from emulink.control import ControlError
    rom='Tab5-Emu/roms/gba/tab5-diagnostic-ab627e01.gba'
    if c.info()['elf_sha256']!=expected_elf or c.status()['state']!='menu':
        raise ValueError('expected stable firmware and library; nothing modified')
    if hashlib.sha256(c.read_file(rom)).hexdigest()!=SHA256:
        raise ValueError('owned GBA fixture differs; nothing modified')
    base=f'Tab5-Emu/states/gba/{SHA256}/core-v1/'+Path(rom).stem
    slot=next((n for n in (4,3,2,1,0) if all(c.stat(base+f'_{n}'+suffix) is None
        for suffix in ('_gba.sav','_gba.sav.bak','_gba.sav.tmp','.bin','.bin.bak','.bin.tmp'))),None)
    if slot is None:raise ValueError('no free disposable fixture slot')
    state=base+f'_{slot}_gba.sav'
    battery=f'Tab5-Emu/saves/gba/{SHA256}/battery.sav'
    blocked_battery=battery+'.sha256.tmp'
    if c.stat(blocked_battery) is not None:
        raise ValueError('existing temporary battery data preserved')
    old=c.settings()['settings'];created=[]
    report.update(complete=False,elf_sha256=expected_elf,fixture_sha256=SHA256,slot=slot)
    def remove_owned_directory(path):
        if c.stat(path) is not None:
            if c.ls(path):raise RuntimeError('temporary directory gained data; preserved: '+path)
            c.rm(path)
    def expect_error(text):
        status=c.status();assert status['state']=='game' and text in status['operation_error'],status
        assert c.info()['elf_sha256']==expected_elf
        return status['operation_error']
    def pause_with_error(text):
        c.tap(1170,60);time.sleep(.3);assert c.status()['paused']
        expect_error(text)
        if capture:
            from emulink.cli import frame_to_png
            frame=c.frame()
            assert (frame.width,frame.height)==(1280,720)
            name={'salvar estado':'save','carregar estado':'load','Saida cancelada':'quit'}[text]
            Path('artifacts/stable-rest-'+name+'-error.local.png').write_bytes(frame_to_png(frame,frame.format=='rgb565be'))
    try:
        c.settings('volume=5');c.settings('resume=0')
        c.launch(rom);time.sleep(.3);c.settings('input_test=1');c.save(slot)
        original=c.read_file(state);digest=hashlib.sha256(original).hexdigest()
        c.mkdir(state+'.tmp');created.append(state+'.tmp')
        try:c.save(slot)
        except ControlError:pass
        else:raise AssertionError('blocked save unexpectedly succeeded')
        report['save_error']=expect_error('salvar estado')
        assert hashlib.sha256(c.read_file(state)).hexdigest()==digest
        report['previous_state_preserved']=True
        pause_with_error('salvar estado');c.press(['B']);time.sleep(.2)
        remove_owned_directory(state+'.tmp');created.remove(state+'.tmp')
        c.write_file(state,b'X')
        try:c.load(slot)
        except ControlError:pass
        else:raise AssertionError('truncated state accepted')
        report['load_error']=expect_error('carregar estado')
        before=c.status()['game_frames'];time.sleep(.2);assert c.status()['game_frames']>before
        pause_with_error('carregar estado');c.press(['B']);time.sleep(.2)
        c.write_file(state,original);c.load(slot);assert not c.status()['operation_error']
        report['valid_load_clears_error']=True
        c.press(['start']);time.sleep(.2)
        previous_battery=c.read_file(battery)
        c.mkdir(blocked_battery);created.append(blocked_battery)
        c.quit(wait=False);time.sleep(.5)
        report['quit_error']=expect_error('Saida cancelada')
        assert c.read_file(battery)==previous_battery
        report['previous_battery_preserved']=True
        pause_with_error('Saida cancelada')
        # Last pause setting is the normal Launcher return. Persistence must
        # refuse it, keep this app running and leave the menu open.
        c.press(['up']);c.press(['A']);time.sleep(.4)
        report['launcher_error']=expect_error('Launcher cancelado')
        assert c.status()['paused']
        remove_owned_directory(blocked_battery);created.remove(blocked_battery)
        c.press(['B']);time.sleep(.2);c.quit()
        assert c.read_file(battery)!=previous_battery
        report['retry_persists_battery']=True
        report['complete']=True
    except Exception as error:
        report['error']=repr(error);raise
    finally:
        try:
            for path in created:remove_owned_directory(path)
            if c.status()['state']=='game':
                if c.status()['paused']:c.press(['B']);time.sleep(.2)
                c.quit()
            for suffix in ('_gba.sav','_gba.sav.bak','.bin','.bin.bak'):
                path=base+f'_{slot}'+suffix
                if c.stat(path) is not None:c.rm(path)
            for key in ('volume','resume','input_test'):c.settings(f'{key}={old[key]}')
            report['disposable_files_removed']=True
        except Exception as error:
            report['cleanup_error']=repr(error);report['complete']=False
            if 'error' not in report:raise

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf-sha256',required=True);parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--capture',action='store_true',help='requires pause snapshot support (protocol 6)')
    args=parser.parse_args()
    if args.report.exists():raise FileExistsError(args.report)
    prepare();from emulink.link import Link;from emulink.control import Control
    report={'complete':False}
    try:
        with Link('tab5-emu').open(timeout_s=15) as link:run(Control(link),args.elf_sha256,report,args.capture)
    finally:args.report.write_text(json.dumps(report,indent=2)+'\n')
