#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tab5_n64_temp="$(mktemp -d /tmp/tab5-n64-console.XXXXXX)"
trap 'rm -rf "$tab5_n64_temp"' EXIT
tab5_n64_python=python3
if [[ -x .venv/bin/python ]]; then tab5_n64_python=.venv/bin/python; fi
tab5_n64_sdk=()
if [[ "$(uname)" == Darwin && -d /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk ]]; then
  tab5_n64_sdk=(-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk)
fi
"$tab5_n64_python" tools/build_n64_console_rom.py > artifacts/n64-console-fixture-build.log
"$tab5_n64_python" tools/stage_n64.py "$tab5_n64_temp/stage"
tab5_n64_flags=(-O2 -std=c11 -D_POSIX_C_SOURCE=200809L -fwrapv -fno-strict-aliasing -fsanitize=address,undefined
  -DNO_ASM -DUSE_CLIENT_ENDIAN -DPLUGIN_API_VERSION=0x0101
  -I "$tab5_n64_temp/stage" -I "$tab5_n64_temp/stage/device/r4300"
  -I "$tab5_n64_temp/stage/rsp" -I "$tab5_n64_temp/stage/rdp"
  -I experiments/n64/mupen/src -I experiments/n64/mupen/src/device/r4300
  -I experiments/n64/mupen/subprojects/xxhash -I components/n64/include)
tab5_n64_objects=()
for tab5_n64_source in "$tab5_n64_temp"/stage/device/r4300/{pure_interp,cp1,cp2}.c \
  "$tab5_n64_temp"/stage/rsp/su.c "$tab5_n64_temp"/stage/rsp/vu/{vu,multiply,add,select,logical,divide}.c \
  "$tab5_n64_temp"/stage/rdp/n64video.c components/n64/src/{n64_console,n64_plugins}.c \
  experiments/n64/mupen/src/device/pif/{cic,n64_cic_nus_6105}.c; do
  tab5_n64_object="$tab5_n64_temp/$(basename "$tab5_n64_source").o"
  "${CC:-cc}" "${tab5_n64_sdk[@]}" "${tab5_n64_flags[@]}" -c "$tab5_n64_source" -o "$tab5_n64_object"
  tab5_n64_objects+=("$tab5_n64_object")
done
"${CC:-cc}" "${tab5_n64_sdk[@]}" "${tab5_n64_flags[@]}" tests/test_n64_console.c "${tab5_n64_objects[@]}" -lm -o "$tab5_n64_temp/test"
tab5_n64_fixture="$($tab5_n64_python -c 'import json; print(json.load(open("artifacts/test-roms/tab5-n64-console-manifest.json"))["file"])')"
UBSAN_OPTIONS=halt_on_error=1 "$tab5_n64_temp/test" "artifacts/test-roms/$tab5_n64_fixture" > artifacts/host-n64-console-tests.txt
cat artifacts/host-n64-console-tests.txt
