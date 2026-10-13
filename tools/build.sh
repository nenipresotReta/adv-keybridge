#!/bin/sh
set -eu
module_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$module_dir"
workspace=$(west topdir)
python "$module_dir/tools/prepare_bluetooth.py" "$workspace/zmk"
python "$module_dir/tools/prepare_espressif.py" "$workspace/zephyr" "$workspace/modules/hal/espressif"
python "$module_dir/tools/prepare_usb.py" "$workspace/modules/lib/tinyusb" "$workspace/zmk"
west build -s "$workspace/zmk/app" -d "$module_dir/build" \
  -b cardputer_adv/esp32s3/procpu -- \
  -DKEYMAP_FILE="$module_dir/config/cardputer_adv.keymap" \
  -DZMK_CONFIG="$module_dir/config" -DZMK_EXTRA_MODULES="$module_dir" \
  -DEXTRA_CONF_FILE="$module_dir/config/native.conf" \
  -DADV_TINYUSB_DIR="$workspace/modules/lib/tinyusb" \
  -DBUILD_VERSION=adv-zmk-0.1.2-native "$@"
