/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 loader address space limits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>
#include <arm64.h>
#include <reactos/arm64/early_uart.h>
#include "../../winload/winload.h"

/*
 * Switch Lite pre-kernel handoff checkpoints. Firmware text output is safe
 * ONLY before ExitBootServices; after that, use the already-installed
 * framebuffer debug callback (which itself checks translation validity).
 * Off in normal LiberNT builds.
 */
#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
#define SWITCH_HO_PRE(Message) FwPrint(Message)
#define SWITCH_HO_POST(Message) do { \
    if (Arm64TrapScreenRoutine) Arm64TrapScreenRoutine(Message); \
} while (0)
#else
#define SWITCH_HO_PRE(Message) ((VOID)0)
#define SWITCH_HO_POST(Message) ((VOID)0)
#endif

BOOLEAN Arm64IsKernelVaMapped(ULONGLONG Va);

#include <reactos/arm64/earlyfont.h>

#define OSL_FB_CELL_HEIGHT 14

extern VOID (*Arm64TrapScreenRoutine)(PCSTR Text);
extern UINT8 arm64_exception_vectors[];
extern ULONG_PTR VramAddress;

static volatile ULONG *OslFbBase;
static ULONG OslFbWidth;
static ULONG OslFbHeight;
static ULONG OslFbStride;
static ULONG OslFbScale;
static ULONG OslFbColumns;
static ULONG OslFbRows;
static ULONG OslFbRow;
static ULONG OslFbColumn;
static ULONG OslFbColor;
static BOOLEAN OslFbRowCleared;

static
BOOLEAN
OslFbTranslates(
    _In_ ULONG_PTR Address)
{
    ULONG64 Result;

    if (((ARM64_READ_SYSREG(CURRENTEL) >> 2) & 3) == 2)
        ARM64_AT(S1E2W, Address);
    else
        ARM64_AT(S1E1W, Address);
    ARM64_ISB();
    Result = ARM64_READ_SYSREG(PAR_EL1);
    return (Result & 1) == 0;
}

static
VOID
OslFbFill(
    _In_ volatile ULONG *Pixels,
    _In_ ULONG Count,
    _In_ ULONG Color)
{
    ULONG_PTR Line;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
        Pixels[Index] = Color;
    for (Line = (ULONG_PTR)Pixels & ~63ULL; Line < (ULONG_PTR)(Pixels + Count); Line += 64)
        ARM64_DC(CIVAC, Line);
}

static
VOID
OslFbPuts(
    _In_ PCSTR Text)
{
    ULONG Y, X, SubY, SubX;

    if (!OslFbBase)
        return;

    if (!OslFbTranslates((ULONG_PTR)OslFbBase) ||
        !OslFbTranslates((ULONG_PTR)(OslFbBase + (SIZE_T)(OslFbHeight - 1) * OslFbStride)))
    {
        volatile ULONG *Alias = (volatile ULONG *)(ARM64_PHYS_MAP_BASE | (ULONG_PTR)OslFbBase);

        if (!OslFbTranslates((ULONG_PTR)Alias) ||
            !OslFbTranslates((ULONG_PTR)(Alias + (SIZE_T)(OslFbHeight - 1) * OslFbStride)))
        {
            return;
        }
        OslFbBase = Alias;
    }

    for (; *Text; Text++)
    {
        if (*Text == '\n' || OslFbColumn >= OslFbColumns)
        {
            OslFbColumn = 0;
            OslFbRow = (OslFbRow + 1) % OslFbRows;
            OslFbRowCleared = FALSE;
            if (*Text == '\n')
                continue;
        }

        if (!OslFbRowCleared)
        {
            for (Y = 0; Y < OSL_FB_CELL_HEIGHT * OslFbScale; Y++)
                OslFbFill(OslFbBase + (SIZE_T)(OslFbRow * OSL_FB_CELL_HEIGHT * OslFbScale + Y) * OslFbStride, OslFbWidth, 0);
            OslFbRowCleared = TRUE;
        }

        for (Y = 0; Y < EARLY_FONT_HEIGHT; Y++)
        {
            UCHAR Bits = ((UCHAR)*Text >= EARLY_FONT_FIRST_CHAR && (UCHAR)*Text <= EARLY_FONT_LAST_CHAR) ?
                         EarlyFont8x13[(UCHAR)*Text - EARLY_FONT_FIRST_CHAR][Y] : 0xFF;

            for (SubY = 0; SubY < OslFbScale; SubY++)
            {
                volatile ULONG *Pixels = OslFbBase +
                                         (SIZE_T)(OslFbRow * OSL_FB_CELL_HEIGHT * OslFbScale + Y * OslFbScale + SubY) * OslFbStride +
                                         OslFbColumn * EARLY_FONT_WIDTH * OslFbScale;

                for (X = 0; X < EARLY_FONT_WIDTH; X++)
                {
                    for (SubX = 0; SubX < OslFbScale; SubX++)
                        Pixels[X * OslFbScale + SubX] = (Bits & (0x80 >> X)) ? OslFbColor : 0;
                }
                for (X = 0; X < EARLY_FONT_WIDTH * OslFbScale; X += 16)
                    ARM64_DC(CIVAC, (ULONG_PTR)(Pixels + X));
            }
        }
        OslFbColumn++;
    }
    ARM64_DSB(SY);
}

