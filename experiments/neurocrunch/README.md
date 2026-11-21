# NeuroCrunch Switch / LiberNT R01 (experimental)

**Target:** `Garyu-tt/x1LiberNT`, integration branch `switch-lite-mariko-r0`, Switch Lite Mariko / Tegra210B01 ARM64. **No Linux runtime on Switch.** The host tools run on Windows, Linux, or macOS with Python 3; only the portable C reader is intended for LiberNT integration after the Switch kernel boot path is reliable.

This is a **safe, stand-alone reference implementation** of a seekable compressed container. It is NOT a patch to the live NT memory manager, a mounted NT filesystem filter, or a claim of better performance on hardware. In particular, the RLE-only baseline is unlikely to help real compressed video, and additional neural/math codecs are research-only until independently verified.

## NCZ1 R01 format

All integers are unsigned little-endian. Header (40 bytes): `magic NCZ1` (4), `version=1` (u16), `flags=0` (u16), `block_bytes` (u32, 65536 or 262144), `logical_bytes` (u64), `block_count` (u64), `index_offset` (u64), `index_entry_bytes=24` (u32). Index is located at EOF or before trailing bytes; indexes are read *lazily*, not fully loaded. Index entry (24 bytes): `physical_offset` (u64), `stored_bytes` (u32), `logical_block_bytes` (u32), `codec` (u8), `reserved=0` (3 bytes), `crc32(raw)` (u32). Stored blocks precede index; codec `0`=RAW, `1`=simple RLE (0x80 tag = repetition of next byte 3..130, otherwise inline literal bytes 1..128).

Reader checks bounds before reading, requires a user-supplied **2× block** scratch allocation (128–512 KiB), performs no dynamic allocations, and verifies CRC32 before exposing data. It accepts a pread-like callback so it can later be backed by native NT file operations and microSD without materializing the archive in RAM. The caller must ensure the underlying file is not concurrently modified. The read callback must do complete reads. No embedded encryption, authentication or crash-consistent writer exists in this R01.

## Reproducible host sanity test

```sh
sh tests/run_tests.sh
python3 pack.py input.bin output.ncz --block 65536
```

`run_tests.sh` is a POSIX host harness for CI; an equivalent native test runner can be used on Windows. A successful host test **is not** proof of Switch compatibility, NT kernel boot, or any speedup.

## Next steps

1. Compare exact compressed size and range-read throughput against RAW, LZ4 and Zstd on representative Nintendo Switch asset/video datasets and real microSD hardware (the benchmark must include CPU, decode scratch, index bytes and system memory).
2. Add an NT kernel/user-mode adapter in a **separate non-boot-critical test component** after existing WinLoad/ntoskrnl bring-up reaches a stable kernel; never modify boot sectors, save partitions or shipping storage drivers as part of R01.
3. Evaluate bounded decompressed asset cache before considering Memory Manager page compression. NT cold-page compression is a separate, high-risk kernel feature requiring page-fault/IRQL/reclaim design and crash-safe bypass.
4. Only after independent QA consider an opt-in read-only filesystem/filter path, video streaming with random seeks, and fallback to uncompressed normal files.

**Do not assume arbitrary 500 GiB fits in 16 GiB losslessly.** A 31.25× ratio is corpus-specific and must be proved, with the entire decoder and all dictionaries accounted for.
