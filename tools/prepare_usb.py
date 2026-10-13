#!/usr/bin/env python3
"""Adapt pinned TinyUSB 0.21.0 to Zephyr; source ownership in ZMK HID events."""
import argparse
import hashlib
from pathlib import Path
from usb_dwc2_edits import HCD_SHA, HCD_EDITS, COMMON_SHA, COMMON_EDITS, HEADER_SHA, HEADER_EDITS
p = argparse.ArgumentParser()
p.add_argument("tinyusb", type=Path)
p.add_argument("zmk", type=Path)
a = p.parse_args()
def patch(path, digest, edits):
    data = path.read_text()
    # Migrate the exact reviewed 0.1.12 IRQ adaptation before normal validation.
    if path.name == "dwc2_esp32.h" and hashlib.sha256(data.encode()).hexdigest() == "6978e130495b813c1245d3e134a1ff07a8b58a1b355bd9f1460c5bec54a385fd":
        data = data.replace("    if (usb_ih[rhport]) { esp_intr_free(usb_ih[rhport]); usb_ih[rhport] = NULL; }", "    esp_intr_free(usb_ih[rhport]);")
        data = data.replace("    if (usb_ih[rhport]) { esp_intr_enable(usb_ih[rhport]); return; }", "    if (usb_ih[rhport]) { int err = esp_intr_enable(usb_ih[rhport]); if (err) adv_usb_irq_error(err); return; }")
    restored = data
    for old, new in edits:
        restored = restored.replace(new, old)
    if hashlib.sha256(restored.encode()).hexdigest() != digest:
        raise SystemExit(f"Unrecognized source: {path}")
    for old, new in edits:
        if new in data:
            continue
        if data.count(old) != 1:
            raise SystemExit(f"Ambiguous patch: {path}")
        data = data.replace(old, new)
    path.write_text(data)
    print(path.name, hashlib.sha256(data.encode()).hexdigest())
