#!/usr/bin/env python3
"""Host-only assembler regression for the optional Switch R6.4 handoff probe."""

import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "bootloader" / "arch" / "arm64" / "kernel_jump.S"
BRK_WORD = struct.pack("<I", 0xD4200000 | (0x4E54 << 5))


class EntryProbeTests(unittest.TestCase):
    def assemble(self, *defines, expect_success=True):
        clang = shutil.which("clang")
        if not clang:
            self.skipTest("clang with ARM64 target is required for assembly tests")
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "kernel_jump.o"
            command = [
                clang, "--target=aarch64-none-elf", "-x", "assembler-with-cpp",
                "-c", *["-D" + name for name in defines], str(SOURCE),
                "-o", str(obj),
            ]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if expect_success:
                self.assertEqual(result.returncode, 0, result.stderr)
                return obj.read_bytes()
            self.assertNotEqual(result.returncode, 0)
            return result.stderr

    def test_probe_in_source_is_explicitly_guarded(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertEqual(source.count("    brk #0x4e54"), 1)
        self.assertIn("#ifdef LIBERNT_SWITCH_ENTRY_BRK_PROBE", source)
        self.assertIn("#error \"ENTRY_BRK_PROBE requires LIBERNT_SWITCH_HANDOFF_TRACE\"", source)
        self.assertLess(source.index("SWITCH_ASM_MARK 10"), source.index("    brk #0x4e54"))
        self.assertLess(source.index("    brk #0x4e54"), source.index("    br x19"))

    def test_normal_assembly_has_no_deliberate_break(self):
        self.assertNotIn(BRK_WORD, self.assemble())

    def test_trace_only_assembly_has_no_deliberate_break(self):
        self.assertNotIn(BRK_WORD, self.assemble("LIBERNT_SWITCH_HANDOFF_TRACE"))

    def test_probe_assembly_emits_exact_break(self):
        binary = self.assemble(
            "LIBERNT_SWITCH_HANDOFF_TRACE", "LIBERNT_SWITCH_ENTRY_BRK_PROBE"
        )
        self.assertEqual(binary.count(BRK_WORD), 1)

    def test_probe_without_trace_fails_closed(self):
        error = self.assemble("LIBERNT_SWITCH_ENTRY_BRK_PROBE", expect_success=False)
        self.assertIn("requires LIBERNT_SWITCH_HANDOFF_TRACE", error)


if __name__ == "__main__":
    unittest.main()
