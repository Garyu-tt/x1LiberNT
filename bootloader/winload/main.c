/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Operating system loader main routine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"

/* Experimental Switch Lite ARM64 entry trace; absent from regular builds. */
#ifdef LIBERNT_SWITCH_WINLOAD_TRACE
#define OSL_SWITCH_TRACE(Text) FwPrint(Text)
#else
#define OSL_SWITCH_TRACE(Text) ((VOID)0)
#endif

#define OSL_STACK_SIZE    0x40000
#define OSL_MAX_ENTRIES   16
#define OSL_KERNEL_MODULE "ntoskrnl.exe"
#define OSL_HAL_MODULE    "hal.dll"
#define OSL_VIDEO_MODULE  "bootvid.dll"
#define OSL_FRAMEBUFFER_VIDEO_FILE "lfbbvid.dll"
#define OSL_RAMDISK_SIZE_OPTION "RDRAMSIZE="
#define OSL_DOWNLOAD_URL_OPTION "HTTPBOOTURL="
#define OSL_DOWNLOAD_ADDRESS_OPTION "HTTPBOOTIP="
#define OSL_SYNC_URL_OPTION "HTTPSYNCURL="
#define OSL_INSTALL_IMAGE_OPTION "INSTALLIMAGE="
#define OSL_INSTALL_HASH_OPTION "INSTALLSHA256="
#define OSL_ADDRESS_CHARS 64
#define OSL_HASH_CHARS    80
#define OSL_DEBUG_PORT_OPTION "DEBUGPORT="
#define OSL_DEBUG_MODULE  "kdcom.dll"
#define OSL_NETWORK_DEBUG_FILE "kdnet.dll"
#define OSL_STARTUP_PREFIX_CHARS (RTL_NUMBER_OF(BM_STARTUP_OPTION) - 1)

PVOID BasicStack;
PVOID UefiServiceStack;

static const PCWSTR OslStorePaths[] =
{
    L"\\EFI\\LiberNT\\Boot\\BCD",
    L"\\EFI\\Microsoft\\Boot\\BCD",
    L"\\Boot\\BCD",
};

static const PCSTR OslNetworkDebugExtensions[] =
{
    "kd_02_10ec.dll",
    "kd_02_1af4.dll",
    "kd_02_8086.dll",
    "kd_8003_1055.dll",
    "kd_8003_1de4.dll",
    "kd_fdt_apple_t8103-dwc3.dll",
    "kd_fdt_spacemit_k1x-emac.dll",
    "kd_usbnet.dll"
};

static BM_ENTRY OslEntries[OSL_MAX_ENTRIES];
static OSL_PARAMETERS OslParameters;
static BM_STARTUP OslStartup;

VOID
OslTrace(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Text[OSL_OPTIONS_CHARS];
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Text, sizeof(Text) - 1, Format, Arguments);
    va_end(Arguments);

    RtlStringCbCatA(Text, sizeof(Text), "\n");
    FwDebugPrint(Text);
}

VOID
UiMessageBox(
    _In_ PCSTR Format,
    ...)
{
    CHAR Text[OSL_OPTIONS_CHARS];
    va_list Arguments;

    va_start(Arguments, Format);
    RtlStringCbVPrintfA(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);

    FwPrint(L"%hs\r\n", Text);
}

static
BOOLEAN
OslNarrow(
    _Out_writes_z_(Chars) PSTR Buffer,
    _In_ ULONG Chars,
    _In_ PCWSTR Text)
{
    ULONG i;

    for (i = 0; Text[i] != UNICODE_NULL; i++)
    {
        if (i >= Chars - 1 || Text[i] > 0x7F)
            return FALSE;
        Buffer[i] = (CHAR)Text[i];
    }

    Buffer[i] = ANSI_NULL;
    return TRUE;
}

static
VOID
OslParseStartup(
    _In_reads_(Chars) PCWSTR Text,
    _In_ ULONG Chars)
{
    ULONG i;

    OslStartup.Profile = 0;

    for (i = 0; i < Chars && Text[i] >= L'0' && Text[i] <= L'9'; i++)
        OslStartup.Profile = OslStartup.Profile * 10 + (Text[i] - L'0');
}

