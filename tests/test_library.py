import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("library", Path(__file__).parents[1] / "tools/library.py")
lib = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lib)


class LibraryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        lib.initialize(self.root)

    def test_paths_and_symlink_escape(self):
        for path in ("../rom.gba", "/rom.gba", "roms//x.gba", "roms/./x.gba", "a\\b", "C:/rom", "a\0b"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                lib.resolve(self.root, path)
        (self.root / "outside").symlink_to(self.root.parent, target_is_directory=True)
        with self.assertRaises(ValueError):
            lib.resolve(self.root, "outside/rom.gba")
        self.assertEqual(lib.resolve(self.root, "roms/gba/Ação livre.gba").name, "Ação livre.gba")

    def test_hash_missing_file_duplicate_and_bad_entry_are_isolated(self):
        rom = self.root / "roms/gba/Ação.gba"
        rom.write_bytes(b"homebrew test fixture, not executable")
        catalog = lib.scan(self.root)
        entry = catalog["games"][0]
        catalog["games"].extend([dict(entry), {"id": "broken", "system": "gba", "title": "x", "rom": "../a.gba"}])
        good, bad = lib.validate(self.root, catalog)
        self.assertEqual(len(good), 1)
        self.assertEqual(len(bad), 2)
        rom.write_bytes(b"modified")
        good, bad = lib.validate(self.root, {"schema_version": 1, "games": [entry]})
        self.assertIn("hash", bad[0]["error"])
        rom.unlink()
        self.assertIn("ausente", lib.validate(self.root, {"schema_version": 1, "games": [entry]})[1][0]["error"])

    def test_init_and_scan_preserve_user_data(self):
        path = self.root / "library/games.json"
        path.write_text("user data")
        lib.initialize(self.root)
        self.assertEqual(path.read_text(), "user data")
        for folder in ("one", "two"):
            target = self.root / "roms/gb" / folder / "same.gb"
            target.parent.mkdir()
            target.write_bytes(folder.encode())
        games = lib.scan(self.root)["games"]
        self.assertNotEqual(games[0]["id"], games[1]["id"])
        self.assertNotEqual(games[0]["sha256"], games[1]["sha256"])

    def test_real_upstream_csv_title_with_comma_and_report(self):
        source = self.root / "metadata.csv"
        data = "# header\nroms/gba/a.gba,,Ação, edição livre\nbad\n../b.gb,,Escape\n"
        source.write_text(data)
        catalog, report = lib.import_csv(self.root, source)
        self.assertEqual(catalog["games"][0]["title"], "Ação, edição livre")
        self.assertEqual(len(report), 3)
        self.assertEqual(source.read_text(), data)

    def test_bounded_parser_and_utf8(self):
        path = self.root / "bad.json"
        path.write_bytes(b"\xff")
        with self.assertRaises(UnicodeError):
            lib.read_catalog(path)
        path.write_bytes(b" " * (lib.MAX_CATALOG + 1))
        with self.assertRaises(ValueError):
            lib.read_catalog(path)
        with self.assertRaises(ValueError):
            lib.validate(self.root, {"schema_version": True, "games": []})

    def test_thumbnail_aspect_and_progressive_rejection(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("instale tools/requirements-host.txt para teste Pillow")
        source, target = self.root / "source.png", self.root / "cover.jpg"
        Image.new("RGB", (800, 400), (20, 40, 60)).save(source)
        lib.thumbnail(source, target)
        self.assertEqual(lib.jpeg_dimensions(target), (360, 180))
        with self.assertRaises(FileExistsError):
            lib.thumbnail(source, target)
        Image.new("RGB", (100, 100)).save(self.root / "progressive.jpg", progressive=True)
        with self.assertRaisesRegex(ValueError, "baseline"):
            lib.jpeg_dimensions(self.root / "progressive.jpg")


if __name__ == "__main__":
    unittest.main()
