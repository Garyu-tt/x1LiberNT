param(
    [ValidateSet('Check','Install','Restore')][string]$Action='Check'
)
$ErrorActionPreference='Stop'
$old='E7CC37D649D4178247A0DFB490884A6523C32126AF1FE80BB32329BE43E9AC45'
$new='3A2B80E114CF65AA3FA631B74B92F293D0012A5BA4FDCFE7C097C2BFA52FD3B8'
$kernel='D35605F76C2F8D235273209A5FE924E1A63375F42F1F679F2EC47DE3E04200B9'
$win='6DC1602D4D96255A261AFA2F29E1946BA1010CD498C679D3C84F0AD6FBA47375'
$bm='562F0307ED8FEC5339C0E9EFEC2CAD26A027D910999C0DE9CF8DFF05E5C61B48'
$hal='L:\Windows\System32\hal.dll'
$backupSd='D:\EFI\LiberNT\Experiments\hal-R5.8-original.dll'
$backupSsd='C:\Users\Public\Documents\LiberNT-Switch-R0\backups\hal-R5.8-original.dll'
$stage='D:\EFI\LiberNT\Experiments\hal-R5.9-matched.dll'
function Get-LabHash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
$d=Get-Disk -Number 1 -ErrorAction Stop
$f=Get-Volume -DriveLetter D -ErrorAction Stop
$n=Get-Volume -DriveLetter L -ErrorAction Stop
if($d.FriendlyName -ne 'hekate SD RAW' -or $d.BusType -ne 'USB' -or
   $d.IsBoot -or $d.IsSystem -or $d.OperationalStatus -ne 'Online' -or
   $f.FileSystemLabel -ne 'SWITCH SD' -or $f.FileSystem -ne 'FAT32' -or
   $n.FileSystemLabel -ne 'LIBERNT-LAB' -or $n.FileSystem -ne 'NTFS'){
   throw 'Wrong Hekate SD disk or volume: refuse to write'
}
if((Get-LabHash 'D:\EFI\BOOT\BOOTAA64.EFI') -ne $bm -or
   (Get-LabHash 'L:\Windows\System32\winload.efi') -ne $win -or
   (Get-LabHash 'L:\Windows\System32\ntoskrnl.exe') -ne $kernel){
   throw 'Boot Manager, WinLoad or kernel changed'
}
$cfg=(& bcdedit.exe /store 'D:\EFI\LiberNT\Boot\BCD' /enum '{default}') -join ' '
if($LASTEXITCODE -ne 0 -or $cfg -notmatch 'DEBUGPORT=SCREEN' -or
   $cfg -notmatch 'partition=L:'){throw 'Unexpected BCD configuration'}
$current=Get-LabHash $hal
if($current -notin @($old,$new)){throw 'Unexpected installed HAL'}
if($Action -eq 'Check'){
   'ACTIVE_HAL_SHA256='+$current
   'CANDIDATE_PRESENT='+(Test-Path -LiteralPath $stage)
   'SD_BACKUP_PRESENT='+(Test-Path -LiteralPath $backupSd)
   return
}
if($Action -eq 'Install'){
   $src=$stage
   $expected=$new
   foreach($b in @($backupSd,$backupSsd)){
     if(Test-Path -LiteralPath $b){
       if((Get-LabHash $b) -ne $old){throw 'Backup file collision'}
     }else{
       if($current -ne $old){throw 'No original active HAL available to back up'}
       Copy-Item -LiteralPath $hal -Destination $b -ErrorAction Stop
     }
     if((Get-LabHash $b) -ne $old){throw 'Backup verification failed'}
   }
}else{
   $src=$backupSd
   $expected=$old
   if((Get-LabHash $backupSd) -ne $old -or
      (Get-LabHash $backupSsd) -ne $old){throw 'Original backup damaged'}
}
if((Get-LabHash $src) -ne $expected){throw 'Replacement hash mismatch'}
if($current -ne $expected){
   Copy-Item -LiteralPath $src -Destination $hal -Force -ErrorAction Stop
}
if((Get-LabHash $hal) -ne $expected){throw 'Post-write SHA-256 failure'}
'VERIFIED_HAL='+$expected
'BOOTMGR_WINLOAD_KERNEL_UNCHANGED=True'
'Safely eject D: and L: before leaving Hekate USB Mass Storage'
