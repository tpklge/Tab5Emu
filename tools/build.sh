#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p artifacts
tab5_profile="${1:-stable}"
case "$tab5_profile" in sm64-native|stable|stable-rv32|stable-n64|sd-cores|n64|n64-cpu-probe|gba-rv32|upstream) ;; *) echo 'Perfil desconhecido' >&2; exit 2;; esac
tab5_gba_heap_check="${TAB5_GBA_HEAP_CHECK:-OFF}"
case "$tab5_gba_heap_check" in ON|OFF) ;; *) echo 'TAB5_GBA_HEAP_CHECK deve ser ON ou OFF' >&2; exit 2;; esac
if [[ -f .tools/esp-idf/export.sh ]]; then
  # SDK installed in this workspace; never modify the user's other IDF.
  source .tools/esp-idf/export.sh
fi
tab5_idf_version="$(idf.py --version)"
if [[ ! "$tab5_idf_version" =~ ^ESP-IDF\ v6\.1([.-]|$) ]]; then
  echo "SDK incompatível: $tab5_idf_version. Use o ESP-IDF 6.1 fixado no SOURCE_MANIFEST. Não alterar o SDK dos outros projetos." >&2
  exit 1
fi
echo "$tab5_idf_version"
# App-only intentionally avoids the unused upstream bootloader/update image.
tab5_config_args=()
if [[ "$tab5_profile" == sm64-native ]]; then
  tab5_config_args=(-D "SDKCONFIG=$PWD/build-sm64-native/sdkconfig"
                    -D "SDKCONFIG_DEFAULTS=$PWD/sdkconfig.defaults;$PWD/sdkconfig.sm64-native.defaults")
fi
idf.py -B "build-$tab5_profile" -D "TAB5_PROFILE=$tab5_profile" -D "TAB5_GBA_HEAP_CHECK=$tab5_gba_heap_check" "${tab5_config_args[@]}" app
python tools/size_report.py "build-$tab5_profile" "artifacts/$tab5_profile-size.json"
