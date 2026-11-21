param(
    [ValidateSet('Check','Install','Restore')]
    [string]$Mode = 'Check',
    [string]$NewSha = ''
)
$ErrorActionPreference = 'Stop'
$base = 'C:\Users\Public\Documents\LiberNT-Switch-R0'
$old = '394D34CFFCE85E26CC6700A1F9751CB08C8BA8EA5ADF5835F4E3AAA9AB064479'
$boot = '562F0307ED8FEC5339C0E9EFEC2CAD26A027D910999C0DE9CF8DFF05E5C61B48'
$win = '6DC1602D4D96255A261AFA2F29E1946BA1010CD498C679D3C84F0AD6FBA47375'
$active = 'L:\Windows\System32\ntoskrnl.exe'
$candidate = Join-Path $base 'staging-r5.9\ntoskrnl-R5.9.exe'
$backupSd = 'D:\EFI\LiberNT\Experiments\ntoskrnl-R5.8-verified-original.exe'
$backupSsd = Join-Path $base 'backups\ntoskrnl-R5.8-verified-original.exe'
function Get-LabHash([string]$path) {
    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}
$disk = Get-Disk -Number 1
$fat = Get-Volume -DriveLetter D
$ntfs = Get-Volume -DriveLetter L
if ($disk.FriendlyName -ne 'hekate SD RAW' -or $disk.BusType -ne 'USB' -or
    $disk.IsBoot -or $disk.IsSystem -or $disk.OperationalStatus -ne 'Online' -or
    $fat.FileSystemLabel -ne 'SWITCH SD' -or $fat.FileSystem -ne 'FAT32' -or
    $ntfs.FileSystemLabel -ne 'LIBERNT-LAB' -or $ntfs.FileSystem -ne 'NTFS') {
    throw 'Wrong device or volume: no changes'
}
if ((Get-LabHash 'D:\EFI\BOOT\BOOTAA64.EFI') -ne $boot -or
    (Get-LabHash 'L:\Windows\System32\winload.efi') -ne $win) {
    throw 'Unexpected Boot Manager or WinLoad'
}
if ((Get-LabHash $backupSd) -ne $old -or (Get-LabHash $backupSsd) -ne $old) {
    throw 'Baseline kernel backups missing or invalid'
}
$bcd = (& bcdedit.exe /store 'D:\EFI\LiberNT\Boot\BCD' /enum '{default}') -join ' '
if ($LASTEXITCODE -ne 0 -or $bcd -notmatch 'DEBUGPORT=SCREEN' -or
    $bcd -notmatch 'partition=L:') {
    throw 'Unexpected BCD configuration'
}
$installed = Get-LabHash $active
if ($Mode -eq 'Check') {
    "Active ntoskrnl.exe SHA256: $installed"
    "Baseline kernel SHA256: $old"
    if (Test-Path -LiteralPath $candidate) {
        'Candidate SHA256: ' + (Get-LabHash $candidate)
    } else {
        'Candidate not staged'
    }
    return
}
if ($NewSha -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'Provide expected complete 64-character candidate SHA256 as -NewSha'
}
$new = $NewSha.ToUpperInvariant()
if ($installed -notin @($old, $new)) {
    throw 'Active kernel differs from both expected versions'
}
if ($Mode -eq 'Install') {
    if ((Get-LabHash $candidate) -ne $new) {
        throw 'New kernel candidate hash mismatch'
    }
    [byte[]]$peBytes = [IO.File]::ReadAllBytes($candidate)
    if ($peBytes.Length -lt 1048576 -or $peBytes[0] -ne 77 -or
        $peBytes[1] -ne 90) { throw 'Invalid PE image' }
    $peOffset = [BitConverter]::ToInt32($peBytes, 60)
    if ($peOffset -lt 64 -or $peOffset + 26 -ge $peBytes.Length -or
        [BitConverter]::ToUInt16($peBytes, $peOffset + 4) -ne 0xAA64) {
        throw 'Candidate is not ARM64 PE'
    }
    if ($installed -ne $new) {
        Copy-Item -LiteralPath $candidate -Destination $active -Force -ErrorAction Stop
    }
    if ((Get-LabHash $active) -ne $new) { throw 'Installation verify failed' }
    'R5.9 diagnostic kernel installed and SHA256 verified'
}
else {
    if ($installed -ne $old) {
        Copy-Item -LiteralPath $backupSd -Destination $active -Force -ErrorAction Stop
    }
    if ((Get-LabHash $active) -ne $old) { throw 'Rollback verify failed' }
    'Original R5.8 kernel restored and SHA256 verified'
}
'Safely eject D: and L: before returning to Hekate'
