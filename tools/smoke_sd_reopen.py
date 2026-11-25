#!/usr/bin/env python3
"""Reopen only our GB fixtures after MSC eject; retain CDC storage diagnostics."""
import argparse
import hashlib
import json
import sys
import threading
import time
from pathlib import Path
from emulator_usb import ROOT, prepare


def run(c, report):
    manifest = json.loads((ROOT/'artifacts/test-roms/tab5-gb-compat-manifest.json').read_text())
    previous = c.settings()['settings']
    assert c.status()['state'] == 'menu' and not c.status()['usb_drive']
    report.update(info=c.info(), complete=False, boots=[])
    for label in ('rtc', 'mbc2'):
        path = 'Tab5-Emu/roms/gb/' + manifest[label]['file']
        assert hashlib.sha256(c.read_file(path)).hexdigest() == manifest[label]['sha256']
    try:
        c.settings('resume=0'); c.settings('input_test=1')
        for n in range(12):
            label = 'mbc2' if n % 3 == 2 else 'rtc'
            c.launch('Tab5-Emu/roms/gb/' + manifest[label]['file'])
            time.sleep(.4)
            status = c.status()
            assert status['state'] == 'game' and not status['paused'] and status['game_frames'] > 5
            c.quit()
            assert c.status()['state'] == 'menu'
            battery = c.read_file('Tab5-Emu/saves/gb/' + manifest[label]['sha256'] + '/battery.sav')
            if label == 'mbc2': assert battery[:3] == bytes((11, 11, 7))
            else: assert battery[0] == ord('R')
            report['boots'].append(dict(fixture=label, frames=status['game_frames']))
        report['msc_after'] = c.settings()['msc_io']
        assert not report['msc_after']['host_allowed']
        report['complete'] = True
    finally:
        if c.status()['state'] == 'game': c.quit()
        c.settings('resume=' + str(previous['resume']))
        c.settings('input_test=' + str(previous['input_test']))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists(): raise FileExistsError(args.report)
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    import serial
    report = {}; stop = threading.Event(); chunks = []
    with serial.Serial('/dev/cu.usbmodemtab5_emu1', 115200, timeout=.1) as console:
        def capture():
            while not stop.is_set():
                data = console.read(4096)
                if data: chunks.append(data)
        reader = threading.Thread(target=capture, daemon=True); reader.start()
        try:
            with Link('tab5-emu').open(timeout_s=30) as link:
                run(Control(link), report)
                sys.path.insert(0, str(ROOT/'managed_components/espp__coredump/python'))
                from espp_coredump.client import CoreDumpClient
                report['coredump_bytes'] = CoreDumpClient(link).size()
                assert report['coredump_bytes'] == 0
        except Exception as error:
            report.update(complete=False, error=repr(error)); raise
        finally:
            stop.set(); reader.join(timeout=2)
            log = b''.join(chunks).decode(errors='replace')
            args.report.with_suffix('.log').write_text(log)
            errors = ('sdmmc_read_sectors_dma: sdmmc_read_blocks failed',
                      'sdmmc_write_sectors_dma: sdmmc_write_blocks failed',
                      'Guru Meditation', 'abort() was called')
            report['storage_or_panic_errors_in_cdc'] = [s for s in errors if s in log]
            if report['storage_or_panic_errors_in_cdc']: report['complete'] = False
            args.report.write_text(json.dumps(report, indent=2)+'\n')
            print(json.dumps({k:v for k,v in report.items() if k != 'info'}, indent=2))
    if not report.get('complete'): raise RuntimeError('SD reopen regression failed; see report/log')
