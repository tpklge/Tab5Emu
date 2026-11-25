#!/usr/bin/env python3
"""Physical GB/SNES SRAM tests using only our original 0BSD counter ROMs."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from emulator_usb import prepare
from build_8bit_test_roms import gameboy,snes


def test_system(c,system,builder,extension,archive_test_reject):
    from emulink.control import ControlError
    data=builder();digest=hashlib.sha256(data).hexdigest()
    name='tab5-sram-'+digest[:12]+extension
    remote='Tab5-Emu/roms/'+system+'/'+name
    if c.read_file(remote)!=data:raise ValueError('fixture ROM differs; no SRAM modified')
    battery='Tab5-Emu/saves/'+system+'/'+digest+'/battery.sav'
    result={'system':system,'rom_sha256':digest,'boots':[],'complete':False}
    if archive_test_reject and c.stat(battery+'.rejected') is not None:
        # Only the exact one-byte sentinel produced by this test is eligible.
        # Any other rejected data is kept untouched for manual inspection.
        rejected=c.read_file(battery+'.rejected')
        if rejected!=b'X':raise ValueError('unknown rejected fixture data; preserved')
        archive=battery+'.rejected.previous-test'
        if c.stat(archive) is None:c.write_file(archive,rejected)
        assert c.read_file(archive)==rejected
        c.rm(battery+'.rejected')
        result['previous_test_reject_archived']=True
    def read_counter():
        payload=c.read_file(battery)
        assert len(payload)==8192 and payload[0]==0x54,'guest CPU did not write the expected SRAM signature'
        return payload
    baseline=0
    if c.stat(battery):baseline=read_counter()[1]
    expected1=(baseline+1)&255;expected2=(expected1+1)&255
    def boot(load=False,save=False):
        c.launch(name);c.settings('input_test=1');time.sleep(.25)
        status=c.status();assert status['state']=='game' and status['game_frames']>0
        if save:c.save(5)
        if load:c.load(5);time.sleep(.1)
        c.quit();time.sleep(.2)
        payload=read_counter();result['boots'].append({'counter':payload[1],'loaded_state':load,'saved_state':save,'frames_before_quit':status['game_frames']})
        return payload
    first=boot(save=True);assert first[1]==expected1
    second=boot();assert second[1]==expected2
    assert c.read_file(battery+'.bak')==first
    restored=boot(load=True);assert restored[1]==expected1
    assert c.read_file(battery+'.bak')==second
    fourth=boot();assert fourth[1]==expected2
    assert c.read_file(battery+'.bak')==first
    result['normal_save_survives_cold_core_init']=True
    result['state_restores_sram']=True
    result['previous_distinct_generation_preserved']=True
    # Corrupt ONLY the disposable fixture's primary, after retaining a valid
    # backup. Do not run this operation on user-game paths.
    if c.stat(battery+'.rejected') is None:
        c.write_file(battery,b'X')
        recovered=boot();assert recovered[1]==expected2
        assert c.read_file(battery+'.bak')==first
        assert c.read_file(battery+'.rejected')==b'X'
        result['invalid_primary_recovered_from_backup']=True
        result['rejected_primary_preserved']=True
    else:result['recovery_not_repeated']='existing rejected fixture preserved'
    # Invalid-state tests also use only this generated ROM's unused slot.
    suffix={'gb':'_gb.sav','gbc':'_gbc.sav','snes':'_snes.sav'}[system]
    invalid='Tab5-Emu/states/'+system+'/'+digest+'/core-v1/'+Path(name).stem+'_4'+suffix
    if c.stat(invalid) is None and c.stat(invalid+'.bak') is None:
        c.launch(name);c.settings('input_test=1');time.sleep(.15);before=c.status()
        c.write_file(invalid,b'X')
        try:
            try:c.load(4)
            except ControlError:result['invalid_state_rejected']=True
            else:raise AssertionError('truncated state accepted')
            time.sleep(.15);after=c.status()
            assert after['state']=='game' and after['game_frames']>before['game_frames']
        finally:c.rm(invalid)
        c.quit();read_counter()
        result['invalid_state_fixture_removed']=True
    result['complete']=True
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--archive-test-reject',action='store_true')
    args=parser.parse_args()
    if args.report.exists():raise FileExistsError(args.report)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    report={'complete':False,'systems':[],'input_source':'CPU instructions in our GB/SNES ROMs; no injected SRAM'}
    try:
        with Link('tab5-emu').open(timeout_s=15) as link:
            c=Control(link)
            if c.status()['state']!='menu':raise ValueError('return to library before running SRAM tests')
            report['info']=c.info();settings=c.settings()['settings'];resume=settings['resume'];input_test=settings['input_test']
            try:
                c.settings('resume=0')
                for system,builder,ext in [('gb',gameboy,'.gb'),('gbc',lambda:gameboy(True),'.gbc'),('snes',snes,'.sfc')]:
                    print('Testing guest-written SRAM:',system,flush=True)
                    result=test_system(c,system,builder,ext,args.archive_test_reject)
                    report['systems'].append(result);print(json.dumps(result),flush=True)
                report['complete']=True
            finally:
                c.settings('resume='+str(resume));c.settings('input_test='+str(input_test))
    except Exception as error:
        report['error']=repr(error);raise
    finally:
        with args.report.open('x') as output:json.dump(report,output,indent=2);output.write('\n')
        print('SRAM report written:',args.report,flush=True)


if __name__=='__main__':main()
