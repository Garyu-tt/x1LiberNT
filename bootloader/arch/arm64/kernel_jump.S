/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later
 * PURPOSE:     ARM64 kernel jump wrapper with proper handoff
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

.text
.align 4

.extern Arm64CompleteCacheMaintenance

#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
.extern Arm64KernelJumpTrace

/*
 * Debug-only framebuffer checkpoint. Preserve the live x19-x21 state,
 * LR and stack alignment. Do NOT invoke UEFI or write to internal eMMC.
 * Do not add a call after TTBR0 removal without separately checking
 * that code, stack and framebuffer data are accessible under TTBR1.
 */
.macro SWITCH_ASM_MARK stage
    stp x19, x20, [sp, #-32]!
    stp x21, x30, [sp, #16]
    mov w0, #\stage
    bl Arm64KernelJumpTrace
    ldp x21, x30, [sp, #16]
    ldp x19, x20, [sp], #32
.endm
#endif

/*
 * Arm64JumpToKernel(KernelEntry, LoaderBlockVA, KernelStack)
 * x0 = Kernel entry point (virtual address)
 * x1 = LoaderBlock (virtual address)
 * x2 = Kernel stack (virtual address)
 *
 * This ensures a clean handoff to the kernel with:
 * - Proper stack switch to kernel virtual address
 * - Cache and TLB maintenance
 * - Correct register state
 */
.global Arm64JumpToKernel
Arm64JumpToKernel:
#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
    /* Before modifying x19-x21, prove assembly entry while retaining ABI. */
    stp x0, x1, [sp, #-32]!
    stp x2, x30, [sp, #16]
    mov w0, #0
    bl Arm64KernelJumpTrace
    ldp x2, x30, [sp, #16]
    ldp x0, x1, [sp], #32
#endif

    /* Save parameters */
    mov x19, x0     /* Kernel entry point */
    mov x20, x1     /* LoaderBlock */
    mov x21, x2     /* Kernel stack */

    /*
     * Continue through the loader's KSEG0 alias before dropping TTBR0.  The
     * loader was entered through identity mappings, but real hardware faults as
     * soon as those mappings are invalidated while PC/SP still depend on them.
     */
    adr x16, .Lkseg0_handoff
    movz x17, #0xFFFF, lsl #48
    movk x17, #0xF800, lsl #32
    orr x16, x16, x17
    br x16

.Lkseg0_handoff:
#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
    /* Last checkpoint before switching to the kernel stack. */
    SWITCH_ASM_MARK 1
#endif

    /* Ensure all memory accesses complete */
    dsb ish
    isb

    /* Switch to kernel stack if provided */
    cmp x21, #0
    b.eq 1f
    /* Align stack to 16 bytes using temp register */
    mov x22, x21
    bic x22, x22, #0xF  /* Clear low 4 bits to ensure 16-byte alignment */
    mov sp, x22     /* Switch to aligned kernel stack */
1:
#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
    /* Probe the new stack while TTBR0 identity mapping still exists. */
    SWITCH_ASM_MARK 2
#endif

    /* Mask interrupts during the hand-off */
    msr daifset, #0xF
#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
    SWITCH_ASM_MARK 3
    SWITCH_ASM_MARK 4
#endif

    /* Flush caches to ensure kernel sees consistent memory */
    bl Arm64CompleteCacheMaintenance
#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
    SWITCH_ASM_MARK 5
    SWITCH_ASM_MARK 6
#endif

    /*
     * Now PC and SP are both in TTBR1/KSEG0 space, so clearing TTBR0 can no
     * longer kill the loader return path before the kernel receives control.
     */
    bl Arm64ClearIdentityMappings
#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
    /*
     * Diagnostic only. Calling the framebuffer helper here also tests
     * execution of its code and stack via TTBR1 after TTBR0 was removed.
     * If the marker is absent, do not attribute the fault to identity
     * clearing alone: the probe itself can influence the transition.
     */
    SWITCH_ASM_MARK 7
#endif

    /* Debug: Check TTBR1 and TCR right before jump */
    mrs x4, ttbr1_el1
    mrs x5, tcr_el1
    mrs x6, sctlr_el1

    /* Ensure all previous operations complete */
    dsb sy
    isb

    /* Try AT instruction to test translation */
    movz x7, #0xFFFF, lsl #48   /* Load 0xFFFFF80000000000 */
    movk x7, #0xF800, lsl #32
    movk x7, #0x4216, lsl #16   /* Add 0x42160000 */
    movk x7, #0x0800, lsl #0    /* Final: 0xFFFFF80042160800 */

    /* Use AT S1E1R to check if address translates */
    /* at s1e1r, x7 - commented out for now as it might cause exception */

    /* Setup parameters for kernel */
    mov x0, x20     /* LoaderBlock in x0 */
    mov x1, #0      /* Clear x1 */
    mov x2, #0      /* Clear x2 */
    mov x3, #0      /* Clear x3 */

    /* Add final synchronization */
    dsb sy
    isb

    /* Ensure TLBs are completely flushed */
    tlbi vmalle1is
    dsb ish
    isb

    /* One more check - read back TTBR1 to ensure it's set */
    mrs x8, ttbr1_el1

    /* Final barriers */
    dsb sy
    isb

    /* Direct branch to kernel */
#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
    SWITCH_ASM_MARK 8

    /*
     * R5.9A: test translation of the REAL kernel entry PC (x19), not
     * a hardcoded address. AT only reports Stage-1 read translation
     * and does not prove instruction execute permission.
     */
    at s1e1r, x19
    isb
    mrs x22, par_el1
    tbnz x22, #0, .Lswitch_entry_va_fault
    SWITCH_ASM_MARK 10
#endif
    br x19

    /* Should never reach here */
    b .

#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
.Lswitch_entry_va_fault:
    SWITCH_ASM_MARK 9
.Lswitch_entry_halt:
    wfe
    b .Lswitch_entry_halt
#endif
