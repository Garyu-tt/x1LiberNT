# Nintendo Switch Lite (Mariko) — experimental LiberNT ARM64 port

> **Status:** research-only, pre-kernel. **No successful LiberNT kernel boot on the console has been demonstrated.**  
> Hardware: Nintendo Switch Lite, Hoag/Vali, NVIDIA Tegra210B01 (Mariko).  
> Development branch: `switch-lite-mariko-r0`. This note is a lab record, not a support claim.

## Scope and provenance

Physical observations below were obtained from an actual Switch Lite running Hekate, a Mariko-compatible UEFI boot chain, and experimental ARM64 EFI diagnostics. The screen photographs were examined during the lab session. QEMU observations are **separate** and cannot substitute for hardware testing.

The initial LiberNT binary is from upstream **Debug ARM64 release `nt10.0-043-2555-g8e478a22a1e`**; it is *not* yet a build from this branch. The fork's `master` commit at branch creation was `f12044497f26fab60b94e2acb5573c632204bc27`. Do not attribute bugs in that release to the latest fork source without reproducing them.

## Boot chain

```text
Hekate (manual config selection; autoboot disabled)
  -> Mariko BL31 / EL2 shim / NintendoSwitchPkg EDK2 UEFI
  -> \EFI\BOOT\BOOTAA64.EFI (replaceable experiment launcher)
  -> \EFI\LiberNT\BOOTAA64.EFI (original upstream ARM64 FreeLoader)
  -> freeldr.ini -> partition(2)\Windows -> ARM64 kernel [NOT REACHED]
```

The confirmed FAT32 boot files and system files are on **microSD**, not the console's internal eMMC. No experimental boot step requires writing to internal eMMC.

## Physical evidence

| Revision | What was observed on actual Switch Lite | Interpretation |
| --- | --- | --- |
| R0 / EFI application | Custom EFI diagnostic displays `EDK2 -> LoadImage -> StartImage : WORKING` | EDK2 can execute our ARM64 EFI application and write to the display. |
| R1 / chain diagnostic | LoadedImage, DevicePath, AllocatePool and `LoadImage` return `EFI_SUCCESS`; last visible line is `[07] Starting FreeLoader NOW` | FreeLoader image is found and loaded; later execution is not visible. |
| R2 / GOP diagnostic | `LocateProtocol(GraphicsOutputProtocol)` returns success, firmware GOP reports **720×1280**, framebuffer **0xF5A00000**; screen remains on `[07]` | GOP is already offered by firmware; **no synthetic GOP is required** on this device. |
| R3 / UEFI event observer | Top-right indicator alternates **green/magenta** after `[07]`; no yellow ExitBootServices indicator was observed | At least one registered UEFI periodic event continues to run. No observed ExitBootServices signal; this alone does not prove the exact FreeLoader stall location. |
| R4 / Block I/O preflight | **Confirmed on the physical Switch Lite:** EFI Block I/O reads LBA 0 successfully (`EFI_SUCCESS`), reports boot signature `0xAA55`, and prints `>> NTFS BOOT SECTOR CONFIRMED <<`; the observer reaches `[07] Starting FreeLoader NOW`. The periodic green/magenta marker is visible; no yellow ExitBootServices marker is visible. | UEFI can read the experimental NTFS partition boot sector. FreeLoader's own NTFS file reads, any kernel handoff, and access to eMMC are *not* established. |

**Interpretation limits:** The last line printed before calling `StartImage` is not proof that FreeLoader reaches a particular C function. No debugger backtrace or serial log from the physical console has yet been obtained. The R3 timer can continue to execute even if other parts of startup stall; the absence of an ExitBootServices indicator is not conclusive.

## Emulation evidence (not physical)

In QEMU `virt` with EDK2 and the upstream ARM64 image, FreeLoader loads, reads `freeldr.ini` and finds an OS entry for `partition(2)\Windows`. Separate R4 preflight emulation found virtual EFI Block I/O handles and read valid boot signatures. This verifies only the generic virtual setup. The headless QEMU instance also reports no GOP; this **does not** mean the Switch firmware lacks GOP.

## microSD layout observed during the lab

- FAT32 / Hekate boot volume: approximately **8.59 GB**.
- Experimental NTFS `LIBERNT-LAB` volume: approximately **80.24 GB**.
- Existing separate Linux partition: approximately **37.58 GB**, preserve it.

The second partition holds the extracted LiberNT `Windows` tree, including ARM64 `ntkrnlmp.exe`, `hal.dll`, and registry hives. This is a manually staged test layout, **not** proof that a full OS installation can boot.

## Open engineering questions

1. **Follow up on R4 hardware evidence:** persist raw EFI Block I/O handle, device-path and partition mapping data; compare with physical microSD layout. Boot-sector read success does not prove FreeLoader file-system access.
2. **Instrument FreeLoader itself:** add minimal early-stage logging around `EfiEntry`, `MachInit`, `UiInitialize`, `MmInitializeMemoryManager`, `MachInitializeBootDevices`, `_changestack` and `RunLoader`. Prefer UART/serial or a robust debug channel; do not rely only on the framebuffer.
3. **Validate the UEFI memory map and ACPI tables** presented by NintendoSwitchPkg on Mariko; compare against exactly what LiberNT ARM64 HAL consumes. Report unsupported or unknown layouts as `INFO_NEEDED`.
4. **Check real SD device-path and partition mapping** before changing `freeldr.ini`, storage drivers or partition tables.
5. **Build from this fork and test**, pin the exact source commit / toolchain / hashes, compare with the upstream release, and keep tests reproducible.
6. Only after proven loader-to-kernel handoff, begin board-specific peripherals (display handoff, input, storage, Wi-Fi, audio, GPU). Drivers remain unproven.

## Safety and contribution boundaries

- Only use Hekate's microSD USB Mass Storage for host access after the console has exited EFI diagnostics; safely eject/unmount host volumes before booting.
- Do not alter the original Hekate config, Linux partition, or internal Switch eMMC. Preserve rescue configurations and known-good EFI loaders.
- Keep Mariko hardware knowledge out of common kernel code. Do not transplant incompatible Windows driver code or Linux GPL code; follow the upstream `AGENTS.md` clean-room and licensing policy.
- No automatic upstream PR or claim of driver/OS functionality until the relevant physical tests exist.

## R5: current boot manager, build, and physical SD staging (not yet boot-tested)

**Important architecture change:** this fork's current ARM64 configuration uses
`bootmgfw.efi -> BCD -> winload.efi` by default. The R0–R4 physical experiments
ran an **older upstream ARM64 FreeLoader** from a prebuilt release; the initial
opt-in `uefildr.c` trace on this branch is retained for research, but the
`uefildr` target is disabled in the current default ARM64 build. The active
R5 path uses the newly instrumented `bootloader/bootmgr/main.c` with
`[SW-BM00]…[SW-BM11]` and `LIBERNT_SWITCH_EFI_TRACE=ON` passed to
the ARM64 `bootmgfw` target only.

**Actual build:**

- Host: laptop WSL Ubuntu, Clang 24 from official LiberNT LLVM releases
  (both LLVM archives verified against their release SHA-256); source ref
  `26e319d75ec5bc7b4ce59ca40444a74a738c4900`; `CCACHE_DISABLE=1`.
- `./configure.sh --clang -a arm64 --no-feeds-update -DREACTOS_TARGET_NT=0xA00 -DLIBERNT_SWITCH_EFI_TRACE=ON -DENABLE_CCACHE=0`
- `ninja -C output-Clang-arm64-debug bootmgfw`
- `bootloader/bootmgfw.efi`: SHA-256
  `231bd9b38646c7c5786bf681f1d04994d068e6279590f39427c0d7065abfd405`
- `bootloader/winload.efi`: SHA-256
  `9da6bc13cbe65a068bf34706981a8ad2dba79f82ed712be7e533b45040b352f7`

**Emulator-only proof:** ARM64 QEMU + EDK2 executed the new Boot Manager.
The original generated BCD was read successfully; the test reached
`[SW-BM06] Boot entries enumerated` and exposed the expected entry
`LiberNT`. The virtual setup did not have the expected matching
system partition, so launching that entry reported an unavailable device.
No kernel-start claim follows from this test.

**Actual microSD identification, read-only:** MBR disk signature
`0x17ECE80C`; FAT32 p1, NTFS p2 starts at byte offset
`8607760384` (LBA `16812032`), existing Linux p3 preserved.
The default BCD targets a **different** MBR signature/offset, so a
Switch-specific BCD was generated with LiberNT's `utf16le` and
`mkhive` tools. It modifies exactly two OS/loader device references.
SHA-256: `6cb7c363a2d659c63c930311b0fb90fea442b9100365d91defbf16bab6b74340`.

**Staged on the physical microSD; physical R5 boot not yet observed:**

- **Active physical boot path uses the modern instrumented Boot Manager directly:**
  `D:\EFI\BOOT\BOOTAA64.EFI` now contains R5 `bootmgfw.efi`
  (SHA-256 `231bd9b38646c7c5786bf681f1d04994d068e6279590f39427c0d7065abfd405`).
- The previous R4 outer wrapper is saved as
  `D:\EFI\LiberNT\BOOTAA64-R4-wrapper-restore.EFI` (SHA-256
  `FB0914C5885CA193ED62ABC419806DD3CB6315590AB9E65EE6AF928FCAE0B6FE`).
  A separate host backup was also verified.
- `D:\EFI\LiberNT\BOOTAA64.EFI` also contains a verified copy of
  `bootmgfw.efi`, but it is **not** used by the direct-R5 boot path.
- Switch-specific BCD is at `D:\EFI\LiberNT\Boot\BCD`.
- Current `winload.efi` was staged to
  `L:\Windows\System32\winload.efi` and a copy to
  `D:\EFI\LiberNT\Boot\winload.efi`.
- Prior old FreeLoader preserved at
  `D:\EFI\LiberNT\BOOTAA64-OldFreeLoader-R4-backup.EFI`
  and in Windows host backups; their hashes were verified.
- A validated rollback script was saved on the laptop:
  `C:\Users\Public\Documents\LiberNT-Switch-R0\restore-R4-FreeLoader.ps1`.
- The Windows system tree on `L:` comes from the **earlier upstream
  October Debug release**, so it may be incompatible with the new
  November `winload.efi`. Kernel boot is **not verified**.
