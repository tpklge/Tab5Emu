import hashlib
import io
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from package import check_image
from package_github_release import validate_source

class PublicReleaseTests(unittest.TestCase):
    def test_native_app_requires_explicit_project_and_valid_digest(self):
        data=bytearray(288);data[0]=0xe9;data[12:14]=b'\x12\0';data[23]=1;data[80:89]=b'tab5-sm64'
        data[-32:]=hashlib.sha256(data[:-32]).digest()
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'native.bin';p.write_bytes(data)
            self.assertEqual(check_image(p,'tab5-sm64'),data)
            with self.assertRaises(ValueError):check_image(p)
            data[44]^=1;p.write_bytes(data)
            with self.assertRaises(ValueError):check_image(p,'tab5-sm64')
    def test_rom_and_private_source_entries_are_rejected(self):
        for name in ('ROMs/game.z64','.tools/baserom.us.z64','artifacts/state.local.bin'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                p=Path(directory)/'source.tar.gz'
                with tarfile.open(p,'w:gz') as archive:
                    info=tarfile.TarInfo('Tab5Emu-source/'+name);info.size=4;archive.addfile(info,io.BytesIO(b'test'))
                with self.assertRaisesRegex(ValueError,'Private/ROM'):validate_source(p)
    def test_platform_readme_is_documentation(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'source.tar.gz'
            with tarfile.open(p,'w:gz') as archive:
                data=b'Nintendo 64 build instructions\n';info=tarfile.TarInfo('Tab5Emu-source/platform/README.N64');info.size=len(data);archive.addfile(info,io.BytesIO(data))
            with self.assertRaisesRegex(ValueError,'Incomplete source'):validate_source(p)
