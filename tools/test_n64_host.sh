#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tab5_probe_tmp="$(mktemp -d /tmp/tab5-n64-host.XXXXXX)"
trap 'rm -rf "$tab5_probe_tmp"' EXIT
tab5_probe_flags=()
if [[ "$(uname)" == Darwin && -d /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk ]]; then
 tab5_probe_flags=(-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk)
fi
.venv/bin/python tools/build_n64_probe.py > artifacts/n64-fixture-build.log
cc "${tab5_probe_flags[@]}" -O2 -std=c11 -fsanitize=address,undefined \
 -I experiments/n64/mupen/src -I experiments/n64/mupen/src/device/r4300 -I experiments/n64/mupen/subprojects/xxhash \
 experiments/n64/probe/pure_interp_probe.c experiments/n64/mupen/src/device/r4300/cp1.c \
 experiments/n64/mupen/src/device/r4300/cp2.c experiments/n64/probe/probe.c \
 experiments/n64/probe/host_main.c -lm -o "$tab5_probe_tmp/probe"
"$tab5_probe_tmp/probe" artifacts/test-roms/tab5-n64-cpu-probe.mips > artifacts/n64-probe-host-result.json
