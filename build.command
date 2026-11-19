#!/bin/zsh
set -eu
cd "${0:A:h}"
command -v arduino-cli >/dev/null
task_root="$PWD"
task_tools="${ALPHAPI_TOOLCHAIN_DIR:-$task_root/toolchain}"
mkdir -p "$task_tools/arduino/user" "$task_tools/arduino/data" "$task_tools/arduino/downloads"
python3 - "$task_tools" <<'PY'
from pathlib import Path
import json, sys
tools = Path(sys.argv[1]).resolve()
config = {'board_manager': {'additional_urls': ['https://espressif.github.io/arduino-esp32/package_esp32_index.json']},
          'directories': {k: str(tools / 'arduino' / k) for k in ('data', 'downloads', 'user')}}
(tools / 'arduino/arduino-cli.yaml').write_text(json.dumps(config))
version = Path('VERSION').read_text().strip()
if len(version) != 4 or not version.isdigit():
    raise SystemExit('VERSION 必须为四位数字')
Path('BuildVersion.h').write_text(f'#pragma once\n#define BUILD_VERSION "{version}"\n')
PY
task_config="$task_tools/arduino/arduino-cli.yaml"
if [[ ! -f "$task_tools/arduino/data/package_esp32_index.json" || ! -f "$task_tools/arduino/data/library_index.json" ]]; then
  arduino-cli --config-file "$task_config" core update-index
fi
arduino-cli --config-file "$task_config" core install esp32:esp32@2.0.14
# Pin the library commit for repeatable builds.
task_nimble="$task_tools/pageturner/NimBLE-Arduino"
task_nimble_commit=a17bec675cd421a5f83bb5fe266fd5b498990aa4
if [[ ! -d "$task_nimble/.git" ]]; then
  mkdir -p "${task_nimble:h}"
  git clone --depth 1 --branch 2.3.7 https://github.com/h2zero/NimBLE-Arduino.git "$task_nimble"
fi
[[ "$(git -C "$task_nimble" rev-parse HEAD)" == "$task_nimble_commit" ]]
task_version="$(cat VERSION)"
task_build="$task_root/build/$task_version"
task_sketch="$task_build/sketch/AlphaPi_PageTurner"
mkdir -p "$task_sketch"
cp ./*.ino ./*.cpp ./*.h "$task_sketch/"
arduino-cli --config-file "$task_config" compile \
  --fqbn 'esp32:esp32:esp32c3:FlashSize=4M,PartitionScheme=default,CDCOnBoot=cdc' \
  --libraries "${task_nimble:h}" --build-path "$task_build/compile" "$task_sketch"
mkdir -p "firmware/output/$task_version"
cp "$task_build/compile/AlphaPi_PageTurner.ino.bin" "firmware/output/$task_version/AlphaPi-PageTurner-$task_version-app.bin"
print "主程序镜像：$task_root/firmware/output/$task_version/AlphaPi-PageTurner-$task_version-app.bin（0x10000）"
