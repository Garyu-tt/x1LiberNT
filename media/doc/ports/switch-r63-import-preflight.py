#!/usr/bin/env python3
"""Read-only ARM64 LiberNT Switch boot import preflight.

WinLoad's framebuffer path registers lfbbvid.dll under the logical
bootvid.dll module name. A bootvid.dll-only export check is insufficient.
"""

import argparse
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

ARM64_MACHINE = 0xAA64
MAX_ENTRIES = 65536


class PEFormatError(ValueError):
    pass


class PE:
    def __init__(self, data: bytes, label: str):
        self.data = data
        self.label = label
        if len(data) < 0x40 or data[:2] != b'MZ':
            raise PEFormatError(f'{label}: missing MZ header')
        pe = self.u32(0x3C)
        if self.take(pe, 4) != b'PE\0\0':
            raise PEFormatError(f'{label}: missing PE signature')
        coff = pe + 4
        machine, section_count, _, _, _, opt_size, _ = self.unpack('<HHIIIHH', coff)
        if machine != ARM64_MACHINE:
            raise PEFormatError(f'{label}: expected ARM64 PE (0xAA64), got 0x{machine:04X}')
        if not 1 <= section_count <= 96:
            raise PEFormatError(f'{label}: invalid section count')
        opt = coff + 20
        self.take(opt, opt_size)
        if opt_size < 112 or self.u16(opt) != 0x20B:
            raise PEFormatError(f'{label}: expected PE32+ optional header')
        self.header_size = self.u32(opt + 60)
        directory_count = self.u32(opt + 108)
        if directory_count < 2:
            raise PEFormatError(f'{label}: missing import/export directories')
        if opt_size < 112 + 8 * 2:
            raise PEFormatError(f'{label}: truncated data directories')
        self.directories = [self.unpack('<II', opt + 112 + 8 * i) for i in range(2)]
        self.sections = []
        for i in range(section_count):
            s = opt + opt_size + i * 40
            self.take(s, 40)
            virtual_size, virtual_address, raw_size, raw_offset = self.unpack('<IIII', s + 8)
            if raw_size and raw_offset + raw_size > len(data):
                raise PEFormatError(f'{label}: section exceeds file')
            self.sections.append((virtual_address, max(virtual_size, raw_size), raw_offset, raw_size))

    def take(self, off, size):
        if off < 0 or size < 0 or off + size > len(self.data):
            raise PEFormatError(f'{self.label}: truncated PE data at 0x{off:X}')
        return self.data[off:off + size]

    def unpack(self, fmt, off):
        return struct.unpack(fmt, self.take(off, struct.calcsize(fmt)))

    def u16(self, off):
        return self.unpack('<H', off)[0]

    def u32(self, off):
        return self.unpack('<I', off)[0]

    def u64(self, off):
        return self.unpack('<Q', off)[0]

    def rva(self, addr, length=1):
        if addr < self.header_size and addr + length <= self.header_size:
            self.take(addr, length)
            return addr
        for va, span, raw, raw_len in self.sections:
            if va <= addr and addr + length <= va + span:
                delta = addr - va
                if delta + length > raw_len:
                    raise PEFormatError(f'{self.label}: RVA 0x{addr:X} is not file-backed')
                self.take(raw + delta, length)
                return raw + delta
        raise PEFormatError(f'{self.label}: unmapped RVA 0x{addr:X}')

    def string(self, addr):
        pos = self.rva(addr)
        end = self.data.find(b'\0', pos, min(len(self.data), pos + 4096))
        if end == -1:
            raise PEFormatError(f'{self.label}: unterminated PE string')
        try:
            return self.data[pos:end].decode('ascii')
        except UnicodeDecodeError as exc:
            raise PEFormatError(f'{self.label}: non-ASCII PE name') from exc

    def exports(self):
        addr, size = self.directories[0]
        if not addr or not size:
            return set()
        e = self.rva(addr, 40)
        base, function_count, name_count, func_rva, name_rva, ord_rva = self.unpack('<IIIIII', e + 16)
        if function_count > MAX_ENTRIES or name_count > function_count:
            raise PEFormatError(f'{self.label}: invalid export counts')
        functions = [self.u32(self.rva(func_rva + 4 * i, 4)) for i in range(function_count)]
        result = {base + i for i, rva in enumerate(functions) if rva}
        for i in range(name_count):
            n = self.u32(self.rva(name_rva + 4 * i, 4))
            ordinal_index = self.u16(self.rva(ord_rva + 2 * i, 2))
            if ordinal_index >= function_count or not functions[ordinal_index]:
                raise PEFormatError(f'{self.label}: invalid export ordinal')
            result.add(self.string(n))
        return result

    def imports(self):
        addr, size = self.directories[1]
        if not addr or not size:
            return {}
        imports = {}
        if size < 20:
            raise PEFormatError(f'{self.label}: truncated import directory')
        ended = False
        for i in range(min(MAX_ENTRIES, size // 20)):
            descriptor = self.rva(addr + i * 20, 20)
            original, _, _, name_rva, first = self.unpack('<IIIII', descriptor)
            if not (original or name_rva or first):
                ended = True
                break
            if not name_rva or not (original or first):
                raise PEFormatError(f'{self.label}: malformed import descriptor')
            dll = self.string(name_rva).lower()
            symbols = imports.setdefault(dll, set())
            thunk = original or first
            found_end = False
            for j in range(MAX_ENTRIES):
                entry = self.u64(self.rva(thunk + j * 8, 8))
                if not entry:
                    found_end = True
                    break
                if entry & (1 << 63):
                    symbols.add(entry & 0xFFFF)
                else:
                    if entry >> 32:
                        raise PEFormatError(f'{self.label}: invalid import hint/name RVA')
                    self.rva(entry, 2)
                    symbols.add(self.string(entry + 2))
            if not found_end:
                raise PEFormatError(f'{self.label}: unterminated thunk array')
        if not ended:
            raise PEFormatError(f'{self.label}: unterminated import descriptor table')
        return imports


@dataclass
class AuditResult:
    errors: list
    checked: list
    unverified: list


def audit(system32: Path, video_mode: str) -> AuditResult:
    chosen_video = 'lfbbvid.dll' if video_mode == 'framebuffer' else 'bootvid.dll'
    physical = {'ntoskrnl.exe': 'ntoskrnl.exe', 'hal.dll': 'hal.dll',
                'bootvid.dll': chosen_video, 'kdcom.dll': 'kdcom.dll'}
    images = {}
    errors, checked, unverified = [], [], []
    for logical, filename in physical.items():
        path = system32 / filename
        try:
            image = PE(path.read_bytes(), filename)
            images[logical] = (image.imports(), image.exports(), filename)
        except (OSError, PEFormatError) as exc:
            errors.append(f'{filename}: {exc}')
    if errors:
        return AuditResult(errors, checked, unverified)
    for logical, (imports, _, filename) in images.items():
        for dependency, symbols in sorted(imports.items()):
            if dependency not in images:
                unverified.append(f'{filename} -> {dependency} (out of scope)')
                continue
            exports = images[dependency][1]
            for symbol in sorted(symbols, key=str):
                if symbol not in exports:
                    errors.append(f'{filename} -> {dependency} [{images[dependency][2]}]: '
                                  f'missing {symbol}')
                else:
                    checked.append(f'{filename} -> {dependency}: {symbol}')
    return AuditResult(errors, checked, unverified)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--system32', type=Path, required=True, help='Mounted LiberNT System32 directory (read only)')
    parser.add_argument('--video', choices=('framebuffer', 'standard'), default='framebuffer')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    result = audit(args.system32, args.video)
    report = {'ok': not result.errors, 'video_file': 'lfbbvid.dll' if args.video == 'framebuffer' else 'bootvid.dll',
              'checked_count': len(result.checked), 'errors': result.errors, 'unverified': result.unverified}
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"Switch boot import check: {'PASS' if report['ok'] else 'FAIL'}; video={report['video_file']}; checked={report['checked_count']}")
        for error in report['errors']:
            print('ERROR: ' + error)
        for note in report['unverified']:
            print('NOT CHECKED: ' + note)
    return 1 if result.errors else 0


if __name__ == '__main__':
    sys.exit(main())
