## R6.3: read-only WinLoad import preflight (host-side patch)

This patch adds `switch-r63-import-preflight.py`, a standalone Python 3
utility for checking **ARM64 PE import/export compatibility on the actual
mounted System32 files**, before another physical boot. It does not replace
boot binaries, boot managers, EFI entries, the SD partition table, or eMMC.

It specifically models the current `bootloader/winload/main.c` framebuffer
path, where `lfbbvid.dll` is loaded under the logical module name
`bootvid.dll`. Checking only the on-disk `bootvid.dll` missed the R6.1/R6.2
failure (`VidRenderStatusText` absent from the actual loaded image).

From PowerShell on the development laptop, when the **correct experimental
Hekate NTFS volume** is mounted and confirmed as `LIBERNT-LAB`:

```powershell
py -3 .\switch-r63-import-preflight.py --system32 'L:\Windows\System32' --video framebuffer
```

Run the `--video standard` variant only for a WinLoad boot with no framebuffer
handoff. `--json` emits a machine-readable report; exit code `0` means all
**in-scope direct core dependencies** resolve, `1` means a missing dependency,
invalid PE, missing file, or incompatible machine type. Out-of-scope DLLs are
reported as `NOT CHECKED`, never silently counted as verified.

The four inspected physical files are `ntoskrnl.exe`, `hal.dll`, `kdcom.dll`
and **either** `lfbbvid.dll` (framebuffer) **or** `bootvid.dll` (standard).
There are no extra Python dependencies. This is strictly read-only. The
program parses PE32+ ARM64 exports, direct imports, name and ordinal imports,
checks RVA bounds and rejects corrupt descriptors.

Host regression tests:

```sh
python3 -m unittest -v test_switch_r63_import_preflight.py
```

Test corpus includes the real *class* of R6.2 bug with synthetic ARM64 PE
images. The real Switch files were not included in the repository and have
**not** been tested by this patch alone. A pass is **not** a kernel execution
proof, a guarantee that WinLoad does not load more modules, a forwarder/
API-set audit, or a proof that dynamic runtime behavior is correct.

The physical R6.2 follow-up remains necessary: safely eject both experimental
SD volumes, launch the existing manual LiberNT UEFI probe profile, and read
the **new** first error or kernel-entry markers. Keep the R6.2 backup and
rollback script available. In particular, do not attempt `ntkrnlmp.exe` with
new R6.1/R6.2 BootVid/LFBBVID because of the older `VidFadeToBlack` import.