static
PBM_ENTRY
OslSelectEntry(VOID)
{
    WCHAR StorePath[OSL_PATH_CHARS] = L"";
    WCHAR Id[BCD_ID_CHARS] = L"";
    PCWSTR Options;
    PVOID Store = NULL;
    ULONG Chars, Size, Count, Default, Start, i;

    Options = FwGetLoadOptions(&Chars);
    for (i = 0; Options && i < Chars && Options[i] != UNICODE_NULL; i++)
    {
        if (i != 0 && Options[i - 1] != L' ')
            continue;

        for (Start = i; i < Chars && Options[i] != UNICODE_NULL && Options[i] != L' '; i++)
            ;

        if (Options[Start] == L'{')
            RtlStringCchCopyNW(Id, RTL_NUMBER_OF(Id), &Options[Start], i - Start);
        else if (Options[Start] == L'\\')
            RtlStringCchCopyNW(StorePath, RTL_NUMBER_OF(StorePath), &Options[Start], i - Start);
        else if (i - Start > OSL_STARTUP_PREFIX_CHARS &&
                 _wcsnicmp(&Options[Start], BM_STARTUP_OPTION, OSL_STARTUP_PREFIX_CHARS) == 0)
            OslParseStartup(&Options[Start + OSL_STARTUP_PREFIX_CHARS], i - Start - OSL_STARTUP_PREFIX_CHARS);
        else if (i - Start == RTL_NUMBER_OF(BM_UNDO_UPDATE_OPTION) - 1 &&
                 _wcsnicmp(&Options[Start], BM_UNDO_UPDATE_OPTION, i - Start) == 0)
            OslStartup.UndoUpdate = TRUE;
    }

    if (StorePath[0] != UNICODE_NULL)
        Store = BcdReadStoreFile(NULL, StorePath, &Size);

    for (i = 0; i < RTL_NUMBER_OF(OslStorePaths) && !Store; i++)
        Store = BcdReadStoreFile(NULL, OslStorePaths[i], &Size);

    if (!Store || !BcdOpenStore(Store, Size))
    {
        FwPrint(L"The Boot Configuration Data store is missing or corrupt.\r\n");
        return NULL;
    }

    if (Id[0] != UNICODE_NULL)
    {
        if (BcdOpenObject(Id, &OslEntries[0].Object) &&
            OslEntries[0].Object.Type == BCD_OBJECT_OSLOADER)
        {
            return &OslEntries[0];
        }

        FwPrint(L"The boot entry %s is not an operating system loader entry.\r\n", Id);
        return NULL;
    }

    Count = BmEnumerateEntries(OslEntries, RTL_NUMBER_OF(OslEntries), &Default);
    if (Count == 0)
    {
        FwPrint(L"The Boot Configuration Data store has no operating system entry.\r\n");
        return NULL;
    }

    return &OslEntries[Default];
}

static
ULONGLONG
OslGetRamdiskExtraBytes(
    _In_ PCSTR Options)
{
    PCSTR Text = strstr(Options, OSL_RAMDISK_SIZE_OPTION);
    ULONGLONG Size = 0;

    if (!Text)
        return 0;

    for (Text += sizeof(OSL_RAMDISK_SIZE_OPTION) - 1; *Text >= '0' && *Text <= '9'; Text++)
        Size = Size * 10 + (*Text - '0');

    if (*Text == 'G' || *Text == 'g')
        Size <<= 30;
    else if (*Text == 'M' || *Text == 'm')
        Size <<= 20;
    else if (*Text == 'K' || *Text == 'k')
        Size <<= 10;

    return Size;
}

static
BOOLEAN
OslGetOption(
    _In_ PCSTR Options,
    _In_ PCSTR Name,
    _Out_writes_z_(Size) PSTR Value,
    _In_ SIZE_T Size)
{
    PCSTR Text = strstr(Options, Name);
    SIZE_T i;

    if (!Text)
        return FALSE;

    Text += strlen(Name);
    for (i = 0; i < Size - 1 && Text[i] != ANSI_NULL && Text[i] != ' '; i++)
        Value[i] = Text[i];

    Value[i] = ANSI_NULL;
    return i != 0;
}

