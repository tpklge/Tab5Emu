#!/usr/bin/env python3
"""Paired on-device interpreter/RV32 measurements with own licensed fixtures."""
import argparse,hashlib,json,time,statistics
from pathlib import Path
from emulator_usb import prepare

LIGHT='ab627e010bda9132328e5b2f69b5ec1c98f19a5a45737a9bce1dc4553d03d234'

def compare(control,manifest,seconds,repeats,report):
    own=Path(manifest);fixture=json.loads(own.read_text())
    scenes=[('light','Tab5-Emu/roms/gba/tab5-diagnostic-ab627e01.gba',LIGHT),
            ('alu-smc','Tab5-Emu/roms/gba/tab5-rv32-bench.gba',fixture['sha256'])]
    for _,rom,sha in scenes:
        if hashlib.sha256(control.read_file(rom)).hexdigest()!=sha:raise RuntimeError('fixture hash differs')
    previous=control.settings();resume=previous['settings']['resume'];cpu=previous['gba_cpu_requested'];input_test=previous['settings']['input_test']
    info=control.info();report.update(firmware=info['firmware'],elf_sha256=info['elf_sha256'],runs=[],complete=False,
        scope='Two own homebrews, same firmware/settings, paired backend selection. Reinitializes core; no claim of a cold physical SD cache.')
    try:
        control.settings('resume=0')
        for scene,rom,sha in scenes:
            for repeat in range(repeats):
                # Alternate order to avoid assigning all later trials to one backend.
                for backend in ([0,1] if repeat%2==0 else [1,0]):
                    expected='gpsp-rv32' if backend else 'gpsp-interpreter'
                    report['pending_run']=dict(scene=scene,repeat=repeat,backend=expected,rom_sha256=sha)
                    control.settings('gba_cpu='+str(backend));load_start=time.monotonic();control.launch(rom);load_seconds=time.monotonic()-load_start
                    control.settings('input_test=1');time.sleep(1)
                    cold=control.settings()['gba_metrics']
                    if cold['backend']!=expected:raise RuntimeError('requested backend is not actually running')
                    samples=[];start=time.monotonic()
                    for n in range(seconds+1):
                        stamp=time.monotonic();status=control.status();diag=control.settings()
                        assert status['state']=='game' and not status['paused'] and status['rom']=='/sdcard/'+rom,status
                        assert diag['gba_metrics']['backend']==expected
                        samples.append(dict(seconds=stamp-start,status=status,diagnostics=diag))
                        if n<seconds:time.sleep(max(0,1-(time.monotonic()-stamp)))
                    a,b=samples[0],samples[-1];elapsed=b['seconds']-a['seconds']
                    metrics={k:b['diagnostics']['gba_metrics'][k]-a['diagnostics']['gba_metrics'][k]
                        for k in ('frames_emulated','frames_drawn','frames_skipped','run_us','video_callback_us','audio_callback_us','pacing_us','execute_us','hardware_us','ppu_us','jit_us','rom_page_us','rom_page_loads','sd_read_us','sd_read_bytes','sd_read_calls')}
                    frames=metrics['frames_emulated'];assert frames>0
                    sink={k:b['diagnostics']['audio_sink'][k]-a['diagnostics']['audio_sink'][k]
                        for k in ('blocks','partial_blocks','empty_blocks','missing_frames','write_errors')}
                    frames_at_quit=control.status()['game_frames']
                    control.quit();correctness={}
                    if scene=='alu-smc':
                        raw=control.read_file(f'Tab5-Emu/saves/gba/{sha}/battery.sav')
                        correctness=dict(magic=raw[:4].decode('ascii',errors='replace'),guest_iterations=int.from_bytes(raw[4:8],'little'),
                            alu_checksum=int.from_bytes(raw[8:12],'little'),arm_thumb_smc_failures=int.from_bytes(raw[12:16],'little'))
                        correctness['frames_before_quit']=frames_at_quit
                        correctness['guest_iterations_per_emulated_frame']=correctness['guest_iterations']/frames_at_quit
                        report['pending_run']['correctness']=correctness
                        assert correctness['magic']=='T5RV' and correctness['guest_iterations']>0
                        assert correctness['alu_checksum']==fixture['alu_checksum']
                        assert correctness['arm_thumb_smc_failures']==0
                    run=dict(scene=scene,repeat=repeat,backend=expected,rom_sha256=sha,seconds=elapsed,
                        emulated_frames_per_second=frames/elapsed,relative_speed=frames/elapsed/59.7275,
                        mean_run_us=metrics['run_us']/frames,load_host_seconds=load_seconds,metrics=metrics,audio_sink=sink,
                        cold_first_second_metrics=cold,correctness=correctness,samples=samples)
                    report['runs'].append(run)
                    report.pop('pending_run',None)
                    print(json.dumps({k:run[k] for k in ('scene','repeat','backend','relative_speed','mean_run_us','correctness')}),flush=True)
        work={backend:statistics.median(r['correctness']['guest_iterations_per_emulated_frame'] for r in report['runs']
            if r['scene']=='alu-smc' and r['backend']==backend) for backend in ('gpsp-interpreter','gpsp-rv32')}
        ratio=work['gpsp-rv32']/work['gpsp-interpreter']
        report['guest_timing_check']=dict(iterations_per_emulated_frame=work,rv32_to_interpreter_ratio=ratio,
            equivalent_within_ten_percent=.9<=ratio<=1.1,
            scope='Whole-run deterministic guest loop counts normalized by emulated frames; not an original-hardware timing oracle.')
        report['production_approved']=False
        report['decision']='Keep interpreter default. Experimental RV32 requires timing and retail compatibility validation.'
        report['complete']=True
    except Exception as error:
        report['error']=repr(error)
        raise
    finally:
        try:
            status=control.status()
            if status['state']=='game' and not status['paused']:control.quit()
            control.settings('resume='+str(resume));control.settings('gba_cpu='+str(cpu))
            control.settings('input_test='+str(input_test))
        except Exception as error:
            # A crash disconnects USB. Preserve the original failure and mark
            # preferences for explicit recovery instead of masking its cause.
            report['cleanup_error']=repr(error)
            if 'error' not in report:raise

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--manifest',type=Path,default=Path('artifacts/test-roms/tab5-rv32-bench-manifest.json'))
    p.add_argument('--seconds',type=int,default=20);p.add_argument('--repeats',type=int,default=2);p.add_argument('--report',type=Path,required=True)
    a=p.parse_args();assert 5<=a.seconds<=300 and 1<=a.repeats<=10
    if a.report.exists():raise FileExistsError(a.report)
    prepare();from emulink.link import Link
    from emulink.control import Control
    result={}
    try:
        with Link('tab5-emu').open(timeout_s=30) as link:compare(Control(link),a.manifest,a.seconds,a.repeats,result)
    finally:a.report.write_text(json.dumps(result,indent=2)+'\n')
