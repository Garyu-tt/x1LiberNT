/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Boot application image loading
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>

#ifdef LIBERNT_SWITCH_DIAG_DIRECT_BOOT
#define BM_APP_DIAG(Message) FwPrint(Message)
#else
#define BM_APP_DIAG(Message) ((VOID)0)
#endif

#define BM_STARTUP_CHARS 48

static
ULONG
BmFoldChecksum(
    _In_ ULONG Sum)
{
    return (Sum & 0xFFFF) + (Sum >> 16);
}

static
ULONG
BmSubtractChecksum(
    _In_ ULONG Sum,
    _In_ ULONG Word)
{
    if ((Sum & 0xFFFF) >= Word)
        return Sum - Word;

    return (((Sum & 0xFFFF) - Word) & 0xFFFF) - 1;
}

static
ULONG
BmImageChecksum(
    _In_reads_bytes_(Length) const UCHAR* Image,
    _In_ ULONG Length,
    _In_ ULONG HeaderSum)
{
    ULONG Sum = 0;
    ULONG i;

    for (i = 0; i + 1 < Length; i += 2)
        Sum = BmFoldChecksum(Sum + (Image[i] | (Image[i + 1] << 8)));

    if (Length & 1)
        Sum = BmFoldChecksum(Sum + Image[Length - 1]);

    Sum = BmFoldChecksum(Sum) & 0xFFFF;
    Sum = BmSubtractChecksum(Sum, HeaderSum & 0xFFFF);
    Sum = BmSubtractChecksum(Sum, HeaderSum >> 16);

    return Sum + Length;
}

BOOLEAN
BmStartApplication(
    _In_ PBM_ENTRY Entry,
    _In_ PCWSTR StorePath,
    _In_ PBM_STARTUP Startup,
    _Out_ PBOOLEAN Cancelled)
{
    WCHAR Options[BM_PATH_CHARS + BCD_ID_CHARS + BM_STARTUP_CHARS];
    WCHAR Path[BM_PATH_CHARS];
    PIMAGE_NT_HEADERS NtHeaders;
    BCD_DEVICE BcdDevice;
    DK_DEVICE Device;
    PVOID Image;
    ULONG Size;
    BOOLEAN Started = FALSE;

    *Cancelled = FALSE;
    BM_APP_DIAG(L"[SW-DB10] BmStartApplication entered\r\n");

    if (Entry->FirmwareDevice)
    {
        Started = FwStartFirmwareApplication(Entry->FirmwareDevice, Entry->FirmwarePath, Entry->FirmwareOptions);
        if (!Started)
            FwPrint(L"%s could not be started.\r\n", Entry->FirmwarePath);
        return Started;
    }

    BM_APP_DIAG(L"[SW-DB11] Resolving BCD device/path\r\n");
    if (!BcdGetDevice(&Entry->Object, BcdLibraryDevice_ApplicationDevice, &BcdDevice) ||
        !BcdGetString(&Entry->Object, BcdLibraryString_ApplicationPath, Path, RTL_NUMBER_OF(Path)))
    {
        FwPrint(L"The boot entry has no application device or path.\r\n");
        return FALSE;
    }

    BM_APP_DIAG(L"[SW-DB12] BCD path resolved; opening image\r\n");
    if (BcdDevice.Type == BcdDeviceBoot)
    {
        Image = FwReadBootFile(Path, &Size);
    }
    else if (DkOpenBcdDevice(&BcdDevice, &Device))
    {
        Image = FsReadFile(&Device, Path, &Size);
    }
    else
    {
        FwPrint(L"The application device was not found.\r\n");
        return FALSE;
    }

    BM_APP_DIAG(L"[SW-DB13] Image read operation returned\r\n");
    if (!Image)
    {
        FwPrint(L"%s could not be read.\r\n", Path);
        return FALSE;
    }

    if (!NT_SUCCESS(RtlImageNtHeaderEx(0, Image, Size, &NtHeaders)) ||
        NtHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_NATIVE ||
        BmImageChecksum(Image, Size, NtHeaders->OptionalHeader.CheckSum) !=
            NtHeaders->OptionalHeader.CheckSum)
    {
        FwPrint(L"%s is not a valid boot application.\r\n", Path);
        FwFreePages(Image, Size);
        return FALSE;
    }

    BM_APP_DIAG(L"[SW-DB14] PE headers and checksum validated\r\n");
    if (NtHeaders->OptionalHeader.Subsystem == FwApplicationSubsystem)
    {
        RtlStringCchPrintfW(Options,
                            RTL_NUMBER_OF(Options),
                            L"%s %s %s%lu%s",
                            Entry->Id,
                            StorePath,
                            BM_STARTUP_OPTION,
                            Startup->Profile,
                            Startup->UndoUpdate ? L" " BM_UNDO_UPDATE_OPTION : L"");
        BM_APP_DIAG(L"[SW-DB15] FwStartImage begin\r\n");
        Started = FwStartImage(Image, Size, Options, Cancelled);
        BM_APP_DIAG(L"[SW-DB16] FwStartImage returned\r\n");
        if (!Started && !*Cancelled)
            FwPrint(L"%s did not start the operating system.\r\n", Path);
    }
    else if (NtHeaders->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_BOOT_APPLICATION)
    {
        FwPrint(L"%s is a Windows boot application, which this boot manager cannot start.\r\n", Path);
    }
    else
    {
        FwPrint(L"%s is not a boot application.\r\n", Path);
    }

    FwFreePages(Image, Size);
    return Started;
}
