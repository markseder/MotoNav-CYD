#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
sketch_dir="$repo_dir/firmware/motonav/MotoNav_CYD_V1_1_2_Stable"
cli="${ARDUINO_CLI:-arduino-cli}"
build_dir="${MOTONAV_BUILD_DIR:-${TMPDIR:-/tmp}/motonav-fnk0104b-build}"
mkdir -p "$build_dir"
# -include must reach library sources too; an include in the INO alone is insufficient.
"$cli" compile \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,CDCOnBoot=cdc' \
  --jobs "${MOTONAV_BUILD_JOBS:-4}" \
  --build-path "$build_dir" \
  --build-property "compiler.cpp.extra_flags=-include \"$sketch_dir/fnk0104b_tft_setup.h\"" \
  "$sketch_dir"
