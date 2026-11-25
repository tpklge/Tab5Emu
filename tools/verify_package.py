#!/usr/bin/env python3
"""Verify a local app/SD/source delivery without installing or extracting it."""
import argparse
import hashlib
import json
import tarfile
import zipfile
from pathlib import Path, PurePosixPath
from package import check_image


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(directory, require_tested=False, allow_dirty=False):
    sums = {}
    for line in (directory / 'SHA256SUMS').read_text().splitlines():
        sha, name = line.split('  ', 1)
        assert Path(name).name == name and name not in sums
        assert digest(directory / name) == sha, name
        sums[name] = sha
    assert set(sums) == {p.name for p in directory.iterdir() if p.is_file() and p.name != 'SHA256SUMS'}
    manifests = list(directory.glob('manifest-*.json'))
    assert len(manifests) == 1
    manifest = json.loads(manifests[0].read_text())
    assert manifest['dirty'] is False or allow_dirty, 'uncommitted development source requires --allow-dirty'
    if require_tested:
        assert all(manifest['verification'].values())
    for entry in manifest['files']:
        path = directory / entry['name']
        assert path.parent == directory
        assert path.stat().st_size == entry['size'] and sums[path.name] == entry['sha256']
        if path.suffix == '.bin':
            data = check_image(path)
            assert data[48:80].split(b'\0')[0].decode() == manifest['app_descriptor']['version']
    archives = list(directory.glob('Tab5-Emu-sd-*.zip'))
    assert len(archives) == 1
    with zipfile.ZipFile(archives[0]) as archive:
        assert archive.testzip() is None
        personal = {'roms', 'bios', 'saves', 'states', 'screenshots', 'logs', 'cache', 'benchmarks'}
        for entry in archive.infolist():
            parts = PurePosixPath(entry.filename).parts
            assert parts[0] == 'Tab5-Emu' and '..' not in parts
            assert entry.is_dir() or len(parts) < 2 or parts[1] not in personal
        prefix = 'Tab5-Emu/assets/fonts/'
        fonts = json.loads(archive.read(prefix + 'manifest.json'))
        for font in fonts['fonts']:
            data = archive.read(prefix + font['file'])
            assert len(data) == font['bytes'] and hashlib.sha256(data).hexdigest() == font['sha256']
    snapshots = list(directory.glob('Tab5Emu-source-*.tar.gz'))
    assert len(snapshots) == 1
    with tarfile.open(snapshots[0], 'r:gz') as archive:
        members = archive.getmembers()
        entries = {entry.name: entry for entry in members}
        assert len(entries) == len(members)
        for entry in members:
            assert entry.isfile() and PurePosixPath(entry.name).parts[0] == 'Tab5Emu-source'
            assert '..' not in PurePosixPath(entry.name).parts
        source = json.load(archive.extractfile('Tab5Emu-source/SOURCE_MANIFEST.json'))
        assert source['sdk']['commit'] == 'fff9895c82d744c7237be8847347bdd1b07c6643'
        assert len(members) == len(source['files']) + 1
        for entry in source['files']:
            name = 'Tab5Emu-source/' + entry['path']
            assert entries[name].size == entry['size']
            assert hashlib.file_digest(archive.extractfile(name), 'sha256').hexdigest() == entry['sha256'], name
    return {'profile': manifest['profile'], 'commit': manifest['commit'],
            'files': len(sums), 'source_files': len(source['files']),
            'checksums': True, 'app_image': True, 'sd_crc_and_fonts': True,
            'no_personal_data_directories': True, 'source_manifest': True,
            'tested': require_tested, 'source_dirty': manifest['dirty']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--require-tested', action='store_true')
    parser.add_argument('--allow-dirty', action='store_true',
                        help='accept an explicitly dirty development snapshot; all file hashes are still checked')
    args = parser.parse_args()
    print(json.dumps(verify(args.directory, args.require_tested, args.allow_dirty), indent=2))
