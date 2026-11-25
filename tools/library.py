#!/usr/bin/env python3
"""Ferramenta offline da biblioteca Tab5-Emu. Não baixa ROMs nem capas."""
import argparse
import hashlib
import json
import re
from pathlib import Path, PurePosixPath

EXTENSIONS = {"snes": (".sfc", ".smc"), "gb": (".gb",), "gbc": (".gbc",),
              "gba": (".gba",), "megadrive": (".md", ".gen", ".smd"),
              "n64": (".z64", ".n64", ".v64")}
MAX_CATALOG = 64 * 1024 * 1024
MAX_GAMES = 100000
MAX_COVER_BYTES = 256 * 1024
COVER_SIZE = (360, 360)  # RGB565 decoded: at most 259200 bytes
DIRECTORIES = ["assets/" + x for x in ("fonts", "icons", "themes", "backgrounds", "locales")]
DIRECTORIES += ["config", "library/covers", "library/thumbnails", "bios", "saves", "states",
                "screenshots", "cache", "logs", "benchmarks", "docs"]
DIRECTORIES += ["roms/" + x for x in EXTENSIONS]


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(65536), b""):
            h.update(block)
    return h.hexdigest()


def resolve(root, relative):
    if not isinstance(relative, str) or not relative or len(relative.encode("utf-8")) > 240:
        raise ValueError("caminho vazio ou maior que 240 bytes")
    if "\\" in relative or ":" in relative or any(ord(c) < 32 for c in relative):
        raise ValueError("caminho contém caractere proibido")
    parts = relative.split("/")
    if any(p in ("", ".", "..") for p in parts) or PurePosixPath(relative).is_absolute():
        raise ValueError("caminho deve ser relativo, sem traversal")
    root = Path(root).resolve()
    path = (root / relative).resolve()
    if not path.is_relative_to(root):
        raise ValueError("link aponta para fora da biblioteca")
    return path


def read_catalog(path):
    with Path(path).open("rb") as f:
        data = f.read(MAX_CATALOG + 1)
    if len(data) > MAX_CATALOG:
        raise ValueError("catálogo maior que 64 MiB")
    return json.loads(data.decode("utf-8", errors="strict"))


def jpeg_dimensions(path):
    with Path(path).open("rb") as f:
        data = f.read(MAX_COVER_BYTES + 1)
    if len(data) > MAX_COVER_BYTES or data[:2] != b"\xff\xd8":
        raise ValueError("capa inválida ou maior que 256 KiB")
    pos = 2
    while pos + 4 <= len(data):
        if data[pos] != 255:
            break
        marker = data[pos + 1]
        if marker == 255:
            pos += 1
            continue
        if marker == 1 or 0xd0 <= marker <= 0xd7:
            pos += 2
            continue
        length = int.from_bytes(data[pos + 2:pos + 4], "big")
        if length < 2 or pos + 2 + length > len(data):
            break
        if marker in (0xc0, 0xc1) and length >= 11:
            height = int.from_bytes(data[pos + 5:pos + 7], "big")
            width = int.from_bytes(data[pos + 7:pos + 9], "big")
            if not 1 <= width <= 360 or not 1 <= height <= 360:
                raise ValueError("capa deve ter até 360×360; gere miniatura")
            return width, height
        if marker in (0xda, 0xd9) or 0xc2 <= marker <= 0xcf and marker not in (0xc4, 0xc8, 0xcc):
            break
        pos += 2 + length
    raise ValueError("capa deve ser JPEG baseline válido")


