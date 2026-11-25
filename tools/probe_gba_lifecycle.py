#!/usr/bin/env python3
"""Exercise GBA scene changes followed by the owned ALU/SMC ROM over USB."""
import argparse
import hashlib
import json
import time
from pathlib import Path

from emulator_usb import ROOT, prepare


def run(args):
    # Never replace an earlier report, including an interrupted run.
    with args.output.open('x') as report:
        report.write('{}\n')
    result = {'complete': False, 'cycles': []}

    def record():
        args.output.write_text(json.dumps(result, indent=2) + '\n')

    def phase(name, **details):
        result['phase'] = name
        result['active'] = details
        record()

    prepare()
    from emulink.link import Link
    from emulink.control import Control
    import sys
    sys.path.insert(0, str(ROOT / 'managed_components/espp__coredump/python'))
    from espp_coredump.client import CoreDumpClient

    fixture = json.loads(args.manifest.read_text())
    with Link(args.serial).open(timeout_s=15) as link:
        control = Control(link)
        old = None
        try:
            result['info'] = control.info()
            if result['info']['elf_sha256'] != args.elf_sha256:
                raise RuntimeError('unexpected firmware ELF; no game launched')
            if control.status()['state'] != 'menu':
                raise RuntimeError('start from the library; current game preserved')
            dump = CoreDumpClient(link)
            if dump.size():
                raise RuntimeError('archive and verify the existing core dump before testing')
            if hashlib.sha256(control.read_file(args.rom)).hexdigest() != fixture['sha256']:
                raise RuntimeError('ALU/SMC ROM differs from the manifest')
            scenes = [json.loads(path.read_text()) for path in args.scene]
            for scene in scenes:
                if hashlib.sha256(control.read_file(scene['path'])).hexdigest() != scene['sha256']:
                    raise RuntimeError('scene state differs from its recorded hash')
            old = control.settings()
            for change in ('volume=5', 'resume=0', 'input_test=1', 'gba_cpu=1'):
                control.settings(change)
            initial = control.status()
            for cycle in range(args.cycles):
                item = {'cycle': cycle, 'scenes': []}
                result['cycles'].append(item)
                record()
                for scene in scenes:
                    phase('scene launch', cycle=cycle, state_sha256=scene['sha256'])
                    control.launch(scene['rom'].removeprefix('/sdcard/'))
                    # Let startup run before restoring the recorded machine.
                    time.sleep(args.seconds)
                    phase('scene load', cycle=cycle, state_sha256=scene['sha256'])
                    control.load(scene['slot'])
                    time.sleep(args.seconds)
                    status = control.status()
                    backend = control.settings()['gba_metrics']['backend']
                    phase('scene quit', cycle=cycle, state_sha256=scene['sha256'], backend=backend)
                    control.quit()
                    item['scenes'].append({'state_sha256': scene['sha256'],
                                           'backend': backend, 'frames': status['game_frames'],
                                           'quit_confirmed': True})
                    record()
                phase('ALU/SMC launch', cycle=cycle)
                control.launch(args.rom)
                time.sleep(args.seconds)
                status = control.status()
                metrics = control.settings()['gba_metrics']
                item['alu_before_quit'] = {'status': status, 'metrics': metrics}
                record()  # Preserve the last observation even if quit resets the device.
                if status['paused'] or metrics['backend'] != 'gpsp-rv32':
                    raise RuntimeError('ALU/SMC did not run with RV32')
                phase('ALU/SMC quit', cycle=cycle)
                control.quit()
                raw = control.read_file('Tab5-Emu/saves/gba/' + fixture['sha256'] + '/battery.sav')
                if len(raw) < 16:
                    raise RuntimeError('truncated ALU/SMC battery')
                guest = {'magic': raw[:4].decode('ascii'),
                         'iterations': int.from_bytes(raw[4:8], 'little'),
                         'checksum': int.from_bytes(raw[8:12], 'little'),
                         'smc_failures': int.from_bytes(raw[12:16], 'little')}
                guest['iterations_per_frame'] = guest['iterations'] / max(1, status['game_frames'])
                item['guest'] = guest
                if (guest['magic'] != 'T5RV' or guest['checksum'] != 4368456
                        or guest['smc_failures'] or not .31 < guest['iterations_per_frame'] < .35):
                    raise RuntimeError('ALU/SMC or aggregate guest timing failed')
                home = control.status()
                if (home['state'] != 'menu' or home['uptime_ms'] <= initial['uptime_ms']
                        or control.info()['elf_sha256'] != args.elf_sha256 or dump.size()):
                    raise RuntimeError('return to library, firmware identity or core dump check failed')
                item['home'] = home
                item['complete'] = True
                record()
                print('Cycle', cycle, 'ALU/SMC and return passed', flush=True)
            result['complete'] = True
            phase('complete')
        except Exception as error:
            result['error'] = repr(error)
            raise
        finally:
            record()
            if old is not None:
                try:
                    control.buttons(0)
                    if control.status()['state'] == 'game':
                        control.quit()
                    for key, value in (('gba_cpu', old['gba_cpu_requested']),
                                       ('resume', old['settings']['resume']),
                                       ('input_test', old['settings']['input_test']),
                                       ('volume', old['settings']['volume'])):
                        control.settings(f'{key}={value}')
                    result['settings_restored'] = True
                except Exception as error:
                    result['cleanup_error'] = repr(error)
                    result['complete'] = False
                record()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf-sha256', required=True)
    parser.add_argument('--serial', default='tab5-emu')
    parser.add_argument('--rom', default='Tab5-Emu/roms/gba/tab5-rv32-bench.gba')
    parser.add_argument('--manifest', type=Path,
                        default=ROOT / 'artifacts/test-roms/tab5-rv32-bench-manifest.json')
    parser.add_argument('--scene', type=Path, action='append', default=[],
                        help='local metadata with rom, path, sha256 and slot; repeat in launch order')
    parser.add_argument('--seconds', type=float, default=12)
    parser.add_argument('--cycles', type=int, default=1)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.seconds < 4 or args.cycles < 1:
        parser.error('use at least four seconds and one cycle')
    result = run(args)
    raise SystemExit(0 if result['complete'] else 1)


if __name__ == '__main__':
    main()
