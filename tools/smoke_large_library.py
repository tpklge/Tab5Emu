#!/usr/bin/env python3
"""Physical pagination/search checks using metadata only; restore the catalog."""
import argparse, json, time
from pathlib import Path
from emulator_usb import prepare


def wait(c):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        d = c.settings()
        if not d['library_loading']:
            assert d['library']['indexed'], d['library']
            assert not d['library']['error'], d['library']
            time.sleep(.3)  # allow the new page's LVGL draw/release cycle
            return d
        time.sleep(.15)
    raise TimeoutError('library worker did not finish')


def run(c, fixture, report, backup, usb=False):
    assert c.status()['state'] == 'menu'
    paths = ('Tab5-Emu/library/games.json', 'Tab5-Emu/cache/library-v1.t5i')
    previous = {}
    for n, path in enumerate(paths):
        previous[path] = c.read_file(path) if c.stat(path) else None
        if previous[path] is not None:
            out = backup.with_name(backup.stem + '-' + str(n) + '.local.bin')
            with out.open('xb') as f:
                f.write(previous[path])
    report.update(info=c.info(), complete=False)
    try:
        for path in paths:
            data = (fixture / path).read_bytes()
            c.write_file(path, data)
            assert c.read_file(path) == data
        c.reload_roms()
        first = wait(c)
        c.tap(640,100,hold_ms=250); time.sleep(.35)  # consume a possible wake-only touch
        assert first['library']['total'] == 5000 and len(c.roms()) == 64
        report['first_page'] = first['library']
        # Exercise the actual next/previous controls, not only the USB setter.
        c.tap(621,674,hold_ms=250); time.sleep(.35)
        assert wait(c)['library']['page'] == 1
        c.tap(545,674,hold_ms=250); time.sleep(.35)
        assert wait(c)['library']['page'] == 0
        c.settings('library_page=78')
        last = wait(c); games = c.roms()
        assert len(games) == 8 and any('ÚnicoTesteFinal' in str(g) for g in games)
        report['last_page'] = dict(last['library'], visible=len(games))
        c.tap(995,149,hold_ms=250); time.sleep(.35)
        for x, y in ((714,327),(732,487),(772,327),(550,487),(829,327)):
            c.tap(x,y,hold_ms=250); time.sleep(.35)
        c.tap(922,234,hold_ms=250); time.sleep(.35)
        found = wait(c); games = c.roms()
        assert found['library']['total'] == 1 and len(games) == 1, (found['library'], games)
        assert 'ÚnicoTesteFinal' in str(games[0])
        report['accent_search'] = dict(query='unico', result=games[0])
        c.tap(1135,149,hold_ms=250); time.sleep(.35)  # favorite the synthetic last entry
        c.tap(715,149,hold_ms=250); time.sleep(.35)
        assert wait(c)['library']['total'] == 1
        c.tap(1135,149,hold_ms=250); time.sleep(.35)
        assert wait(c)['library']['total'] == 0
        report['favorite_across_index'] = True
        c.tap(575,149,hold_ms=250); time.sleep(.35)
        assert wait(c)['library']['total'] == 5000
        # CRC failure must leave the cache untouched and use the bounded
        # fallback, rather than display a partly trusted indexed page.
        damaged = bytearray((fixture / paths[1]).read_bytes())
        damaged[-1] ^= 1
        c.write_file(paths[1], damaged); c.reload_roms()
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and c.settings()['library_loading']:
            time.sleep(.15)
        invalid = c.settings()['library']
        assert not invalid['indexed'] and invalid['error'], invalid
        assert c.read_file(paths[1]) == damaged
        report['bad_crc_refused_without_rewriting'] = True
        c.write_file(paths[1], (fixture / paths[1]).read_bytes())
        c.reload_roms(); assert wait(c)['library']['total'] == 5000
        c.settings('library_page=0'); wait(c)
        c.settings('library_page=78')
        c.launch('Tab5-Emu/roms/gba/tab5-diagnostic-ab627e01.gba')
        time.sleep(.5); assert c.status()['game_frames'] > 5
        c.quit(); wait(c)
        report['launch_while_page_requested'] = True
        c.tap(855,149,hold_ms=250); time.sleep(.35)
        assert wait(c)['library']['total'] == 1
        assert len(c.roms()) == 1
        report['recent_across_index'] = True
        c.tap(575,149,hold_ms=250); time.sleep(.35); wait(c)
        report['catalog_checks_complete'] = True
        if usb:
            from smoke_index_usb import check
            report['usb_handover']={}
            check(c,report['usb_handover'])
        report['complete'] = True
    except Exception as error:
        report['error'] = repr(error)
        raise
    finally:
        # These two paths are the only catalog files this test owns. The
        # user's ROM directory may be changing and is never rolled back.
        try:
            if c.status()['usb_drive']:
                raise RuntimeError('SD still owned by host; original catalog backup retained for restore after safe OS eject')
            for path in paths:
                if previous[path] is None:
                    if c.stat(path): c.rm(path)
                else:
                    c.write_file(path, previous[path])
                    assert c.read_file(path) == previous[path]
            c.reload_roms()
            report['original_catalog_restored'] = True
        except Exception as error:
            report['complete']=False
            report['restore_error']=repr(error)
            if 'error' not in report:raise


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixture', type=Path, default=Path('artifacts/test-roms/large-library'))
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--usb',action='store_true',help='also test index cancellation and exact macOS disk eject')
    a = p.parse_args()
    if a.report.exists(): raise FileExistsError(a.report)
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    result = {}
    try:
        with Link('tab5-emu').open(timeout_s=30) as l:
            run(Control(l), a.fixture, result, a.report,a.usb)
    finally:
        a.report.write_text(json.dumps(result, indent=2, ensure_ascii=False)+'\n')
