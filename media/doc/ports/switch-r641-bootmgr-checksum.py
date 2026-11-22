#!/usr/bin/env python3
"""Host-side LiberNT Switch R6.4.1 PE CheckSum repair.

Runs on Windows against Hekate USB Mass Storage. Default: read-only.
--install fixes PE CheckSum and activates only the expected BRK diagnostic image.
--rollback restores the hash-pinned original. No raw disks/eMMC are written.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ORIG = "6DC1602D4D96255A261AFA2F29E1946BA1010CD498C679D3C84F0AD6FBA47375"
BAD = "57C62A03F45AD44C697A5376E55B3A67721FBB24B42793A1DAA17D7E20D07D84"
S = Path("L:/Windows/System32")
D = Path("D:/EFI/LiberNT/Experiments")
ACTIVE = S / "winload.efi"
OLD = S / "winload-R6.4-preBRK.efi"
OLD2 = D / "winload-R6.4-preBRK-backup.efi"
BAD_IMAGE = D / "winload-R6.4-BRK-diagnostic.efi"
FIXED_IMAGE = D / "winload-R6.4.1-BRK-checksum.efi"
STAGE = S / "winload-R6.4.1-stage.tmp"
BK_INSTALL = S / "winload-R6.4.1-before-install.efi"
BK_ROLLBACK = S / "winload-R6.4.1-before-rollback.efi"

def sha(b):
    return hashlib.sha256(b).hexdigest().upper()

def read_sha(path, expected):
    b = path.read_bytes()
    if sha(b) != expected:
        raise ValueError("Unexpected SHA256 of " + str(path) + ": " + sha(b))
    return b

def ckpos(b):
    if len(b) < 0x200 or b[:2] != b"MZ":
        raise ValueError("Invalid MZ")
    pe = struct.unpack_from("<I", b, 0x3c)[0]
    if pe < 0x40 or pe + 24 + 68 > len(b) or b[pe:pe+4] != b"PE\x00\x00":
        raise ValueError("Invalid PE")
    if struct.unpack_from("<H", b, pe + 4)[0] != 0xaa64:
        raise ValueError("Not ARM64 PE")
    if struct.unpack_from("<H", b, pe + 24)[0] != 0x20b:
        raise ValueError("Not PE32+")
    if struct.unpack_from("<H", b, pe + 20)[0] < 68:
        raise ValueError("Truncated optional header")
    return pe + 24 + 64

def regenerate(b):
    pos = ckpos(b)
    if b[0x203c:0x2040] != bytes.fromhex("80 ca 29 d4"):
        raise ValueError("Expected BRK instruction absent")
    n = bytearray(b)
    n[pos:pos+4] = b"\x00" * 4
    total = 0
    for i in range(0, len(n)-1, 2):
        total += n[i] | (n[i+1] << 8)
        total = (total & 65535) + (total >> 16)
    if len(n) & 1:
        total += n[-1]
        total = (total & 65535) + (total >> 16)
    total = (total & 65535) + (total >> 16)
    struct.pack_into("<I", n, pos, ((total & 65535) + len(n)) & 0xffffffff)
    fixed = bytes(n)
    if any(x != y for i,(x,y) in enumerate(zip(b, fixed)) if not pos <= i < pos+4):
        raise ValueError("Changes outside checksum field")
    if bootmgr_verify(fixed) != struct.unpack_from("<I", fixed, pos)[0]:
        raise ValueError("BootMgr checksum verification failed")
    return fixed

def bootmgr_verify(b):
    """Exact checksum algorithm used by bootloader/bootmgr/app.c."""
    pos = ckpos(b)
    hdr = struct.unpack_from("<I", b, pos)[0]
    t = 0
    for i in range(0, len(b)-1, 2):
        t += b[i] | b[i+1] << 8
        t = (t & 65535) + (t >> 16)
    if len(b) & 1:
        t = (t & 65535) + (t >> 16)
        t += b[-1]
    t = ((t & 65535) + (t >> 16)) & 65535
    for w in (hdr & 65535, hdr >> 16):
        t = t-w if t >= w else ((t-w) & 65535)-1
        t &= 0xffffffff
    return (t + len(b)) & 0xffffffff

def check_usb():
    if os.name != "nt":
        raise RuntimeError("Windows-only installer")
    ps = ("$ErrorActionPreference='Stop';"
          "$l=Get-Volume -DriveLetter L;$d=Get-Volume -DriveLetter D;"
          "$lp=Get-Partition -DriveLetter L;$dp=Get-Partition -DriveLetter D;"
          "$k=Get-Disk -Number $lp.DiskNumber;"
          "[pscustomobject]@{ll=$l.FileSystemLabel;dl=$d.FileSystemLabel;"
          "lf=$l.FileSystem;df=$d.FileSystem;ln=$lp.DiskNumber;"
          "dn=$dp.DiskNumber;name=$k.FriendlyName;bus=[string]$k.BusType}"
          "|ConvertTo-Json -Compress")
    p = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive",
                        "-Command", ps], text=True, capture_output=True, timeout=25, check=True)
    info = json.loads(p.stdout)
    if info["ll"] != "LIBERNT-LAB" or info["dl"] != "SWITCH SD" or \
       info["lf"] != "NTFS" or info["df"] != "FAT32" or \
       info["ln"] != info["dn"] or info["name"] != "hekate SD RAW" or \
       info["bus"] != "USB":
        raise RuntimeError("Not the expected Hekate USB SD: " + str(info))

def new_file(path,b):
    if path.exists():
        if sha(path.read_bytes()) != sha(b):
            raise FileExistsError("Existing file mismatches: " + str(path))
        return
    tmp=path.with_name(path.name+".writing")
    if tmp.exists():
        raise FileExistsError(str(tmp))
    try:
        with tmp.open("xb") as f:
            f.write(b)
            f.flush()
            os.fsync(f.fileno())
        if sha(tmp.read_bytes()) != sha(b):
            raise IOError("Write verification failed")
        tmp.rename(path)
    finally:
        tmp.unlink(missing_ok=True)

def atomic_replace(b, prior_hash, backup):
    if STAGE.exists() or backup.exists():
        raise FileExistsError("Temp or swap backup exists; no overwrite")
    new_file(STAGE, b)
    try:
        fn = ctypes.WinDLL("kernel32", use_last_error=True).ReplaceFileW
        fn.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p,
                       ctypes.c_wchar_p, ctypes.c_uint32,
                       ctypes.c_void_p, ctypes.c_void_p]
        fn.restype = ctypes.c_int
        if not fn(str(ACTIVE), str(STAGE), str(backup), 0, None, None):
            raise ctypes.WinError(ctypes.get_last_error())
        if sha(backup.read_bytes()) != prior_hash or sha(ACTIVE.read_bytes()) != sha(b):
            raise IOError("Post-install verification FAILED")
    finally:
        STAGE.unlink(missing_ok=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    g=parser.add_mutually_exclusive_group()
    g.add_argument("--install",action="store_true")
    g.add_argument("--rollback",action="store_true")
    args=parser.parse_args()
    check_usb()
    read_sha(OLD,ORIG)
    read_sha(OLD2,ORIG)
    bad=read_sha(BAD_IMAGE,BAD)
    fixed=regenerate(bad)
    fixed_hash=sha(fixed)
    active_hash=sha(ACTIVE.read_bytes())
    if active_hash not in (ORIG, BAD, fixed_hash):
        raise RuntimeError("Unexpected active WinLoad: "+active_hash)
    print("ACTIVE:",active_hash)
    print("R6.4.1:",fixed_hash)
    print("FIXED_PE_CHECKSUM:",hex(struct.unpack_from("<I",fixed,ckpos(fixed))[0]))
    if args.install:
        if active_hash==fixed_hash:
            print("ALREADY_INSTALLED")
            return
        new_file(FIXED_IMAGE,fixed)
        atomic_replace(fixed,active_hash,BK_INSTALL)
        print("R6.4.1_INSTALLED_OK (intentional diagnostic stop)")
    elif args.rollback:
        if active_hash==ORIG:
            print("ALREADY_RESTORED")
            return
        atomic_replace(read_sha(OLD,ORIG),active_hash,BK_ROLLBACK)
        print("R6.4.1_ROLLBACK_OK")
    else:
        print("DRY_RUN_OK: no changes")

if __name__=="__main__":
    try:
        main()
    except Exception as exc:
        print("ERROR:",type(exc).__name__,str(exc),file=sys.stderr)
        sys.exit(1)
