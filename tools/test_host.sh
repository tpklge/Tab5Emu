#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tab5_root="$PWD"
tab5_tmp="$(mktemp -d /tmp/tab5emu-host.XXXXXX)"
trap 'rm -rf "$tab5_tmp"' EXIT
tab5_sdk_flags=()
if [[ "$(uname)" == Darwin && -d /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk ]]; then
  tab5_sdk_flags=(-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk)
fi
tab5_python=python3
if [[ -x .venv/bin/python ]]; then tab5_python=.venv/bin/python; fi
mkdir -p artifacts
"$tab5_python" -m unittest discover -s tests -v > artifacts/host-python-tests.txt 2>&1
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/tab5-emu/include -c components/tab5-emu/storage/file_read.cpp -o "$tab5_tmp/file-read.o"
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/tab5-emu/include tests/test_file_read.cpp "$tab5_tmp/file-read.o" -o "$tab5_tmp/file-read-test"
"$tab5_tmp/file-read-test" > artifacts/host-file-read-tests.txt
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/tab5-emu/include -I components/statistics/include -I components/gbc/include \
  tests/test_platform.cpp components/tab5-emu/storage/atomic_write.cpp -o "$tab5_tmp/platform"
"$tab5_tmp/platform" > artifacts/host-platform-tests.txt
cc "${tab5_sdk_flags[@]}" -Wno-deprecated-declarations -fsanitize=address,undefined \
  -I managed_components/espressif__cjson/cJSON \
  -c managed_components/espressif__cjson/cJSON/cJSON.c -o "$tab5_tmp/cjson.o"
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "-DTAB5_CONTENT_ROOT=\"$tab5_tmp/content\"" \
  -I components/rom_info/include -I components/tab5-emu/include \
  -I managed_components/espp__format/include -I managed_components/espp__format/detail/fmt/include \
  -I managed_components/espressif__cjson/cJSON \
  tests/test_catalog.cpp components/rom_info/src/library.cpp components/rom_info/src/library_index.cpp "$tab5_tmp/cjson.o" "$tab5_tmp/file-read.o" -o "$tab5_tmp/catalog"
"$tab5_tmp/catalog" > artifacts/host-catalog-tests.txt
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "-DTAB5_CONTENT_ROOT=\"$tab5_tmp/profiles\"" \
  -I components/tab5-emu/include -I managed_components/espressif__cjson/cJSON \
  tests/test_input_profiles.cpp components/tab5-emu/input_profiles/input_profiles.cpp \
  "$tab5_tmp/cjson.o" "$tab5_tmp/file-read.o" -o "$tab5_tmp/profiles-test"
"$tab5_tmp/profiles-test" > artifacts/host-input-profile-tests.txt
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  "-DTAB5_CONTENT_ROOT=\"$tab5_tmp/battery\"" -I components/tab5-emu/include \
  tests/test_battery_store.cpp components/tab5-emu/storage/battery_store.cpp \
  components/tab5-emu/storage/atomic_write.cpp -o "$tab5_tmp/battery-test"
"$tab5_tmp/battery-test" > artifacts/host-battery-store-tests.txt
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/settings/include -I managed_components/espressif__cjson/cJSON \
  tests/test_settings_json.cpp components/settings/src/settings_json.cpp "$tab5_tmp/cjson.o" -o "$tab5_tmp/settings-test"
"$tab5_tmp/settings-test" > artifacts/host-settings-json-tests.txt
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/genesis/include tests/test_md_sram.cpp -o "$tab5_tmp/md-test"
"$tab5_tmp/md-test" > artifacts/host-md-sram-tests.txt
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/genesis/include tests/test_md_eeprom.cpp -o "$tab5_tmp/md-eeprom-test"
"$tab5_tmp/md-eeprom-test" > artifacts/host-md-eeprom-tests.txt
cc "${tab5_sdk_flags[@]}" -fsanitize=address,undefined -I components/gbc/include -I components/gbc/gnuboy/gnuboy/include \
  -c components/gbc/src/rtc.c -o "$tab5_tmp/rtc.o"
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/gbc/include -I components/gbc/gnuboy/gnuboy/include tests/test_rtc.cpp "$tab5_tmp/rtc.o" -o "$tab5_tmp/rtc-test"
"$tab5_tmp/rtc-test" > artifacts/host-rtc-tests.txt
"$tab5_python" tools/stage_snes_apu.py components/snes/snes9x2005/source/apu_blargg.c "$tab5_tmp/snes_apu.c"
cc "${tab5_sdk_flags[@]}" -std=c11 -O3 -fwrapv -fsanitize=address,undefined -DUSE_BLARGG_APU -D__LIBRETRO__ \
  -I tests/host_stubs -I components/snes/snes9x2005/source -I components/snes/snes9x2005/libretro-common/include \
  tests/test_snes_apu.c "$tab5_tmp/snes_apu.c" -o "$tab5_tmp/snes-apu-test"
