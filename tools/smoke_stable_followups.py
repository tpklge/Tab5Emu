#!/usr/bin/env python3
"""Stable RTC/pause/editor/resources/catalog checks with owned ROMs and rollback."""
import argparse
import hashlib
import json
import struct
import time
from pathlib import Path
from emulator_usb import prepare, catalog_page
from smoke_gba import SHA256 as GBA_SHA
from smoke_large_library import wait

def capture(c, label):
    from emulink.cli import frame_to_png
    frame=c.frame()
    assert (frame.width,frame.height)==(1280,720)
    Path('artifacts/stable-rest-'+label+'.local.png').write_bytes(frame_to_png(frame,frame.format=='rgb565be'))
    return hashlib.sha256(frame.data).hexdigest()

def state_clock(raw):
    fields={}
    for at in range(0,4096,8):
        tag=raw[at:at+4]
        if tag==b'\0'*4:break
        fields[tag]=struct.unpack_from('<I',raw,at+4)[0]
    assert fields[b'rtcS']==0
    return ((fields[b'rtcd']*86400+fields[b'rtch']*3600+fields[b'rtcm']*60+fields[b'rtcs'])*60+fields[b'rtct'])%(512*86400*60)

def check_rtc(c, report):
    fixture=json.loads(Path('artifacts/test-roms/tab5-gb-compat-manifest.json').read_text())['rtc']
    rom='Tab5-Emu/roms/gb/'+fixture['file'];sha=fixture['sha256']
    assert hashlib.sha256(c.read_file(rom)).hexdigest()==sha
    stem=f'Tab5-Emu/states/gb/{sha}/core-v1/'+Path(rom).stem
    suffixes=('_gb.sav','_gb.sav.bak','_gb.sav.tmp','.bin','.bin.bak','.bin.tmp')
    slot=next((n for n in range(5) if all(c.stat(stem+f'_{n}'+suffix) is None for suffix in suffixes)),None)
    if slot is None:raise ValueError('no disposable RTC slot')
    state=stem+f'_{slot}_gb.sav'
    try:
        c.launch(rom);time.sleep(.3);started=time.monotonic();c.save(slot)
        initial=state_clock(c.read_file(state))
        c.tap(1170,60);time.sleep(.25);assert c.status()['paused']
        frames=c.status()['game_frames'];time.sleep(2.2)
        assert c.status()['game_frames']==frames
        report['pause_snapshot_sha256']=capture(c,'rtc-pause')
        c.press(['B']);time.sleep(.15);c.save(slot)
        delta=(state_clock(c.read_file(state))-initial)%(512*86400*60)
        elapsed=time.monotonic()-started
        assert delta>=120 and abs(delta/60-elapsed)<2,(delta,elapsed)
        report['pause_advance_ticks_60hz']=delta
        report['pause_test_wall_seconds']=elapsed
        # Load through the real menu after a separate pause. Only time after
        # load belongs to the restored timeline.
        saved=state_clock(c.read_file(state))
        c.tap(1170,60);time.sleep(2.2)
        c.press(['down']);c.press(['down']);c.press(['A']);time.sleep(1.1)
        assert c.status()['paused'] and not c.status()['operation_error']
        c.press(['B']);time.sleep(.15);c.save(slot)
        delta=(state_clock(c.read_file(state))-saved)%(512*86400*60)
        assert 60<=delta<180,delta
        report['after_state_load_ticks_60hz']=delta
        report['complete']=True
    finally:
        if c.status()['state']=='game':
            if c.status()['paused']:c.press(['B']);time.sleep(.2)
            c.quit()
        for suffix in suffixes:
            path=stem+f'_{slot}'+suffix
            if c.stat(path) is not None:c.rm(path)
        report['disposable_slot_removed']=True

def check_editor(c, report):
    rom='Tab5-Emu/roms/gba/tab5-diagnostic-ab627e01.gba'
    assert hashlib.sha256(c.read_file(rom)).hexdigest()==GBA_SHA
    path='Tab5-Emu/config/input_profiles.json'
    original=c.read_file(path) if c.stat(path) else None
    try:
        c.launch(rom);time.sleep(.2);c.tap(1170,60);time.sleep(.25)
        for _ in range(6):c.press(['down'])
        c.press(['A']);time.sleep(.25)
        report['editor_snapshot_sha256']=capture(c,'editor')
        c.press(['down']);c.press(['B']);time.sleep(.2)
        report['pause_snapshot_sha256']=capture(c,'editor-cancelled')
        assert report['editor_snapshot_sha256']!=report['pause_snapshot_sha256']
        c.press(['B']);time.sleep(.2);assert not c.status()['paused'];c.quit()
        assert (c.read_file(path) if c.stat(path) else None)==original
        report['cancel_preserves_profiles']=True;report['complete']=True
    finally:
        if c.status()['state']=='game':
            if c.status()['paused']:c.press(['B']);time.sleep(.2)
            if c.status()['paused']:raise RuntimeError('editor did not close; game preserved')
            c.quit()

