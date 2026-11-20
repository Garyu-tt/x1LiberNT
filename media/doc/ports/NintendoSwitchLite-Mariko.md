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
| R4 / Block I/O preflight | **Staged on microSD but no physical-console result reported yet** | Do not mark Block I/O as verified on Switch. |

**Interpretation limits:** The last line printed before calling `StartImage` is not proof that FreeLoader reaches a particular C function. No debugger backtrace or serial log from the physical console has yet been obtained. The R3 timer can continue to execute even if other parts of startup stall; the absence of an ExitBootServices indicator is not conclusive.

## Emulation evidence (not physical)

In QEMU `virt` with EDK2 and the upstream ARM64 image, FreeLoader loads, reads `freeldr.ini` and finds an OS entry for `partition(2)\Windows`. Separate R4 preflight emulation found virtual EFI Block I/O handles and read valid boot signatures. This verifies only the generic virtual setup. The headless QEMU instance also reports no GOP; this **does not** mean the Switch firmware lacks GOP.

## microSD layout observed during the lab

- FAT32 / Hekate boot volume: approximately **8.59 GB**.
- Experimental NTFS `LIBERNT-LAB` volume: approximately **80.24 GB**.
- Existing separate Linux partition: approximately **37.58 GB**, preserve it.

The second partition holds the extracted LiberNT `Windows` tree, including ARM64 `ntkrnlmp.exe`, `hal.dll`, and registry hives. This is a manually staged test layout, **not** proof that a full OS installation can boot.

## Open engineering questions

1. **Observe R4 on hardware:** enumerate EFI Block I/O handles and read-only probe the NTFS boot sector on the actual Switch, comparing device-path and partition ordering.
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

## Next milestone

**R0 milestone:** reproducibly reach FreeLoader on Switch Lite and capture a physical debug trace clearly identifying its last executed startup stage.  
**R1 milestone:** first confirmed transition into the LiberNT ARM64 kernel.  
**R2 milestone:** basic storage, framebuffer and input under the kernel.

The milestone labels here describe planned targets and are distinct from the R0–R4 EFI diagnostic builds above.
