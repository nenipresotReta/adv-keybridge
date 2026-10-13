#!/bin/sh
set -eu
module_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
"${PYTHON:-python3}" "$module_dir/tools/check_architecture.py"
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/adv-zmk-test.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$module_dir/tests/stubs" -I"$module_dir/include" \
  "$module_dir/tests/screen_test.c" "$module_dir/src/screen_state.c" -o "$test_dir/screen_test"
"$test_dir/screen_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$module_dir/tests/stubs" -I"$module_dir/include" \
  "$module_dir/tests/battery_test.c" "$module_dir/src/battery_trend.c" -o "$test_dir/battery_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$module_dir/include" "$module_dir/tests/battery_trend_test.c" \
  "$module_dir/src/battery_trend.c" -o "$test_dir/battery_trend_test"
"$test_dir/battery_trend_test"
"$test_dir/battery_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$module_dir/include" \
  "$module_dir/tests/native_test.c" "$module_dir/tests/hid_test.c" \
  "$module_dir/src/ui_render.c" "$module_dir/src/hid_report.c" \
  -o "$test_dir/native_test"
"$test_dir/native_test" "$@"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$module_dir/tests/stubs" -I"$module_dir/include" -I"$module_dir/src" \
  "$module_dir/tests/usb_input_test.c" "$module_dir/src/usb_input.c" \
  -o "$test_dir/usb_input_test"
"$test_dir/usb_input_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$module_dir/src" \
  "$module_dir/tests/usb_session_test.c" "$module_dir/src/usb/session.c" \
  -o "$test_dir/usb_session_test"
"$test_dir/usb_session_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$module_dir/tests/stubs" -I"$module_dir/include" -I"$module_dir/src" \
  "$module_dir/tests/usb_diagnostics_test.c" -o "$test_dir/usb_diagnostics_test"
"$test_dir/usb_diagnostics_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$module_dir/tests/stubs" -I"$module_dir/include" -I"$module_dir/src" \
  "$module_dir/tests/usb_transport_test.c" "$module_dir/src/usb/transport.c" "$module_dir/src/hid_report.c" \
  -o "$test_dir/usb_transport_test"
"$test_dir/usb_transport_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$module_dir/tests/stubs" -I"$module_dir/include" \
  "$module_dir/tests/sd_log_test.c" -o "$test_dir/sd_log_test"
"$test_dir/sd_log_test"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -I"$module_dir/tests/stubs" -I"$module_dir/include" \
  "$module_dir/tests/log_console_test.c" -o "$test_dir/log_console_test"
"$test_dir/log_console_test"
"${PYTHON:-python3}" "$module_dir/tests/read_logs_test.py"
if [ -n "${ADV_TINYUSB_DIR:-}" ]; then
  "${PYTHON:-python3}" "$module_dir/tools/test_dwc2.py" "$ADV_TINYUSB_DIR"
  "${PYTHON:-python3}" "$module_dir/tools/test_usb_irq.py" "$ADV_TINYUSB_DIR"
  "${PYTHON:-python3}" "$module_dir/tools/test_usb_retry.py" "$ADV_TINYUSB_DIR"
fi
