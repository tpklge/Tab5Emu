#!/usr/bin/env python3
"""Restore public packaging inputs on CI; never rebuild the measured binaries."""
import argparse
import hashlib
import json
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]


def restore_managed(snapshot):
    with tarfile.open(snapshot, 'r:gz') as archive:
        manifest = json.load(archive.extractfile('Tab5Emu-source/SOURCE_MANIFEST.json'))
        expected = {entry['path']: entry for entry in manifest['files']}
        count = 0
        for entry in archive:
            if not entry.name.startswith('Tab5Emu-source/managed_components/'):
                continue
            parts = PurePosixPath(entry.name).parts
            if not entry.isfile() or '..' in parts:
                raise ValueError('Invalid managed source entry: ' + entry.name)
            relative = '/'.join(parts[1:])
            data = archive.extractfile(entry).read()
            proof = expected[relative]
            if len(data) != proof['size'] or hashlib.sha256(data).hexdigest() != proof['sha256']:
                raise ValueError('Managed source hash differs: ' + relative)
            target = ROOT / relative
            if not target.resolve().is_relative_to(ROOT.resolve()):
                raise ValueError('Managed source escapes checkout')
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as output:
                output.write(data)
            count += 1
    if not count:
        raise ValueError('Managed sources missing from stable snapshot')
    return count


def restore_apps(assets):
    delivery = json.loads((ROOT / 'artifacts/n64-performance-delivery.json').read_text())
    for key, build, name in [
        ('general_firmware', 'build-sd-cores', 'tab5-emu-v0.2.3-perf.bin'),
        ('native_firmware', 'build-sm64-native', 'tab5-sm64-v0.1.0-native.bin'),
    ]:
        proof = delivery[key]
        data = (assets / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != proof['sha256']:
            raise ValueError('Firmware differs from hardware delivery: ' + name)
        destination = ROOT / build
        destination.mkdir(parents=True, exist_ok=True)
        (destination / (proof['project'] + '.bin')).write_bytes(data)
    destination = ROOT / 'build-sd-cores/sd/Tab5-Emu/cores'
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(assets / 'Tab5Emu-microSD-v0.2.3.zip') as archive:
        for core in delivery['general_firmware']['cores']:
            if Path(core['file']).name != core['file']:
                raise ValueError('Invalid core filename')
            data = archive.read('Tab5-Emu/cores/' + core['file'])
            if len(data) != core['bytes'] or hashlib.sha256(data).hexdigest() != core['sha256']:
                raise ValueError('Core differs from hardware delivery: ' + core['core'])
            (destination / core['file']).write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stable', required=True, type=Path)
    parser.add_argument('--assets', required=True, type=Path)
    args = parser.parse_args()
    snapshots = list(args.stable.glob('Tab5Emu-source-*.tar.gz'))
    if len(snapshots) != 1:
        parser.error('Expected one validated stable source archive')
    print('Restored managed source files:', restore_managed(snapshots[0]))
    restore_apps(args.assets)
