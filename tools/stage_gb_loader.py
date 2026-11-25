#!/usr/bin/env python3
"""Generate the audited gnuboy loader with a recoverable SRAM allocation failure."""
import argparse
import hashlib
from pathlib import Path

PIN = 'c953d46172d960ba54262c53ffa2c73a1c86a316cebe84adb9c1626bfbdf4b82'

def stage(source, output):
    source, output = Path(source), Path(output)
    raw = source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != PIN:
        raise ValueError('gnuboy loader changed; audit the SRAM failure adapter')
    text = raw.decode()
    original = '\tram.sbank = (byte (*)[8192])sram_ptr;'
    assert text.count(original) == 1
    text = text.replace(original, '\tif (!sram_ptr) { ram.sbank = NULL; return -1; }\n' + original)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path); parser.add_argument('output', type=Path)
    args = parser.parse_args(); stage(args.source, args.output)
