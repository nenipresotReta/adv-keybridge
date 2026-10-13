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
policy = Path(__file__).with_name("ble_profile_policy.inc").read_text()
edits += [
    ("int update_advertising(void) {", "#if IS_ENABLED(CONFIG_CARDPUTER_UI)\n" + policy + "#endif\n\nint update_advertising(void) {"),
    ("    enum advertising_type desired_adv = ZMK_ADV_NONE;", "    enum advertising_type desired_adv = ZMK_ADV_NONE;\n"
     "#if IS_ENABLED(CONFIG_CARDPUTER_UI)\n"
     "    unsigned key = adv_pages_enabled_mask() | ((unsigned)active_profile << 8) | ((unsigned)zmk_ble_active_profile_is_open() << 16);\n"
     "    if (!(adv_pages_enabled_mask() & BIT(active_profile))) {\n"
     "        if (advertising_status != ZMK_ADV_NONE) { CHECKED_ADV_STOP(); }\n"
     "        return 0;\n    }\n"
     "    if (key != adv_policy_key) {\n"
     "        if (advertising_status != ZMK_ADV_NONE) { CHECKED_ADV_STOP(); }\n"
     "        adv_policy_key = key;\n"
     "    }\n#endif"),
    ("static void update_advertising_callback(struct k_work *work) { update_advertising(); }",
     "static void update_advertising_callback(struct k_work *work) {\n"
     "#if IS_ENABLED(CONFIG_CARDPUTER_UI)\n    adv_disconnect_disabled();\n#endif\n"
     "    update_advertising();\n}"),
    ("K_WORK_DEFINE(update_advertising_work, update_advertising_callback);",
     "K_WORK_DEFINE(update_advertising_work, update_advertising_callback);\n"
     "#if IS_ENABLED(CONFIG_CARDPUTER_UI)\n"
     "void adv_ble_policy_refresh(void) { k_work_submit(&update_advertising_work); }\n#endif"),
    ("    if (info.role == BT_CONN_ROLE_PERIPHERAL && !pairing_allowed_for_current_profile(conn)) {",
     "#if IS_ENABLED(CONFIG_CARDPUTER_UI)\n"
     "    if (info.role == BT_CONN_ROLE_PERIPHERAL && adv_peer_disabled(bt_conn_get_dst(conn)))\n"
     "        return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;\n#endif\n"
     "    if (info.role == BT_CONN_ROLE_PERIPHERAL && !pairing_allowed_for_current_profile(conn)) {"),
]
# Keep the upstream macro intact outside Cardputer builds.
old_call = "bt_le_adv_start(ZMK_ADV_CONN_NAME, zmk_ble_ad, ARRAY_SIZE(zmk_ble_ad), NULL, 0)"
# Replace only its original macro occurrence; the helper uses a local parameter.
old_macro = "#define CHECKED_OPEN_ADV()"
edits.append((old_macro, "#if IS_ENABLED(CONFIG_CARDPUTER_UI)\n#define ADV_OPEN_START() adv_start_filtered()\n#else\n#define ADV_OPEN_START() " + old_call + "\n#endif\n\n" + old_macro))
edits.append(("err = " + old_call + ";", "err = ADV_OPEN_START();"))
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
