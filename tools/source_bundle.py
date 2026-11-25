#!/usr/bin/env python3
"""Snapshot local de fonte do stable, incluindo submódulos e managed sources."""
import argparse
import hashlib
import json
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUBMODULES = ["components/emu-ui", "components/emulink", "components/gbc/gnuboy", "components/genesis/gwenesis"]


def bundle(output, all_components=False):
    paths = set()
    # Includes local modifications and new files; no dependency on a commit
    # having been made. The bundle manifest fingerprints exact source bytes.
    raw = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=ROOT)
    for name in raw.decode().split("\0"):
        path = ROOT / name
        if name and path.is_file() and not name.startswith(("artifacts/", "docs/images/", "ROMs/")):
            paths.add(path)
    modules = list(SUBMODULES)
    if all_components:
        entries = subprocess.check_output(["git", "ls-files", "--stage", "-z"], cwd=ROOT).decode().split("\0")
        modules = sorted(entry.split("\t", 1)[1] for entry in entries if entry.startswith("160000 "))
        for directory in modules:
            if not (ROOT / directory / ".git").exists():
                raise ValueError("Initialize the source dependency before packaging: " + directory)
    for directory in modules + ["managed_components"]:
        for path in (ROOT / directory).rglob("*"):
            if path.is_file() and path.name != ".git" and ".git" not in path.relative_to(ROOT).parts and "__pycache__" not in path.parts:
                paths.add(path)
    files = []
    output.parent.mkdir(parents=True, exist_ok=True)
    def public_metadata(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.mtime = 0
        return info
    # x:gz ensures an older delivery is never replaced silently.
    with tarfile.open(output, "x:gz", dereference=True) as archive:
        for path in sorted(paths):
            if not path.resolve().is_relative_to(ROOT) or '.local.' in path.name:
                continue
            resolved=path.resolve().relative_to(ROOT).parts
            if resolved and resolved[0] in ('.tools','ROMs','artifacts','dist','build'):
                continue
            relative = path.relative_to(ROOT).as_posix()
            archive.add(path, arcname="Tab5Emu-source/" + relative, recursive=False, filter=public_metadata)
            files.append({"path": relative, "size": path.stat().st_size,
                          "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        payload = json.dumps({"format": "tab5-emu-source-snapshot-v1", "files": files,
                              "all_initialized_components": all_components,
                              "submodule_status": subprocess.check_output(["git", "submodule", "status", "--recursive"], cwd=ROOT, text=True).splitlines(),
                              "sdk": {"url": "https://github.com/espressif/esp-idf.git", "commit": "fff9895c82d744c7237be8847347bdd1b07c6643"}}, indent=2).encode()
        import io
        info = tarfile.TarInfo("Tab5Emu-source/SOURCE_MANIFEST.json")
        info.size = len(payload)
        archive.addfile(info, io.BytesIO(payload))
    return {"source_files": len(files), "bytes": output.stat().st_size,
            "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path)
    p.add_argument("--all-components", action="store_true")
    args=p.parse_args()
    print(json.dumps(bundle(args.output,args.all_components), indent=2))
