#!/usr/bin/env python3
"""Physical GB RTC/MBC2 and MD six-button/SMD checks using own fixtures only."""
import argparse,hashlib,json,struct,time
from pathlib import Path
from emulator_usb import prepare

def check_rtc_snapshot(clock, latched):
    # This fixture initializes once, then preserves its clock across boots.
    # It may have advanced several days since the first test session.
    assert len(clock)==9 and clock[0]==0x32544352 and len(latched)==5
    assert 0<=clock[1]<512 and 0<=clock[2]<24 and 0<=clock[3]<60 and 0<=clock[4]<60
    assert 0<=clock[5]<60 and clock[6] in (0,1) and clock[7]==1
    sec,minute,hour,day,flags=latched
    assert sec<60 and minute<60 and hour<24 and flags&0x80
    assert bool(flags&0x40)==bool(clock[6])
    live=clock[1]*86400+clock[2]*3600+clock[3]*60+clock[4]
    snap=(day|((flags&1)<<8))*86400+hour*3600+minute*60+sec
    assert (live-snap)%(512*86400)<=1, 'latched registers differ from saved RTC'

def run(c,report):
    gb=json.loads(Path('artifacts/test-roms/tab5-gb-compat-manifest.json').read_text())
    md=json.loads(Path('artifacts/test-roms/tab5-md-pad6-manifest.json').read_text())
    old=c.settings();resume=old['settings']['resume'];six=old['md_six_buttons']
    report.update(firmware=c.info()['firmware'],elf_sha256=c.info()['elf_sha256'],complete=False)
    try:
        c.settings('resume=0')
        for label in ('rtc','mbc2'):
            sha=gb[label]['sha256'];rom='Tab5-Emu/roms/gb/'+gb[label]['file']
            assert hashlib.sha256(c.read_file(rom)).hexdigest()==sha
            c.launch(rom);time.sleep(5 if label=='rtc' else .5);assert c.status()['game_frames']>5
            c.quit();raw=c.read_file(f'Tab5-Emu/saves/gb/{sha}/battery.sav')
            if label=='mbc2':
                assert raw[0]==11 and raw[1]==11 and raw[2]==7
                report['mbc2']={'nibble_write':raw[0],'mirror_read':raw[1],'zero_bank_alias':raw[2],'container_bytes':len(raw)}
            else:
                assert raw[0]==ord('R')
                clock_path=f'Tab5-Emu/saves/gb/{sha}/rtc-v2.bin'
                clock=struct.unpack('<8IQ',c.read_file(clock_path))
                check_rtc_snapshot(clock,raw[8:13])
                report['rtc']={'clock_day':clock[1],'clock_seconds':clock[4],'carry':clock[7],'timestamp':clock[8],'latched_registers':list(raw[8:13])}
                # Verify offline advancement only if the board clock is trusted.
                if old.get('clock_epoch',0) and clock[8]:
                    start=clock[1]*86400+clock[2]*3600+clock[3]*60+clock[4];time.sleep(3)
                    c.launch(rom);time.sleep(.5);c.quit()
                    again=struct.unpack('<8IQ',c.read_file(clock_path))
                    end=again[1]*86400+again[2]*3600+again[3]*60+again[4]
                    delta=(end-start)%(512*86400)
                    assert delta>=3
                    report['rtc']['offline_advance_seconds']=delta
                else:report['rtc']['offline_physical']='not counted: board clock untrusted'
        c.settings('md_six_buttons=1')
        for ext in ('md','smd'):
            sha=md[ext]['sha256'];rom='Tab5-Emu/roms/megadrive/'+md[ext]['file']
            assert hashlib.sha256(c.read_file(rom)).hexdigest()==sha
            c.launch(rom);time.sleep(.3)
            # X/Y/Z/Mode plus Start must reach the guest without opening menus.
            c.buttons(4|1024|2048|16|32);time.sleep(.35);assert not c.status()['paused'];c.buttons(0)
            state=f'Tab5-Emu/states/genesis/{sha}/core-v1/'+Path(rom).stem
            if ext=='md':c.save(5)
            c.quit();raw=c.read_file(f'Tab5-Emu/saves/genesis/{sha}/battery.sav')
            assert raw[:3]==b'T56' and raw[4:8].hex()==md['expected_snapshot_hex'],raw[:16].hex()
            report[ext]={'rom_sha256':sha,'six_button_snapshot':raw[4:8].hex(),'mode_start_reaches_guest':True,'battery_bytes':len(raw)}
            slot=next((n for n in (4,3,2,1,0) if c.stat(state+f'_{n}_genesis.sav') is None and c.stat(state+f'_{n}_genesis.sav.bak') is None),None) if ext=='md' else None
            if slot is not None:
                c.settings('md_six_buttons=0');c.launch(rom);c.load(5);c.save(slot);c.quit()
                saved=c.read_file(state+f'_{slot}_genesis.sav')
                assert saved[92:96]==b'PAD6' and saved[99]==0
                c.settings('md_six_buttons=1');c.launch(rom);c.load(slot);c.save(slot);c.quit()
                saved=c.read_file(state+f'_{slot}_genesis.sav');assert saved[99]==1
                report['state_keeps_current_controller_mode_both_directions']=True
                report['controller_state_disposable_slot']=slot
            c.settings('md_six_buttons=1')
        report['complete']=True
    finally:
        if c.status()['state']=='game' and not c.status()['paused']:c.quit()
        c.settings('resume='+str(resume));c.settings('md_six_buttons='+str(int(six)))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    if a.report.exists():raise FileExistsError(a.report)
    prepare();from emulink.link import Link
    from emulink.control import Control
    result={}
    try:
        with Link('tab5-emu').open(timeout_s=30) as link:run(Control(link),result)
    finally:a.report.write_text(json.dumps(result,indent=2)+'\n')
