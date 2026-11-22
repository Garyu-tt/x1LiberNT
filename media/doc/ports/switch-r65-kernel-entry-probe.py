#!/usr/bin/env python3
"""Switch Lite R6.5 kernel first-instruction BRK diagnostic. Default read only."""
import ctypes, hashlib, importlib.util, os, struct, sys
from pathlib import Path

LOCAL=Path(__file__).with_name('switch-r641-bootmgr-checksum.py')
ROOT=LOCAL if LOCAL.exists() else Path(os.environ['TEMP'])/'LiberNT-R641-checksum'/'switch-r641-bootmgr-checksum.py'
sp=importlib.util.spec_from_file_location('r641',ROOT)
old=importlib.util.module_from_spec(sp);sp.loader.exec_module(old)
L=Path('L:/Windows/System32'); D=Path('D:/EFI/LiberNT/Experiments')
kernel=L/'ntoskrnl.exe'; winload=L/'winload.efi'
oldk=L/'ntoskrnl-R6.5-original.exe'; oldkf=D/'ntoskrnl-R6.5-original.exe'
oldw=L/'winload-R6.4-preBRK.efi'; candidate=D/'ntoskrnl-R6.5-entry-BRK.exe'
kstage=L/'ntoskrnl-R6.5-install-tmp.exe'; wstage=L/'winload-R6.5-restore-tmp.efi'
kb=L/'ntoskrnl-R6.5-swap-original.exe'; wb=L/'winload-R6.5-swap-r641.efi'
krstage=L/'ntoskrnl-R6.5-rollback-tmp.exe'; krb=L/'ntoskrnl-R6.5-prior-diagnostic.exe'
ORIGK='D35605F76C2F8D235273209A5FE924E1A63375F42F1F679F2EC47DE3E04200B9'
ORIGW='6DC1602D4D96255A261AFA2F29E1946BA1010CD498C679D3C84F0AD6FBA47375'
R641W='6E6A745A44E51F10DD9FD3D652AB3B3AC0E4CAFED7335C12D45C21F5B7A8A88E'
def sha(b): return hashlib.sha256(b).hexdigest().upper()
def load(p): return p.read_bytes()
def exact(p,h):
    b=load(p)
    if sha(b)!=h: raise ValueError('HASH_MISMATCH '+str(p))
    return b
def build(original):
    if sha(original)!=ORIGK: raise ValueError('kernel hash mismatch')
    b=bytearray(original)
    pe=struct.unpack_from('<I',b,0x3c)[0]
    if b[:2]!=b'MZ' or b[pe:pe+4]!=b'PE\x00\x00' or struct.unpack_from('<H',b,pe+4)[0]!=0xaa64:
        raise ValueError('not ARM64 PE')
    op=pe+24;cnt=struct.unpack_from('<H',b,pe+6)[0]
    optsize=struct.unpack_from('<H',b,pe+20)[0]
    entry_rva=struct.unpack_from('<I',b,op+16)[0]
    if not (0<cnt<96 and optsize>=112 and struct.unpack_from('<H',b,op)[0]==0x20b):
        raise ValueError('bad PE options')
    entryoff=None
    for i in range(cnt):
        pos=op+optsize+40*i
        vs,va,rawsz,raw=struct.unpack_from('<IIII',b,pos+8)
        flags=struct.unpack_from('<I',b,pos+36)[0]
        if va<=entry_rva<va+max(vs,rawsz):
            if not flags&0x20000000: raise ValueError('not executable')
            if entry_rva-va+4>rawsz: raise ValueError('not file-backed')
            entryoff=raw+entry_rva-va
            break
    if entryoff!=0x12750 or b[entryoff:entryoff+4]!=bytes.fromhex('49 10 38 D5'):
        raise ValueError('wrong entrypoint opcode or file offset')
    b[entryoff:entryoff+4]=bytes.fromhex('A0 CA 29 D4')
    checksum_off=old.ckpos(bytes(b))
    struct.pack_into('<I',b,checksum_off,0)
    t=0
    for i in range(0,len(b)-1,2):
        t+=b[i]|b[i+1]<<8
        t=(t&65535)+(t>>16)
    if len(b)&1: t+=(b[-1]); t=(t&65535)+(t>>16)
    t=(t&65535)+(t>>16)
    struct.pack_into('<I',b,checksum_off,(t&65535)+len(b))
    out=bytes(b)
    if old.bootmgr_verify(out)!=struct.unpack_from('<I',out,checksum_off)[0]:
        raise ValueError('PE checksum mismatch')
    changes={i for i,(x,y) in enumerate(zip(original,out)) if x!=y}
    allowed=set(range(entryoff,entryoff+4))|set(range(checksum_off,checksum_off+4))
    if not changes.issubset(allowed): raise ValueError('unexpected binary modifications')
    return out,entry_rva
