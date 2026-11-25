#!/usr/bin/env python3
"""Recover/refuse damaged saves using ONLY our disposable 0BSD GBA fixture."""
import argparse
import json
import time
from pathlib import Path
from emulator_usb import prepare
from smoke_gba import SHA256,inspect_frame


def exercise(c,report):
    if c.status()['state']!='menu':raise ValueError('return to library first')
    assert c.info()['firmware']=='0.1.5','this check requires the shared BatteryStore adapter'
    rom='tab5-diagnostic-ab627e01.gba'
    save=f'Tab5-Emu/saves/gba/{SHA256}/battery.sav'
    resume=c.settings()['settings']['resume'];c.settings('resume=0')
    try:
        previous=c.read_file(save+'.bak');assert len(previous)==131072 and previous[0]==0x54
        if c.stat(save+'.rejected') is None:
            c.write_file(save,b'X')
            c.launch(rom);time.sleep(.2)
            assert inspect_frame(c)['counter_units']==previous[1]%10
            c.press(['start']);c.quit()
            primary=c.read_file(save)
            assert primary[0]==0x54 and primary[1]==(previous[1]+1)&255
            assert c.read_file(save+'.bak')==previous and c.read_file(save+'.rejected')==b'X'
            report['invalid_primary_recovered']=True
            report['rejected_primary_preserved']=True
        # Invalidating both generations must refuse the game, never persist
        # a default RAM image over them. Copies remain in host memory and are
        # restored below. These paths are the generated fixture's only.
        primary=c.read_file(save);backup=c.read_file(save+'.bak')
        try:
            c.write_file(save,b'X');c.write_file(save+'.bak',b'X')
            c.launch(rom,wait=False);time.sleep(1)
            assert c.status()['state']=='menu'
            assert c.read_file(save)==b'X' and c.read_file(save+'.bak')==b'X'
            report['both_invalid_refused_without_overwrite']=True
        finally:
            c.write_file(save,primary);c.write_file(save+'.bak',backup)
            assert c.read_file(save)==primary and c.read_file(save+'.bak')==backup
        c.launch(rom);time.sleep(.2)
        assert inspect_frame(c)['counter_units']==primary[1]%10
        c.quit();report['valid_save_restored_and_game_reopened']=True
    finally:c.settings('resume='+str(resume))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    if args.report.exists():raise FileExistsError(args.report)
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    report={'rom_sha256':SHA256,'complete':False,'scope':'disposable own fixture only'}
    try:
        with Link('tab5-emu').open(timeout_s=10) as link:exercise(Control(link),report)
        report['complete']=True
    except Exception as error:report['error']=repr(error);raise
    finally:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        with args.report.open('x') as output:json.dump(report,output,indent=2);output.write('\n')
        print(json.dumps(report,indent=2))


if __name__=='__main__':main()