def validate(root, catalog, check_files=True, max_games=MAX_GAMES):
    """Um item inválido não invalida os outros. Erros estruturais rejeitam o arquivo."""
    if not isinstance(catalog, dict) or type(catalog.get("schema_version")) is not int or catalog["schema_version"] != 1:
        raise ValueError("schema_version deve ser 1")
    games = catalog.get("games")
    if not isinstance(games, list) or len(games) > max_games:
        raise ValueError(f"games deve ser lista de até {max_games} jogos")
    seen, paths, valid, errors = set(), set(), [], []
    for index, game in enumerate(games):
        try:
            if not isinstance(game, dict):
                raise ValueError("entrada deve ser objeto")
            identity = game.get("id", "")
            if not isinstance(identity, str) or not re.fullmatch(r"[a-zA-Z0-9_-]{1,80}", identity):
                raise ValueError("id inválido")
            if identity in seen:
                raise ValueError("id repetido")
            seen.add(identity)
            system = game.get("system")
            if system not in EXTENSIONS:
                raise ValueError("sistema desconhecido")
            for key, limit in (("title", 160), ("description", 2048), ("developer", 160), ("publisher", 160)):
                value = game.get(key, "")
                if not isinstance(value, str) or len(value.encode("utf-8")) > limit or "\0" in value:
                    raise ValueError(key + " inválido ou longo demais")
            if not game.get("title", "").strip():
                raise ValueError("título vazio")
            for key, low, high in (("year", 0, 2100), ("players", 1, 16)):
                if key in game and (type(game[key]) is not int or not low <= game[key] <= high):
                    raise ValueError(key + " fora do intervalo")
            for key in ("genre", "language", "tags"):
                if key in game and (not isinstance(game[key], list) or len(game[key]) > 16 or
                                    any(not isinstance(x, str) or len(x.encode("utf-8")) > 80 or "\0" in x for x in game[key])):
                    raise ValueError(key + " deve conter até 16 textos de até 80 bytes")
            rom = resolve(root, game.get("rom"))
            if rom.suffix.lower() not in EXTENSIONS[system]:
                raise ValueError("extensão não corresponde ao sistema")
            rom_key = str(rom).casefold()
            if rom_key in paths:
                raise ValueError("ROM repetida")
            paths.add(rom_key)
            if check_files and not rom.is_file():
                raise ValueError("ROM ausente: " + game["rom"])
            if "sha256" in game and (not isinstance(game["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", game["sha256"])):
                raise ValueError("sha256 inválido")
            if check_files and game.get("sha256") and digest(rom) != game["sha256"]:
                raise ValueError("hash da ROM difere do catálogo")
            if game.get("cover"):
                cover = resolve(root, game["cover"])
                if cover.suffix.lower() not in (".jpg", ".jpeg"):
                    raise ValueError("capa deve ser JPEG baseline")
                if check_files and not cover.is_file():
                    raise ValueError("capa ausente: " + game["cover"])
                if check_files and cover.stat().st_size > MAX_COVER_BYTES:
                    raise ValueError("capa maior que 256 KiB; gere miniatura")
                if check_files:
                    jpeg_dimensions(cover)
            valid.append(game)
        except (ValueError, TypeError, UnicodeError) as exc:
            errors.append({"index": index, "error": str(exc)})
    return valid, errors


def write_json_new(path, value):
    """Criação exclusiva: nunca sobrescreve preferências ou catálogo existentes."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as f:
        json.dump(value, f, ensure_ascii=False, indent=2)
        f.write("\n")


def initialize(root):
    root = Path(root)
    for directory in DIRECTORIES:
        (root / directory).mkdir(parents=True, exist_ok=True)
    defaults = {"library/games.json": {"schema_version": 1, "games": []},
                "config/input_profiles.json": {"schema_version": 1, "global": {}, "systems": {}, "games": {}},
                "config/settings.json": {"schema_version": 1},
                "config/systems.json": {"schema_version": 1, "systems": list(EXTENSIONS)},
                "assets/version.json": {"schema_version": 1, "resource_version": "0.1.0"}}
    for path, value in defaults.items():
        try:
            write_json_new(root / path, value)
        except FileExistsError:
            pass


def scan(root):
    games = []
    for system, extensions in EXTENSIONS.items():
        for path in sorted((Path(root) / "roms" / system).rglob("*")):
            if not any(part.startswith('.') for part in path.relative_to(Path(root)/'roms'/system).parts) and path.is_file() and path.suffix.lower() in extensions:
                relative = path.relative_to(root).as_posix()
                resolve(root, relative)
                sha = digest(path)
                games.append({"id": system + "-" + hashlib.sha256(relative.encode()).hexdigest()[:24],
                              "system": system, "title": path.stem, "rom": relative, "sha256": sha})
    return {"schema_version": 1, "games": games}


def import_csv(root, source):
    # Upstream d120517: two separators, then title including any commas.
    # This deliberately does not claim RFC4180 quoted-field support.
    games, report = [], []
    for number, line in enumerate(Path(source).read_text(encoding="utf-8-sig").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        parts = [s.strip() for s in line.split(",", 2)]
        try:
            if len(parts) != 3:
                raise ValueError("esperadas rom_path,boxart_path,name")
            rom, cover, title = parts
            resolve(root, rom)
            system = next((s for s, exts in EXTENSIONS.items() if Path(rom).suffix.lower() in exts), None)
            if not system:
                raise ValueError("sistema fora do perfil estável/experimental")
            entry = {"id": system + "-" + hashlib.sha256(rom.encode()).hexdigest()[:24],
                     "system": system, "title": title, "rom": rom}
            if cover:
                resolve(root, cover)
                entry["cover"] = cover
            games.append(entry)
            report.append({"line": number, "status": "importado", "rom": rom})
        except ValueError as exc:
            report.append({"line": number, "status": "ignorado", "error": str(exc)})
    return {"schema_version": 1, "games": games}, report


def thumbnail(source, target):
    from PIL import Image, ImageOps
    with Image.open(source) as original:
        original.load()  # Pillow enforces its decompression-bomb limit
        image = ImageOps.exif_transpose(original).convert("RGB")
        image.thumbnail(COVER_SIZE, Image.Resampling.LANCZOS)
        with Path(target).open("xb") as output:
            image.save(output, "JPEG", quality=85, progressive=False)
    if Path(target).stat().st_size > MAX_COVER_BYTES:
        raise ValueError("miniatura excedeu orçamento")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path("sdcard/Tab5-Emu"))
    sub = p.add_subparsers(dest="action", required=True)
    sub.add_parser("init")
    v = sub.add_parser("validate")
    v.add_argument("--catalog", type=Path)
    v.add_argument("--no-files", action="store_true")
    s = sub.add_parser("scan")
    s.add_argument("--output", type=Path, required=True)
    c = sub.add_parser("import-csv")
    c.add_argument("source", type=Path)
    c.add_argument("--output", type=Path, required=True)
    c.add_argument("--report", type=Path, required=True)
    t = sub.add_parser("thumbnail")
    t.add_argument("source", type=Path)
    t.add_argument("target", type=Path)
    a = p.parse_args()
    if a.action == "init":
        initialize(a.root)
    elif a.action == "validate":
        valid, errors = validate(a.root, read_catalog(a.catalog or a.root / "library/games.json"), not a.no_files)
        print(json.dumps({"valid": len(valid), "errors": errors}, ensure_ascii=False, indent=2))
        return bool(errors)
    elif a.action == "scan":
        write_json_new(a.output, scan(a.root))
    elif a.action == "import-csv":
        catalog, report = import_csv(a.root, a.source)
        write_json_new(a.output, catalog)
        write_json_new(a.report, report)
    else:
        thumbnail(a.source, a.target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
