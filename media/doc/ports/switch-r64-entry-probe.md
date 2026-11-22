# Switch Lite R6.4: opt-in post-TTBR0 exception-vector probe

**Status:** source-only diagnostic; intentionally non-booting when armed.

The existing R5.9A loader prints `[SW-A10] Kernel entry VA readable` and
then branches to the NT ARM64 entrypoint. The `AT S1E1R` probe is a
**read-permission** translation test. It does not confirm instruction execute
permission, nor does an import/export check show that kernel code executed.

R6.4 inserts a conditional `brk #0x4e54` **between** the successful
`[SW-A10]` marker and `br x19`, strictly when the separate diagnostic
`LIBERNT_SWITCH_ENTRY_BRK_PROBE` macro is defined. The unconditional
normal boot path and the existing R5.9A instrumentation are unchanged.

This deliberately triggers a synchronous exception at EL1 to determine
whether the exception-vector (VBAR), stack, mappings and framebuffer trap
reporter survive the TTBR0 transition. The handler should report a BRK
exception (typically ESR_EL1 EC 0x3c) and an ELR pointing to the probe.
The machine is **not expected to boot the kernel in this mode**.

## Experiment build (only after reviewing the toolchain target)

Compile **only** the experimental WinLoad image with both:

- `LIBERNT_SWITCH_HANDOFF_TRACE`
- `LIBERNT_SWITCH_ENTRY_BRK_PROBE`

Enabling the probe without the handoff trace is a hard compilation error.
Do not enable it for normal distribution binaries. Before a physical test,
confirm the actual loader path and hashes, preserve the existing boot image
and rollback files, and deploy the probe as a distinct inactive artifact.
Do not replace Hekate, firmware partitions, eMMC or the default SD loader
as part of this source-only PR.

Run host regression tests with:

```sh
python3 -m unittest -v media/doc/ports/test_switch_r64_entry_brk_probe.py
```

The tests use `clang --target=aarch64-none-elf` when available. They
verify the default and trace-only builds contain **no** diagnostic BRK,
the opt-in build contains exactly one, and a build enabling only the
probe fails closed.

## Interpretation

- `[SW-A10]` followed by an ARM64 BRK trap frame: the current loader's
  exception-reporting path works after TTBR0 removal. Investigate kernel
  executable mappings, EL1 setup and the first ARM64 kernel instructions.
- `[SW-A10]` with no BRK trap frame: inspect VBAR, vector mappings,
  handler stack and display path. Absence of output does not uniquely
  identify a cause.
- No `[SW-A10]`: inspect earlier handoff markers and whether the correct
  probe-enabled WinLoad was actually launched.

**Hardware status:** no R6.4 image has been built, flashed or booted by this
patch alone. A successful host assembly test is not a successful Switch boot.
