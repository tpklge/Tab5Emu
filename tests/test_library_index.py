import importlib.util,json,struct,tempfile,unittest,zlib
from pathlib import Path
import sys
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import library_index
class IndexTests(unittest.TestCase):
    def test_appledouble_and_hidden_folders_are_not_games(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name);folder=root/'roms/n64';(folder/'.metadata').mkdir(parents=True)
            for file in ('game.z64','._game.z64','.metadata/game.z64'):(folder/file).write_bytes(b'fixture')
            self.assertEqual(library_index.build(root)['games'],1)
            self.assertEqual(len(library_index.library.scan(root)['games']),1)
    def test_discovery_output_limit_preserves_prior_index(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name);(root/'roms/gba').mkdir(parents=True);(root/'cache').mkdir()
            (root/'roms/gba'/('x'*100+'.gba')).write_bytes(b'')
            target=root/'cache/library-v1.t5i';target.write_bytes(b'prior-index')
            with patch.object(library_index,'MAX_INDEX_BYTES',128):
                with self.assertRaisesRegex(ValueError,'index exceeds'):library_index.build(root)
            self.assertEqual(target.read_bytes(),b'prior-index')
    def test_large_unicode_catalog_and_source_binding(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name);(root/'library').mkdir()
            catalog={'schema_version':1,'games':[dict(id=f'g{n}',system='gba',title=f'Ação {n}',rom=f'roms/gba/game{n}.gba') for n in range(5000)]}
            source=root/'library/games.json';source.write_text(json.dumps(catalog,ensure_ascii=False))
            result=library_index.build(root,False);self.assertEqual(result['games'],5000)
            data=(root/'cache/library-v1.t5i').read_bytes()
            header=struct.unpack('<8sIIIIQQ24x',data[:64]);self.assertEqual(header[2],5000)
            self.assertEqual(header[3],len(data));self.assertEqual(header[4],zlib.crc32(data[64:]))
            self.assertEqual(header[5],source.stat().st_size);self.assertEqual(header[6],zlib.crc32(source.read_bytes()))
            self.assertIn('Ação'.encode(),data)
    def test_bad_source_never_replaces_prior_index(self):
        with tempfile.TemporaryDirectory() as name:
            root=Path(name);(root/'library').mkdir();(root/'cache').mkdir()
            target=root/'cache/library-v1.t5i';target.write_bytes(b'prior-index')
            (root/'library/games.json').write_text('{invalid json}')
            with self.assertRaises(ValueError):library_index.build(root)
            self.assertEqual(target.read_bytes(),b'prior-index')
