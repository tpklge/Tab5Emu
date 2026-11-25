import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(shutil.which("cmake"), "CMake unavailable")
class NativeBuildGate(unittest.TestCase):
    def test_optional_scummvm_does_not_load_sources_during_discovery(self):
        component = Path(__file__).resolve().parents[1] / "components/scummvm/CMakeLists.txt"
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "gate.cmake"
            script.write_text('function(idf_component_register)\nendfunction()\nset(CMAKE_BUILD_EARLY_EXPANSION TRUE)\n'+f'include("{component}")\n')
            subprocess.run([shutil.which("cmake"), "-P", str(script)], check=True, capture_output=True)
    def test_optional_component_does_not_require_private_assets(self):
        component = Path(__file__).resolve().parents[1] / "components/sm64_native/CMakeLists.txt"
        for profile, early in (("stable", "TRUE"), ("sm64-native", "TRUE"), ("stable", "FALSE")):
            with self.subTest(profile=profile, early=early), tempfile.TemporaryDirectory() as directory:
                script = Path(directory) / "gate.cmake"
                script.write_text(
                    'function(idf_component_register)\nendfunction()\n'
                    f'set(TAB5_PROFILE "{profile}")\n'
                    f'set(CMAKE_BUILD_EARLY_EXPANSION {early})\n'
                    'set(SM64_NATIVE_SOURCE "/missing-private-assets")\n'
                    'set(PYTHON "/missing-python-must-not-run")\n'
                    f'include("{component}")\n'
                )
                subprocess.run([shutil.which("cmake"), "-P", str(script)], check=True, capture_output=True)
