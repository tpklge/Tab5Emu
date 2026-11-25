#!/usr/bin/env python3
"""Physical boot/video/controller/battery checks with the original 0BSD N64 ROM."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
from build_n64_console_rom import build
from emulator_usb import prepare,catalog_page,capture_lcd


def run(c,report,work,expected):
    from emulink.control import Buttons,ControlError
    from emulink.cli import frame_to_png
    start=c.status();info=c.info();assert start['state']=='menu' and info['elf_sha256']==expected
    report['info']=info
    report['before']=start;old=c.settings()['settings'];work.mkdir(parents=True,exist_ok=True)
    fixture=build();sha=hashlib.sha256(fixture).hexdigest();remote=f'Tab5-Emu/roms/n64/tab5-n64-console-{sha[:12]}.z64'
    assert c.read_file(remote)==fixture
    battery=f'Tab5-Emu/saves/n64/{sha}/battery.sav'
    def wait_frames(minimum=3,deadline=90):
        end=time.monotonic()+deadline
        while time.monotonic()<end:
            s=c.status()
            if s['state']=='game' and s['game_frames']>=minimum:return s
            if s.get('launch_error') or s.get('operation_error'):raise RuntimeError(s)
            time.sleep(.5)
        raise TimeoutError('N64 VI did not advance')
    def shot(name):
        frame=c.frame();assert (frame.width,frame.height)==(320,240)
        (work/(name+'.local.png')).write_bytes(frame_to_png(frame,frame.format=='rgb565be'))
        byteorder='big' if frame.format=='rgb565be' else 'little'
        colors={int.from_bytes(frame.data[i:i+2],byteorder) for i in range(0,len(frame.data),2)}
        return colors
    try:
        c.settings('resume=0');c.settings('volume=5');c.settings('input_test=1')
        report['catalog']=catalog_page(c,system='n64')
        home=capture_lcd(c);(work/'n64-battery-home.local.png').write_bytes(frame_to_png(home,home.format=='rgb565be'))
        before=c.read_file(battery) if c.stat(battery) else None
        c.launch(remote);report['boot']=wait_frames()
        assert shot('n64-green')=={0x07e0}
        previous=c.status()['game_frames'];c.buttons(Buttons.A)
        wait_frames(previous+2);assert shot('n64-red')=={0xf800};c.buttons(0)
        report['a_changes_rdp_color']=True
        try:c.save(5)
        except ControlError as e:report['state_rejected']=str(e)
        else:raise AssertionError('N64 instant state unexpectedly accepted')
        c.quit();first=c.read_file(battery);assert len(first)==198656 and first[1:4]==b'N64'
        if before:assert first[0]==(before[0]+1)&255
        c.launch(remote);report['second_boot']=wait_frames();c.quit()
        second=c.read_file(battery);assert second[0]==(first[0]+1)&255 and second[1:4]==b'N64'
        assert c.read_file(battery+'.bak')==first
        report.update(complete=True,rom_sha256=sha,battery_bytes=len(second),first_counter=first[0],second_counter=second[0],previous_battery_preserved=True,after=c.status())
    finally:
        c.buttons(0)
        if c.status()['state']=='game':c.quit()
        for key in ('volume','resume','input_test'):c.settings(f'{key}={old[key]}')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--elf',required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--work',type=Path,default=Path('artifacts'));a=p.parse_args()
    assert not a.report.exists();prepare()
    from emulink.link import Link
    from emulink.control import Control
    report={'complete':False,'elf_sha256':a.elf}
    try:
        with Link('tab5-emu').open(timeout_s=30) as link:
            c=Control(link);run(c,report,a.work,a.elf)
            sys.path.insert(0,'managed_components/espp__coredump/python');from espp_coredump.client import CoreDumpClient
            report['coredump_bytes']=CoreDumpClient(link).size();assert report['coredump_bytes']==0
    except Exception as e:report['error']=repr(e);raise
    finally:a.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('complete','first_counter','second_counter','coredump_bytes')},indent=2))
