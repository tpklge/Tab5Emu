#!/usr/bin/env python3
"""Stage the pinned feeder with a one-frame wait and the existing DMA ring."""
import argparse
import hashlib
from pathlib import Path
PIN='5c6aedcbd54a6524b25dc4637201996f86702a155effc97335ed07a0f8487a18'

def stage(source, output):
    raw=Path(source).read_bytes()
    if hashlib.sha256(raw).hexdigest()!=PIN:
        raise ValueError('audio feeder changed; audit local adaptation')
    text=raw.decode()
    for before,after in (
        ('WHOLE_BLOCK_WAIT_MS = 24','WHOLE_BLOCK_WAIT_MS = 16'),
        ('DMA_DESC_NUM = 12','DMA_DESC_NUM = 16'),
        ('24 ms','16 ms'),('12 x 240','16 x 240'),('60 ms','80 ms'),('65 ms','87 ms'),
        ('Longer than one\n/// block at every rate the boards support (a block is 1/60 s), so a producer\n/// whose next frame simply has not arrived yet is not padded; shorter than the',
         'At most one\n/// frame interval (16 ms, rounded down from 1/60 s), absorbing producer jitter\n/// while keeping a slow producer wait below a block; shorter than the')):
        if before not in text:raise ValueError('audio adaptation anchor missing')
        text=text.replace(before,after)
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True);output.write_text(text)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();stage(a.source,a.output)
