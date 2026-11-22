# LiberNT Switch Lite R6.5: trap at the first NT kernel instruction

Hardware evidence from R6.4.1 (November 22, 2026) shows `[SW-A10] Kernel entry VA readable`, followed by `ESR=0xF2004E54`. That proves the deliberate WinLoad `BRK #0x4E54` was executed and its post-TTBR0 synchronous exception was displayed. It **does not** establish that the NT entrypoint executed.

R6.5 replaces only the **first instruction of the exact installed ARM64 `ntoskrnl.exe` entrypoint** with `BRK #0x4E55` (AArch64 instruction bytes `A0 CA 29 D4` at the verified file offset `0x12750`). It fixes the kernel PE checksum, verifies that no other body bytes changed, and restores the original R5.9A WinLoad.

## Target and safeguards

- Intended for the experimental `D: SWITCH SD` FAT32 + `L: LIBERNT-LAB` NTFS partitions presented over Hekate USB Mass Storage. Neither Windows' internal disk nor Switch eMMC is touched.
- `switch-r65-kernel-entry-probe.py` requires the neighboring R6.4.1 `switch-r641-bootmgr-checksum.py` (or the previously staged Windows TEMP helper). It verifies actual USB disk identity and pinned SHA-256 hashes before writes.
- Original kernel SHA-256: `D35605F76C2F8D235273209A5FE924E1A63375F42F1F679F2EC47DE3E04200B9`.
- Restored WinLoad SHA-256: `6DC1602D4D96255A261AFA2F29E1946BA1010CD498C679D3C84F0AD6FBA47375`.
- Patched kernel SHA-256: `6811D9DADC3DF50F01003DFCA343D34A20EECC5E53BC3E0C76C059AAAC2649E1`.
- Original kernel is kept on both experimental SD volumes and the atomic-swap backup. A successful diagnostic **must not boot NT**.

## Command interface (when the two SD volumes are mounted)

```powershell
py -3 D:\EFI\LiberNT\Experiments\R65-kernel-entry-probe.py --dry-run
py -3 D:\EFI\LiberNT\Experiments\R65-kernel-entry-probe.py --install
py -3 D:\EFI\LiberNT\Experiments\R65-kernel-entry-probe.py --rollback
```

Read back hashes after any install/rollback. If USB Mass Storage is not connected, do not force mounting or invoke raw disk writes. Eject USB Mass Storage safely before a physical boot.

## Expected outcome

- `ESR=0xF2004E55`: the processor fetched and executed the instruction at the **NT kernel entrypoint**, and the exception handler is alive. Inspect the trap's `ELR` to correlate with the actual relocated kernel entry VA.
- Instruction abort or mapping fault: inspect `ESR_EL1`, `FAR_EL1`, `ELR_EL1`, TTBR1 executable PTEs and current EL.
- `[SW-A10]` only: the transition to the kernel has not been demonstrated; check whether the correct kernel and normal WinLoad are deployed.

## Confirmed live SD state

The R6.5 installer returned `R6.5_INSTALLED_OK` via authenticated JediControl. Independent SD readback confirmed the patched kernel and original WinLoad hashes above; verified backup copies exist on both SD partitions. This is **installation verification only**, not a physical R6.5 boot result. Hardware test requires a new photo.

For host regression tests: `python3 -m unittest -v test_switch_r65_kernel_entry_probe.py`.
