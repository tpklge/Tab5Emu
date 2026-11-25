#!/usr/bin/env python3
"""Use o Python do ESP-IDF (pyelftools/esp_idf_size). Não altera ELF/map reais."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from elftools.elf.elffile import ELFFile


def report(build):
    description = build / "project_description.json"
    project = json.loads(description.read_text())["project_name"] if description.exists() else "tab5-emu"
    image = build / (project + ".bin")
    data = image.read_bytes()
    with (build / (project + ".elf")).open("rb") as stream:
        elf = ELFFile(stream)
        sections = {s.name: {"size": s["sh_size"], "address": hex(s["sh_addr"]), "type": s["sh_type"]}
                    for s in elf.iter_sections() if s["sh_flags"] & 2}
    overlays = {k.removeprefix(".tab5_ovl."): v["size"] for k, v in sections.items() if k.startswith(".tab5_ovl.")}
    # IDF's parser does not accept GNU ld's OVERLAY output-section LMA suffix.
    # Strip only that suffix in a temporary copy; all addresses/sizes and the
    # original linker map stay unchanged. Per-archive figures omit overlay
    # payload attribution, so report each payload from ELF separately.
    original_map = (build / (project + ".map")).read_text()
    normalized = re.sub(r"(?m)^(\.[^\n]+?0x[0-9a-fA-F]+) load address 0x[0-9a-fA-F]+$", r"\1", original_map)
    with tempfile.TemporaryDirectory(prefix="tab5-size-") as temporary:
        source = Path(temporary) / "normalized.map"
        source.write_text(normalized)
        raw = subprocess.check_output([sys.executable, "-m", "esp_idf_size", "--archives", "--format", "json2", str(source)])
        archives = json.loads(raw)
    return {"image_bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "overlay_payload_bytes_by_core": overlays, "overlay_shared_psram_bytes": max(overlays.values(), default=0),
            "resident_ui_code_bytes": sections.get(".tab5_res", {}).get("size", 0),
            "sections": sections, "archives_without_overlay_payload_attribution": archives,
            "note": "Overlay payload already belongs to flash store: do not add it to image size twice. Runtime heap not measured."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps(report(args.build), ensure_ascii=False, indent=2) + "\n")