static
BOOLEAN
OslHasDebugPort(
    _In_ PCSTR Options,
    _In_ PCSTR Port)
{
    SIZE_T Length = strlen(Port);
    PCSTR Text;

    for (Text = strstr(Options, OSL_DEBUG_PORT_OPTION); Text; Text = strstr(Text, OSL_DEBUG_PORT_OPTION))
    {
        Text += RTL_NUMBER_OF(OSL_DEBUG_PORT_OPTION) - 1;
        if (_strnicmp(Text, Port, Length) == 0 && (Text[Length] == ANSI_NULL || Text[Length] == ' '))
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
OslDebugRequested(
    _In_ PCSTR Options)
{
    BOOLEAN Requested = FALSE;
    PCSTR Text = Options, End;
    SIZE_T Length;

    while (*Text)
    {
        while (*Text == ' ' || *Text == '/')
            Text++;

        for (End = Text; *End && *End != ' '; End++)
            ;

        Length = End - Text;
        if (Length == 7 && _strnicmp(Text, "NODEBUG", 7) == 0)
            return FALSE;

        if ((Length == 5 && _strnicmp(Text, "DEBUG", 5) == 0) ||
            (Length > 5 && _strnicmp(Text, "DEBUG=", 6) == 0) ||
            (Length >= RTL_NUMBER_OF(OSL_DEBUG_PORT_OPTION) - 1 &&
             _strnicmp(Text, OSL_DEBUG_PORT_OPTION, RTL_NUMBER_OF(OSL_DEBUG_PORT_OPTION) - 1) == 0))
        {
            Requested = TRUE;
        }

        Text = End;
    }

    return Requested;
}

static
VOID
OslLoadNetworkDebugger(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Directory)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    ULONG i;

    if (!PeLoadModule(&LoaderBlock->LoadOrderListHead,
                      Device,
                      Directory,
                      OSL_NETWORK_DEBUG_FILE,
                      OSL_DEBUG_MODULE,
                      LoaderSystemCode,
                      LDRP_DRIVER_DEPENDENT_DLL,
                      &Entry))
    {
        FwPrint(L"%hs could not be loaded.\r\n", OSL_NETWORK_DEBUG_FILE);
        return;
    }

    for (i = 0; i < RTL_NUMBER_OF(OslNetworkDebugExtensions); i++)
    {
        PeLoadModule(&LoaderBlock->LoadOrderListHead,
                     Device,
                     Directory,
                     OslNetworkDebugExtensions[i],
                     OslNetworkDebugExtensions[i],
                     LoaderSystemCode,
                     LDRP_DRIVER_DEPENDENT_DLL,
                     &Entry);
    }
}

static
PLDR_DATA_TABLE_ENTRY
OslLoadCoreImage(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Directory,
    _In_ PCWSTR FileName,
    _In_ PCSTR ModuleName,
    _In_ TYPE_OF_MEMORY MemoryType)
{
    CHAR Path[OSL_PATH_CHARS];
    PLDR_DATA_TABLE_ENTRY Entry;
    PVOID Base;

    if (!NT_SUCCESS(RtlStringCbPrintfA(Path, sizeof(Path), "%s%ws", Directory, FileName)))
        return NULL;

    Base = PeLoadImage(Device, Path, MemoryType);
    if (!Base)
    {
        FwPrint(L"%hs could not be loaded.\r\n", Path);
        return NULL;
    }

    if (!PeLdrAllocateDataTableEntry(&LoaderBlock->LoadOrderListHead,
                                     ModuleName,
                                     ModuleName,
                                     PaToVa(Base),
                                     &Entry))
    {
        return NULL;
    }

    PeInitializeSecurityCookie(Entry);
    return Entry;
}

static
VOID
OslMain(VOID)
{
    CHAR SystemRoot[OSL_PATH_CHARS], Directory[OSL_PATH_CHARS], Options[OSL_OPTIONS_CHARS];
    CHAR Url[OSL_PATH_CHARS], Address[OSL_ADDRESS_CHARS], Hash[OSL_HASH_CHARS];
    PVOID Image = NULL;
    ULONG ImageSize = 0;
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    PLDR_DATA_TABLE_ENTRY Kernel, Hal, Video;
    PBM_ENTRY Entry;
    DK_DEVICE OsDevice;
    SIZE_T Length;

    OSL_SWITCH_TRACE(L"[SW-WL09] Selecting boot entry\r\n");
    OslTrace("OS loader: selecting the boot entry");
    Entry = OslSelectEntry();
    if (!Entry)
        return;
    OSL_SWITCH_TRACE(L"[SW-WL10] Boot entry selected\r\n");

    OSL_SWITCH_TRACE(L"[SW-WL11] Reading boot parameters begin\r\n");
    OslTrace("OS loader: reading the boot entry");

    if (!OslGetParameters(Entry, &OslParameters) ||
        !OslNarrow(SystemRoot, sizeof(SystemRoot) - 1, OslParameters.SystemRoot) ||
        !OslNarrow(Options, sizeof(Options), OslParameters.LoadOptions))
    {
        FwPrint(L"The boot entry has no usable OS device, system root or load options.\r\n");
        return;
    }
    OSL_SWITCH_TRACE(L"[SW-WL12] Boot parameters parsed\r\n");

    AppendBootTimeOptions(Options, sizeof(Options), &OslStartup);
    if (!FwDebugEnabled && OslDebugRequested(Options))
    {
        FwDebugEnable();
        FwDebugAnnounce("LiberNT OS Loader", FALSE);
    }
    FwDebugScreen = OslHasDebugPort(Options, "SCREEN");

    Length = strlen(SystemRoot);
    if (Length == 0 || SystemRoot[Length - 1] != '\\')
        RtlStringCbCatA(SystemRoot, sizeof(SystemRoot), "\\");

    OslTrace("OS loader: system root %s, options %s", SystemRoot, Options);

    OSL_SWITCH_TRACE(L"[SW-WL13] OS device resolution begin\r\n");
    if (OslParameters.OsDevice.Ramdisk)
    {
        if (OslGetOption(Options, OSL_DOWNLOAD_URL_OPTION, Url, sizeof(Url)))
        {
            Image = FwDownloadFile(Url,
                                   OslGetOption(Options, OSL_DOWNLOAD_ADDRESS_OPTION, Address, sizeof(Address))
                                       ? Address : NULL,
                                   TRUE,
                                   &ImageSize);
        }

        if (!OslBuildRamdisk(&OslParameters.OsDevice,
                             Image,
                             ImageSize,
                             OslGetRamdiskExtraBytes(Options),
                             &OsDevice))
        {
            return;
        }
    }
    else if (!DkOpenBcdDevice(&OslParameters.OsDevice, &OsDevice))
    {
        FwPrint(L"The operating system device was not found.\r\n");
        return;
    }
    else
    {
        if (OslGetOption(Options, OSL_SYNC_URL_OPTION, Url, sizeof(Url)))
        {
            OslHttpSync(&OsDevice,
                        Url,
                        OslGetOption(Options, OSL_DOWNLOAD_ADDRESS_OPTION, Address, sizeof(Address))
                            ? Address : NULL);
        }

        OslResumeFromHibernation(&OsDevice);

        if (OslGetOption(Options, OSL_INSTALL_IMAGE_OPTION, Url, sizeof(Url)))
        {
            OslInstallImage(&OsDevice,
                            SystemRoot,
                            Url,
                            OslGetOption(Options, OSL_INSTALL_HASH_OPTION, Hash, sizeof(Hash)) ? Hash : NULL,
                            OslStartup.UndoUpdate);
        }
    }

    OSL_SWITCH_TRACE(L"[SW-WL14] OS device resolution completed\r\n");
    PeSetSystemRoot(SystemRoot);
    RtlStringCbPrintfA(Directory, sizeof(Directory), "%ssystem32\\", SystemRoot);

    OSL_SWITCH_TRACE(L"[SW-WL15] Loader block allocation begin\r\n");
    LoaderBlock = OslAllocateLoaderBlock();
    if (!LoaderBlock)
    {
        FwPrint(L"Not enough memory for the loader block.\r\n");
        return;
    }

    OSL_SWITCH_TRACE(L"[SW-WL16] Loader block allocated\r\n");
    OSL_SWITCH_TRACE(L"[SW-WL17] Configuration tree begin\r\n");
    LoaderBlock->ConfigurationRoot = OslBuildConfigurationTree(LoaderBlock);
    OSL_SWITCH_TRACE(L"[SW-WL18] Configuration tree returned\r\n");

    OSL_SWITCH_TRACE(L"[SW-WL19] SYSTEM hive load begin\r\n");
    OslTrace("OS loader: loading the SYSTEM hive");
    if (!OslLoadSystemHive(LoaderBlock, &OsDevice, SystemRoot))
    {
        FwPrint(L"The SYSTEM registry hive is missing or corrupt.\r\n");
        return;
    }

    OSL_SWITCH_TRACE(L"[SW-WL20] SYSTEM hive loaded\r\n");
    OslTrace("OS loader: loading %ls and %ls", OslParameters.KernelFile, OslParameters.HalFile);
    OSL_SWITCH_TRACE(L"[SW-WL21] Kernel image load begin\r\n");
    Kernel = OslLoadCoreImage(LoaderBlock,
                              &OsDevice,
                              Directory,
                              OslParameters.KernelFile,
                              OSL_KERNEL_MODULE,
                              LoaderSystemCode);
    OSL_SWITCH_TRACE(L"[SW-WL22] Kernel load returned\r\n");
    OSL_SWITCH_TRACE(L"[SW-WL23] HAL image load begin\r\n");
    Hal = Kernel ? OslLoadCoreImage(LoaderBlock,
                                    &OsDevice,
                                    Directory,
                                    OslParameters.HalFile,
                                    OSL_HAL_MODULE,
                                    LoaderHalCode)
                 : NULL;
    OSL_SWITCH_TRACE(L"[SW-WL24] HAL load returned\r\n");
    if (Hal && OslHasDebugPort(Options, "NET"))
        OslLoadNetworkDebugger(LoaderBlock, &OsDevice, Directory);

    OSL_SWITCH_TRACE(L"[SW-WL25] Driver/import checks begin\r\n");
    if (!Hal ||
        (FrameBufferData &&
         !PeLoadModule(&LoaderBlock->LoadOrderListHead,
                       &OsDevice,
                       Directory,
                       OSL_FRAMEBUFFER_VIDEO_FILE,
                       OSL_VIDEO_MODULE,
                       LoaderBootDriver,
                       LDRP_DRIVER_DEPENDENT_DLL,
                       &Video)) ||
        !PeResolveImports(&LoaderBlock->LoadOrderListHead, &OsDevice, Directory, Kernel) ||
        !PeResolveImports(&LoaderBlock->LoadOrderListHead, &OsDevice, Directory, Hal))
    {
        FwPrint(L"The kernel and the hardware abstraction layer could not be loaded.\r\n");
        return;
    }

    OSL_SWITCH_TRACE(L"[SW-WL26] Driver/import checks completed\r\n");
    OSL_SWITCH_TRACE(L"[SW-WL27] NLS load begin\r\n");
    OslTrace("OS loader: loading the NLS files");
    if (!OslLoadNlsData(LoaderBlock, &OsDevice, SystemRoot))
    {
        FwPrint(L"The national language support files could not be loaded.\r\n");
        return;
    }

    OSL_SWITCH_TRACE(L"[SW-WL28] NLS loaded\r\n");
    OSL_SWITCH_TRACE(L"[SW-WL29] Boot driver list load begin\r\n");
    OslTrace("OS loader: loading the boot drivers");
    if (!OslLoadBootDrivers(LoaderBlock, &OsDevice, SystemRoot))
    {
        FwPrint(L"The boot driver list could not be built.\r\n");
        return;
    }

    OSL_SWITCH_TRACE(L"[SW-WL30] Boot driver list built\r\n");
    OslLoadDriverDatabase(LoaderBlock, &OsDevice, SystemRoot);
    OSL_SWITCH_TRACE(L"[SW-WL31] Driver database scan returned\r\n");

    OSL_SWITCH_TRACE(L"[SW-WL32] Finalize loader block begin\r\n");
    if (!OslFinalizeLoaderBlock(LoaderBlock, &OsDevice, SystemRoot, Options))
    {
        FwPrint(L"The operating system partition has no partition number.\r\n");
        return;
    }

    OSL_SWITCH_TRACE(L"[SW-WL33] Loader block finalized\r\n");
    OSL_SWITCH_TRACE(L"[SW-WL34] Kernel address space preparation begin\r\n");
    OslTrace("OS loader: preparing the kernel address space");
    if (!OslArchPrepareHandoff(LoaderBlock))
    {
        FwPrint(L"The kernel address space could not be prepared.\r\n");
        return;
    }

    OSL_SWITCH_TRACE(L"[SW-WL35] Kernel address space prepared\r\n");
    OslTrace("OS loader: handing over, boot device %s, hal device %s",
             LoaderBlock->ArcBootDeviceName ? LoaderBlock->ArcBootDeviceName : "(none)",
             LoaderBlock->ArcHalDeviceName ? LoaderBlock->ArcHalDeviceName : "(none)");
    OslTrace("OS loader: nt path %s, options %s",
             LoaderBlock->NtBootPathName ? LoaderBlock->NtBootPathName : "(none)",
             LoaderBlock->LoadOptions ? LoaderBlock->LoadOptions : "(none)");
    OslTrace("OS loader: entry %p, loader block %p, arc disks %p, registry %lu bytes at %p",
             Kernel->EntryPoint,
             PaToVa(LoaderBlock),
             LoaderBlock->ArcDiskInformation,
             LoaderBlock->RegistryLength,
             LoaderBlock->RegistryBase);

    OSL_SWITCH_TRACE(L"[SW-WL36] OslArchStartKernel begin\r\n");
    OslArchStartKernel(Kernel->EntryPoint, PaToVa(LoaderBlock));
}

VOID
ExecuteLoaderCleanly(
    PVOID PreviousStack)
{
    UefiServiceStack = PreviousStack;
    OSL_SWITCH_TRACE(L"[SW-WL08] Loader stack active\r\n");
    OslMain();
}

VOID
BlMain(VOID)
{
    PUCHAR Stack;

    OSL_SWITCH_TRACE(L"[SW-WL00] BlMain entered\r\n");
    FwDebugAnnounce("LiberNT OS Loader", FALSE);
    OSL_SWITCH_TRACE(L"[SW-WL01] Announce completed\r\n");
    OslTrace("OS loader: describing the platform");
    OSL_SWITCH_TRACE(L"[SW-WL02] OslArchInitialize begin\r\n");
    OslArchInitialize();
    OSL_SWITCH_TRACE(L"[SW-WL03] OslArchInitialize done\r\n");

    OslTrace("OS loader: reading the memory map");
    OSL_SWITCH_TRACE(L"[SW-WL04] Memory manager begin\r\n");
    if (!MmInitializeMemoryManager())
    {
        FwPrint(L"The memory map could not be read.\r\n");
        return;
    }
    OSL_SWITCH_TRACE(L"[SW-WL05] Memory manager done\r\n");

    BlHeapAllocator = MmHeapAllocate;

    OSL_SWITCH_TRACE(L"[SW-WL06] Allocating loader stack\r\n");
    OslTrace("OS loader: allocating the loader stack");
    Stack = MmAllocateMemoryWithType(OSL_STACK_SIZE, LoaderOsloaderStack);
    if (!Stack)
    {
        FwPrint(L"Not enough memory for the loader stack.\r\n");
        return;
    }

    BasicStack = Stack + OSL_STACK_SIZE;
    OslTrace("OS loader: switching to the loader stack at %p", BasicStack);
    OSL_SWITCH_TRACE(L"[SW-WL07] Switching loader stack\r\n");
    OslArchSwitchStack();
}
