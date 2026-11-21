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

## Next milestone

**R0 milestone:** capture real-device checkpoints from the current ARM64 `bootmgfw` and verify the BCD device mapping.  
**R1 milestone:** first confirmed `bootmgfw -> winload` transition, followed by first confirmed LiberNT ARM64 kernel entry.  
**R2 milestone:** basic storage, framebuffer and input under the kernel.

The milestone labels here describe planned targets and are distinct from the R0–R5 EFI diagnostic builds above.