- Hekate autoboot is disabled, no partition rewriting, no internal
  console eMMC writes.

**Chain A/B check:** QEMU was also tested using R4 diagnostic as the
outer EFI image loading the modern Boot Manager as its child. It reached
`[SW-BM10] Entering boot menu`, but then emitted a
`Synchronous Exception`. A previous **direct** bootmgr QEMU run did not
show that exception. To remove the R4 wrapper as a confounder, the actual
SD's default EFI path is now set to **direct bootmgfw**. This is a
hypothesis-driven isolation step, not proof that the wrapper caused the
exception.

**Next hardware observation:** safely eject both USB Mass Storage volumes,
boot `More Configs -> LiberNT R0 - Mariko UEFI PROBE`, capture the
`[SW-BMxx]` diagnostics. The R4 preflight should **not** appear in the
new direct-R5 test. The trace tells us which boot manager stage completes.
If needed restore **both** the R4 wrapper and old FreeLoader by the
validated host script after re-entering Hekate USB Mass Storage.

## R5 legacy FreeLoader build (separate experiment, not active)

The modern `bootmgfw` test remains active on the physical microSD (see above).
A separate opt-in CMake path now enables the *legacy* `uefildr` on ARM64
without changing standard boot image selection:

```sh
# Requires the LiberNT LLVM 24 build toolchain already installed in WSL.
cmake -S . -B output-Clang-arm64-debug -DLIBERNT_SWITCH_BUILD_LEGACY_UEFILDR=ON
CCACHE_DISABLE=1 ninja -C output-Clang-arm64-debug -j4 uefildr
```

A successful *physical* Switch boot is **not** implied by this build.

- Source branch includes `acfd1dc3` (opt-in legacy target) and
  `a6e9ac45` (mirror trace output to debugger after `DebugInit`).
- `boot/freeldr/freeldr/uefildr.efi` was built in WSL with LLVM 24:
  PE32+ ARM64 EFI, **2,087,424 bytes**, SHA-256
  `d1e15930d276198a55d820cad236c448394656e7887b927ac479baa76d068070`.
- Separate QEMU `virt` / EDK2 test reached trace checkpoints
  `[SW-00]` through `[SW-22] RunLoader begin`. QEMU logs also showed
  no GOP in the *headless virtual machine*, not the physical Switch.
  This QEMU FAT test does **not** contain the physical Switch system volume.
- To avoid touching the active modern `bootmgfw` route, the R5 legacy binary
  was copied *only* to
  `D:\EFI\LiberNT\Experiments\FreeLoader-R5-UART-trace.EFI`.
  Destination SHA-256 was verified; active
  `D:\EFI\BOOT\BOOTAA64.EFI` still had modern `bootmgfw`
  hash `231bd9b38646c7c5786bf681f1d04994d068e6279590f39427c0d7065abfd405`.
- Experimental R5 legacy is **not selected by Hekate** until a controlled
  change to the default EFI boot path is made. Do not mistake file staging
  for an observed console test.
- Both modern bootmgfw and legacy FreeLoader need separate, accurately
  version-pinned physical checkpoints. Legacy FreeLoader is newer than the
  October LiberNT OS system tree on NTFS and may not load it compatibly.

## R5.1 physical Boot Manager observation and direct-boot diagnostic

On the physical **Switch Lite** with direct `bootmgfw.efi` R5 (original
SHA-256 `231bd9b38646c7c5786bf681f1d04994d068e6279590f39427c0d7065abfd405`),
the photographed screen shows:

```text
[SW-BM00] BlMain entered
[SW-BM01] Firmware announcement complete
[SW-BM02] BmReadStore begin
[SW-BM03] BmReadStore returned
[SW-BM05] BCD store opened
[SW-BM06] Boot entries enumerated
[SW-BM10] Entering boot menu
```

A LiberNT logo was visible, but the expected interactive boot menu was not
visible. No `[SW-BM11]` or successful `winload` handoff is proven.
The green blocks at the screen edge were also seen in earlier firmware
screens; their origin is unconfirmed. `BmRunMenu` calls `GuiInitialize`
and `FbGuiVtbl.DisplayMenu`. That menu code may retain the firmware
screen while a positive timeout remains, and waits on timer, keyboard,
touchscreen or volume-button input; **the precise physical cause is not yet
known**.

To distinguish a GUI/input/timer problem from BCD device and `winload`
problems, a temporary one-shot diagnostic was added:
- `LIBERNT_SWITCH_DIAG_DIRECT_BOOT=ON`, requiring the existing
  `LIBERNT_SWITCH_EFI_TRACE=ON`; source changes in
  `bootloader/bootmgr/main.c`, `bootloader/bootmgr/app.c`, and
  `bootloader/CMakeLists.txt`.
- Bypasses `BmRunMenu` and calls `BmStartApplication` on the currently
  selected BCD default entry; it avoids the update-state write path and
  does not enter a blocking "press any key" prompt when the attempt fails.
- New checkpoints `[SW-DB00]`–`[SW-DB16]` distinguish BCD resolution,
  opening image, PE verification, and starting the loader.
- WSL LLVM 24, Debug ARM64 build via
  `cmake -S . -B output-Clang-arm64-debug -DLIBERNT_SWITCH_DIAG_DIRECT_BOOT=ON`
  and `ninja -C output-Clang-arm64-debug -j4 bootmgfw`.
- Output PE32+ ARM64 EFI SHA-256:
  `d9ecbd0a97ee59160104e6ccdb3eafa804209ade00126ad595495ad8d5dad658`.
- QEMU `virt` + EDK2 confirmed `[SW-DB00]`–`[SW-DB12]`, then reported
  `The application device was not found`, followed by `[SW-DB02]`
  and `[SW-DB05]`. This is expected in the virtual case without the
  physical NTFS system partition and **does not indicate the same error on
  the Switch**.
- The active microSD `D:\EFI\BOOT\BOOTAA64.EFI` was updated to that
  diagnostic with SHA-256 verification. Previous GUI Boot Manager was
  preserved at
  `D:\EFI\LiberNT\Experiments\bootmgfw-R5-original-GUI-backup.EFI`.
  A spare R5.1 binary is at
  `D:\EFI\LiberNT\Experiments\bootmgfw-R5.1-DirectDiagnostic.EFI`.
- A checksum-guarded selector/rollback script was prepared at
  `C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r5.1-boot-selector.ps1`.
- Hekate autoboot is still off. The NTFS system volume, existing Linux
  partition and internal eMMC were not modified during this diagnostic
  update.

**R5.1 physical result is now confirmed; see the R5.2 section and the subsequent R5.3 photo evidence.** Safely eject Hekate USB storage, boot
the manual `LiberNT R0 - Mariko UEFI PROBE` configuration, and photograph
the last `[SW-DBxx]` checkpoint or any new failure message.
A successful `winload` or kernel boot is not assumed.

## R5.2: physical R5.1 handoff reached; EDK2-to-WinLoad tracing

The physical Switch Lite **R5.1 screenshot** reaches the following checkpoints
in the *new* Boot Manager direct-boot diagnostic:

```text
[SW-DB13] Image read operation returned
[SW-DB14] PE headers and checksum validated
[SW-DB15] FwStartImage begin
```

This demonstrates the real physical BCD system device was resolved, the image
at `\Windows\system32\winload.efi` was read and the PE headers
and checksum passed validation. **It does not prove `BootServices->StartImage`
was reached or that WinLoad executed.** `[SW-DB16]` was not visible.

The BCD store was independently checked on the Windows host:

- OS entry: `device partition=L:`, `osdevice partition=L:`
- Application: `\Windows\system32\winload.efi`
- System root: `\Windows`
- BCD checksum during R5.2 staging:
  `f512a1d7a291c8ccb58a225feb3284171abd8eab48af1de4c7d125baf0864e63`

The following opt-in tracing was added to `switch-lite-mariko-r0`:
1. `bootloader/uefi/platform.c`: `[SW-FW00]`–`[SW-FW08]` around
   `LoadImage`, `HandleProtocol`, log closure and `StartImage`.
2. `bootloader/uefi/uefi.c`: `[SW-UEFI-E00]`–`[SW-UEFI-E01]` in
   the **shared** EFI entry function (applies to both EFI binaries).
3. `bootloader/winload/main.c`: `[SW-WL00]`–`[SW-WL10]` tracing
   WinLoad setup, memory, stack switch and boot entry selection.
4. Separate ARM64 Debug CMake flags:
   `LIBERNT_SWITCH_HANDOFF_TRACE=ON` and
   `LIBERNT_SWITCH_WINLOAD_TRACE=ON`.
   Note: UEFI platform and entry sources live in shared `bootlib`, so their
   flags must be defined on **bootlib** rather than just the final executable.

WSL LLVM 24 built both ARM64 EFI executables successfully:

| R5.2 EFI artifact | SHA-256 |
| --- | --- |
| `bootmgfw.efi` | `562f0307ed8fec5339c0e9efec2cad26a027d910999c0de9cf8dff05e5c61b48` |
| `winload.efi` | `753489ab9567c35e6dafee6e0ff7b193a9227e3c73a72d7009b07d79529dafe9` |

The exact binaries were verified to contain the UTF-16 checkpoints.
In **isolated QEMU** the R5.2 Boot Manager read the BCD and reached
`[SW-DB12]`; it then failed to resolve the system device because
the virtual FAT-only test lacks the physical second NTFS partition.
A separate QEMU boot of R5.2 WinLoad confirmed `[SW-UEFI-E00]`,
`[SW-WL00]`–`[SW-WL09]` before returning when no suitable OS
entry could be selected. **Neither emulation test proves the actual console
has started WinLoad.**