def check_resources(c, report):
    config=json.loads(c.read_file('Tab5-Emu/config/settings.json'))
    name=config.get('ui',{}).get('theme','system')
    if name not in ('system','dark','light'):raise ValueError('unknown configured theme preserved')
    path='Tab5-Emu/assets/themes/'+name+'.json'
    original=c.read_file(path)
    theme=json.loads(original)
    if theme.get('schema_version')!=1 or theme.get('mode') not in ('console','grayscale','invert'):
        raise ValueError('preexisting invalid theme preserved')
    try:
        c.write_file(path,b'{"schema_version":2,"mode":"console"}')
        c.reload_roms();time.sleep(.5)
        assert c.settings()['resources']['theme_error']
        capture(c,'invalid-theme')
        report['wrong_schema_explained']=True
    finally:
        c.write_file(path,original);assert c.read_file(path)==original
        c.reload_roms();time.sleep(.5)
        assert not c.settings()['resources']['theme_error']
        report['theme_restored']=True
    report['complete']=True

def check_catalog(c, report, backup):
    paths=('Tab5-Emu/library/games.json','Tab5-Emu/cache/library-v1.t5i')
    fixture=Path('artifacts/test-roms/large-library')
    previous={p:c.read_file(p) if c.stat(p) else None for p in paths}
    for n,p in enumerate(paths):
        if previous[p] is not None:
            with backup.with_name(backup.stem+f'-catalog-{n}.local.bin').open('xb') as out:
                out.write(previous[p])
    try:
        for path in paths:
            raw=(fixture/path).read_bytes();c.write_file(path,raw);assert c.read_file(path)==raw
        c.reload_roms();wait(c)
        for offset in (0,63,64,4928,4992,5000):
            page=catalog_page(c,offset);assert page['total']==5000
            if offset<5000:assert page['roms'] and page['next']>offset
            else:assert not page['roms']
        found=catalog_page(c,search='unico')
        assert found['total']==1 and 'ÚnicoTesteFinal' in found['roms'][0]['name']
        report['full_catalog_pages_and_accent_search']=True
        c.settings('library_page=78');wait(c)
        rom='Tab5-Emu/roms/gba/tab5-diagnostic-ab627e01.gba'
        assert all(g['path']!='/sdcard/'+rom for g in c.roms())
        c.launch(rom);time.sleep(.25);assert c.status()['game_frames']>0;c.quit()
        report['launch_outside_visible_page']=True;report['complete']=True
    finally:
        if c.status()['state']=='game':c.quit()
        for path,raw in previous.items():
            if raw is None:
                if c.stat(path):c.rm(path)
            else:c.write_file(path,raw);assert c.read_file(path)==raw
        c.reload_roms();report['catalog_restored']=True

def run(c, expected, report, backup):
    assert c.info()['elf_sha256']==expected and c.status()['state']=='menu'
    old=c.settings()['settings']
    report.update(complete=False,elf_sha256=expected)
    try:
        c.settings('volume=5');c.settings('resume=0');c.settings('input_test=1')
        for name,check in (('rtc',check_rtc),('editor',check_editor),('resources',check_resources)):
            report[name]={};check(c,report[name]);print(name+' passed',flush=True)
        report['catalog']={};check_catalog(c,report['catalog'],backup);print('catalog passed',flush=True)
        report['complete']=True
    except Exception as error:report['error']=repr(error);raise
    finally:
        for key in ('volume','resume','input_test'):c.settings(f'{key}={old[key]}')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf-sha256',required=True);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    if args.report.exists():raise FileExistsError(args.report)
    prepare();from emulink.link import Link;from emulink.control import Control
    report={'complete':False}
    try:
        with Link('tab5-emu').open(timeout_s=20) as link:run(Control(link),args.elf_sha256,report,args.report)
    finally:args.report.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