static
VOID
OslFbPrepare(VOID)
{
    if (!FrameBufferData || !VramAddress || FrameBufferData->BitsPerPixel != 32 ||
        FrameBufferData->ScreenWidth < 320 || FrameBufferData->ScreenHeight < 200 ||
        FrameBufferData->PixelsPerScanLine < FrameBufferData->ScreenWidth)
    {
        return;
    }

    OslFbWidth = FrameBufferData->ScreenWidth;
    OslFbHeight = FrameBufferData->ScreenHeight;
    OslFbStride = FrameBufferData->PixelsPerScanLine;
    OslFbScale = (OslFbWidth >= 2048) ? 2 : 1;
    OslFbColumns = OslFbWidth / (EARLY_FONT_WIDTH * OslFbScale);
    OslFbRows = OslFbHeight / (OSL_FB_CELL_HEIGHT * OslFbScale);
    OslFbRow = 0;
    OslFbRowCleared = FALSE;
    OslFbColumn = 0;
    OslFbColor = FrameBufferData->PixelMasks.RedMask | FrameBufferData->PixelMasks.GreenMask |
                 FrameBufferData->PixelMasks.BlueMask;
    if (!OslFbColor)
        OslFbColor = 0x00FFFFFF;
    OslFbBase = (volatile ULONG *)(VramAddress + FrameBufferData->FrameBufferOffset);
    Arm64TrapScreenRoutine = OslFbPuts;
}

const PFN_NUMBER MmArchMaxLoaderPage = MM_MAX_PAGE_LOADER;
const PFN_NUMBER MmArchMaxRamdiskPage = MM_MAX_PAGE_LOADER;

void _exituefi(VOID);
void _changestack(VOID);

BOOLEAN
EarlyUartInitialize(
    UINT64 UartBaseOverride);

const PCSTR OslArchSystemIdentifier = "ARM processor family";

static
VOID
Arm64LateDebugPutChar(
    UCHAR Character)
{
    EarlyUartPutc(Character);
}

VOID
OslArchAddConfiguration(
    _In_ PCONFIGURATION_COMPONENT_DATA System)
{
    UNREFERENCED_PARAMETER(System);
}

TYPE_OF_MEMORY
OslArchDescriptorType(
    _In_ PFN_NUMBER Page,
    _In_ TYPE_OF_MEMORY Type)
{
    UNREFERENCED_PARAMETER(Page);
    return Type;
}

BOOLEAN
OslArchMapFirmwareRuntime(
    _In_ ULONGLONG PhysicalStart,
    _In_ ULONGLONG PageCount,
    _In_ BOOLEAN Device,
    _In_ BOOLEAN Executable,
    _Out_ PULONGLONG VirtualStart)
{
    ULONGLONG Size = PageCount << MM_PAGE_SHIFT;
    ULONG Attributes;

    if (Device)
        Attributes = ARM64_MAP_ATTR_DEVICE;
    else
        Attributes = ARM64_MAP_ATTR_NORMAL | (Executable ? ARM64_MAP_ATTR_EXECUTE : 0);

    *VirtualStart = ARM64_KSEG0_BASE + PhysicalStart;
    return Arm64MapVirtualMemory(PhysicalStart, PhysicalStart, Size, Attributes) &&
           Arm64MapVirtualMemory(*VirtualStart, PhysicalStart, Size, Attributes);
}