The physical microSD was updated with **R5.2 Boot Manager** at
`D:\EFI\BOOT\BOOTAA64.EFI` and **R5.2 WinLoad** at
`L:\Windows\System32\winload.efi`. Before changes, verified
original SHA-256 backups of *both* files were saved to the laptop SSD and
`D:\EFI\LiberNT\Experiments\`. A checksum-guarded rollback
script was parsed successfully and stored at:
`C:\Users\Public\Documents\LiberNT-Switch-R0\restore-r5.1-from-r5.2.ps1`.

The BCD store, Hekate configuration, existing Linux partition and internal
Nintendo eMMC were **not** changed. Hekate remains `autoboot=0`.

**R5.2 physical-console result is confirmed in the next section.** After safely ejecting
Hekate USB Mass Storage, start the manual `LiberNT R0 - Mariko UEFI PROBE`
profile and record the last `[SW-FWxx]`, `[SW-UEFI-Exx]` and
`[SW-WLxx]` checkpoints. If `[SW-FW07]` appears but no WinLoad
entry checkpoint is displayed, further inspect `StartImage`/
firmware entry rather than assuming a kernel failure.

## R5.3: physical WinLoad entry confirmed, deeper storage/kernel tracing

The **physical Switch Lite photograph of the R5.2 diagnostic** confirms
execution in the real WinLoad application, not merely the Boot Manager's
request to start it. Visible milestones include:

```text
[SW-FW07] StartImage begin
[SW-UEFI-E00] Shared EFI entry reached
[SW-UEFI-E01] Firmware watchdog disabled
[SW-WL00] BlMain entered
[SW-WL01] Announce completed
[SW-WL02] OslArchInitialize begin
[SW-WL03] OslArchInitialize done
[SW-WL04] Memory manager begin
[SW-WL05] Memory manager done
[SW-WL06] Allocating loader stack
[SW-WL07] Switching loader stack
[SW-WL08] Loader stack active
[SW-WL09] Selecting boot entry
[SW-WL10] Boot entry selected
```

**Interpretation:** The physical console has executed ARM64 WinLoad,
initialized the memory manager, switched to the loader stack, and selected
an OS boot entry. The last observed checkpoint is `[SW-WL10]`; it does
**not** prove successful `OslGetParameters`, opening the OS device,
loading the SYSTEM registry hive, reading kernel or HAL binaries, or
starting the kernel. The absence of later markers is not a backtrace.

Commit `dc2b06fd48fc9a73c7da38f2925782cb2e3de21a` adds opt-in
`[SW-WL11]`–`[SW-WL36]` checkpoints in
`bootloader/winload/main.c`, specifically around boot parameter parsing,
OS device resolution, loader block and configuration tree, SYSTEM hive,
kernel and HAL module load, import resolution, NLS, boot drivers and
`OslArchStartKernel`. This modifies only the trace-enabled build.

### R5.3 build / staging

- LLVM 24 ARM64 EFI build succeeded in WSL on the connected laptop.
- PE32+ ARM64 `winload.efi`, 1,329,664 bytes.
- SHA-256 `021b85b733c1bdd95850f58a467e50a7322cde00cf4573556e442abf6a9f385b`.
- A **standalone headless QEMU** test of that exact EFI file reached
  `[SW-WL09] Selecting boot entry`. QEMU lacks the physical SD BCD
  setup used by the console, so its inability to reach `[SW-WL10]`
  is not a hardware regression.
- After confirming the Hekate USB disk, labels, partition offsets and
  prior file hashes, **only** `L:\Windows\System32\winload.efi` was
  replaced with R5.3; SHA-256 after copying matched the build.
- `D:\EFI\BOOT\BOOTAA64.EFI` remained R5.2 Boot Manager
  (SHA-256 `562f0307ed8fec5339c0e9efec2cad26a027d910999c0de9cf8dff05e5c61b48`).
- `D:\EFI\LiberNT\Boot\BCD` remained unchanged
  (SHA-256 `f512a1d7a291c8ccb58a225feb3284171abd8eab48af1de4c7d125baf0864e63`).
- R5.2 WinLoad backups are verified on SSD and FAT32 under
  `D:\EFI\LiberNT\Experiments\winload-R5.2-confirmed-physical.EFI`.
- Rollback script created and PowerShell syntax-checked:
  `C:\Users\Public\Documents\LiberNT-Switch-R0\restore-winload-r5.2-from-r5.3.ps1`.
  It is not executed unless restoration is requested or necessary.
- Hekate/Linux partitions and Nintendo eMMC were not changed.

**R5.3 physical-console result is pending.** On the next manual boot,
record the last `[SW-WLxx]` marker to narrow the failure toward BCD
boot parameters, SD device access, SYSTEM hive, or kernel image loading.
Do not claim a successful kernel entry until `[SW-WL36]` and a separate
kernel-side marker are observed.

## Read-only R5.3 preflight and early-kernel display planning

After the R5.3 SD deployment, a subsequent host check again confirmed
`L:\Windows\System32\winload.efi` SHA-256
`021b85b733c1bdd95850f58a467e50a7322cde00cf4573556e442abf6a9f385b`.
**No physical R5.3 photograph has been supplied yet.**

Read-only inspection of the currently mounted second NTFS volume found:

| File | Status |
| --- | --- |
| `\Windows\System32\winload.efi` | Present, PE ARM64 (0xAA64), 1,329,664 bytes |
| `\Windows\System32\ntoskrnl.exe` | Present, PE ARM64 (0xAA64), 4,608,000 bytes |
| `\Windows\System32\ntkrnlmp.exe` | Present, PE ARM64 (0xAA64), 4,677,632 bytes |
| `\Windows\System32\hal.dll` | Present, PE ARM64 (0xAA64), 1,798,144 bytes |
| `\Windows\System32\lfbbvid.dll` | Present, PE ARM64 (0xAA64), 1,011,712 bytes |
| `\Windows\System32\bootvid.dll` | Present, PE ARM64 (0xAA64), 1,011,712 bytes |
| `\Windows\System32\config\SYSTEM` | Present, 978,944 bytes |
| `\Windows\System32\*.nls` | 133 files found |
| `\Windows\System32\drivers\*.sys` | 161 files found |

Presence is **not** proof of successful registry parsing, image relocations,
module imports, driver compatibility or boot.

### Next kernel-level observable

The **current source** in `ntos/ke/arm64/earlyscreen.c` contains an early
framebuffer console that is conditional on `DEBUGPORT=SCREEN`, and
`ntos/ke/arm64/boot.c` calls `KiArm64EarlyScreenInitialize` after
establishing early mappings. The actual older kernel binary staged on SD
has not been confirmed to include the same source revision or exact
early-screen startup banner. Kernel entry is therefore still unproven.

The **active** BCD OS entry instead has
`loadoptions KDWATCHDOG=0 DEBUGPORT=FILE`, so the early screen console
is not currently requested. A **separate test copy** of the BCD was
prepared entirely on the laptop at:

`C:\Users\Public\Documents\LiberNT-Switch-R0\staging-r5.3\BCD-screen-debug-test`

with `loadoptions KDWATCHDOG=0 DEBUGPORT=SCREEN`.
It is **NOT** active on SD and should not be switched until after the
physical R5.3 result is captured. Copy-only baseline stored at
`BCD-snapshot-before-SCREEN`.

During the lab the live BCD SHA-256 changed between observations, but the
readable `{default}` OS device, system root, kernel, HAL and loadoptions
continued to report the same values. **Cause of the mutable hive metadata
is unproven**; do not rely on a historical BCD hash as a perpetual immutable
artifact. Inspect and back up the current live store before making any
future BCD changes; do not overwrite it when its contents differ.

**Next actual device milestone:** observe the already-installed R5.3
`[SW-WL11]`–`[SW-WL36]` markers; then decide whether to activate the
screen-debug BCD in a separate, one-variable experiment. A visible kernel
entry should be established independently from loader messages.

## R5.4: physical R5.3 stops in ARM64 kernel address-space preparation

A new **physical Nintendo Switch Lite photograph** confirms that WinLoad
R5.3 progresses much further than the previous R5.2 photo. Visible
checkpoints include:

```text
[SW-WL11] Reading boot parameters begin
[SW-WL12] Boot parameters parsed
[SW-WL13] OS device resolution begin
[SW-WL14] OS device resolution completed
[SW-WL15] Loader block allocation begin
[SW-WL16] Loader block allocated
[SW-WL17] Configuration tree begin
[SW-WL18] Configuration tree returned
[SW-WL19] SYSTEM hive load begin
[SW-WL20] SYSTEM hive loaded
[SW-WL21] Kernel image load begin
[SW-WL22] Kernel load returned
[SW-WL23] HAL image load begin
[SW-WL24] HAL load returned
[SW-WL25] Driver/import checks begin
[SW-WL26] Driver/import checks completed
[SW-WL27] NLS load begin
[SW-WL28] NLS loaded
[SW-WL29] Boot driver list load begin
[SW-WL30] Boot driver list built
[SW-WL31] Driver database scan returned
[SW-WL32] Finalize loader block begin
[SW-WL33] Loader block finalized
[SW-WL34] Kernel address space preparation begin
```

The last visible marker is **`[SW-WL34]`**. No `[SW-WL35]` or
`[SW-WL36]` appears. This localizes the next research target to
`OslArchPrepareHandoff(LoaderBlock)` in
`bootloader/arch/arm64/archmm.c`. It **does not prove the kernel
has started**; nor does it identify the exact internal call without
further checkpoints.

### R5.4 instrumentation, source and hardware staging

New opt-in diagnostics were committed on `switch-lite-mariko-r0`:

- `bootloader/arch/arm64/archmm.c`: `[SW-HO00]`–`[SW-HO15]`
  trace machine-specific setup, `WinLdrSetupMemoryLayout`, page-table
  initialization, framebuffer alias mapping, framebuffer console setup,
  `ExitBootServices`, watchdog/vector/processor-context initialization,
  and shared user page initialization.
- `bootloader/winload/memory.c`: `[SW-ML00]`–`[SW-ML12]`
  trace memory allocation, EFI memory map synchronization, mappings,
  runtime service descriptors, and loader descriptor conversion.
- `bootloader/CMakeLists.txt`: ensure `LIBERNT_SWITCH_HANDOFF_TRACE`
  applies to `winload`, which actually compiles both the relevant files.
- Pre-`ExitBootServices` messages use `FwPrint`. **After** the firmware
  services are gone they use the existing `Arm64TrapScreenRoutine`
  framebuffer callback, if installed and mapped; they must **never**
  invoke UEFI console services at that stage. Missing post-exit
  markers cannot by themselves prove no instruction was executed.

Build succeeded in WSL LLVM 24 using the existing ARM64 Debug toolchain.
The `winload.efi` artifact is 1,332,736 bytes, PE32+ ARM64 EFI, SHA-256
`a8b2a8276839aaa546b0c0a04e4796b034d89f1f093e6a7f6518df6c74f9b38d`.
Strings inspection confirmed all selected UTF-16 and ASCII markers.
Standalone QEMU EDK2 boot of the exact image reached
`[SW-WL09]`, consistent with previous FAT-only QEMU runs; the
handoff path is **not** exercised in that virtual configuration.

On the physical Hekate SD (disk `hekate SD RAW`, USB, `D:` FAT32
`SWITCH SD`, `L:` NTFS `LIBERNT-LAB`), the original R5.3
WinLoad SHA-256 `021b85b733c1bdd95850f58a467e50a7322cde00cf4573556e442abf6a9f385b`
was verified and saved to:

- `C:\Users\Public\Documents\LiberNT-Switch-R0\backups\winload-R5.3-confirmed-physical.EFI`
- `D:\EFI\LiberNT\Experiments\winload-R5.3-confirmed-physical.EFI`

R5.4 was copied to
`D:\EFI\LiberNT\Experiments\winload-R5.4-handoff-trace.EFI`
and then installed **only** at
`L:\Windows\System32\winload.efi`. SHA-256 matched after installation.

The active R5.2 Boot Manager remained
`D:\EFI\BOOT\BOOTAA64.EFI`, SHA-256
`562f0307ed8fec5339c0e9efec2cad26a027d910999c0de9cf8dff05e5c61b48`.
The live BCD retained `DEBUGPORT=FILE`. No BCD, partition,
Hekate or eMMC mutation was made as part of this R5.4 deployment.

Rollback script prepared and syntax-checked at
`C:\Users\Public\Documents\LiberNT-Switch-R0\restore-winload-r5.3-from-r5.4.ps1`;
it is not run automatically.

**R5.4 real-device result remains pending.** Boot Hekate's existing
manual `LiberNT R0 - Mariko UEFI PROBE` entry after ejecting both host
volumes, and photograph the last `[SW-HO]` and `[SW-ML]` markers.
Do not change BCD or install a new kernel in the same experiment.

## R5.5: Switch R5.4 passed ExitBootServices

The real Switch Lite R5.4 photograph confirms [SW-ML00]–[SW-ML12] and
[SW-HO00]–[SW-HO15], including successful memory descriptor population,
page-table preparation, framebuffer mapping, return from ExitBootServices,
watchdog stopping, exception vector setup, processor context initialization,
and the shared user page zeroing. The last visible checkpoint is **[SW-HO15]**.
All checkpoints after [SW-HO10] were drawn by the post-UEFI framebuffer
callback, not firmware OutputString. The kernel has **not** been proven
to execute. UefiLeaveBootServices halts if ExitBootServices fails, which
supports this as a genuine transition out of UEFI.

The R5.4 trace attempted its next checkpoint [SW-WL35] via FwPrint after
returning to OslMain, followed by three OslTrace calls and [SW-WL36].
FwPrint **does** check BootServicesExitedFlag before attempting firmware
console output, but still uses logging / direct serial paths. These are
possible confounders, **not** a confirmed crash cause.

### R5.5 isolated change

- Source commit `05593fdd`: the ARM64 trace-enabled path instead emits
  ASCII [SW-WL35] and [SW-WL36] via Arm64TrapScreenRoutine and bypasses
  verbose serial/log calls after EBS. Normal builds retain original code.
- LLVM 24 Debug ARM64 WinLoad built: PE32+ ARM64, 1,332,224 bytes.
- SHA-256: `ef5f9dadc7d6b174e075739dbead9e5c6e7cdeba1e99f197efa28f37909ce8c8`.
- Standalone QEMU EFI test reached [SW-WL09]. QEMU lacks the NTFS system
  partition and does **not** validate kernel handoff.
- On physical SD, replaced ONLY `L:\Windows\System32\winload.efi` after
  verifying the current R5.4 hash; destination SHA-256 was reverified.
- Saved R5.4 backups on the notebook SSD and the SD FAT32 volume.
- R5.2 Boot Manager, BCD, Linux partition, Hekate and internal eMMC
  were not modified. Hekate autoboot remains disabled.
- Rollback: `C:\Users\Public\Documents\LiberNT-Switch-R0\restore-winload-r5.4-from-r5.5.ps1`
  (created and PowerShell syntax-checked; not executed).

**R5.5 physical-console result pending.** After safely ejecting the
two Hekate USB Mass Storage volumes, boot the same manual profile and
photograph [SW-WL35] / [SW-WL36] or the last earlier checkpoint.
Even [SW-WL36] proves only an attempted kernel jump, not kernel entry.
## R5.6: physical R5.5 reached the kernel-jump call site

A **physical Switch Lite photograph** of the R5.5 WinLoad run displays,
after prior [SW-HO11] through [SW-HO15] post-ExitBootServices checkpoints:

```text
[SW-WL35] Kernel address space prepared (post-EBS)
[SW-WL36] Entering OslArchStartKernel
```

This proves that WinLoad continued executing past
`OslArchPrepareHandoff()` and reached the imminent call to
`OslArchStartKernel()` on the actual Switch Lite. **It does not prove
entry into the ARM64 kernel** or into the assembly function
`Arm64JumpToKernel`. The screen stops showing new diagnostics at
the handoff call site.

### Isolating the handoff (R5.6)

The new opt-in ARM64 trace probes split the call site into:

| Marker | Location / interpretation |
| --- | --- |
| `SW-J00` | `OslArchStartKernel` function entered |
| `SW-J01` | LoaderBlock VA-to-PA conversion returned |
| `SW-J02` | Kernel stack pointer read |
| `SW-J03` | About to call `Arm64JumpToKernel` |
| `SW-A00` | Assembly jump routine entered with original arguments preserved |
| `SW-A01` | Reached the KSEG0 alias location |
| `SW-A02` | Kernel stack was selected; identity mapping still active |

The assembly checkpoints call a framebuffer-only callback while
TTBR0 identity mapping is still available; they preserve x19-x21,
LR, x0-x2 where applicable, and 16-byte stack alignment.
**Instrumentation itself can influence the failure**, particularly
when invoking C code after switching to the KSEG0 alias or kernel
stack. No C callback is inserted *after* TTBR0 is cleared.
The regular non-trace WinLoad build is not changed.

Source commits in `switch-lite-mariko-r0`:
- `9dc65073`: C-side markers and assembly entry handler
- `c5762d4d`: ARM64 assembly checkpoint sites
- `48484f49`: corrected GAS macro-argument escaping after catching
  a build error

The LLVM 24 Debug ARM64 WinLoad build passed after the macro fix.
PE32+ EFI ARM64 size **1,332,736 bytes**, SHA-256
`db0568146d58eb5b7900f527bacc55fa867b10d4f33faf0ea7d1048ee14f3718`.
The exact binary was verified to contain ASCII strings for the new
stage markers. Its standalone QEMU EFI test again reached
`[SW-WL09]`; it cannot test the kernel jump without the real OS
device and compatible boot configuration.

The artifact was staged **on the laptop only** at:

`C:\Users\Public\Documents\LiberNT-Switch-R0\staging-r5.6\winload-R5.6.efi`

The Hekate USB Mass Storage disk was **not connected** when R5.6 was
compiled and staged. No copy to the Switch SD has been attempted,
and the last verified installed WinLoad on the microSD is still R5.5,
SHA-256 `ef5f9dadc7d6b174e075739dbead9e5c6e7cdeba1e99f197efa28f37909ce8c8`.

Prepared (not executed) checksum-guarded PowerShell installer/rollback:

`C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r5.6-installer.ps1`

The script passed static PowerShell syntax parsing, takes
`-Action Install` or `-Action Restore`, guards the Hekate USB
disk and both volume labels, verifies both firmware and file SHA-256,
preserves R5.5 backups to SD FAT32 and the notebook SSD, and writes
only the WinLoad file on experimental NTFS. Always finish by safely
ejecting Windows-mounted volumes before leaving USB Mass Storage.

**Next physical test:** Switch must be back in Hekate USB Mass Storage
before installation; verify R5.5 hash first. Then boot R5.6 with the
same BCD, Boot Manager and kernel untouched, photograph the
`SW-Jxx` / `SW-Axx` markers. If `SW-A02` appears, focus on
cache maintenance, TTBR0 clearing and the final branch. Do not label
the kernel "running" until a kernel-side entry marker is observed.

### R5.6 installation update

After the initial offline staging, the user reconnected Hekate USB Mass
Storage. A fresh read-only guard positively identified `hekate SD RAW`
as a non-system USB disk with `D:` FAT32 `SWITCH SD` and
`L:` NTFS `LIBERNT-LAB`. At installation time:

- Active R5.5 WinLoad SHA-256:
  `ef5f9dadc7d6b174e075739dbead9e5c6e7cdeba1e99f197efa28f37909ce8c8`
- R5.6 staging image SHA-256:
  `db0568146d58eb5b7900f527bacc55fa867b10d4f33faf0ea7d1048ee14f3718`
- Boot Manager unchanged SHA-256:
  `562f0307ed8fec5339c0e9efec2cad26a027d910999c0de9cf8dff05e5c61b48`
- BCD was readable and still had `KDWATCHDOG=0 DEBUGPORT=FILE`,
  `osdevice partition=L:`, `systemroot \Windows`.

The first invocation of the prepared installer failed **before copying**
because the PowerShell function named `H` resolved to the built-in
`Get-History` alias. Replaced it with `Get-LabHash`, verified the script
still parsed successfully and checked that the active R5.5 SHA-256 had
not changed. The corrected installer then succeeded:

- Active `L:\Windows\System32\winload.efi` now **R5.6** with expected hash.
- R5.5 backups checked on FAT32 and on the laptop's SSD:
  `D:\EFI\LiberNT\Experiments\winload-R5.5-before-R5.6.efi`,
  `C:\Users\Public\Documents\LiberNT-Switch-R0\backups\winload-R5.5-before-R5.6.efi`.
- The installation/rollback script is
  `C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r5.6-installer.ps1`,
  invoked with `-Action Install` or `-Action Restore`.
- No kernel, Hekate boot configuration or internal eMMC writes were made. No explicit BCD modification was requested. However, BCD **file SHA-256 changed across repeated `bcdedit /enum` reads** while `loadoptions KDWATCHDOG=0 DEBUGPORT=FILE`, `device/osdevice partition=L:` and `systemroot \\Windows` stayed the same; the cause of the binary metadata changes is not yet established.

**R5.6 is installed, but has not been booted on the physical Switch.**
Safely eject both Windows volumes, return to Hekate and use the
existing manual `LiberNT R0 - Mariko UEFI PROBE` configuration.
The next photo should show whether `[SW-J00]` through `[SW-A02]`
appear. Missing markers do not by themselves prove a specific fault,
because the new debug callback can affect execution after KSEG0 or
stack switching.

## R5.7: physical R5.6 reached KSEG0 and kernel stack, cache/TTBR0 probes

A new photograph from the **physical Nintendo Switch Lite** shows:

```text
[SW-HO11] ExitBootServices returned
[SW-HO12] Firmware watchdog stopped
[SW-HO13] Exception vectors set
[SW-HO14] Processor context set
[SW-HO15] User shared page cleared
[SW-WL35] Kernel address space prepared (post-EBS)
[SW-WL36] Entering OslArchStartKernel
[SW-J00] OslArchStartKernel entered
[SW-J01] Loader block VA-to-PA returned
[SW-J02] Kernel stack pointer fetched
[SW-J03] Arm64JumpToKernel call next
[SW-A00] Jump assembly entry
[SW-A01] KSEG0 alias reached
[SW-A02] Kernel stack selected
```

This is **direct evidence of ARM64 loader assembly execution after the
KSEG0 alias branch and kernel-stack selection**. No subsequent assembly
marker existed in R5.6; this photograph therefore does NOT by itself
prove a cache-management fault, TTBR0 failure, a successful final
`br x19`, or entry into the LiberNT kernel.

### R5.7 controlled test

Under `LIBERNT_SWITCH_HANDOFF_TRACE`, new framebuffer-only ARM64
assembly checkpoints (no UEFI calls) were committed:

| Marker | Probe location |
| --- | --- |
| `SW-A03` | DAIF interrupt mask set |
| `SW-A04` | Before `Arm64CompleteCacheMaintenance` |
| `SW-A05` | Returned from cache maintenance |
| `SW-A06` | Before `Arm64ClearIdentityMappings` |
| `SW-A07` | After TTBR0 identity-map clearing returns |
| `SW-A08` | Immediately before the final `br x19` |

Checkpoint helpers preserve x19-x21, LR and 16-byte stack alignment.
The `SW-A07` and `SW-A08` screen callbacks themselves require
function code, framebuffer mapping and the stack to remain accessible
under TTBR1, **so they may influence the outcome of the diagnostic**.
An absent post-TTBR0 marker cannot alone prove that clearing TTBR0 failed.
No C call is added *after* the final `br x19`, and no kernel-side
startup checkpoint is claimed.

- Source commits: `f9e05aa0` (stage handler) and `492f79de`
  (ARM64 assembly checkpoint sites).
- ARM64 Debug WinLoad built successfully from the fork via LLVM 24.
- PE32+ EFI ARM64, **1,333,248 bytes**.
- SHA-256: `b91a87c0f5ab1a60d77959f1169b0ccc3079a403a76c1e6915611b4c69d2828f`.
- Strings inspection confirms `[SW-A03]`–`[SW-A08]` markers.
- Standalone QEMU (EDK2, no experimental NTFS system partition) reaches
  `[SW-WL09]`, so deep kernel handoff is only testable on real Switch.
- Staged source on notebook:
  `C:\Users\Public\Documents\LiberNT-Switch-R0\staging-r5.7\winload-R5.7.efi`.
- With Hekate's SD connected over USB Mass Storage, verified non-system
  USB disk, FAT32 `D:` labeled `SWITCH SD`, NTFS `L:` labeled
  `LIBERNT-LAB`, and expected Boot Manager and R5.6 WinLoad hashes.
- Prior R5.6 WinLoad backed up with SHA-256 verification to both SSD
  and FAT32:
  `C:\Users\Public\Documents\LiberNT-Switch-R0\backups\winload-R5.6-confirmed-physical.EFI`,
  `D:\EFI\LiberNT\Experiments\winload-R5.6-confirmed-physical.EFI`.
- Installed **only** `L:\Windows\System32\winload.efi` and verified it
  matches R5.7 SHA-256. Boot Manager R5.2 remains
  `562f0307ed8fec5339c0e9efec2cad26a027d910999c0de9cf8dff05e5c61b48`.
- BCD continues to report `KDWATCHDOG=0 DEBUGPORT=FILE` and
  Hekate has `autoboot=0`. Neither BCD nor kernel, Linux partition or
  internal eMMC was intentionally modified.
- Prepared syntax-checked rollback/reinstall script:
  `C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r5.7-restore-installer.ps1`
  with `-Action Restore` / `-Action Install`, verifying target disk,
  both images and backups before replacement. Not executed.

**The R5.7 physical result is now confirmed in the R5.8 section below.** Safely eject both volumes
from Windows, exit Hekate USB Mass Storage and boot the same
`More Configs → LiberNT R0 - Mariko UEFI PROBE` profile. Photograph
the last `SW-Axx` marker. `[SW-A08]` would prove the loader reached
the last instruction before the kernel branch; a kernel-side marker
is still required to prove `KiSystemStartup` executed.

## R5.8: real Switch R5.7 completed cache, TTBR0 teardown, reached final kernel branch

The physical Switch Lite photo from R5.7 shows these *new* checkpoints:

```text
[SW-A03] Interrupts masked
[SW-A04] Cache maintenance begin
[SW-A05] Cache maintenance returned
[SW-A06] TTBR0 identity clear begin
[SW-A07] TTBR0 clear returned
[SW-A08] Final kernel branch next
```

The last observed marker is **`SW-A08`**, immediately *before* the
`br x19` in `bootloader/arch/arm64/kernel_jump.S`.
The real device made it through cache maintenance, the return from
`Arm64ClearIdentityMappings`, post-TTBR0 framebuffer diagnostic
calls, and the last synchronization barriers. It does **not** show
that `br x19` succeeded or that even one `ntoskrnl.exe` instruction ran.
The code after TTBR0 cleaning is partly instrumented, and the
instrumentation itself can affect the test.

### R5.8 one-variable early kernel-screen experiment

Current *source* under `ntos/ke/arm64/earlyscreen.c` provides
an early framebuffer console conditional on
`LoaderBlock->LoadOptions` containing `DEBUGPORT=SCREEN`.
It is initialized within `KiArm64EnsureIdentityMapping` after entering
the C startup function. The *installed kernel image* differs from
the current source: raw ASCII inspection of the existing
4,608,000-byte kernel did **not** find the current source's expected
`LiberNT kernel started` banner. It **did** contain
`DEBUGPORT=SCREEN`. This makes the screen test inconclusive
if nothing new appears.

To test the pre-existing on-screen debugging without changing the
loader or kernel, the **only changed live file** is the experimental
FAT32 BCD store at:

`D:\EFI\LiberNT\Boot\BCD`

Its {default} OS entry now contains:

```text
device       partition=L:
path         \Windows\system32\winload.efi
osdevice     partition=L:
systemroot   \Windows
loadoptions  KDWATCHDOG=0 DEBUGPORT=SCREEN
```

The original `DEBUGPORT=FILE` BCD was copied and hash-verified
on both the notebook SSD and Switch FAT32 **before** this one-variable
test. Files:

- `C:\Users\Public\Documents\LiberNT-Switch-R0\backups\BCD-R5.7-before-SCREEN`
- `D:\EFI\LiberNT\Experiments\BCD-R5.7-before-SCREEN`
- Edited staging copy:
  `C:\Users\Public\Documents\LiberNT-Switch-R0\staging-r5.7\BCD-screen-experimental`

The BCD default entry was verified after installation with bcdedit.
Note that BCD hive file bytes/hashes have been observed to change
between reads; compare **entry semantics** and preserve backups,
rather than assuming its file SHA-256 remains static.

Unchanged and verified:

- Boot Manager R5.2 SHA-256:
  `562f0307ed8fec5339c0e9efec2cad26a027d910999c0de9cf8dff05e5c61b48`
- WinLoad R5.7 SHA-256:
  `b91a87c0f5ab1a60d77959f1169b0ccc3079a403a76c1e6915611b4c69d2828f`
- Installed ntoskrnl.exe SHA-256:
  `394d34cffce85e26cc6700a1f9751cb08c8ba8ea5adf5835f4e3aaa9ab064479`

The internal Nintendo eMMC, Linux partition, kernel, firmware,
and boot manager were not modified in this experiment.

A rollback/reapply script was created and passed static PowerShell
syntax parsing:

`C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r5.8-bcd-screen-selector.ps1`

Run with `-Mode Restore` to restore `DEBUGPORT=FILE`, or
`-Mode Screen` to reapply the on-screen test, with storage
and boot file identity checks. The script was not executed after creation.

**R5.8 real-device result is pending.** Safely eject both mounted Hekate
USB Mass Storage volumes, boot the existing manual LiberNT UEFI probe
profile and photograph any new kernel-side text. If the photo only shows
`SW-A08`, this is not decisive; the installed kernel may lack
the source's early framebuffer banner. A separate opt-in kernel-entry
probe and baseline-checked kernel build would be the next investigation,
but **no new kernel binary has been built or deployed yet**.

## R5.9 — physical R5.8 unchanged; kernel source probe and R5.9A installed

**Physical R5.8 photograph:** With BCD DEBUGPORT=SCREEN, the last visible
checkpoint is still [SW-A08] Final kernel branch next. No independent
kernel-side output is visible. This alone does NOT prove br x19 failed.

### Kernel-entry source probes (not installed)

Opt-in flag LIBERNT_SWITCH_KERNEL_ENTRY_TRACE=ON enables a framebuffer
color-bar probe in ntos/ke/arm64/switch_entry_probe.c, invoked directly
from KiSystemStartup in bootstack.S and three additional ARM64 C startup
locations in boot.c. The marker colors represent assembly entry,
startup C entry, exception vectors, and identity mapping preparation.
All three changed ARM64 compilation objects passed their individual
LLVM 24 compile checks. Full ntoskrnl linking/build is **NOT COMPLETE**:
the cold target has >8,000 dependencies; the build was intentionally
interrupted after partial dependency compilation. The original physical
ntoskrnl.exe was not changed. Github commits f7326f96, 7f51b762,
dacbd974, d62b073b. Compiled objects are not a bootable kernel.

### Fast alternate test R5.9A (WinLoad only)

Source commits 5f4ea1f5 and cc0e5323 add diagnostic ARM64 translation
check for the actual x19 kernel entry address, after [SW-A08] and before
br x19: AT S1E1R, x19; read PAR_EL1.F. [SW-A09] indicates translation
FAULT (the test holds in a diagnostic loop); [SW-A10] indicates
EL1 read translation succeeded before proceeding with br x19.
Read translation does NOT prove execution permission or kernel entry.

R5.9A ARM64 EFI WinLoad compiled with LLVM 24 (1,333,760 bytes);
SHA-256 6dc1602d4d96255a261afa2f29e1946ba1010cd498c679d3c84f0ad6fba47375.
The new marker strings were verified in the PE image. QEMU standalone
smoke test reached [SW-WL09]; it cannot exercise the deep real-OS path.
Notebook staging:
C:\Users\Public\Documents\LiberNT-Switch-R0\staging-r5.9a\winload-R5.9a.efi

Physical Hekate SD was confirmed as non-system USB disk 'hekate SD RAW',
D: FAT32 'SWITCH SD', L: NTFS 'LIBERNT-LAB'. Original R5.7 WinLoad hash
b91a87c0f5ab1a60d77959f1169b0ccc3079a403a76c1e6915611b4c69d2828f
and R5.9A candidate hashes were verified before copying.
R5.7 WinLoad backed up, with checksums, at:
C:\Users\Public\Documents\LiberNT-Switch-R0\backups\winload-R5.7-confirmed-physical-before-R5.9A.EFI
D:\EFI\LiberNT\Experiments\winload-R5.7-confirmed-physical-before-R5.9A.EFI
R5.9A staged at D:\EFI\LiberNT\Experiments\winload-R5.9A-entry-translation-probe.EFI
and installed ONLY at L:\Windows\System32\winload.efi. Installation SHA-256
verified. Boot Manager R5.2, original ntoskrnl.exe, BCD DEBUGPORT=SCREEN,
Hekate configuration, Linux partition and internal eMMC were unchanged.
SHA-256 of original kernel:
394d34cffce85e26cc6700a1f9751cb08c8ba8ea5adf5835f4e3aaa9ab064479.

Rollback script (created, syntax validated, not executed):
C:\Users\Public\Documents\LiberNT-Switch-R0\restore-winload-r5.7-from-r5.9a.ps1

**R5.9A physical result pending.** Safely eject D: and L:, leave Hekate
USB Mass Storage and boot the existing manual LiberNT R0 - Mariko UEFI
PROBE profile. Photograph [SW-A09] or [SW-A10], if either appears.
No kernel execution claim is justified by these loader-only stages.
## R5.9A physical confirmation — entrypoint Stage-1 read translation succeeds

On the real Nintendo Switch Lite, the user reports **[SW-A10]**
after booting the deployed R5.9A ARM64 WinLoad variant. This is
the branch of the trace after `AT S1E1R, x19` and a clear
`PAR_EL1.F` fault bit.

**Confirmed:** the actual candidate kernel entry virtual address in
`x19` was successfully translated for an EL1 *read* by the active
Stage-1 translation regime at the final WinLoad handoff. The loader
was still running following its TTBR0 cleanup and at the point just
before `br x19`.

Static read-only validation of the unchanged SD kernel PE:
`ntoskrnl.exe` has ARM64 machine `0xAA64`, entry RVA
`0x00012400`, and the entrypoint lies inside PE section `.text`
with image characteristics `0x68000020`, including executable
section flag `IMAGE_SCN_MEM_EXECUTE`. This verifies the **on-disk
PE section attribute**, not the permissions installed by the live
ARM64 page tables.

**Not confirmed:** executable permission (PXN / UXN), the branch's
successful completion, execution of `KiSystemStartup`, or stable
kernel startup. `[SW-A10]` is emitted by WinLoad, not by ntoskrnl.
A successful read translation is not a substitute for executing the
kernel.

The next reliable independent proof is an opt-in kernel-entry
checkpoint inside `ntos/ke/arm64/bootstack.S` or a carefully guarded
early exception-vector diagnostic. The source for the R5.9
`KiArm64EntryProbe` already exists in the fork, and individual ARM64
objects compiled; **the complete kernel binary has not been linked
and has not been deployed**. Preserve the working R5.9A WinLoad and
the current kernel on the Switch until a testable full kernel artifact
is produced and verified. No further device-side modification was
performed for this report.

### R5.9 source-level execution-permission audit

Physical R5.9A has reached `[SW-A10]`: `AT S1E1R, x19` read
translation succeeded, but kernel execution remains unproven.

Read-only inspection of
`bootloader/arch/arm64/mmu_v2.c` confirms that
`Arm64ApplyImageSectionProtections()` examines the PE section flag
`IMAGE_SCN_MEM_EXECUTE`. For executable sections it clears
`PTE_BLOCK_PXN` and `PTE_BLOCK_UXN`; the routine is called in
`Arm64EnablePageTables()`. The currently installed ARM64 kernel's PE
entrypoint resides in an executable `.text` section (RVA `0x12400`).

This is positive evidence for the **intended** executable mapping,
not proof of the runtime page-table bits or a successful branch.
A kernel-side entry marker remains necessary.

The source's opt-in R5.9 probe was independently validated at the
object/symbol level: `KiSystemStartup` references
`KiArm64EntryProbe`, and the latter has a global symbol in
`switch_entry_probe.c.obj`. The full cross-compiled kernel image was
not yet available when this note was recorded. The experimental SD
kernel remained byte-identical to its verified baseline.

## R5.9 experimental ARM64 ntoskrnl successfully built and installed on SD

After the physical Switch Lite R5.9A **`[SW-A10]`** report, the source's
opt-in `LIBERNT_SWITCH_KERNEL_ENTRY_TRACE=ON` test kernel was
fully cross-compiled on the connected laptop using WSL/LLVM 24.
The build passed **7,887 Ninja actions** and linked a complete
`ntoskrnl.exe` rather than only separate object files:

- Candidate: PE32+ **ARM64** (`0xAA64`), **Native** subsystem,
  13 sections, 8,409,088 bytes, `ImageBase=0x400000`.
- PE `AddressOfEntryPoint=0x12750` in an executable `.text` section.
- SHA-256 `d35605f76c2f8d235273209a5fe924e1a63375f42f1f679f2ec47de3e04200b9`.
- Disassembly of the **linked** PE at VA `0x412750` starts with
  `mrs x9, CPACR_EL1`, enables FP/ASIMD, then saves x0/LR and
  calls the entry framebuffer probe with `w1=1`.
  This positively confirms an instrumented **binary** entrypoint;
  it does *not* confirm any instruction executed on the Switch.

The kernel probe paints small bars in the upper-right framebuffer:
**magenta** = assembly `KiSystemStartup` stage 1,
**cyan** = startup C stage 2, **yellow** = exception vectors stage 3,
**white** = identity-map preparation stage 4. The routine does not call
UEFI firmware services. The display may remain unchanged if the kernel
does not execute, if the framebuffer alias/format is unavailable, or if
the probe itself triggers a failure.

### ABI / static checks

- Baseline kernel export names: **3,363**.
- Experimental R5.9 export names: **3,376**.
- **No baseline export names removed**; 13 were added. However, export
  *ordinals* changed for 3,047 shared names, so ordinal-based consumers
  should not be assumed compatible.
- Existing HAL: all **62** imports from ntoskrnl are present by name
  in R5.9; no ordinal import was observed.
- **161** existing `System32/drivers/*.sys` files were examined
  through a temporary **read-only** WSL DrvFs mount (unmounted after
  inspection); 155 imported 6,861 names from ntoskrnl, all of which
  exist in R5.9. This is not proof of full runtime ABI compatibility.
- The staged candidate on SSD was verified byte-for-byte against the
  WSL output by SHA-256.

### Physical SD staging and rollback

The Hekate USB disk was identified as `hekate SD RAW` (non-system
USB), FAT32 `D:` labeled `SWITCH SD`, NTFS `L:` labeled
`LIBERNT-LAB`.

The **original** kernel SHA-256
`394d34cffce85e26cc6700a1f9751cb08c8ba8ea5adf5835f4e3aaa9ab064479`
was backed up and SHA-verified on **both** SD FAT32 and laptop SSD:

- `D:\EFI\LiberNT\Experiments\ntoskrnl-R5.8-verified-original.exe`
- `C:\Users\Public\Documents\LiberNT-Switch-R0\backups\ntoskrnl-R5.8-verified-original.exe`

The new R5.9 image was retained at:

- `C:\Users\Public\Documents\LiberNT-Switch-R0\staging-r5.9\ntoskrnl-R5.9.exe`
- `D:\EFI\LiberNT\Experiments\ntoskrnl-R5.9-entry-probe.exe`

A checksum-guarded PowerShell selector is tracked at
`media/doc/ports/switch-r59-kernel-selector.ps1` and copied to
`C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r5.9-kernel-selector.ps1`.
It passed syntax parsing and real `-Mode Check` validation of the USB
device, two kernel backups, Boot Manager, WinLoad, and BCD.

The **only active OS binary replaced** was
`L:\Windows\System32\ntoskrnl.exe`; `-Mode Install -NewSha <verified hash>`
completed successfully, and independent readback verified that the
installed kernel, SD spare, and SSD candidate have the new SHA-256.
Original backups still have the old SHA-256. The existing R5.9A
WinLoad, R5.2 Boot Manager, BCD `DEBUGPORT=SCREEN`, Hekate
`autoboot=0`, Linux partition, and Nintendo eMMC were not changed.

Restoration command after reconnecting SD via Hekate USB Mass Storage:

```powershell
& 'C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r5.9-kernel-selector.ps1' -Mode Restore -NewSha D35605F76C2F8D235273209A5FE924E1A63375F42F1F679F2EC47DE3E04200B9
```

**R5.9 physical kernel execution is NOT YET CONFIRMED.** After safely
ejecting both SD volumes from Windows, boot the existing manual
`LiberNT R0 - Mariko UEFI PROBE` entry and photograph the upper-right
area for the magenta/cyan/yellow/white bars. A color bar generated
from `KiSystemStartup` would be independent proof that execution
entered the newly built ntoskrnl, which `[SW-A10]` cannot establish.

## R5.9 physical rerun: firmware entered EDK II UEFI Shell, not LiberNT

User's next physical photo shows **UEFI Interactive Shell v2.2**
and its initial mapping table / `startup.nsh` countdown.
It is **not** WinLoad trace output nor a kernel panic. There are no
visible `[SW-Axx]` or kernel color markers in this photograph.
This rerun does not prove either kernel success or kernel failure.

The experimental SD was again verified through the authorized
Hekate USB mass-storage interface on the laptop:

- USB disk `hekate SD RAW`, FAT32 `D:` (`SWITCH SD`),
  NTFS `L:` (`LIBERNT-LAB`), both healthy in Windows.
- `D:\EFI\BOOT\BOOTAA64.EFI` SHA-256
  `562f0307ed8fec5339c0e9efec2cad26a027d910999c0de9cf8dff05e5c61b48`.
- `L:\Windows\System32\winload.efi` SHA-256
  `6dc1602d4d96255a261afa2f29e1946ba1010cd498c679d3c84f0ad6fba47375`.
- `L:\Windows\System32\ntoskrnl.exe` R5.9 SHA-256
  `d35605f76c2f8d235273209a5fe924e1a63375f42f1f679f2ec47de3e04200b9`.
- Hekate profile unchanged:
  `bootloader/ini/11-LiberNT-R0-UEFI-probe.ini`,
  `l4t=1`, `boot_prefixes=/switchroot/libernt-r0/`;
  `autoboot=0`. `bl33.bin` is the existing Mariko UEFI shim.
- Before the fix, **no** `startup.nsh` file existed on FAT32.
- The photograph is blurry: individual `fsN:` versus `blkN:`
  mappings cannot be identified reliably.

### Temporary UEFI Shell fallback diagnostic

An opt-in shell script was added to the fork at
`media/doc/ports/switch-r59-shell-rescue.nsh`, then downloaded to
the laptop staging area and **installed only** as new FAT32 root file
`D:\startup.nsh`. Its SHA-256 was read back as
`43ed5475711dd5bbab1f0ab7034155f6be75ee1af60aa6e0eae727e90e44e714`.

The script scans `fs0:` through `fs7:`, only attempts to run
`fsN:\EFI\BOOT\BOOTAA64.EFI` when the **same** mapping has the
experiment-specific `fsN:\EFI\LiberNT\Boot\BCD` marker.
The startup script prints `[MECHANICUS] LiberNT-Switch R5.9 shell fallback`
and `[FOUND]`, `[RETURNED]` or `[DIAG]` messages, and finally `map`.
This is a fallback; it does not alter firmware NVRAM BootOrder or
any previous EFI file. Standard EDK II UEFI Shell runs a FAT
`startup.nsh` when its filesystem can be discovered.

**Limitations:** if firmware maps only `blkN:` and no FAT `fsN:`,
it cannot discover the script; that would point to a firmware
SD/filesystem visibility issue, NOT demonstrate a kernel failure.
A Boot Manager that returns to the script is likewise a loader path
failure, not yet a kernel startup result.

The script's physical effectiveness **is not yet tested**. With
the user having safely ejected both Windows volumes and exited
Hekate USB Mass Storage, the next test is to start the same
manual LiberNT profile and photograph any `[MECHANICUS]`,
`[FOUND]`, `[RETURNED]`, or `[SW-Axx]` messages, plus any
kernel bars.

**Rollback:** reconnect Hekate USB mass storage and delete only
`D:\startup.nsh`; no prior file was overwritten. The script can
also be skipped with ESC at the EDK II Shell startup prompt.
Do not touch the internal eMMC or reformat SD.

## R6.0: physical shell fallback revealed kernel/HAL export mismatch

The second physical Switch Lite photo after installing the UEFI Shell
`startup.nsh` confirms that the FAT32 filesystem was accessible as
`FS0:`, and the fallback script **ran successfully** and launched
`fs0:\EFI\BOOT\BOOTAA64.EFI`. Boot Manager and WinLoad then
returned with an explicit binary import-resolution error:

```text
ntoskrnl.exe needs HalArm64ConnectInterrupt from hal.dll, which does not export it.
The kernel and the hardware abstraction layer could not be loaded.
[SW-FU08] StartImage returned: 1
[SW-DB16] FwStartImage returned
\Windows\system32\winload.efi did not start the operating system.
[SW-DB02] BmStartApplication returned
[SW-DB05] Loader failed
[RETURNED] Boot Manager exited from fs0:
```

This corrects the previous hypothesis: UEFI SD detection works and the
new kernel is being rejected **before execution**, due to a
version mismatch between the R5.9 kernel and the previously installed
HAL. The old loader's `[SW-A10]` read-translation success belongs to
a test with the older kernel and does not establish kernel entry.

### Verified missing dependencies

Read-only PE analysis of the installed HAL (SHA-256
`e7cc37d649d4178247a0dfb490884a6523c32126af1fe80bb32329be43e9ac45`)
against the R5.9 `ntoskrnl.exe` found **three** imports absent
from the old HAL:

- `HalArm64ConnectInterrupt`
- `HalArm64DisconnectInterrupt`
- `HalpAcpiQueryFaults`

All three are implemented and exported by the current source tree.
Building the **matching ARM64 HAL from the same source as R5.9** took
181 Ninja steps and succeeded. The new `hal.dll` is PE32+ ARM64,
2,105,344 bytes, SHA-256:

`ec484cce061d7722ff8f3f44445f5dda32ff0bb787da359604d2909005f52993`.

Its exports contain all **142** old HAL export names plus seven
additional names (149 total). No export names were removed. **94**
shared export ordinals changed, which could affect ordinal consumers
even though name-based lookups are complete.

The new kernel imports **45** HAL symbols and all 45 exist in the
new HAL; the new HAL imports **78** kernel symbols and all 78 are
exported by R5.9. A read-only inspection of 937 installed system PE
files found 19 modules importing HAL (145 total imported symbols).
All 145 imports resolve by name against the new HAL; no ordinal-only
HAL imports were found among these checked files. These static checks
are not a full runtime compatibility guarantee.

### Safe hardware staging and installation

Target explicitly verified: `hekate SD RAW`, USB, non-system disk,
FAT32 `D:` labeled `SWITCH SD`, NTFS `L:` labeled `LIBERNT-LAB`.
Before copying, the active Boot Manager, R5.9A WinLoad, R5.9 kernel,
old HAL and BCD `DEBUGPORT=SCREEN` were hash/config-verified.

The **old HAL** was backed up and SHA-verified in both places:

- `C:\Users\Public\Documents\LiberNT-Switch-R0\backups\hal-R5.9-confirmed-physical.dll`
- `D:\EFI\LiberNT\Experiments\hal-R5.9-confirmed-physical.dll`

The new candidate was staged and verified:

- Notebook: `C:\Users\Public\Documents\LiberNT-Switch-R0\staging-r6.0\hal-R6.0.dll`
- FAT32: `D:\EFI\LiberNT\Experiments\hal-R6.0-matched-kernel.dll`

**Only** `L:\Windows\System32\hal.dll` was replaced.
Post-install SHA-256 matched the new HAL. The following were
re-verified unchanged:

- R5.9 `ntoskrnl.exe` SHA-256 `d35605f76c2f8d235273209a5fe924e1a63375f42f1f679f2ec47de3e04200b9`
- R5.9A `winload.efi` SHA-256 `6dc1602d4d96255a261afa2f29e1946ba1010cd498c679d3c84f0ad6fba47375`
- R5.2 Boot Manager SHA-256 `562f0307ed8fec5339c0e9efec2cad26a027d910999c0de9cf8dff05e5c61b48`
- BCD remained `KDWATCHDOG=0 DEBUGPORT=SCREEN`.
- No writes were made to Hekate config, Linux partition or Switch eMMC.

A guarded HAL rollback/reapply script was added to the repository at
`media/doc/ports/switch-r60-hal-selector.ps1` and placed on the
laptop at
`C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r6.0-hal-selector.ps1`.
It supports `-Mode Check`, `-Mode Install`, `-Mode Restore`,
requires exact USB volume identities and SHA-256 checks, and has
been syntax-tested and executed **read-only with -Mode Check**.

**R6.0 physical test pending:** safely eject Windows volumes `D:`
and `L:`, leave Hekate USB Mass Storage, and run the same
`More Configs → LiberNT R0 - Mariko UEFI PROBE` profile. With
`startup.nsh` still present, firmware Shell can automatically
invoke the Boot Manager. Photograph the first new WinLoad error
or the upper-right kernel entry color bars. If the original export
error disappears, the HAL mismatch was addressed; a further
error or lack of color bars will localize the next failing stage.

To roll back the *HAL only*, reattach SD via Hekate USB Mass Storage
and run:
```powershell
& 'C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r6.0-hal-selector.ps1' -Mode Restore
```

The kernel has **not** yet been confirmed to execute on Switch Lite.

## R6.1 offline preflight: next import mismatch found in BootVid

After the R6.0 HAL update was verified on Hekate's USB SD, a new
read-only transitive import audit of the **active R5.9 ntoskrnl.exe,
R6.0 hal.dll, kdcom.dll, and installed original bootvid.dll**
detected that the kernel requires **16 named BootVid exports**, but
the old on-SD BootVid only supplies **14 of these**. The two missing
exports are:

```text
VidRenderStatusText
VidUseCompactFont
```

All other first-tier imports of the four modules resolved by name.
This is a **predicted next loader failure** from PE import inspection,
not yet a physical error report. The user's R6.0 real-console boot
result is still pending.

The current source tree already defines and implements both API
functions in `drivers/base/bootvid/bootvid.spec` and
`drivers/base/bootvid/framebuf/bootvid.c`, so an ARM64 BootVid rebuild
was performed from the **same fork/source tree as the kernel and HAL**.
Ninja completed the target and linked a PE32+ ARM64 Native
`bootvid.dll`, 1,437,696 bytes, SHA-256:

`2d40aab426d65b858af10ce73105eba45f4f4b60c6056b227aa1f9ba9a1372b6`

Static checks on the new build: **all 16/16** R5.9 kernel imports
from BootVid resolve; **all 16/16** BootVid imports from the
R5.9 kernel resolve. There are no further imported DLLs in the new
BootVid. These are name-based checks, not a boot/runtime guarantee.

The candidate was staged and SHA-verified on the laptop at:

`C:\Users\Public\Documents\LiberNT-Switch-R0\staging-r6.1\bootvid-R6.1.dll`

A guarded installation/rollback script was committed to
`media/doc/ports/switch-r61-bootvid-selector.ps1` and downloaded to:

`C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r6.1-bootvid-selector.ps1`

PowerShell parser result: **0 errors**. The script supports
`-Mode Check`, `-Mode Install`, and `-Mode Restore`, checks
the expected non-system Hekate USB device, FAT32/NTFS volume labels,
Boot Manager, WinLoad, kernel, HAL and BCD, and will first preserve
the original BootVid on **both SD FAT32 and laptop SSD** before
installing the new image. It checks SHA-256 after writes.

**Physical media state:** During this diagnostic phase the user
disconnected the Switch SD from laptop USB Mass Storage. Windows
reported no disk `hekate SD RAW`, no volumes D: or L:, and a WSL
read-only DrvFs mount lost its backing device; the stale WSL
mount was cleanly unmounted. Accordingly, **no R6.1 image was
written to the Switch** and the selector's install/restore mode
was NOT run. Staging and parsing were laptop-only.

Next: review the user's real R6.0 boot screenshot first. If a
BootVid import error occurs, reconnect Hekate USB Mass Storage,
re-verify the same disk/partitions, run the guarded R6.1 selector
with `-Mode Check`, then `-Mode Install`. Only after hash-
verified installation and safe eject attempt the manual UEFI probe
again. Do not touch Hekate configuration, Linux partition, or eMMC.

## R6.1 physical photo and verified BootVid replacement

A new physical Nintendo Switch Lite image after the R6.0 matched HAL
deployment shows that WinLoad progressed beyond the previous
`HalArm64ConnectInterrupt` import error, then reported:

```text
ntoskrnl.exe needs VidRenderStatusText from bootvid.dll, which does not export it.
The kernel and the hardware abstraction layer could not be loaded.
[SW-FU08] StartImage returned: 1
[SW-DB05] Loader failed
[RETURNED] Boot Manager exited from fs0:
```

This is a **load-time import mismatch**, not evidence that the kernel
ran. The firmware SD FAT32 filesystem was visible as `fs0:`, and the
previous UEFI Shell `startup.nsh` again reached the Boot Manager.

At this later point the Hekate USB SD was *reconnected*, so the
"not mounted / no write" preflight note above is historical, not the
current deployment status. The verified FAT32 `D:` and NTFS `L:`
volumes were available.

The new R6.1 ARM64 BootVid image was built from the same source tree
as R5.9 ntoskrnl; the independently prepared, checksum-guarded
`switch-r6.1-bootvid-selector.ps1` variant was already staged by
another concurrent session. To avoid conflicting experimental
artifacts, the existing R6.1 candidate/selector were reused after
independent read-only PE and dependency verification rather than
overwriting the competing candidate.

- Old active `bootvid.dll`: SHA-256
  `53cc4a992e2b318d8f1c0325e1b69f74e1f041b99ea2f5c066d3382ebd5dccfe`,
  1,011,712 bytes.
- Staged and subsequently installed R6.1 `bootvid.dll`: SHA-256
  `2d40aab426d65b858af10ce73105eba45f4f4b60c6056b227aa1f9ba9a1372b6`,
  1,437,696 bytes, PE32+ ARM64.
- New BootVid exports `VidRenderStatusText`. All **16** functions
  imported by R5.9 ntoskrnl from BootVid are present.
- A separate local build of BootVid produced another SHA due to
  nonidentical build artifacts/settings; it was NOT substituted for
  the validated staged R6.1 candidate.
- `-Mode Install` completed successfully. The active
  `L:\Windows\System32\bootvid.dll` readback matches the exact staged
  R6.1 SHA-256. Two verified original backups were created:
  `D:\EFI\LiberNT\Experiments\bootvid-R6.0-original.dll` and
  `C:\Users\Public\Documents\LiberNT-Switch-R0\backups\bootvid-R6.0-original.dll`.
  R6.1 staging copy remains under
  `D:\EFI\LiberNT\Experiments\bootvid-R6.1-matched-kernel.dll`.
- `-Mode Check` passed after installation; hashes of
  R5.9 ntoskrnl, R6.0 HAL, R5.9A WinLoad and R5.2 Boot Manager were
  verified unchanged.

### Important fallback-kernel caveat

A read-only scan of **938** installed System32 DLL, EXE and driver
files identified two BootVid import consumers. Current
`ntoskrnl.exe` imports the 16 symbols now provided by R6.1. Older
fallback `ntkrnlmp.exe` still imports `VidFadeToBlack`, which
the R6.1 BootVid does **not** export (the new BootVid exports
`VidRenderStatusText` and `VidUseCompactFont` instead).
Thus **do not switch to ntkrnlmp.exe while R6.1 BootVid is active**
without restoring the original BootVid or addressing this ABI
difference. Full compatibility is not established.

Additional static dependency-closure check of the active
core component set found **no missing named imports**:

- `ntoskrnl.exe -> hal.dll`: 45/45 present.
- `ntoskrnl.exe -> bootvid.dll`: 16/16 present.
- `ntoskrnl.exe -> kdcom.dll`: 1/1 present.
- `hal.dll -> ntoskrnl.exe`: 78/78 present.
- `bootvid.dll -> ntoskrnl.exe`: 16/16 present.
- `kdcom.dll -> ntoskrnl.exe`: 2/2 present.

These are **static, named-exports checks only**, not a successful
loader run or runtime ABI guarantee.

**R6.1 physical boot result pending.** Safely eject both `D:` and
`L:` in Windows, exit Hekate USB Mass Storage, run
`More Configs -> LiberNT R0 - Mariko UEFI PROBE` and photograph
the first new error, trace, or framebuffer color bars. The firmware
Shell fallback may still execute `startup.nsh`.

The original BootVid can be restored after reconnection:

```powershell
& 'C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r6.1-bootvid-selector.ps1' -Mode Restore
```

No Hekate configuration, Linux partition or internal Nintendo eMMC
was modified as part of the R6.1 BootVid installation. No kernel
execution on real Switch Lite is claimed yet.

## R6.2: repeated BootVid failure traced to preloaded lfbbvid.dll

The real Switch Lite photo after installing R6.1 BootVid still
reported `ntoskrnl.exe needs VidRenderStatusText from bootvid.dll,
which does not export it`. This is a WinLoad module import
failure; it does NOT prove any execution inside the NT kernel.

Read-only source inspection found a decisive alias in
`bootloader/winload/main.c`:

```c
#define OSL_VIDEO_MODULE "bootvid.dll"
#define OSL_FRAMEBUFFER_VIDEO_FILE "lfbbvid.dll"
```

When `FrameBufferData` exists, WinLoad invokes
`PeLoadModule(..., "lfbbvid.dll", "bootvid.dll", ...)`
before `PeResolveImports(..., Kernel)`. Consequently it has already
registered the **lfbbvid.dll file** as the `bootvid.dll` module.
Updating only `L:\Windows\System32\bootvid.dll` in R6.1
could not fix this particular framebuffer boot path.

The mounted Switch NTFS contained exactly one copy:
`L:\Windows\System32\lfbbvid.dll`, original SHA-256
`5c55d64f0ec931fa1cfad8efdb12dd6d463ed6c1243e8202b516da3ead0c6a12`.
Original size: 1,011,712 bytes. Its PE exports do not contain
`VidRenderStatusText`.

Built `drivers/base/bootvid/lfbbvid.dll` for ARM64 using Ninja.
It is PE32+ Native ARM64, SHA-256:
`11eb091cc0af95320deeac504f29051d7bdc4ff8ac6b7da4b856a1933aea3530`.
The new image exports `VidRenderStatusText` and
`VidUseCompactFont`, preserving 14 of the 15 old export names;
like R6.1 BootVid, it does NOT expose old `VidFadeToBlack`.
Static named-import closure checks passed for R5.9 kernel
-> new LFBBVID and new LFBBVID -> R5.9 kernel. This is not
proof that the loader correctly maps or runs the DLL.

The correct R6.2 file was copied with SHA-256 verification into
`C:\Users\Public\Documents\LiberNT-Switch-R0\staging-r6.2\lfbbvid-R6.2.dll`.
A checksum-guarded installer/rollback was committed at
`media/doc/ports/switch-r62-lfbbvid-selector.ps1` and installed
on laptop at
`C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r6.2-lfbbvid-selector.ps1`.
PowerShell parse verification showed zero errors and `-Mode Check`
passed the device labels, BCD and boot component hashes.

The physical Hekate SD was identified as the same non-system USB
disk `hekate SD RAW`, `D:` FAT32 `SWITCH SD`, `L:`
NTFS `LIBERNT-LAB`. `-Mode Install` then successfully
replaced **only** the active
`L:\Windows\System32\lfbbvid.dll`, and readback confirmed
SHA-256 `11eb091c...`.

Original R6.1 LFBBVID was backed up on both laptop SSD and SD FAT32:

- `C:\Users\Public\Documents\LiberNT-Switch-R0\backups\lfbbvid-R6.1-original.dll`
- `D:\EFI\LiberNT\Experiments\lfbbvid-R6.1-original.dll`

R6.2 candidate is also staged on FAT32:
`D:\EFI\LiberNT\Experiments\lfbbvid-R6.2-matched-kernel.dll`.
Independent readback checked all three SHA-256s. WinLoad,
Boot Manager, ntoskrnl R5.9, HAL R6.0, BootVid R6.1 and BCD
remain unchanged. Nintendo eMMC / Linux partition untouched.

**R6.2 physical boot result is pending.** Safely eject D: and L:,
exit Hekate Mass Storage, and launch the original manual
`More Configs -> LiberNT R0 - Mariko UEFI PROBE` profile.
Observe the new error or magenta/cyan/yellow/white kernel
entry bars. There may be further load-time blockers.

Rollback after SD reattachment:

```powershell
& 'C:\Users\Public\Documents\LiberNT-Switch-R0\switch-r6.2-lfbbvid-selector.ps1' -Mode Restore
```

Do not switch to the old `ntkrnlmp.exe` while R6.2 is active:
the old kernel expects `VidFadeToBlack`, which the new
LFBBVID no longer provides.

## Next milestone

**R0 milestone:** capture real-device checkpoints from the current ARM64 `bootmgfw` and verify the BCD device mapping.  
**R1 milestone:** first confirmed `bootmgfw -> winload` transition, followed by first confirmed LiberNT ARM64 kernel entry.  
**R2 milestone:** basic storage, framebuffer and input under the kernel.

The milestone labels here describe planned targets and are distinct from the R0–R5 EFI diagnostic builds above.
