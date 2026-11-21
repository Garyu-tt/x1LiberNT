param(
    [ValidateSet('Check', 'Install', 'Restore')]
    [string]$Mode = 'Check'
)

$ErrorActionPreference = 'Stop'

$expectedBootManager = '562F0307ED8FEC5339C0E9EFEC2CAD26A027D910999C0DE9CF8DFF05E5C61B48'
$expectedWinload = '6DC1602D4D96255A261AFA2F29E1946BA1010CD498C679D3C84F0AD6FBA47375'
$expectedKernel = 'D35605F76C2F8D235273209A5FE924E1A63375F42F1F679F2EC47DE3E04200B9'
$oldHal = 'E7CC37D649D4178247A0DFB490884A6523C32126AF1FE80BB32329BE43E9AC45'
$newHal = 'EC484CCE061D7722FF8F3F44445F5DDA32FF0BB787DA359604D2909005F52993'

$active = 'L:\Windows\System32\hal.dll'
$backupSD = 'D:\EFI\LiberNT\Experiments\hal-R5.9-confirmed-physical.dll'
$backupSSD = 'C:\Users\Public\Documents\LiberNT-Switch-R0\backups\hal-R5.9-confirmed-physical.dll'
$stagedSD = 'D:\EFI\LiberNT\Experiments\hal-R6.0-matched-kernel.dll'

function Get-LabHash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

$disk = Get-Disk -Number 1 -ErrorAction Stop
$fat = Get-Volume -DriveLetter D -ErrorAction Stop
$ntfs = Get-Volume -DriveLetter L -ErrorAction Stop

if ($disk.FriendlyName -ne 'hekate SD RAW' -or
    $disk.BusType -ne 'USB' -or $disk.IsSystem -or $disk.IsBoot -or
    $disk.OperationalStatus -ne 'Online' -or
    $fat.FileSystemLabel -ne 'SWITCH SD' -or $fat.FileSystem -ne 'FAT32' -or
    $ntfs.FileSystemLabel -ne 'LIBERNT-LAB' -or $ntfs.FileSystem -ne 'NTFS') {
    throw 'Not the expected Hekate SD device: no changes'
}

if ((Get-LabHash 'D:\EFI\BOOT\BOOTAA64.EFI') -ne $expectedBootManager -or
    (Get-LabHash 'L:\Windows\System32\winload.efi') -ne $expectedWinload -or
    (Get-LabHash 'L:\Windows\System32\ntoskrnl.exe') -ne $expectedKernel) {
    throw 'Boot chain does not match the verified R5.9 baseline'
}

$entry = (& bcdedit.exe /store 'D:\EFI\LiberNT\Boot\BCD' /enum '{default}') -join ' '
if ($LASTEXITCODE -ne 0 -or
    $entry -notmatch 'DEBUGPORT=SCREEN' -or
    $entry -notmatch 'partition=L:') {
    throw 'Unexpected BCD entry; no changes'
}

if ((Get-LabHash $backupSD) -ne $oldHal -or
    (Get-LabHash $backupSSD) -ne $oldHal -or
    (Get-LabHash $stagedSD) -ne $newHal) {
    throw 'HAL backups or staged candidate failed SHA256 verification'
}

$current = Get-LabHash $active
if ($current -notin @($oldHal, $newHal)) {
    throw 'Unexpected active HAL: no changes'
}

if ($Mode -eq 'Check') {
    'ACTIVE_HAL_SHA256=' + $current
    'OLD_HAL_BACKUPS_VERIFIED=True'
    'NEW_HAL_CANDIDATE_VERIFIED=True'
    'BOOT_CHAIN_VERIFIED=True'
    return
}

if ($Mode -eq 'Restore') {
    $source = $backupSD
    $targetHash = $oldHal
}
else {
    $source = $stagedSD
    $targetHash = $newHal
}

if ($current -ne $targetHash) {
    Copy-Item -LiteralPath $source -Destination $active -Force -ErrorAction Stop
}
if ((Get-LabHash $active) -ne $targetHash) {
    throw 'HAL post-write SHA256 verification failed'
}
'VERIFIED_MODE=' + $Mode
'ACTIVE_HAL_SHA256=' + (Get-LabHash $active)
'Safely eject both D: and L: before returning to Hekate.'
