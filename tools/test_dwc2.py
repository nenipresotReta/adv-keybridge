#!/usr/bin/env python3
"""Compile actual pinned TinyUSB register routines against fake MMIO.

The legacy FIFO routine must demonstrate overlap; the patched one must not.
This verifies the programmed registers, not a duplicate of the layout formula.
It does not simulate USB signalling, DMA hardware, or real IRQ scheduling.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
from usb_dwc2_edits import HCD_SHA, HCD_EDITS, COMMON_SHA, COMMON_EDITS, HEADER_SHA, HEADER_EDITS

p = argparse.ArgumentParser()
p.add_argument("tinyusb", type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parent.parent
dwc2 = a.tinyusb.resolve() / "src/portable/synopsys/dwc2"

def checked(name, digest, edits):
    text = (dwc2 / name).read_text()
    original = text
    for old, new in edits:
        assert text.count(new) == 1, f"Missing patch in {name}"
        original = original.replace(new, old)
    assert hashlib.sha256(original.encode()).hexdigest() == digest
    return text, original

def function(text, name):
    match = re.search(r"[^\n]*\b" + name + r"\([^;]*?\)\s*\{", text)
    assert match, name
    depth = 1
    end = match.end()
    while depth:
        if text[end] == "{": depth += 1
        if text[end] == "}": depth -= 1
        end += 1
    return text[match.start():end]

hcd, legacy = checked("hcd_dwc2.c", HCD_SHA, HCD_EDITS)
common, _ = checked("dwc2_common.c", COMMON_SHA, COMMON_EDITS)
header, _ = checked("dwc2_common.h", HEADER_SHA, HEADER_EDITS)
snippets = [function(hcd, "dma_host_enabled"), function(hcd, "dfifo_host_init"),
            function(legacy, "dfifo_host_init").replace("dfifo_host_init", "legacy_fifo_init"),
            function(common, "reset_core"), function(header, "dfifo_flush_tx"),
            function(header, "dfifo_flush_rx"), function(hcd, "hcd_init"),
            function(hcd, "hcd_deinit"), function(hcd, "handle_channel_irq")]

with tempfile.TemporaryDirectory(prefix="adv-dwc2-test.") as tmp:
    path = Path(tmp)
    (path / "zephyr").mkdir()
    (path / "zephyr/kernel.h").write_text("""
#pragma once
#include <stdint.h>
uint32_t k_cycle_get_32(void);
#define k_us_to_cyc_ceil32(us) ((uint32_t)(us))
""")
    (path / "production.inc").write_text("\n\n".join(snippets))
    binary = path / "test"
    subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I"+str(path), "-I"+str(root / "include"),
                    "-I"+str(dwc2), str(root / "tests/dwc2_test.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
