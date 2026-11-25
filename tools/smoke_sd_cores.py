#!/usr/bin/env python3
"""Exercise exact-build SD cores on Tab5, using original diagnostic ROMs only."""
import argparse,hashlib,json,sys,time
from pathlib import Path
from emulator_usb import prepare,catalog_page,capture_lcd
from build_8bit_test_roms import gameboy,snes
from build_md_test_rom import build as megadrive
from build_n64_console_rom import build as n64


def run(c,report,build,expected):
    from emulink.cli import frame_to_png
    assert c.status()['state']=='menu' and c.info()['elf_sha256']==expected
    report['info']=c.info();before=c.settings();old=before['settings'];report['before']=c.status()
    assert before['gba_cpu_requested']==0 and before['limits']['gba_cpu']==[0,0]
    report['catalog_totals']={s:catalog_page(c,system=s)['total'] for s in ('gb','gbc','gba','snes','megadrive','n64')}
    assert all(report['catalog_totals'].values())
    fixtures=[]
    for system,data,extension in [('gb',gameboy(),'.gb'),('gbc',gameboy(True),'.gbc'),('snes',snes(),'.sfc')]:
        sha=hashlib.sha256(data).hexdigest();fixtures.append((system,f'Tab5-Emu/roms/{system}/tab5-sram-{sha[:12]}{extension}',sha))
    data,_=megadrive();sha=hashlib.sha256(data).hexdigest();fixtures.append(('megadrive','Tab5-Emu/roms/megadrive/tab5-md-sram-'+sha[:12]+'.md',sha))
    fixtures.append(('gba','Tab5-Emu/roms/gba/tab5-diagnostic-ab627e01.gba','ab627e010bda9132328e5b2f69b5ec1c98f19a5a45737a9bce1dc4553d03d234'))
    data=n64();sha=hashlib.sha256(data).hexdigest();fixtures.append(('n64','Tab5-Emu/roms/n64/tab5-n64-console-'+sha[:12]+'.z64',sha))
    for _,path,sha in fixtures:assert hashlib.sha256(c.read_file(path)).hexdigest()==sha
    report['boots']=[]
    def boot(system,path):
        started=time.monotonic();c.launch(path);end=time.monotonic()+90
        while time.monotonic()<end:
            state=c.status()
            if state.get('operation_error') or state.get('launch_error'):raise RuntimeError(state)
            if state['game_frames']>=(3 if system=='n64' else 20):break
            time.sleep(.2)
        else:raise TimeoutError('Core did not advance: '+system)
        f=c.frame();assert any(f.data)
        c.quit();after=c.status();assert after['state']=='menu' and not after['last_panic']
        report['boots'].append({'system':system,'rom':path,'load_and_run_seconds':round(time.monotonic()-started,3),'frames':state['game_frames'],'frame_size':[f.width,f.height],'core_metrics':state.get('core_metrics',{}),'after_psram_free':after['psram_free'],'after_psram_largest':after['psram_largest']})
        print('Boot/quit from SD passed:',system,flush=True)
    try:
        c.settings('resume=0');c.settings('volume=5');c.settings('input_test=1');c.console_filter('all');time.sleep(.4)
        f=capture_lcd(c);Path('artifacts/sd-cores-home.local.png').write_bytes(frame_to_png(f,f.format=='rgb565be'))
        for system,path,_ in fixtures:boot(system,path)
        # Repeated cross-core loads must replace executable bytes and release N64 RAM.
        for index in (0,5,4,5,2,3):
            system,path,_=fixtures[index];boot(system,path)
        # A malformed copy of this build's own GBC module must never run.
        manifest=json.loads((build/'sd/Tab5-Emu/cores/manifest.json').read_text())
        member=next(m for m in manifest['cores'] if m['core']=='gbc');payload=(build/'sd/Tab5-Emu/cores'/member['file']).read_bytes();remote='Tab5-Emu/cores/'+member['file']
        assert hashlib.sha256(c.read_file(remote)).hexdigest()==member['sha256']
        try:
            c.write_file(remote,b'bad');c._call(0x03,fixtures[0][1].encode(),timeout_s=20)
            end=time.monotonic()+10
            while time.monotonic()<end:
                state=c.status()
                if state['launch_error']:break
                time.sleep(.2)
            assert state['state']=='menu' and 'corrompido' in state['launch_error'],state
            report['truncated_core_rejected']=True
        finally:
            c.write_file(remote,payload);assert hashlib.sha256(c.read_file(remote)).hexdigest()==member['sha256']
            report['damaged_test_module_restored']=True
        boot(fixtures[0][0],fixtures[0][1]);report['core_recovery_passed']=True
        c.console_filter('all');report['after']=c.status();report['settings_after']=c.settings();report['complete']=True
    finally:
        if c.status()['state']=='game':c.quit()
        for key in ('volume','resume','input_test'):c.settings(f'{key}={old[key]}')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--elf',required=True);p.add_argument('--build',type=Path,default=Path('build-sd-cores'));p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    assert not a.report.exists();prepare();from emulink.link import Link;from emulink.control import Control
    report={'complete':False,'elf_sha256':a.elf}
    try:
        with Link('tab5-emu').open(timeout_s=30) as link:
            run(Control(link),report,a.build,a.elf)
            sys.path.insert(0,'managed_components/espp__coredump/python');from espp_coredump.client import CoreDumpClient
            report['coredump_bytes']=CoreDumpClient(link).size();assert report['coredump_bytes']==0
    except Exception as e:report['error']=repr(e);raise
    finally:a.report.write_text(json.dumps(report,indent=2)+'\n')
    print('All SD cores, switching, malformed-file refusal and recovery passed.')
