@echo -off
echo [MECHANICUS] LiberNT-Switch R5.9 shell fallback
echo [CHK] Probing known LiberNT FAT32 layout (read-only)...
if exist fs0:\EFI\LiberNT\Boot\BCD then
  if exist fs0:\EFI\BOOT\BOOTAA64.EFI then
    echo [FOUND] LiberNT BCD and Boot Manager at fs0:
    fs0:\EFI\BOOT\BOOTAA64.EFI
    echo [RETURNED] Boot Manager exited from fs0:
  endif
endif
if exist fs1:\EFI\LiberNT\Boot\BCD then
  if exist fs1:\EFI\BOOT\BOOTAA64.EFI then
    echo [FOUND] LiberNT BCD and Boot Manager at fs1:
    fs1:\EFI\BOOT\BOOTAA64.EFI
    echo [RETURNED] Boot Manager exited from fs1:
  endif
endif
if exist fs2:\EFI\LiberNT\Boot\BCD then
  if exist fs2:\EFI\BOOT\BOOTAA64.EFI then
    echo [FOUND] LiberNT BCD and Boot Manager at fs2:
    fs2:\EFI\BOOT\BOOTAA64.EFI
    echo [RETURNED] Boot Manager exited from fs2:
  endif
endif
if exist fs3:\EFI\LiberNT\Boot\BCD then
  if exist fs3:\EFI\BOOT\BOOTAA64.EFI then
    echo [FOUND] LiberNT BCD and Boot Manager at fs3:
    fs3:\EFI\BOOT\BOOTAA64.EFI
    echo [RETURNED] Boot Manager exited from fs3:
  endif
endif
if exist fs4:\EFI\LiberNT\Boot\BCD then
  if exist fs4:\EFI\BOOT\BOOTAA64.EFI then
    echo [FOUND] LiberNT BCD and Boot Manager at fs4:
    fs4:\EFI\BOOT\BOOTAA64.EFI
    echo [RETURNED] Boot Manager exited from fs4:
  endif
endif
if exist fs5:\EFI\LiberNT\Boot\BCD then
  if exist fs5:\EFI\BOOT\BOOTAA64.EFI then
    echo [FOUND] LiberNT BCD and Boot Manager at fs5:
    fs5:\EFI\BOOT\BOOTAA64.EFI
    echo [RETURNED] Boot Manager exited from fs5:
  endif
endif
if exist fs6:\EFI\LiberNT\Boot\BCD then
  if exist fs6:\EFI\BOOT\BOOTAA64.EFI then
    echo [FOUND] LiberNT BCD and Boot Manager at fs6:
    fs6:\EFI\BOOT\BOOTAA64.EFI
    echo [RETURNED] Boot Manager exited from fs6:
  endif
endif
if exist fs7:\EFI\LiberNT\Boot\BCD then
  if exist fs7:\EFI\BOOT\BOOTAA64.EFI then
    echo [FOUND] LiberNT BCD and Boot Manager at fs7:
    fs7:\EFI\BOOT\BOOTAA64.EFI
    echo [RETURNED] Boot Manager exited from fs7:
  endif
endif
echo [DIAG] No successful automatic LiberNT handoff.
echo [DIAG] If no fsN: entries appear, inspect UEFI SD drivers.
map
echo [DIAG] The original Hekate/EFI files were not changed by this script.
