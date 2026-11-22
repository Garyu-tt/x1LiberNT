#!/usr/bin/env python3
"""Host-only tests for the opt-in R6.4.1 WinLoad PE CheckSum repair."""
import importlib.util
from pathlib import Path
import random
import struct
import sys
import unittest

MODULE = Path(__file__).with_name("switch-r641-bootmgr-checksum.py")
spec = importlib.util.spec_from_file_location("switch_r641", MODULE)
mod = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)

def fake_pe(seed, size=65536):
    rng = random.Random(seed)
    b = bytearray(rng.randbytes(size))
    b[:2] = b"MZ"
    struct.pack_into("<I", b, 0x3c, 0x80)
    b[0x80:0x84] = b"PE\x00\x00"
    struct.pack_into("<H", b, 0x84, 0xaa64)
    struct.pack_into("<H", b, 0x94, 0xf0)
    struct.pack_into("<H", b, 0x98, 0x20b)
    b[0x203c:0x2040] = bytes.fromhex("80 ca 29 d4")
    return bytes(b)

class TestChecksum(unittest.TestCase):
    def test_checksum_matches_actual_bootmgr_algorithm(self):
        for seed in range(10):
            original = fake_pe(seed, 65536 + seed)
            fixed = mod.regenerate(original)
            off = mod.ckpos(fixed)
            self.assertEqual(struct.unpack_from("<I", fixed, off)[0], mod.bootmgr_verify(fixed))

    def test_mutated_boot_app_rejected_until_repaired(self):
        original = bytearray(mod.regenerate(fake_pe(55, 1333760)))
        off = mod.ckpos(original)
        original[0x203c:0x2040] = bytes.fromhex("60 02 1f d6")
        self.assertNotEqual(struct.unpack_from("<I", original, off)[0], mod.bootmgr_verify(original))
        original[0x203c:0x2040] = bytes.fromhex("80 ca 29 d4")
        fixed = mod.regenerate(original)
        self.assertEqual(struct.unpack_from("<I", fixed, off)[0], mod.bootmgr_verify(fixed))

    def test_only_checksum_field_changes(self):
        a = fake_pe(11, 1333760)
        b = mod.regenerate(a)
        off = mod.ckpos(a)
        diff = {i for i,(x,y) in enumerate(zip(a,b)) if x!=y}
        self.assertTrue(diff)
        self.assertTrue(diff.issubset(set(range(off,off+4))))
        self.assertEqual(a[0x203c:0x2040],b[0x203c:0x2040])

    def test_bad_headers_fail_closed(self):
        for data in (b"", b"MZ" + b"\x00"*30, b"foobar"*100):
            with self.assertRaises(ValueError):
                mod.regenerate(data)

    def test_missing_expected_brk_fails_closed(self):
        bad = bytearray(fake_pe(22))
        bad[0x203c:0x2040] = b"ABCD"
        with self.assertRaisesRegex(ValueError, "BRK"):
            mod.regenerate(bad)

if __name__=="__main__":
    unittest.main()
