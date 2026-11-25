#!/usr/bin/env python3
"""Record raw A164/USB HID while keyboard actions are isolated from the UI."""
import argparse
import json
import time
from pathlib import Path
from emulator_usb import prepare


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds',type=float,default=90)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--restore-actions',action='store_true',help='disable isolation on exit; default leaves the test screen safe for late key presses')
    args=parser.parse_args()
    if args.report.exists():raise FileExistsError(args.report)
    if not 1<=args.seconds<=600:raise ValueError('duration must be 1..600 seconds')
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    events=[];report={'events':events,'complete':False,'physical_key_identity_not_injected':True}
    with Link('tab5-emu').open(timeout_s=10) as link:
        control=Control(link)
        if control.status()['state']!='menu':raise ValueError('return to library first')
        control.settings('input_test=1')
        time.sleep(.5) # let a just-completed remote quit finish its HOME setup
        control.settings('input_test=1')
        start=time.monotonic();previous=None
        try:
            while time.monotonic()-start<args.seconds:
                diagnostics=control.settings()
                p=diagnostics['platform']
                isolated=diagnostics['settings'].get('input_test')==1
                current=(tuple(p['keys_hid']),p['a164_present'],p.get('keyboard_buttons'),isolated)
                if current!=previous:
                    event={'seconds':time.monotonic()-start,'keys_hid':list(current[0]),'a164_present':current[1],'keyboard_buttons':current[2],'input_test':isolated}
                    events.append(event);print(json.dumps(event),flush=True);previous=current
                if not isolated:raise RuntimeError('keyboard isolation was disabled; capture stopped')
                time.sleep(.03)
            report['complete']=True
        except KeyboardInterrupt:
            report['complete']=True
            report['stopped_by_operator']=True
        except Exception as error:
            report['error']=repr(error)
            raise
        finally:
            report['duration_seconds']=time.monotonic()-start
            try:
                if args.restore_actions:control.settings('input_test=0')
            finally:
                combinations={'E+A+O+K':[4,8,14,18],'T+Y':[23,28],'E+A+I+O+J+K+T+Y':[4,8,12,13,14,18,23,28]}
                report['exact_combinations_observed']={name:any(e['keys_hid']==keys for e in events) for name,keys in combinations.items()}
                report['no_keyboard_game_actions']=all(e['keyboard_buttons']==0 for e in events)
                args.report.parent.mkdir(parents=True,exist_ok=True)
                with args.report.open('x') as output:json.dump(report,output,indent=2);output.write('\n')
                print(json.dumps({k:v for k,v in report.items() if k!='events'},indent=2),flush=True)


if __name__=='__main__':main()
