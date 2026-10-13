#!/usr/bin/env python3
"""Reproducible, board-scoped fixes against the pinned ZMK Bluetooth source."""
from pathlib import Path
import argparse
import hashlib

parser = argparse.ArgumentParser()
parser.add_argument("zmk", type=Path)
args = parser.parse_args()
path = args.zmk / "app/src/ble.c"
original_hash = "fcf8925613bcf7685053911a565cf897824a56adf0a63c5e6499c604a4f459c2"
edits = [
    ("        update_advertising();\n        return;\n    }\n\n    LOG_DBG(\"Connected %s\", addr);\n\n    update_advertising();",
     "#if IS_ENABLED(CONFIG_CARDPUTER_UI)\n"
     "        k_work_submit(&update_advertising_work);\n"
     "#else\n        update_advertising();\n#endif\n"
     "        return;\n    }\n\n    LOG_DBG(\"Connected %s\", addr);\n\n"
     "#if IS_ENABLED(CONFIG_CARDPUTER_UI)\n"
     "    /* Keep controller commands out of the ESP Bluetooth RX callback. */\n"
     "    k_work_submit(&update_advertising_work);\n"
     "#else\n    update_advertising();\n#endif"),
    ("    set_profile_address(active_profile, dst);\n    update_advertising();",
     "    set_profile_address(active_profile, dst);\n"
     "#if IS_ENABLED(CONFIG_CARDPUTER_UI)\n"
     "    k_work_submit(&update_advertising_work);\n"
     "#else\n    update_advertising();\n#endif"),
]
contents = path.read_text()
restored = contents
for old, new in edits:
    restored = restored.replace(new, old)
if hashlib.sha256(restored.encode()).hexdigest() != original_hash:
    raise SystemExit("Unrecognized ZMK ble.c; refusing patch")
for old, new in edits:
    if new in contents:
        continue
    if contents.count(old) != 1:
        raise SystemExit("Unexpected source context; refusing patch")
    contents = contents.replace(old, new)
path.write_text(contents)
print("app/src/ble.c", hashlib.sha256(path.read_bytes()).hexdigest())
