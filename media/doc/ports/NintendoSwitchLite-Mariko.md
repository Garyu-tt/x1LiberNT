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

## Next milestone

**R0 milestone:** capture real-device checkpoints from the current ARM64 `bootmgfw` and verify the BCD device mapping.  
**R1 milestone:** first confirmed `bootmgfw -> winload` transition, followed by first confirmed LiberNT ARM64 kernel entry.  
**R2 milestone:** basic storage, framebuffer and input under the kernel.

The milestone labels here describe planned targets and are distinct from the R0–R5 EFI diagnostic builds above.
