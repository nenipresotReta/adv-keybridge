#!/usr/bin/env python3
"""Backport official #92130 configuration and match Zephyr 4.1 IRQ typedefs.

The controller blob and OSI adapter are kept together at HAL 8502aafa.
"""
from pathlib import Path
import argparse
import hashlib

parser = argparse.ArgumentParser()
parser.add_argument("zephyr", type=Path)
parser.add_argument("hal", type=Path)
args = parser.parse_args()
kconfig = args.zephyr / "drivers/bluetooth/hci/Kconfig.esp32"
upstream = Path(__file__).resolve().parent.parent / "upstream/Kconfig.esp32"
assert hashlib.sha256(upstream.read_bytes()).hexdigest() == "ca38ef33fcf0ae49945d243ce97426d25aca30056cde34b00768170c02f4efed"
if hashlib.sha256(kconfig.read_bytes()).hexdigest() not in {
    "dc034c675ef9dc9ddda9b517fdb06137736309112d2a6baeab7d027b05803cc8",
    hashlib.sha256(upstream.read_bytes()).hexdigest(),
}:
    raise SystemExit("Unrecognized Zephyr ESP32 Kconfig")
kconfig.write_bytes(upstream.read_bytes())
adapter = args.hal / "zephyr/esp32s3/src/bt/esp_bt_adapter.c"
old = "(intr_handle_t *) p->handle"
new = "(struct intr_handle_data_t **) p->handle"
contents = adapter.read_text()
restored = contents.replace(new, old)
if hashlib.sha256(restored.encode()).hexdigest() != "dc1df16f0f696436863e12b00a2d6c4098091fc526237234936245e93946f6d7":
    raise SystemExit("Unrecognized HAL ESP32-S3 Bluetooth adapter")
if old in contents:
    assert contents.count(old) == 1
    adapter.write_text(contents.replace(old, new))
print("ESP32 controller Kconfig and Zephyr 4.1 IRQ compatibility prepared")

# ESP32-S3 has separate RTC slow (0x50000000) and fast (0x600fe000)
# segments. The 4.1 simple loader recognizes only fast memory and stops parsing
# at the first slow segment, leaving incorrect flash MMU offsets after elf2image.
loader = args.zephyr / "soc/espressif/common/loader.c"
old_rtc = "#define IS_RTC(addr) (addr >= SOC_RTC_DRAM_LOW && addr < SOC_RTC_DRAM_HIGH)"
new_rtc = """#if defined(CONFIG_SOC_SERIES_ESP32S3)
#define IS_RTC(addr) ((addr >= SOC_RTC_DRAM_LOW && addr < SOC_RTC_DRAM_HIGH) || \\
                      (addr >= SOC_RTC_DATA_LOW && addr < SOC_RTC_DATA_HIGH))
#else
#define IS_RTC(addr) (addr >= SOC_RTC_DRAM_LOW && addr < SOC_RTC_DRAM_HIGH)
#endif"""
contents = loader.read_text()
restored = contents.replace(new_rtc, old_rtc)
if hashlib.sha256(restored.encode()).hexdigest() not in {
    "56cfa3cfbda650719aab001b8b349c9d87ce71a1d51595c9c3b29360f0b901be",
    "455a30566cd84d2d9b478bdefb0870a8195af87dff577ff1c581ed2ac751d260",
}:
    raise SystemExit("Unrecognized Zephyr simple loader")
loader.write_text(restored.replace(old_rtc, new_rtc))
print("ESP32-S3 simple loader RTC slow/fast segment recognition prepared")
