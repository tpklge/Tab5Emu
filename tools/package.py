#!/usr/bin/env python3
"""Empacota app-only e SD sem incluir ROMs, saves ou dados do dispositivo."""
import argparse
import hashlib
import json
import subprocess
import zipfile
from pathlib import Path
import library

ROOT = Path(__file__).resolve().parents[1]


def check_image(path, project="tab5-emu"):
    data = Path(path).read_bytes()
    if len(data) < 288 or data[0] != 0xe9 or int.from_bytes(data[12:14], "little") != 0x12:
        raise ValueError("esperada imagem ESP32-P4 app-only")
    if data[80:112].split(b"\0")[0] != project.encode():
        raise ValueError("descritor de aplicativo inesperado")
    if data[23] != 1 or hashlib.sha256(data[:-32]).digest() != data[-32:]:
        raise ValueError("digest interno da imagem inválido")
    return data


def package(version, profile, image_path, destination, evidence_path=None):
    destination.mkdir(parents=True, exist_ok=True)
    files = []
    app_descriptor = None
    if image_path:
        data = check_image(image_path)
        app_descriptor = {"version": data[48:80].split(b"\0")[0].decode(),
                          "project": data[80:112].split(b"\0")[0].decode(),
                          "idf": data[144:176].split(b"\0")[0].decode()}
        target = destination / f"tab5-emu-{profile}-{version}.bin"
        with target.open("xb") as output:
            output.write(data)
        files.append(target)
    source = ROOT / "sdcard/Tab5-Emu"
    library.initialize(source)  # Git checkouts do not retain empty directories.
    # All packaged personal-data directories must remain empty.
    for name in ("roms", "bios", "saves", "states", "screenshots", "logs", "cache", "benchmarks"):
        if any(p.is_file() for p in (source / name).rglob("*")):
            raise ValueError("não distribuir dados pessoais: " + name)
    archive = destination / f"Tab5-Emu-sd-{version}.zip"
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("Tab5-Emu/", "")
        for path in sorted(source.rglob("*")):
            name = "Tab5-Emu/" + path.relative_to(source).as_posix()
            if path.is_dir():
                z.writestr(name + "/", "")
            else:
                z.write(path, name)
        for path in sorted((ROOT / "docs").glob("*.md")):
            # Original README contains full-flash instructions, kept in repo only.
            if path.name in ("UPSTREAM_README.md", "PLAN.md"):
                continue
            z.write(path, "Tab5-Emu/docs/" + path.name)
        z.write(ROOT / "README.md", "Tab5-Emu/docs/README.md")
        z.write(ROOT / "schemas/games.schema.json", "Tab5-Emu/docs/games.schema.json")
        z.write(ROOT / "schemas/example.games.json", "Tab5-Emu/docs/example.games.json")
        z.write(ROOT / "schemas/settings.schema.json", "Tab5-Emu/docs/settings.schema.json")
    files.append(archive)
    for snapshot in destination.glob("Tab5Emu-source-*.tar.gz"):
        files.append(snapshot)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True))
    manifest = {"format": "tab5-emu-distribution-v1", "launcher_native_manifest": False,
                "version": version, "profile": profile, "commit": revision, "dirty": dirty,
                "app_descriptor": app_descriptor,
                "chip": "esp32p4", "idf": "6.1.0", "resource_schema": 1,
                "requirements": {"flash_mhz": 80, "flash_mode": "dio", "cpu_mhz": 360,
                                 "psram_mib": 32, "partition_table_offset": "0x8000",
                                 "bootloader_replacement": False, "slot": "consultar plano real do launcher"},
                "verification": {"built": bool(image_path), "uploaded": False, "installed": False,
                                 "booted": False, "return_confirmed": False, "physical_tests": False},
                "files": [{"name": p.name, "size": p.stat().st_size, "sha256": library.digest(p)} for p in files]}
    if evidence_path:
        evidence = json.loads(evidence_path.read_text())
        binary = next((item for item in manifest["files"] if item["name"].endswith(".bin")), None)
        if not binary or evidence.get("sha256") != binary["sha256"]:
            raise ValueError("relatório de envio não corresponde a este binário")
        manifest["verification"].update({"uploaded": evidence.get("uploaded") is True,
            "received_hash_verified": evidence.get("received_hash_verified") is True,
            "installed": evidence.get("installed") is True, "booted": evidence.get("booted") is True,
            "return_confirmed": evidence.get("return_confirmed") is True,
            "physical_tests": evidence.get("physical_tests") is True})
        manifest["launcher_plan"] = evidence.get("plan", {})
        manifest["physical_test_scope"] = evidence.get("tests", {})
        manifest["remaining_limitations"] = evidence.get("limitations", [])
        proof = destination / f"verification-{profile}-{version}.json"
        library.write_json_new(proof, {"sha256": evidence["sha256"],
            "verification": manifest["verification"], "tests": manifest["physical_test_scope"],
            "limitations": manifest["remaining_limitations"]})
        manifest["files"].append({"name": proof.name, "size": proof.stat().st_size, "sha256": library.digest(proof)})
    library.write_json_new(destination / f"manifest-{profile}-{version}.json", manifest)
    with (destination / "SHA256SUMS").open("x") as output:
        for p in sorted(destination.iterdir()):
            if p.is_file() and p.name != "SHA256SUMS":
                output.write(library.digest(p) + "  " + p.name + "\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--profile", choices=("stable", "stable-rv32", "n64-cpu-probe", "gba-rv32", "upstream"), default="stable")
    parser.add_argument("--image", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    print(json.dumps(package(args.version, args.profile, args.image, args.output, args.evidence), indent=2, ensure_ascii=False))
