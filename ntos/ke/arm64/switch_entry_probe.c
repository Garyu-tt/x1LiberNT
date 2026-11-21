#include <ntoskrnl.h>

#ifdef LIBERNT_SWITCH_KERNEL_ENTRY_TRACE
#define SWITCH_PROBE_ALIAS 0xFFFFFC0000000000ULL

VOID NTAPI
KiArm64EntryProbe(PLOADER_PARAMETER_BLOCK LoaderBlock, ULONG Stage)
{
    PLOADER_PARAMETER_FRAMEBUFFER FrameBuffer;
    volatile ULONG *Pixels;
    UINT64 Address;
    ULONG X, Y, Color;

    if (!LoaderBlock || !LoaderBlock->Extension || Stage == 0 || Stage > 4)
        return;

    if (LoaderBlock->Extension->Size <
        FIELD_OFFSET(LOADER_PARAMETER_EXTENSION, GopFramebuffer) +
        sizeof(LOADER_PARAMETER_FRAMEBUFFER))
        return;

    FrameBuffer = &LoaderBlock->Extension->GopFramebuffer;
    if (FrameBuffer->PixelFormat != 32 ||
        FrameBuffer->HorizontalResolution < 320 ||
        FrameBuffer->VerticalResolution < 120 ||
        FrameBuffer->PixelsPerScanLine < FrameBuffer->HorizontalResolution ||
        FrameBuffer->FrameBufferBase.QuadPart == 0 ||
        FrameBuffer->FrameBufferSize <
        (UINT64)44 * FrameBuffer->PixelsPerScanLine * sizeof(ULONG))
        return;

    Address = SWITCH_PROBE_ALIAS | (UINT64)FrameBuffer->FrameBufferBase.QuadPart;
    ARM64_AT(S1E1W, Address);
    ARM64_ISB();
    if (ARM64_READ_SYSREG(PAR_EL1) & 1ULL)
        return;

    Pixels = (volatile ULONG *)(ULONG_PTR)Address;
    Color = Stage == 1 ? 0x00FF00FFUL :
            Stage == 2 ? 0x0000FFFFUL :
            Stage == 3 ? 0x00FFFF00UL : 0x00FFFFFFUL;

    for (Y = 24; Y < 44; Y++)
    {
        volatile ULONG *Row = Pixels + (SIZE_T)Y * FrameBuffer->PixelsPerScanLine +
                              FrameBuffer->HorizontalResolution - Stage * 30 - 12;
        for (X = 0; X < 24; X++)
            Row[X] = Color;
        ARM64_DC(CIVAC, (ULONG_PTR)Row);
        ARM64_DC(CIVAC, (ULONG_PTR)(Row + 23));
    }
    ARM64_DSB(SY);
}
#endif