def store(path,data):
    if path.exists():
        if sha(load(path))!=sha(data):raise ValueError('existing file differs: '+str(path))
        return
    tmp=path.with_name(path.name+'.writing')
    if tmp.exists():raise ValueError('temporary file exists: '+str(tmp))
    try:
        with tmp.open('xb') as f:
            f.write(data);f.flush();os.fsync(f.fileno())
        if sha(load(tmp))!=sha(data):raise IOError('staged hash mismatch')
        tmp.rename(path)
    finally:tmp.unlink(missing_ok=True)
def replace(target,data,expected,stage,backup):
    if stage.exists() or backup.exists(): raise ValueError('temp or backup exists: '+str(stage))
    if sha(load(target))!=expected:raise ValueError('active hash has changed: '+str(target))
    store(stage,data)
    try:
        fn=ctypes.WinDLL('kernel32',use_last_error=True).ReplaceFileW
        fn.argtypes=[ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_void_p]
        fn.restype=ctypes.c_int
        if not fn(str(target),str(stage),str(backup),0,None,None):raise ctypes.WinError(ctypes.get_last_error())
        if sha(load(target))!=sha(data) or sha(load(backup))!=expected:raise IOError('POST_WRITE_HASH_FAILURE')
    finally:stage.unlink(missing_ok=True)
def main():
    mode=sys.argv[1:] or ['--dry-run']
    if len(mode)!=1 or mode[0] not in ('--dry-run','--install','--rollback'):
        raise ValueError('use --dry-run | --install | --rollback')
    old.check_usb()
    base=exact(oldw,ORIGW)
    kactive=load(kernel);wactive=load(winload)
    korig=exact(oldk,ORIGK) if oldk.exists() else exact(kernel,ORIGK)
    kpatched,ep=build(korig)
    kh=sha(kpatched);ka=sha(kactive);wa=sha(wactive)
    print('KERNEL_AEP_RVA='+hex(ep)+' KERNEL_PROBE_SHA256='+kh)
    print('CURRENT_KERNEL='+ka+' CURRENT_WINLOAD='+wa)
    if ka not in (ORIGK,kh) or wa not in (ORIGW,R641W):
        raise ValueError('unexpected active file hashes')
    if mode[0]=='--dry-run':
        print('R6.5_DRY_RUN_OK')
        return
    store(oldk,korig);store(oldkf,korig)
    if mode[0]=='--install':
        store(candidate,kpatched)
        if ka==ORIGK:
            replace(kernel,kpatched,ORIGK,kstage,kb)
            print('KERNEL_PROBE_ACTIVE')
        if wa==R641W:
            replace(winload,base,R641W,wstage,wb)
            print('NORMAL_WINLOAD_RESTORED')
        if sha(load(kernel))!=kh or sha(load(winload))!=ORIGW:
            raise ValueError('final install hash mismatch')
        print('R6.5_INSTALLED_OK')
    else:
        if ka==kh:
            replace(kernel,korig,kh,krstage,krb)
            print('KERNEL_RESTORED')
        if wa==R641W:
            replace(winload,base,R641W,wstage,wb)
            print('NORMAL_WINLOAD_RESTORED')
        if sha(load(kernel))!=ORIGK or sha(load(winload))!=ORIGW:
            raise ValueError('final rollback hash mismatch')
        print('R6.5_ROLLBACK_OK')
if __name__=='__main__':
    try:main()
    except Exception as e:
        print('ERROR',type(e).__name__,str(e),file=sys.stderr)
        sys.exit(1)