VOID
OslArchInitialize(VOID)
{
    Arm64DescribeDeviceTreePlatform();
    EarlyUartInitialize(0);
    FwLateDebugPutChar = Arm64LateDebugPutChar;
    Arm64InstallAppleSmbios();
}

VOID
OslArchPlaceRamdisk(VOID)
{
}

VOID
FwExitBootServices(VOID)
{
    _exituefi();
}

VOID
OslArchSwitchStack(VOID)
{
    _changestack();
}

BOOLEAN
OslArchPrepareHandoff(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    SWITCH_HO_PRE(L"[SW-HO00] WinLdrSetupMachineDependent begin\r\n");
    WinLdrSetupMachineDependent(LoaderBlock);
    SWITCH_HO_PRE(L"[SW-HO01] Machine-dependent setup completed\r\n");

    SWITCH_HO_PRE(L"[SW-HO02] WinLdrSetupMemoryLayout begin\r\n");
    if (!WinLdrSetupMemoryLayout(LoaderBlock))
    {
        SWITCH_HO_PRE(L"[SW-HO-ERR] WinLdrSetupMemoryLayout returned FALSE\r\n");
        return FALSE;
    }
    SWITCH_HO_PRE(L"[SW-HO03] WinLdrSetupMemoryLayout completed\r\n");

    SWITCH_HO_PRE(L"[SW-HO04] Arm64PreparePageTables begin\r\n");
    Arm64PreparePageTables();
    SWITCH_HO_PRE(L"[SW-HO05] Arm64PreparePageTables completed\r\n");

    if (FrameBufferData && VramAddress)
    {
        ULONGLONG FrameBase = VramAddress + FrameBufferData->FrameBufferOffset;
        ULONGLONG FrameEnd = FrameBase + (ULONGLONG)FrameBufferData->PixelsPerScanLine * FrameBufferData->ScreenHeight * sizeof(ULONG);

        SWITCH_HO_PRE(L"[SW-HO06] Framebuffer alias mapping begin\r\n");
        FrameBase &= ~((ULONGLONG)MM_PAGE_SIZE - 1);
        FrameEnd = (FrameEnd + MM_PAGE_SIZE - 1) & ~((ULONGLONG)MM_PAGE_SIZE - 1);
        if (!Arm64IsKernelVaMapped(ARM64_PHYS_MAP_BASE | FrameBase) &&
            !Arm64MapVirtualMemory(ARM64_PHYS_MAP_BASE | FrameBase, FrameBase, FrameEnd - FrameBase, ARM64_MEM_ATTR_NORMAL_WC))
        {
            SWITCH_HO_PRE(L"[SW-HO-WARN] Framebuffer alias mapping failed\r\n");
            ERR("winload: framebuffer alias mapping failed\n");
        }
        SWITCH_HO_PRE(L"[SW-HO07] Framebuffer alias mapping returned\r\n");
    }
    else
    {
        SWITCH_HO_PRE(L"[SW-HO07] No framebuffer data to map\r\n");
    }

    SWITCH_HO_PRE(L"[SW-HO08] OslFbPrepare begin\r\n");
    OslFbPrepare();
    SWITCH_HO_PRE(L"[SW-HO09] OslFbPrepare completed\r\n");

    /* This is the last guaranteed EFI console checkpoint. */
    SWITCH_HO_PRE(L"[SW-HO10] ExitBootServices begin\r\n");
    FwExitBootServices();

    /* Never use firmware FwPrint after ExitBootServices. */
    SWITCH_HO_POST("[SW-HO11] ExitBootServices returned\n");
    Arm64StopFirmwareWatchdog();
    SWITCH_HO_POST("[SW-HO12] Firmware watchdog stopped\n");
    Arm64SetVbarAuto((ULONG_PTR)arm64_exception_vectors);
    SWITCH_HO_POST("[SW-HO13] Exception vectors set\n");
    WinLdrSetProcessorContext(OSL_NT_VERSION);
    SWITCH_HO_POST("[SW-HO14] Processor context set\n");
    RtlZeroMemory((PVOID)KI_USER_SHARED_DATA, MM_PAGE_SIZE);
    SWITCH_HO_POST("[SW-HO15] User shared page cleared\n");
    return TRUE;
}

