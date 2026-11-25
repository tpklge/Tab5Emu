#!/usr/bin/env python3
"""Compare real core variants to the same guest VI; host timings are not Tab5 FPS."""
import argparse,json,os,statistics,subprocess,time
from pathlib import Path
VARIANTS=(('reference',0,0,1),('audio_hle',0,1,1),('fast_cpu',1,0,1),('fast_cpu_audio_hle',1,1,1),('fast_cpu_audio_hle_160',1,1,2))
def run(executable,rom,output,target=600,repeats=3):
 report={'format':'tab5-n64-host-alternatives-v1','target_vi':target,'repeats':repeats,'timings_are_host_only':True,'variants':[]}
 for name,cpu,audio,step in VARIANTS:
  entry={'name':name,'fast_cpu':bool(cpu),'audio_hle':bool(audio),'raster_step':step,'runs':[]};report['variants'].append(entry)
  for repetition in range(repeats):
   env=dict(os.environ,TAB5_N64_TARGET_VI=str(target),TAB5_N64_FAST_CPU=str(cpu),TAB5_N64_AUDIO_HLE=str(audio),TAB5_N64_RASTER_STEP=str(step))
   env.pop('TAB5_N64_PLAY',None)
   start=time.monotonic();result=subprocess.run([str(executable),str(rom),'500000000'],env=env,text=True,capture_output=True,check=True)
   metrics=json.loads(result.stdout);assert not metrics['error'] and metrics['vi_frames']==target
   entry['runs'].append(dict(metrics,host_wall_s=time.monotonic()-start));output.write_text(json.dumps(report,indent=2)+'\n')
  entry['median_run_us']=statistics.median(x['run_us'] for x in entry['runs'])
  entry['speedup_vs_reference']=report['variants'][0]['median_run_us']/entry['median_run_us']
  if step==1:
   reference=report['variants'][0]['runs'][0]
   for metric in ('instructions','vi_frames','frame_checksum','audio_frames','pc','cause','epc'):
    if any(x[metric]!=reference[metric] for x in entry['runs']):raise ValueError('Guest state mismatch '+name+' '+metric)
   entry['guest_state_matches_reference']=True
  print(name,round(entry['median_run_us']/1e6,4),'host seconds;',round(entry['speedup_vs_reference'],3),'x',flush=True)
  output.write_text(json.dumps(report,indent=2)+'\n')
 report['complete']=True;output.write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--executable',type=Path,required=True);p.add_argument('--rom',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--target-vi',type=int,default=600);p.add_argument('--repeats',type=int,default=3);a=p.parse_args();run(a.executable.resolve(),a.rom.resolve(),a.output,a.target_vi,a.repeats)
