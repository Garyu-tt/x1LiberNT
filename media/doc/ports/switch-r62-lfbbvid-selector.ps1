param(
  [ValidateSet('Check','Install','Restore')]
  [string]$Mode='Check'
)
$ErrorActionPreference='Stop'
$base='C:\Users\Public\Documents\LiberNT-Switch-R0'
$old='5C55D64F0EC931FA1CFAD8EFDB12DD6D463ED6C1243E8202B516DA3EAD0C6A12'
$new='11EB091CC0AF95320DEEAC504F29051D7BDC4FF8AC6B7DA4B856A1933AEA3530'
$boot='562F0307ED8FEC5339C0E9EFEC2CAD26A027D910999C0DE9CF8DFF05E5C61B48'
$win='6DC1602D4D96255A261AFA2F29E1946BA1010CD498C679D3C84F0AD6FBA47375'
$nt='D35605F76C2F8D235273209A5FE924E1A63375F42F1F679F2EC47DE3E04200B9'
$hal='EC484CCE061D7722FF8F3F44445F5DDA32FF0BB787DA359604D2909005F52993'
$bootvid='2D40AAB426D65B858AF10CE73105EBA45F4F4B60C6056B227AA1F9BA9A1372B6'
$active='L:\Windows\System32\lfbbvid.dll'
$candidate=Join-Path $base 'staging-r6.2\lfbbvid-R6.2.dll'
$backupSsd=Join-Path $base 'backups\lfbbvid-R6.1-original.dll'
$backupSd='D:\EFI\LiberNT\Experiments\lfbbvid-R6.1-original.dll'
$stageSd='D:\EFI\LiberNT\Experiments\lfbbvid-R6.2-matched-kernel.dll'
function Get-LabHash([string]$p) {
    (Get-FileHash -LiteralPath $p -Algorithm SHA256 -ErrorAction Stop).Hash
}
$disk=Get-Disk -Number 1 -ErrorAction Stop
$fat=Get-Volume -DriveLetter D -ErrorAction Stop
$ntfs=Get-Volume -DriveLetter L -ErrorAction Stop
if($disk.FriendlyName -ne 'hekate SD RAW' -or $disk.BusType -ne 'USB' -or
   $disk.IsBoot -or $disk.IsSystem -or $disk.OperationalStatus -ne 'Online' -or
   $fat.FileSystemLabel -ne 'SWITCH SD' -or $fat.FileSystem -ne 'FAT32' -or
   $ntfs.FileSystemLabel -ne 'LIBERNT-LAB' -or $ntfs.FileSystem -ne 'NTFS') {
   throw 'Unrecognized Hekate SD / volume: NO WRITE'
}
foreach($entry in @(
 @{p='D:\EFI\BOOT\BOOTAA64.EFI';sha=$boot},
 @{p='L:\Windows\System32\winload.efi';sha=$win},
 @{p='L:\Windows\System32\ntoskrnl.exe';sha=$nt},
 @{p='L:\Windows\System32\hal.dll';sha=$hal},
 @{p='L:\Windows\System32\bootvid.dll';sha=$bootvid}
)) {
 if((Get-LabHash $entry.p) -ne $entry.sha) {
   throw ('Preflight changed: ' + $entry.p)
 }
}
$bcd=(& bcdedit.exe /store 'D:\EFI\LiberNT\Boot\BCD' /enum '{default}') -join ' '
if($LASTEXITCODE -ne 0 -or $bcd -notmatch 'DEBUGPORT=SCREEN' -or
   $bcd -notmatch 'partition=L:') {throw 'Unexpected BCD'}
$current=Get-LabHash $active
if($current -notin @($old,$new)) {throw 'Unknown active LFBBVID: refuse modification'}
if($Mode -eq 'Check') {
 'ACTIVE_LFBBVID_SHA256='+$current
 'NEW_CANDIDATE_PRESENT='+(Test-Path -LiteralPath $candidate)
 'BACKUP_SD_PRESENT='+(Test-Path -LiteralPath $backupSd)
 'BACKUP_SSD_PRESENT='+(Test-Path -LiteralPath $backupSsd)
 'BOOT_CHAIN_HASHES_OK=True'
 return
}
if($Mode -eq 'Install') {
 if((Get-LabHash $candidate) -ne $new) {throw 'R6.2 candidate SHA mismatch'}
 [byte[]]$bytes=[IO.File]::ReadAllBytes($candidate)
 if($bytes.Length -lt 65536 -or $bytes[0] -ne 77 -or $bytes[1] -ne 90) {
  throw 'Candidate has no MZ'
 }
 $offset=[BitConverter]::ToInt32($bytes,60)
 if($offset -lt 64 -or $offset+26 -ge $bytes.Length -or
    [BitConverter]::ToUInt32($bytes,$offset) -ne 0x4550 -or
    [BitConverter]::ToUInt16($bytes,$offset+4) -ne 0xAA64) {
  throw 'Candidate is not ARM64 PE'
 }
 foreach($back in @($backupSsd,$backupSd)) {
  if(Test-Path -LiteralPath $back) {
   if((Get-LabHash $back) -ne $old) {throw ('Existing backup collision: '+$back)}
  } else {
   if($current -ne $old) {throw 'Cannot create original backup; original not active'}
   Copy-Item -LiteralPath $active -Destination $back -ErrorAction Stop
  }
  if((Get-LabHash $back) -ne $old) {throw 'Backup verification failed'}
 }
 if(Test-Path -LiteralPath $stageSd) {
  if((Get-LabHash $stageSd) -ne $new) {throw 'Staged filename collision'}
 } else {
  Copy-Item -LiteralPath $candidate -Destination $stageSd -ErrorAction Stop
 }
 if((Get-LabHash $stageSd) -ne $new) {throw 'Staged hash failed'}
 $src=$stageSd
 $expected=$new
} else {
 if((Get-LabHash $backupSd) -ne $old -or (Get-LabHash $backupSsd) -ne $old) {
  throw 'Original backups missing or damaged'
 }
 $src=$backupSd
 $expected=$old
}
if($current -ne $expected) {
 Copy-Item -LiteralPath $src -Destination $active -Force -ErrorAction Stop
}
if((Get-LabHash $active) -ne $expected) {throw 'Active LFBBVID hash verification failed'}
'MODE='+$Mode
'ACTIVE_LFBBVID_SHA256='+(Get-LabHash $active)
'OTHER_BOOT_FILES_UNCHANGED=True'
'Safely eject D: and L: before returning to Hekate'
