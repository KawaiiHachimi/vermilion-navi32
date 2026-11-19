#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# linux_display_capture_test.py: host test of tools/capture/linux_display_capture.py's EDID serial stripping and
# register deny list (no hardware, any OS):  python3 tests/mock/linux_display_capture_test.py
import importlib.util, os, sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("cap", os.path.join(here, "../../tools/capture/linux_display_capture.py"))
cap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cap)
fails = checks = 0


def check(c, what):
    global fails, checks
    checks += 1
    if not c:
        fails += 1
        print("FAIL", what)


def fix(e):
    for b in range(0, len(e), 128):
        if e[b] == 0x70:
            end = 5 + e[b + 2]
            e[b + end] = (-sum(e[b + 1:b + end])) & 0xFF
        e[b + 127] = (-sum(e[b:b + 127])) & 0xFF


SER = b"\x12\x34\x56\x78"
e = bytearray(384)
e[0:8] = bytes([0, 255, 255, 255, 255, 255, 255, 0]); e[12:16] = SER; e[126] = 2
e[90:95] = bytes([0, 0, 0, 0xFF, 0]); e[95:108] = b"SERIAL123456\n"          # 0xFF descriptor
e[54:72] = bytes(range(1, 19))                                                # a DTD, must survive
e[128] = 0x02; e[129] = 3; e[130] = 4
d = 256; e[d] = 0x70; e[d + 1] = 0x20; e[d + 2] = 3 + 12 + 3 + 4; e[d + 3] = 0; e[d + 4] = 0
e[d + 5] = 0x20; e[d + 6] = 0; e[d + 7] = 12                                 # DisplayID 2 product id block
e[d + 8:d + 11] = b"\x00\xe0\x91"; e[d + 11:d + 13] = b"\x01\x02"; e[d + 13:d + 17] = SER; e[d + 17] = 7; e[d + 18] = 33
e[d + 20] = 0x22; e[d + 21] = 0; e[d + 22] = 4; e[d + 23:d + 27] = b"\xaa\xbb\xcc\xdd"   # a timing block, must survive
fix(e)
s = cap.strip_edid(bytes(e))
check(len(s) == 384, "length kept")
check(SER not in s, "no 4-byte serial left anywhere")
check(b"SERIAL" not in s, "0xFF descriptor blanked")
check(s[54:72] == bytes(range(1, 19)), "DTD unchanged")
check(s[d + 23:d + 27] == b"\xaa\xbb\xcc\xdd" and s[d + 8:d + 13] == b"\x00\xe0\x91\x01\x02", "other DisplayID data unchanged")
for b in range(0, 384, 128):
    check(sum(s[b:b + 128]) % 256 == 0, f"block {b // 128} checksum")
check(sum(s[d + 1:d + 6 + s[d + 2]]) % 256 == 0, "DisplayID section checksum")
check(cap.strip_edid(b"\x00" * 10) == b"\x00" * 10, "short input passed through")
check(cap.denied(0x1A751) and cap.denied(0x1A762) and cap.denied(0x34C0 + 0x1F56) and cap.denied(0x34C0 + 0x1EB2), "deny list")
check(not cap.denied(0x056D2) and not any(cap.denied(r) for r in cap.FIXED), "fixed registers readable")
print(f"linux_display_capture_test: {checks - fails}/{checks} checks passed")
sys.exit(1 if fails else 0)
