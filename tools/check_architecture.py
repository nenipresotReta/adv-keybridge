#!/usr/bin/env python3
"""Enforce direct include boundaries for the USB subsystem and pure renderer.

This is a dependency guard, not a C parser or a concurrency proof. Add a new
dependency only after reviewing module ownership and tests.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
STANDARD = {"errno.h", "stdbool.h", "stdint.h", "stddef.h", "stdio.h", "string.h"}
KERNEL = {"zephyr/kernel.h", "zephyr/init.h", "zephyr/sys/atomic.h", "zephyr/logging/log.h"}
ALLOWED = {
    "include/adv_power.h": set(),
    "src/power_state.c": {"adv_power.h"},
    "src/power.c": KERNEL | {"esp_attr.h", "esp_sleep.h", "driver/rtc_io.h", "zephyr/drivers/gpio.h", "zephyr/sys/poweroff.h", "adv_power.h", "adv_screen.h", "adv_usb.h", "adv_sd_log.h"},
    "include/adv_pages.h": set(),
    "src/pages_state.c": {"adv_pages.h"},
    "src/pages.c": KERNEL | {"adv_pages.h", "adv_screen.h", "zephyr/settings/settings.h", "zmk/ble.h", "zmk/workqueue.h"},
    "src/behavior_page.c": {"drivers/behavior.h", "zmk/behavior.h", "adv_pages.h"},
    "src/sd_log.c": KERNEL | {"ff.h", "zephyr/fs/fs.h", "zephyr/logging/log_backend.h",
        "zephyr/logging/log_output.h", "adv_sd_log.h", "adv_battery_history.h"},
    "include/adv_sd_log.h": set(),
    "src/log_console.c": {"zephyr/shell/shell.h", "adv_sd_log.h"},
    "src/usb_host.c": KERNEL | {"adv_usb.h", "adv_power.h", "adv_usb_dwc2.h", "usb/board.h",
        "usb/diagnostics.h", "usb/input.h", "usb/session.h", "usb/transport.h"},
    "src/usb/board.c": {"zephyr/init.h", "esp_rom_gpio.h", "soc/gpio_sig_map.h",
        "soc/gpio_pins.h", "soc/rtc_cntl_struct.h", "hal/usb_wrap_ll.h", "tusb.h",
        "portable/synopsys/dwc2/dwc2_type.h", "board.h"},
    "src/usb/diagnostics.c": {"zephyr/init.h", "zephyr/kernel.h", "adv_usb_dwc2.h", "diagnostics.h"},
    "src/usb/session.c": {"session.h"},
    "src/usb/transport.c": KERNEL | {"tusb.h", "adv_usb_keys.h", "adv_usb_dwc2.h",
        "transport.h", "board.h", "diagnostics.h"},
    "src/usb_input.c": {"zephyr/kernel.h", "zephyr/sys/atomic.h",
        "zmk/events/keycode_state_changed.h", "adv_usb_keys.h", "adv_hid.h", "adv_power.h", "usb/input.h"},
    "src/hid_report.c": {"adv_hid.h"},
    "src/ui_render.c": {"adv_ui.h"},
    "src/battery.c": KERNEL | {"zephyr/drivers/adc.h", "adv_battery.h", "adv_battery_trend.h", "adv_battery_history.h"},
    "include/adv_battery_history.h": set(),
    "src/battery_history.c": KERNEL | {"adv_battery_history.h", "zephyr/fs/fs.h"},
    "include/adv_battery.h": set(),
    "include/adv_battery_trend.h": {"adv_battery.h"},
    "src/battery_trend.c": {"adv_battery_trend.h"},
    "include/adv_screen.h": {"dt-bindings/adv_screen.h"},
    "src/screen_state.c": {"adv_screen.h"},
    "src/screen.c": KERNEL | {"adv_screen.h", "adv_pages.h", "adv_power.h", "zephyr/drivers/display.h",
        "zephyr/drivers/pwm.h", "zephyr/settings/settings.h", "zmk/keymap.h", "zmk/workqueue.h"},
    "src/behavior_screen.c": {"drivers/behavior.h", "zmk/behavior.h", "adv_screen.h"},
    "include/adv_usb.h": set(),
    "include/adv_usb_dwc2.h": {"zephyr/kernel.h"},
    "src/usb/board.h": set(),
    "src/usb/diagnostics.h": set(),
    "src/usb/input.h": {"adv_hid.h"},
    "src/usb/session.h": set(),
    "src/usb/transport.h": {"adv_usb.h", "adv_hid.h"},
}

def violations(relative, text):
    includes = set(re.findall(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', text, re.M))
    errors = [f"{relative}: forbidden dependency {dep}" for dep in sorted(includes - STANDARD - ALLOWED[relative])]
    if relative.endswith(".c") and relative != "src/usb/board.c":
        if re.search(r'\b(?:RTCCNTL|USB_WRAP|usb_wrap_ll_\w+|esp_rom_gpio_\w+)\b|0x600[0-9a-fA-F]+', text):
            errors.append(f"{relative}: USB registers belong to board.c")
    if relative.endswith(".c") and relative != "src/usb/transport.c":
        if re.search(r'\b(?:tuh_\w+|tusb_init)\s*\(', text):
            errors.append(f"{relative}: TinyUSB calls belong to transport.c")
    return errors

def main():
    errors=[]
    for relative in ALLOWED:
        errors += violations(relative, (ROOT / relative).read_text())
    for path in (ROOT / "src/usb").glob("*"):
        if path.suffix in {".c", ".h"} and str(path.relative_to(ROOT)) not in ALLOWED:
            errors.append(f"{path.name}: new module needs an explicit dependency boundary")
    for relative in ("src/ui.c", "src/behavior_usb_host.c"):
        text=(ROOT / relative).read_text()
        if re.search(r'#\s*include\s*[<"](?:usb/|tusb|adv_usb_dwc2)', text):
            errors.append(f"{relative}: use public adv_usb.h, not USB internals")
    keymap=(ROOT / "config/cardputer_adv.keymap").read_text()
    layers=re.findall(r'^\s*(base|function|settings)\s*\{',keymap,re.M)
    if layers != ["base","function","settings"]:
        errors.append("Settings must be isolated on the highest layer (index 2)")
    match=re.search(r'\bsettings\s*\{.*?bindings\s*=\s*<(.*?)>;',keymap,re.S)
    if not match:
        errors.append("Missing settings keymap")
    else:
        bindings=re.findall(r'&(\w+)\b',match.group(1))
        if len(bindings)!=56 or any(b not in {"screen","none"} for b in bindings):
            errors.append("Settings keys must be local screen commands or blocked; no HID/BLE/USB fallthrough")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("PASS: USB module dependency boundaries")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