"$tab5_tmp/snes-apu-test" > artifacts/host-snes-apu-tests.txt
(
  cd "$tab5_tmp"
  cmake -P "$tab5_root/tools/md_cpu_adapter.cmake"
)
cc "${tab5_sdk_flags[@]}" -O3 -fno-strict-aliasing -fsanitize=address,undefined \
  -Wno-unknown-pragmas -Wno-implicit-function-declaration \
  -I "$tab5_tmp/m68k-adapter" -I components/genesis/include \
  -I components/genesis/gwenesis/src/savestate \
  -c "$tab5_tmp/m68k-adapter/m68kcpu.c" -o "$tab5_tmp/md-cpu.o"
cc "${tab5_sdk_flags[@]}" -O3 -fno-strict-aliasing -fsanitize=address,undefined \
  -I "$tab5_tmp/m68k-adapter" -I components/genesis/include \
  -I components/genesis/gwenesis/src/savestate \
  tests/test_md_memory_core.c "$tab5_tmp/md-cpu.o" -o "$tab5_tmp/md-memory-test"
"$tab5_tmp/md-memory-test" > artifacts/host-md-memory-core-tests.txt
"$tab5_python" - "$tab5_tmp" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0,'tools')
from build_md_eeprom_rom import build
for board in ('sega','ea','acclaim16'):
    (Path(sys.argv[1])/(board+'.md')).write_bytes(build(board))
PY
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I "$tab5_tmp/m68k-adapter" -I components/genesis/include \
  -I components/genesis/gwenesis/src/savestate \
  tests/test_md_eeprom_core.cpp "$tab5_tmp/md-cpu.o" -o "$tab5_tmp/md-eeprom-core-test"
"$tab5_tmp/md-eeprom-core-test" "$tab5_tmp" > artifacts/host-md-eeprom-core-tests.txt
"$tab5_python" tools/stage_gb_loader.py components/gbc/gnuboy/gnuboy/src/loader.c "$tab5_tmp/gb_loader.c"
cc "${tab5_sdk_flags[@]}" -fsanitize=address,undefined \
  -I tests/host_stubs -I components/shared_memory/include \
  -c components/shared_memory/src/shared_memory.c -o "$tab5_tmp/shared-memory.o"
cc "${tab5_sdk_flags[@]}" -fsanitize=address,undefined \
  -I tests/host_stubs -I components/gbc/gnuboy/gnuboy/include \
  -c "$tab5_tmp/gb_loader.c" -o "$tab5_tmp/gb-loader.o"
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I tests/host_stubs -I components/gbc/include -I components/gbc/gnuboy/port/include \
  -I components/gbc/gnuboy/gnuboy/include -I components/shared_memory/include \
  tests/test_gb_memory.cpp components/gbc/src/gb_memory.cpp \
  "$tab5_tmp/shared-memory.o" "$tab5_tmp/gb-loader.o" -o "$tab5_tmp/gb-memory-test"
"$tab5_tmp/gb-memory-test" > artifacts/host-gb-memory-tests.txt
"$tab5_python" tools/stage_audio_policy.py managed_components/espp__codec/include/i2s_tx_feed.hpp "$tab5_tmp/audio/i2s_tx_feed.hpp"
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I tests/audio_stubs -I "$tab5_tmp/audio" tests/test_audio_feeder.cpp -o "$tab5_tmp/audio-feeder-test"
"$tab5_tmp/audio-feeder-test" > artifacts/host-audio-feeder-tests.txt
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/tab5-emu/include tests/test_audio_preroll.cpp -o "$tab5_tmp/audio-preroll-test"
"$tab5_tmp/audio-preroll-test" > artifacts/host-audio-preroll-tests.txt
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/statistics/include tests/test_presentation_rate.cpp -o "$tab5_tmp/presentation-rate-test"
"$tab5_tmp/presentation-rate-test" > artifacts/host-presentation-rate-tests.txt
c++ "${tab5_sdk_flags[@]}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/sm64_native/include tests/test_sm64_save.cpp -o "$tab5_tmp/sm64-save-test"
"$tab5_tmp/sm64-save-test" > artifacts/host-sm64-save-tests.txt
echo 'Host: Python, suites C++, APU SNES e memoria M68000 passaram (ASan/UBSan).'
