#!/usr/bin/env python3
"""USB diagnostics/benchmarks using upstream emulink. No firmware flashing."""
import argparse
import json
import sys
import time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"components/emulink/tools/emulink"))

def prepare():
    import usb.backend.libusb1
    import libusb_package
    if not usb.backend.libusb1.get_backend(find_library=libusb_package.find_library):
        raise RuntimeError("libusb backend unavailable")

def capture_lcd(control):
    import struct
    from emulink.control import Frame, SCREENSHOT, SCREENSHOT_READ
    info=control._call(SCREENSHOT,b'lcd')
    data=bytearray()
    while len(data)<info['size']:
        part=control._call(SCREENSHOT_READ,struct.pack('<IH',len(data),min(4000,info['size']-len(data))),raw=True)
        if not part:raise RuntimeError('LCD capture ended early')
        data.extend(part)
    if len(data)!=info['size']:raise RuntimeError('LCD capture size differs')
    return Frame(info['width'],info['height'],info['format'],bytes(data))

def catalog_page(control,offset=0,system='',search=''):
    if not isinstance(offset,int) or not 0<=offset<=100000:
        raise ValueError('offset fora do limite')
    if system not in ('','gb','gbc','gba','snes','megadrive','genesis','n64') or '\0' in search or len(search.encode('utf-8'))>128:
        raise ValueError('sistema ou busca invalida')
    payload=str(offset).encode()+b'\0'+system.encode()+b'\0'+search.encode('utf-8')
    page=control._call(0x17,payload,timeout_s=25)
    if not isinstance(page.get('roms'),list) or not isinstance(page.get('total'),int) or not offset<=page.get('next',-1)<=max(offset,page['total']):
        raise ValueError('paginacao invalida recebida')
    if page['next']<page['total'] and page['next']==offset:
        raise ValueError('consulta sem progresso')
    return page

def run(args):
    prepare()
    from emulink.link import Link, find_devices
    from emulink.control import Control
    if args.action=="devices":
        return [{k:v for k,v in d.items() if k!='dev'} for d in find_devices()]
    with Link("tab5-emu").open(timeout_s=args.wait) as link:
        control=Control(link)
        if args.action=='catalog':return catalog_page(control,args.offset,args.system,args.search)
        if args.action in ("info","status","roms"):
            return getattr(control,args.action)()
        if args.action=="settings":return control.settings(args.change)
        if args.action=="launch":
            reply=control._call(0x03,args.match.encode(),timeout_s=40)
            control.wait_state('game',60)
            return {"rom":reply['rom']}
        if args.action=="quit":control.quit();return {"quit":True}
        if args.action=="press":control.press(args.buttons,hold_ms=args.hold);return {"pressed":args.buttons}
        if args.action=="tap":control.tap(args.x,args.y);return {"tap":[args.x,args.y]}
        if args.action=="reload":return {"games":control.reload_roms()}
        if args.action=="push":return {"files":control.push(str(args.source),args.remote)}
        if args.action in ("save","load"):getattr(control,args.action)(args.slot);return {args.action:args.slot}
        if args.action=="screenshot":
            from emulink.cli import frame_to_png
            frame=capture_lcd(control) if args.lcd else control.frame()
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_bytes(frame_to_png(frame,frame.format=="rgb565be"))
            return {"file":str(args.output),"width":frame.width,"height":frame.height,"format":frame.format}
        if args.action=="benchmark":
            if args.seconds < 3:
                raise ValueError("benchmark exige pelo menos 3 segundos")
            samples=[]
            started=time.monotonic()
            while time.monotonic()-started<args.seconds:
                stamp=time.monotonic()
                status=control.status()
                if status.get('state') != 'game' or status.get('console') != 'Game Boy Advance':
                    raise RuntimeError("benchmark GBA exige jogo GBA ativo; nenhuma medição publicada")
                if samples and status.get('rom') != samples[0]['status'].get('rom'):
                    raise RuntimeError("ROM mudou durante o benchmark")
                diagnostics=control.settings()
                if 'frames_emulated' not in diagnostics.get('gba_metrics',{}):
                    raise RuntimeError("firmware sem instrumentação GBA")
                samples.append({"host_seconds":stamp-started,"status":status,"diagnostics":diagnostics})
                time.sleep(max(0,1-(time.monotonic()-stamp)))
            first,last=samples[0],samples[-1]
            elapsed=last['host_seconds']-first['host_seconds']
            a,b=first['diagnostics'].get('gba_metrics',{}),last['diagnostics'].get('gba_metrics',{})
            emulated=b.get('frames_emulated',0)-a.get('frames_emulated',0)
            presented=last['status']['frames_presented']-first['status']['frames_presented']
            if emulated <= 0 or presented < 0 or elapsed <= 0:
                raise RuntimeError("contadores sem progresso ou reiniciados; medição inválida")
            result={"host_interval_seconds":elapsed,"emulated_frames":emulated,"presented_frames":presented,
                "emulated_frames_per_second":emulated/elapsed if elapsed else None,
                "relative_gba_speed":emulated/elapsed/59.7275 if elapsed else None,
                "presented_frames_per_second":presented/elapsed if elapsed else None,"samples":samples}
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(json.dumps(result,indent=2)+'\n')
            return {k:v for k,v in result.items() if k!='samples'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--wait',type=float,default=10)
    sub=p.add_subparsers(dest='action',required=True)
    for action in ('devices','status','info','roms','reload','quit'):sub.add_parser(action)
    s=sub.add_parser('settings');s.add_argument('change',nargs='?')
    s=sub.add_parser('launch');s.add_argument('match')
    s=sub.add_parser('catalog');s.add_argument('--offset',type=int,default=0);s.add_argument('--system',default='');s.add_argument('--search',default='')
    s=sub.add_parser('press');s.add_argument('buttons',nargs='+');s.add_argument('--hold',type=int,default=150)
    s=sub.add_parser('tap');s.add_argument('x',type=int);s.add_argument('y',type=int)
    s=sub.add_parser('push');s.add_argument('source',type=Path);s.add_argument('remote')
    for action in ('save','load'):
        s=sub.add_parser(action);s.add_argument('slot',type=int)
    s=sub.add_parser('screenshot');s.add_argument('output',type=Path);s.add_argument('--lcd',action='store_true')
    s=sub.add_parser('benchmark');s.add_argument('--seconds',type=float,default=30);s.add_argument('--output',type=Path,required=True)
    args=p.parse_args();print(json.dumps(run(args),indent=2,ensure_ascii=False))

if __name__=='__main__':main()
