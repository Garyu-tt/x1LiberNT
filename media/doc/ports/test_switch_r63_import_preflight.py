"""Read-only Switch boot core import auditor regression tests."""

import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path
from contextlib import redirect_stdout
from io import StringIO

MODULE = Path(__file__).with_name('switch-r63-import-preflight.py')
spec = importlib.util.spec_from_file_location('switch_r63_preflight', MODULE)
preflight = importlib.util.module_from_spec(spec)
import sys
sys.modules[spec.name] = preflight
spec.loader.exec_module(preflight)


def fake_arm64_pe(exports=(), imports=None, machine=0xAA64):
    data = bytearray(0x3200)
    data[0:2] = b'MZ'
    struct.pack_into('<I', data, 0x3C, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 0x84, machine, 1, 0, 0, 0, 0xF0, 0)
    opt = 0x98
    struct.pack_into('<H', data, opt, 0x20B)
    struct.pack_into('<I', data, opt + 60, 0x200)
    struct.pack_into('<I', data, opt + 108, 16)
    section = opt + 0xF0
    data[section:section + 8] = b'.rdata\0\0'
    struct.pack_into('<IIII', data, section + 8, 0x3000, 0x1000, 0x3000, 0x200)
    cursor = 0x200

    def alloc(length, align=8):
        nonlocal cursor
        cursor = (cursor + align - 1) & ~(align - 1)
        pos = cursor
        cursor += length
        assert cursor < len(data)
        return pos

    def rva(pos):
        return pos - 0x200 + 0x1000

    def string(value):
        blob = value.encode('ascii') + b'\0'
        pos = alloc(len(blob), 1)
        data[pos:pos + len(blob)] = blob
        return rva(pos)

    if exports:
        edir = alloc(40)
        funcs = alloc(4 * len(exports))
        names = alloc(4 * sum(isinstance(x, str) for x in exports))
        ords = alloc(2 * sum(isinstance(x, str) for x in exports))
        for i, name in enumerate(exports):
            struct.pack_into('<I', data, funcs + i * 4, 0x2000 + i * 4)
        index = 0
        for i, name in enumerate(exports):
            if isinstance(name, str):
                struct.pack_into('<I', data, names + index * 4, string(name))
                struct.pack_into('<H', data, ords + index * 2, i)
                index += 1
        struct.pack_into('<IIHHIIIIIII', data, edir, 0, 0, 0, 0, 0, 1,
                         len(exports), index, rva(funcs), rva(names), rva(ords))
        struct.pack_into('<II', data, opt + 112, rva(edir), 40)

    if imports:
        idir = alloc(20 * (len(imports) + 1))
        for i, (dll, symbols) in enumerate(imports.items()):
            thunk = alloc(8 * (len(symbols) + 1))
            for j, symbol in enumerate(symbols):
                if isinstance(symbol, int):
                    value = (1 << 63) | symbol
                else:
                    hintname = alloc(2 + len(symbol) + 1)
                    data[hintname + 2: hintname + 2 + len(symbol) + 1] = symbol.encode() + b'\0'
                    value = rva(hintname)
                struct.pack_into('<Q', data, thunk + 8 * j, value)
            struct.pack_into('<IIIII', data, idir + 20 * i, rva(thunk), 0, 0, string(dll), rva(thunk))
        struct.pack_into('<II', data, opt + 120, rva(idir), 20 * (len(imports) + 1))
    return bytes(data)


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name)
        self.write('ntoskrnl.exe', fake_arm64_pe(['KernelExport'], {'hal.dll': ['HalExport'], 'bootvid.dll': ['VidRenderStatusText']}))
        self.write('hal.dll', fake_arm64_pe(['HalExport'], {'ntoskrnl.exe': ['KernelExport']}))
        self.write('kdcom.dll', fake_arm64_pe(['KdExport']))
        self.write('bootvid.dll', fake_arm64_pe(['VidRenderStatusText']))
        self.write('lfbbvid.dll', fake_arm64_pe(['OldVid']))

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, data):
        (self.path / name).write_bytes(data)

    def test_framebuffer_alias_detects_r62_failure(self):
        report = preflight.audit(self.path, 'framebuffer')
        self.assertFalse(not report.errors)
        self.assertTrue(any('lfbbvid.dll' in e and 'VidRenderStatusText' in e for e in report.errors), report.errors)

    def test_standard_video_path_uses_bootvid(self):
        self.assertEqual([], preflight.audit(self.path, 'standard').errors)

    def test_matched_lfbbvid_passes(self):
        self.write('lfbbvid.dll', fake_arm64_pe(['VidRenderStatusText']))
        report = preflight.audit(self.path, 'framebuffer')
        self.assertEqual([], report.errors)
        self.assertEqual(3, len(report.checked))

    def test_missing_hal_export_detected(self):
        self.write('hal.dll', fake_arm64_pe(['OtherName']))
        report = preflight.audit(self.path, 'standard')
        self.assertTrue(any('HalExport' in e for e in report.errors))

    def test_missing_file_rejected(self):
        (self.path / 'kdcom.dll').unlink()
        self.assertTrue(preflight.audit(self.path, 'standard').errors)

    def test_bad_machine_rejected(self):
        self.write('hal.dll', fake_arm64_pe(['HalExport'], machine=0x8664))
        self.assertTrue(any('ARM64' in e for e in preflight.audit(self.path, 'standard').errors))

    def test_truncated_pe_rejected(self):
        self.write('hal.dll', b'MZ')
        self.assertTrue(preflight.audit(self.path, 'standard').errors)

    def test_ordinal_resolution(self):
        self.write('hal.dll', fake_arm64_pe([42]))
        self.write('ntoskrnl.exe', fake_arm64_pe(['KernelExport'], {'hal.dll': [1]}))
        self.assertEqual([], preflight.audit(self.path, 'standard').errors)

    def test_unknown_dependencies_not_misrepresented_as_verified(self):
        self.write('ntoskrnl.exe', fake_arm64_pe(['KernelExport'], {'hal.dll': ['HalExport'], 'external.dll': ['Something']}))
        report = preflight.audit(self.path, 'standard')
        self.assertEqual([], report.errors)
        self.assertTrue(any('external.dll' in e for e in report.unverified))

    def test_corrupt_import_thunk_rejected(self):
        blob = bytearray(fake_arm64_pe(['KernelExport'], {'hal.dll': ['HalExport']}))
        image = preflight.PE(blob, 'kernel')
        desc = image.rva(image.directories[1][0], 20)
        thunk_rva = struct.unpack_from('<I', blob, desc)[0]
        thunk = image.rva(thunk_rva, 8)
        struct.pack_into('<Q', blob, thunk, 0x7FFFFFFF)
        self.write('ntoskrnl.exe', blob)
        self.assertTrue(preflight.audit(self.path, 'standard').errors)

    def test_corrupt_export_count_rejected(self):
        blob = bytearray(fake_arm64_pe(['HalExport']))
        image = preflight.PE(blob, 'hal')
        export = image.rva(image.directories[0][0], 40)
        struct.pack_into('<I', blob, export + 20, preflight.MAX_ENTRIES + 1)
        self.write('hal.dll', blob)
        self.assertTrue(preflight.audit(self.path, 'standard').errors)

    def test_no_import_directory_allowed(self):
        self.write('ntoskrnl.exe', fake_arm64_pe(['KernelExport']))
        self.assertEqual([], preflight.audit(self.path, 'standard').errors)

    def test_json_exit_code(self):
        out = StringIO()
        with redirect_stdout(out):
            status = preflight.main(['--system32', str(self.path), '--json'])
        result = json.loads(out.getvalue())
        self.assertEqual(1, status)
        self.assertEqual('lfbbvid.dll', result['video_file'])
        self.assertFalse(result['ok'])


if __name__ == '__main__':
    unittest.main()
