#!/usr/bin/env python3
"""Portable streaming NCZ1 R01 packer. Experimental RLE + RAW only.

Usage: python3 pack.py source_file output.ncz [--block 65536|262144]
Input is never modified. The output is atomically renamed on successful write.
"""
import argparse
import os
import struct
import tempfile
import zlib

HEADER = struct.Struct('<4sHHIQQQI')   # 40 bytes
ENTRY = struct.Struct('<QIIB3xI')     # 24 bytes


def encode_rle(data):
    """High-bit tag: byte repeated 3..130; low-bit tag: literal 1..128."""
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        j = i + 1
        while j < n and j - i < 130 and data[j] == data[i]:
            j += 1
        run = j - i
        if run >= 3:
            out.extend((0x80 | (run - 3), data[i]))
            i = j
        else:
            start = i
            i += run
            while i < n and i - start < 128:
                j = i + 1
                while j < n and j - i < 3 and data[j] == data[i]:
                    j += 1
                if j - i >= 3:
                    break
                i += 1
            span = data[start:i]
            out.append(len(span) - 1)
            out.extend(span)
    return out


def pack(src, dest, block):
    if os.path.abspath(src) == os.path.abspath(dest):
        raise ValueError('Input and output paths must differ')
    size = os.stat(src).st_size
    nblocks = (size + block - 1) // block
    target_dir = os.path.dirname(os.path.abspath(dest))
    fd, tmp = tempfile.mkstemp(prefix='.ncz-', dir=target_dir)
    try:
        with os.fdopen(fd, 'wb') as out, open(src, 'rb') as inp:
            out.write(b'\x00' * HEADER.size)
            records = []
            while True:
                raw = inp.read(block)
                if not raw:
                    break
                packed = encode_rle(raw)
                codec, payload = (1, packed) if len(packed) < len(raw) else (0, raw)
                records.append((out.tell(), len(payload), len(raw), codec, zlib.crc32(raw)))
                out.write(payload)
            assert len(records) == nblocks
            offset = out.tell()
            for record in records:
                out.write(ENTRY.pack(*record))
            out.seek(0)
            out.write(HEADER.pack(b'NCZ1', 1, 0, block, size, nblocks, offset, ENTRY.size))
            out.flush()
            os.fsync(out.fileno())
        os.replace(tmp, dest)
        print(f'NCZ1 {size} logical bytes -> {os.stat(dest).st_size} physical bytes '
              f'({nblocks} blocks, {block} byte/block)')
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('input')
    p.add_argument('output')
    p.add_argument('--block', type=int, choices=(65536, 262144), default=65536)
    a = p.parse_args()
    pack(a.input, a.output, a.block)
