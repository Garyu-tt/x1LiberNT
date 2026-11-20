/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Entry point and helpers
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#include <uefildr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/* GLOBALS ********************************************************************/

EFI_HANDLE GlobalImageHandle;
EFI_SYSTEM_TABLE *GlobalSystemTable;
PVOID UefiServiceStack;
PVOID BasicStack;
PVOID UefiFirmwareStackFrame;

void _changestack(VOID);

/*
 * Experimental, opt-in UEFI entry trace for real ARM64 hardware.
 * The default FreeLoader build is unaffected. Enable with
 * -DLIBERNT_SWITCH_EFI_TRACE in an isolated Debug build only.
 *
 * Uses firmware text output: no hardcoded Tegra address or display layout.
 * A checkpoint proves that the previous operation returned, not that the
 * kernel has been entered.
 */
#ifdef LIBERNT_SWITCH_EFI_TRACE
static VOID
UefiTraceStage(
    _In_ CONST CHAR16 *Message)
{
    if (GlobalSystemTable &&
        GlobalSystemTable->ConOut &&
        GlobalSystemTable->ConOut->OutputString)
    {
        GlobalSystemTable->ConOut->OutputString(
            GlobalSystemTable->ConOut,
            (CHAR16 *)Message);
    }
}
#define UEFI_TRACE_STAGE(text) UefiTraceStage(text)
#else
#define UEFI_TRACE_STAGE(text) ((void)0)
#endif

/* FUNCTIONS ******************************************************************/

EFI_STATUS
EfiEntry(
    _In_ EFI_HANDLE ImageHandle,
    _In_ EFI_SYSTEM_TABLE *SystemTable)
{
    PCSTR CmdLine = ""; // FIXME: Determine a command-line from UEFI boot options

    GlobalImageHandle = ImageHandle;
    GlobalSystemTable = SystemTable;
    UefiFirmwareStackFrame = &CmdLine;
    UEFI_TRACE_STAGE(L"[SW-00] EfiEntry entered\r\n");

#if DBG
    UEFI_TRACE_STAGE(L"[SW-01] UefiLogInitialize begin\r\n");
    UefiLogInitialize();
    UEFI_TRACE_STAGE(L"[SW-02] UefiLogInitialize done\r\n");
#endif

    /* Load the default settings from the command-line */
    UEFI_TRACE_STAGE(L"[SW-03] LoadSettings begin\r\n");
    LoadSettings(CmdLine);
    UEFI_TRACE_STAGE(L"[SW-04] LoadSettings done\r\n");

    /* Debugger pre-initialization */
    UEFI_TRACE_STAGE(L"[SW-05] DebugInit begin\r\n");
    DebugInit(BootMgrInfo.DebugString);
    UEFI_TRACE_STAGE(L"[SW-06] DebugInit done\r\n");

    UEFI_TRACE_STAGE(L"[SW-07] MachInit begin\r\n");
    MachInit(CmdLine);
    UEFI_TRACE_STAGE(L"[SW-08] MachInit done\r\n");

    /* UI pre-initialization */
    UEFI_TRACE_STAGE(L"[SW-09] UiInitialize begin\r\n");
    if (!UiInitialize(FALSE))
    {
        UiMessageBoxCritical("Unable to initialize UI.");
        goto Quit;
    }
    UEFI_TRACE_STAGE(L"[SW-10] UiInitialize done\r\n");

    /* Initialize memory manager */
    UEFI_TRACE_STAGE(L"[SW-11] MmInitializeMemoryManager begin\r\n");
    if (!MmInitializeMemoryManager())
    {
        UiMessageBoxCritical("Unable to initialize memory manager.");
        goto Quit;
    }
    UEFI_TRACE_STAGE(L"[SW-12] MmInitializeMemoryManager done\r\n");

    /* Initialize I/O subsystem */
    UEFI_TRACE_STAGE(L"[SW-13] FsInit begin\r\n");
    FsInit();
    UEFI_TRACE_STAGE(L"[SW-14] FsInit done\r\n");

    /* Initialize the module list */
    UEFI_TRACE_STAGE(L"[SW-15] PeLdrInitializeModuleList begin\r\n");
    if (!PeLdrInitializeModuleList())
    {
        UiMessageBoxCritical("Unable to initialize module list.");
        goto Quit;
    }
    UEFI_TRACE_STAGE(L"[SW-16] PeLdrInitializeModuleList done\r\n");

    UEFI_TRACE_STAGE(L"[SW-17] MachInitializeBootDevices begin\r\n");
    if (!MachInitializeBootDevices())
    {
        UiMessageBoxCritical("Error when detecting hardware.");
        goto Quit;
    }
    UEFI_TRACE_STAGE(L"[SW-18] MachInitializeBootDevices done\r\n");

    /* 0x32000 is what UEFI defines, but we can go smaller if we want */
    SIZE_T StackSize = 0x32000;
    UEFI_TRACE_STAGE(L"[SW-19] Allocate OS loader stack begin\r\n");
    PVOID AllocatedStackMem = MmAllocateMemoryWithType(StackSize, LoaderOsloaderStack);

    if (!AllocatedStackMem)
    {
        UiMessageBoxCritical("Unable to allocate OS loader stack.");
        goto Quit;
    }

    /* Stacks grow downward so set the initial stack pointer to the top of the allocated region */
    BasicStack = (PVOID)((ULONG_PTR)AllocatedStackMem + StackSize);
    UEFI_TRACE_STAGE(L"[SW-20] Stack allocated; change stack next\r\n");
    _changestack();

Quit:
    UEFI_TRACE_STAGE(L"[SW-FF] Startup failed; reboot path\r\n");
    /* If we reach this point, something went wrong before, therefore reboot */
    Reboot();

    UNREACHABLE;
    return 0;
}

DECLSPEC_NORETURN
void
ExecuteLoaderCleanly(PVOID PreviousStack)
{
    TRACE("ExecuteLoaderCleanly Entry\n");
    UefiServiceStack = PreviousStack;
    UEFI_TRACE_STAGE(L"[SW-21] ExecuteLoaderCleanly reached\r\n");

    UEFI_TRACE_STAGE(L"[SW-22] RunLoader begin\r\n");
    RunLoader();
    Reboot();
    UNREACHABLE;
}

DECLSPEC_NORETURN
VOID __cdecl Reboot(VOID)
{
    WARN("Performing cold reset\n");

    if (GlobalSystemTable && GlobalSystemTable->RuntimeServices)
    {
        /* Attempt a native firmware cold reset */
        GlobalSystemTable->RuntimeServices->ResetSystem(
            EfiResetCold,
            EFI_SUCCESS,
            0,
            NULL
        );
    }

    /* Fallback dead-loop if runtime services are missing or fail to respond */
    for (;;)
    {
#if defined(_M_IX86) || defined(_M_AMD64)
        __halt();
#else
        /* Fallback placeholder loop for other platforms */
        NOTHING;
#endif
    }
}