DECLSPEC_NORETURN
VOID
OslArchStartKernel(
    _In_ PVOID EntryPoint,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_BLOCK Physical;

    SWITCH_HO_POST("[SW-J00] OslArchStartKernel entered\n");
    Physical = VaToPa(LoaderBlock);
    SWITCH_HO_POST("[SW-J01] Loader block VA-to-PA returned\n");

    /*
     * No firmware calls after EBS. Validate the stack pointer access
     * separately from the following assembly jump; only the experiment
     * emits these checkpoints.
     */
#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
    {
        ULONGLONG KernelStack = (ULONGLONG)(ULONG_PTR)Physical->KernelStack;

        SWITCH_HO_POST("[SW-J02] Kernel stack pointer fetched\n");
        SWITCH_HO_POST("[SW-J03] Arm64JumpToKernel call next\n");
        Arm64JumpToKernel((ULONGLONG)(ULONG_PTR)EntryPoint,
                          (ULONGLONG)(ULONG_PTR)LoaderBlock,
                          KernelStack);
    }
#else
    Arm64JumpToKernel((ULONGLONG)(ULONG_PTR)EntryPoint,
                      (ULONGLONG)(ULONG_PTR)LoaderBlock,
                      (ULONGLONG)Physical->KernelStack);
#endif

    for (;;)
        ;
}

/*
 * Called only from the instrumented ARM64 jump wrapper while it still
 * has a valid, 16-byte aligned stack. It must not use firmware services.
 */
#ifdef LIBERNT_SWITCH_HANDOFF_TRACE
VOID
Arm64KernelJumpTrace(
    _In_ ULONG Stage)
{
    if (!Arm64TrapScreenRoutine)
        return;

    switch (Stage)
    {
        case 0:
            Arm64TrapScreenRoutine("[SW-A00] Jump assembly entry\n");
            break;
        case 1:
            Arm64TrapScreenRoutine("[SW-A01] KSEG0 alias reached\n");
            break;
        case 2:
            Arm64TrapScreenRoutine("[SW-A02] Kernel stack selected\n");
            break;
        case 3:
            Arm64TrapScreenRoutine("[SW-A03] Interrupts masked\n");
            break;
        case 4:
            Arm64TrapScreenRoutine("[SW-A04] Cache maintenance begin\n");
            break;
        case 5:
            Arm64TrapScreenRoutine("[SW-A05] Cache maintenance returned\n");
            break;
        case 6:
            Arm64TrapScreenRoutine("[SW-A06] TTBR0 identity clear begin\n");
            break;
        case 7:
            Arm64TrapScreenRoutine("[SW-A07] TTBR0 clear returned\n");
            break;
        case 8:
            Arm64TrapScreenRoutine("[SW-A08] Final kernel branch next\n");
            break;
        case 9:
            Arm64TrapScreenRoutine("[SW-A09] Kernel entry VA translation FAULT\n");
            break;
        case 10:
            Arm64TrapScreenRoutine("[SW-A10] Kernel entry VA readable\n");
            break;
        default:
            break;
    }
}
#endif

BOOLEAN
OslArchPrepareResume(
    _In_ PLNT_HIBER_HEADER Header,
    _In_ ULONG64 HighestPage)
{
    UNREFERENCED_PARAMETER(Header);
    UNREFERENCED_PARAMETER(HighestPage);
    return FALSE;
}

DECLSPEC_NORETURN
VOID
OslArchResume(
    _In_ ULONG64 Operations)
{
    UNREFERENCED_PARAMETER(Operations);

    for (;;)
        ;
}
