"""Regression tests: Switch R6.5 kernel-entry diagnostic PE patch."""
import importlib.util
from pathlib import Path
import random
import struct
import sys
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).with_name('switch-r65-kernel-entry-probe.py')
spec = importlib.util.spec_from_file_location('r65_entry', SOURCE)
r65 = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = r65
spec.loader.exec_module(r65)

def fake_kernel(seed=1):
    rng = random.Random(seed)
    b = bytearray(rng.randbytes(0x22000))
    b[:2] = b'MZ'
    struct.pack_into('<I', b, 0x3c, 0x80)
    b[0x80:0x84] = b'PE\x00\x00'
    struct.pack_into('<HHIIIHH', b, 0x84, 0xaa64, 1, 0, 0, 0, 0xf0, 0)
    struct.pack_into('<H', b, 0x98, 0x20b)
    struct.pack_into('<I', b, 0x98+16, 0x13550)
    struct.pack_into('<IIII', b, 0x188+8, 0x20000, 0x1000, 0x20000, 0x200)
    struct.pack_into('<I', b, 0x188+36, 0x60000020)
    b[0x12750:0x12754] = bytes.fromhex('49 10 38 D5')
    return bytes(b)

class KernelEntryProbeTests(unittest.TestCase):
    def test_probe_opcode_is_brk_4e55(self):
        self.assertEqual(bytes.fromhex('A0 CA 29 D4'), struct.pack('<I',0xD4200000|(0x4e55<<5)))

    def test_patch_changes_only_entry_and_checksum(self):
        before=fake_kernel()
        with patch.object(r65,'ORIGK',r65.sha(before)):
            after,rva=r65.build(before)
        self.assertEqual(rva,0x13550)
        self.assertEqual(after[0x12750:0x12754],bytes.fromhex('A0 CA 29 D4'))
        ch=r65.old.ckpos(after)
        differences={i for i,(a,b) in enumerate(zip(before,after)) if a!=b}
        self.assertTrue(differences)
        self.assertTrue(differences.issubset(set(range(0x12750,0x12754))|set(range(ch,ch+4))))
        self.assertEqual(r65.old.bootmgr_verify(after),struct.unpack_from('<I',after,ch)[0])

    def test_mutated_entry_rejected(self):
        before=bytearray(fake_kernel())
        before[0x12750]=0
        with patch.object(r65,'ORIGK',r65.sha(before)):
            with self.assertRaisesRegex(ValueError,'wrong entrypoint'):
                r65.build(bytes(before))

    def test_wrong_sha_rejected(self):
        with self.assertRaisesRegex(ValueError,'hash mismatch'):
            r65.build(fake_kernel())

    def test_non_executable_section_rejected(self):
        before=bytearray(fake_kernel())
        struct.pack_into('<I',before,0x188+36,0x40000040)
        with patch.object(r65,'ORIGK',r65.sha(before)):
            with self.assertRaisesRegex(ValueError,'not executable'):
                r65.build(bytes(before))

    def test_invalid_pe_rejected(self):
        before=bytearray(fake_kernel())
        before[0x80:0x84]=b'BAD!'
        with patch.object(r65,'ORIGK',r65.sha(before)):
            with self.assertRaisesRegex(ValueError,'ARM64 PE'):
                r65.build(bytes(before))

if __name__=='__main__':
    unittest.main()
