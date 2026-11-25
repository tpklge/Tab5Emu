#!/usr/bin/env python3
"""Build the project's own 0BSD test ROM; no downloads/proprietary logo/BIOS."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def build(output):
    prefix = os.getenv("TAB5_ARM_PREFIX")
    if not prefix:
        gcc = shutil.which("arm-none-eabi-gcc")
        if not gcc:
            gcc = str(Path.home()/".platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-gcc")
        prefix = gcc.removesuffix("gcc")
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT/"tests/homebrew/gba"
    elf = output/"tab5-diagnostic-gba.elf"
    image = output/"tab5-diagnostic.gba"
    subprocess.run([prefix+"gcc", "-mcpu=arm7tdmi", "-marm", "-Os", "-ffreestanding", "-fno-builtin",
        "-nostdlib", "-Wl,--build-id=none", "-T", str(source/"link.ld"), str(source/"start.S"),
        str(source/"main.c"), "-lgcc", "-o", str(elf)], check=True)
    subprocess.run([prefix+"objcopy", "-O", "binary", str(elf), str(image)], check=True)
    data = bytearray(image.read_bytes())
    data[0xa0:0xac]=b"TAB5 TEST   "
    data[0xac:0xb0]=b"T5DE"
    data[0xb0:0xb2]=b"00"
    data[0xb2]=0x96
    data[0xbd]=(-sum(data[0xa0:0xbd])-0x19)&255
    data.extend(bytes(max(0,32768-len(data))))
    image.write_bytes(data)
    report={"license":"0BSD", "file":image.name, "size":len(data), "sha256":hashlib.sha256(data).hexdigest(),
        "boot":"game; proprietary logo absent", "source":"tests/homebrew/gba", "compiler":subprocess.check_output([prefix+"gcc","--version"],text=True).splitlines()[0]}
    (output/"manifest.json").write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output",type=Path,default=ROOT/"artifacts/test-roms")
    print(json.dumps(build(p.parse_args().output),indent=2))
