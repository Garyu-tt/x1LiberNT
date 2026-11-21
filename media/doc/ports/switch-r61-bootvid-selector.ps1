param(
    [ValidateSet('Check','Install','Restore')]
    [string]$Mode = 'Check'
)

$ErrorActionPreference = 'Stop'
$root = 'C:\Users\Public\Documents\LiberNT-Switch-R0'
$oldHash = '53CC4A992E2B318D8F1C0325E1B69F74E1F041B99EA2F5C066D3382EBD5DCCFE'
$newHash = '2D40AAB426D65B858AF10CE73105EBA45F4F4B60C6056B227AA1F9BA9A1372B6'
$bootHash = '562F0307ED8FEC5339C0E9EFEC2CAD26A027D910999C0DE9CF8DFF05E5C61B48'
$winHash = '6DC1602D4D96255A261AFA2F29E1946BA1010CD498C679D3C84F0AD6FBA47375'
$kernelHash = 'D35605F76C2F8D235273209A5FE924E1A63375F42F1F679F2EC47DE3E04200B9'
$halHash = 'EC484CCE061D7722FF8F3F44445F5DDA32FF0BB787DA359604D2909005F52993'

$active = 'L:\Windows\System32\bootvid.dll'
$candidate = Join-Path $root 'staging-r6.1\bootvid-R6.1.dll'
$backupSd = 'D:\EFI\LiberNT\Experiments\bootvid-R6.0-original.dll'
$backupSsd = Join-Path $root 'backups\bootvid-R6.0-original.dll'
$stagedSd = 'D:\EFI\LiberNT\Experiments\bootvid-R6.1-matched-kernel.dll'

function Get-LabHash([string]$path) {
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256 -ErrorAction Stop).Hash
}
function Assert-CandidateArm64([string]$path) {
    [byte[]]$bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 65536 -or $bytes[0] -ne 77 -or $bytes[1] -ne 90) {
        throw 'Candidate does not contain a valid DOS header'
    }
    $pe = [BitConverter]::ToInt32($bytes,60)
    if ($pe -lt 64 -or ($pe+26) -gt $bytes.Length -or
        [BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or
        [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0xAA64) {
        throw 'Candidate is not an ARM64 PE file'
    }
}
$disk = Get-Disk -Number 1 -ErrorAction Stop
$fat = Get-Volume -DriveLetter D -ErrorAction Stop
$ntfs = Get-Volume -DriveLetter L -ErrorAction Stop
if ($disk.FriendlyName -ne 'hekate SD RAW' -or $disk.BusType -ne 'USB' -or
    $disk.IsBoot -or $disk.IsSystem -or $disk.OperationalStatus -ne 'Online' -or
    $fat.FileSystemLabel -ne 'SWITCH SD' -or $fat.FileSystem -ne 'FAT32' -or
    $ntfs.FileSystemLabel -ne 'LIBERNT-LAB' -or $ntfs.FileSystem -ne 'NTFS') {
    throw 'Hekate USB SD not positively identified: no changes'
}
if ((Get-LabHash 'D:\EFI\BOOT\BOOTAA64.EFI') -ne $bootHash -or
    (Get-LabHash 'L:\Windows\System32\winload.efi') -ne $winHash -or
    (Get-LabHash 'L:\Windows\System32\ntoskrnl.exe') -ne $kernelHash -or
    (Get-LabHash 'L:\Windows\System32\hal.dll') -ne $halHash) {
    throw 'Validated R6.0 boot chain has changed: no changes'
}
$bcdText = (& bcdedit.exe /store 'D:\EFI\LiberNT\Boot\BCD' /enum '{default}') -join ' '
if ($LASTEXITCODE -ne 0 -or $bcdText -notmatch 'DEBUGPORT=SCREEN' -or
    $bcdText -notmatch 'partition=L:') {
    throw 'BCD differs from tested configuration'
}
$current = Get-LabHash $active
if ($current -notin @($oldHash,$newHash)) {
    throw 'Active bootvid is neither expected original nor R6.1'
}
if ($Mode -eq 'Check') {
    'BOOTVID_ACTIVE_SHA256=' + $current
    'ORIGINAL_BOOTVID_SHA256=' + $oldHash
    'NEW_CANDIDATE_PRESENT=' + (Test-Path -LiteralPath $candidate)
    'BACKUP_SD_PRESENT=' + (Test-Path -LiteralPath $backupSd)
    'BACKUP_SSD_PRESENT=' + (Test-Path -LiteralPath $backupSsd)
    'R6.0_BOOT_CHAIN_VERIFIED=True'
    return
}
if ($Mode -eq 'Install') {
    if ((Get-LabHash $candidate) -ne $newHash) {
        throw 'R6.1 candidate checksum mismatch'
    }
    Assert-CandidateArm64 $candidate
    foreach ($dst in @($backupSsd,$backupSd)) {
        if (Test-Path -LiteralPath $dst) {
            if ((Get-LabHash $dst) -ne $oldHash) {
                throw ('Existing backup collision: ' + $dst)
            }
        } else {
            if ($current -ne $oldHash) {
                throw 'Cannot create baseline backup after original replaced'
            }
            Copy-Item -LiteralPath $active -Destination $dst -ErrorAction Stop
        }
        if ((Get-LabHash $dst) -ne $oldHash) {
            throw 'Original backup did not verify'
        }
    }
    if (Test-Path -LiteralPath $stagedSd) {
        if ((Get-LabHash $stagedSd) -ne $newHash) {
            throw 'Existing R6.1 SD stage file differs'
        }
    } else {
        Copy-Item -LiteralPath $candidate -Destination $stagedSd -ErrorAction Stop
    }
    if ((Get-LabHash $stagedSd) -ne $newHash) {
        throw 'SD staging failed verification'
    }
    $source = $stagedSd
    $expected = $newHash
} else {
    foreach ($dst in @($backupSd,$backupSsd)) {
        if ((Get-LabHash $dst) -ne $oldHash) {
            throw 'Original BootVid backup missing or corrupted'
        }
    }
    $source = $backupSd
    $expected = $oldHash
}
if ($current -ne $expected) {
    Copy-Item -LiteralPath $source -Destination $active -Force -ErrorAction Stop
}
if ((Get-LabHash $active) -ne $expected) {
    throw 'BootVid post-write verification failed'
}
'BOOTVID_MODE_VERIFIED=' + $Mode
'ACTIVE_SHA256=' + (Get-LabHash $active)
'KERNEL_HAL_WINLOAD_BOOTMANAGER_UNCHANGED=True'
'Safely eject both D: and L: before booting from Hekate'