dwc2 = a.tinyusb / "src/portable/synopsys/dwc2"
patch(dwc2 / "hcd_dwc2.c", HCD_SHA, HCD_EDITS)
patch(dwc2 / "dwc2_common.c", COMMON_SHA, COMMON_EDITS)
patch(dwc2 / "dwc2_common.h", HEADER_SHA, HEADER_EDITS)
patch(a.tinyusb / "src/osal/osal_zephyr.h",
      "2891766f7cc3ff678a74e0fd2f39653f38592f7f29b8905b4fb5c83a23c83729", [
    ('  // K_MSGQ_DEFINE already initializes the queue\n  return (osal_queue_t) qdef;',
     '  // A reinitialized host must not receive events from its previous session.\n'
     '  k_msgq_purge(qdef);\n  return (osal_queue_t) qdef;'),
    ('  return 0 == k_msgq_put(qhdl, data,  in_isr ? K_NO_WAIT : K_FOREVER);',
     '  (void) in_isr;\n'
     '  // The owner task can enqueue its own events: waiting on itself deadlocks.\n'
     '  bool ok = 0 == k_msgq_put(qhdl, data, K_NO_WAIT);\n'
     '  if (!ok) { extern void adv_usb_event_queue_error(void); adv_usb_event_queue_error(); }\n'
     '  return ok;'),
])
patch(a.tinyusb / "src/class/hid/hid_host.c",
      "83856a19edbec34129fc1a7477c371fdced2108e6f9ebf77247974455bb9f2d6", [
    ('    tuh_hid_report_received_cb(daddr, idx, epbuf->epin, (uint16_t) xferred_bytes);',
     '    tuh_hid_report_received_cb(daddr, idx, epbuf->epin,\n'
     '        result == XFER_RESULT_SUCCESS ? (uint16_t) xferred_bytes : 0);'),
])
patch(a.tinyusb / "src/portable/synopsys/dwc2/dwc2_esp32.h",
      "5465d886b354ef9a419931905cc2081bb0c9ef52ced7e4e7434fa420f8d72eec", [
    ('#include "freertos/FreeRTOS.h"\n#include "freertos/task.h"\n\n#include "esp_intr_alloc.h"',
     '#include <zephyr/kernel.h>\n#include <zephyr/drivers/interrupt_controller/intc_esp32.h>'),
    ('static intr_handle_t usb_ih[TU_ARRAY_SIZE(_dwc2_controller)];',
     'static struct intr_handle_data_t *usb_ih[TU_ARRAY_SIZE(_dwc2_controller)];\nextern void adv_usb_irq_error(int error);'),
    ('    esp_intr_alloc(_dwc2_controller[rhport].irqnum, ESP_INTR_FLAG_LOWMED,\n'
     '                   dwc2_int_handler_wrap, (void*)(uintptr_t)tu_u16(role, rhport), &usb_ih[rhport]);',
     '    if (usb_ih[rhport]) { int err = esp_intr_enable(usb_ih[rhport]); if (err) adv_usb_irq_error(err); return; }\n'
     '    int err = esp_intr_alloc(_dwc2_controller[rhport].irqnum, ESP_INTR_FLAG_LEVEL1,\n'
     '                   dwc2_int_handler_wrap, (void*)(uintptr_t)tu_u16(role, rhport), &usb_ih[rhport]);\n'
     '    if (err) adv_usb_irq_error(err);'),
    ('    esp_intr_free(usb_ih[rhport]);',
     '    // Retain one owner IRQ allocation. Zephyr 4.1 esp_intr_free nests an IRQ lock.\n'
     '    if (usb_ih[rhport]) { int err = esp_intr_disable(usb_ih[rhport]); if (err) adv_usb_irq_error(err); }'),
    ('  vTaskDelay(pdMS_TO_TICKS(1));', '  k_msleep(1);'),
])
patch(a.zmk / "app/include/zmk/events/keycode_state_changed.h",
      "fb397093cfc096e41cd9001e399784c417d4671f6b6362b0a4af0b9eaf33a010", [
    ('struct zmk_keycode_state_changed {',
     'struct zmk_keycode_state_changed {\n#if IS_ENABLED(CONFIG_CARDPUTER_USB_HOST)\n'
     '    uint8_t adv_source; /* 0 = ZMK, 1..4 = USB HID interfaces */\n#endif'),
])
patch(a.zmk / "app/src/hid_listener.c",
      "160a47da3d916f8dc4bacc8c5b8dfc0b442aa11158291f5a29d36fcb64bea9cf", [
    ('static int hid_listener_keycode_pressed',
     '#if IS_ENABLED(CONFIG_CARDPUTER_USB_HOST)\n'
     'extern int adv_hid_key_transition(const struct zmk_keycode_state_changed *ev);\n'
     '#define ADV_TRANSITION(ev) adv_hid_key_transition(ev)\n'
     '#define ADV_BUILTIN(ev) ((ev)->adv_source == 0)\n'
     '#else\n#define ADV_TRANSITION(ev) 2\n#define ADV_BUILTIN(ev) true\n#endif\n\n'
     'static int hid_listener_keycode_pressed'),
    ('    int err, explicit_mods_changed, implicit_mods_changed;\n\n    if (!is_mod',
     '    int err, explicit_mods_changed, implicit_mods_changed;\n'
     '    int transition = ADV_TRANSITION(ev);\n\n    if (transition == 2 && !is_mod'),
    ('    err = zmk_hid_press(ZMK_HID_USAGE(ev->usage_page, ev->keycode));',
     '    err = transition ? zmk_hid_press(ZMK_HID_USAGE(ev->usage_page, ev->keycode)) : 0;'),
    ('    implicit_mods_changed = zmk_hid_implicit_modifiers_press(ev->implicit_modifiers);',
     '    implicit_mods_changed = ADV_BUILTIN(ev) ? zmk_hid_implicit_modifiers_press(ev->implicit_modifiers) : 0;'),
    ('    err = zmk_hid_release(ZMK_HID_USAGE(ev->usage_page, ev->keycode));\n    if (err < 0)',
     '    int transition = ADV_TRANSITION(ev);\n'
     '    err = transition ? zmk_hid_release(ZMK_HID_USAGE(ev->usage_page, ev->keycode)) : 0;\n    if (err < 0)'),
    ('    implicit_mods_changed = zmk_hid_implicit_modifiers_release();',
     '    implicit_mods_changed = ADV_BUILTIN(ev) ? zmk_hid_implicit_modifiers_release() : 0;'),
])
