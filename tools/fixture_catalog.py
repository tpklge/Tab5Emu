"""Temporary catalog for explicitly owned diagnostic ROMs, with exact rollback."""
from contextlib import contextmanager
import hashlib
import json
import time
from pathlib import Path


def wait(c):
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        state=c.settings()
        if not state['library_loading']:
            assert not state['library']['error'],state['library']
            time.sleep(.3)
            return
        time.sleep(.15)
    raise TimeoutError('Fixture catalog worker did not finish')


@contextmanager
def temporary_fixture_catalog(c, fixtures, backup):
    paths = ('Tab5-Emu/library/games.json', 'Tab5-Emu/cache/library-v1.t5i')
    originals = {p: c.read_file(p) if c.stat(p) else None for p in paths}
    page = c.settings().get('library', {}).get('page', 0)
    for n, path in enumerate(paths):
        if originals[path] is not None:
            with backup.with_name(backup.name+f'-catalog-{n}.local.bin').open('xb') as f:
                f.write(originals[path])
    games = []
    for remote, data in fixtures:
        if c.stat(remote) is None:
            c.write_file(remote, data)
        assert c.read_file(remote) == data, 'Owned fixture differs; preserved'
        relative = remote.removeprefix('Tab5-Emu/')
        system = relative.split('/')[1]
        games.append({'id': 'fixture-'+hashlib.sha256(data).hexdigest()[:32], 'system': system,
                      'title': Path(remote).stem, 'rom': relative})
    try:
        c.write_file(paths[0], (json.dumps({'schema_version': 1, 'games': games})+'\n').encode())
        if originals[paths[1]] is not None: c.rm(paths[1])
        c.reload_roms(); wait(c)
        yield
    finally:
        if c.status()['state'] == 'game': c.quit()
        for path in paths:
            if originals[path] is None:
                if c.stat(path): c.rm(path)
            else:
                c.write_file(path, originals[path]); assert c.read_file(path) == originals[path]
        c.reload_roms(); wait(c)
        if originals[paths[1]] is not None and page:
            c.settings('library_page='+str(page)); wait(c)
