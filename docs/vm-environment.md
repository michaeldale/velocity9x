# Local 86Box environment

Status: 86Box 6.0 S3 target boots Windows 98
Recorded: 2026-08-08

The local 86Box installations and guest media are external project inputs. Do
not commit or package their executables, ROMs, NVR files, or disk images.

## Working baseline

- Root: `<86Box 4.2 install directory>`
- 86Box version: 4.2
- Executable SHA-256:
  `18D71766F2A24A0FFD7712F0BC239CC3F0C5017BD5B58D47FE54FE9FD336279F`
- Guest disk: `Win98HDD.vhd`, 504,074,752 bytes
- Guest-disk SHA-256:
  `65FC609B263D95172A494B0EEB9E631CFAC7E68C762B6A430CBFAD52A3A8449D`

This is the user-confirmed bootable baseline. Preserve it and perform driver
work only on a separate copy or recoverable snapshot.

## 86Box 6.0 target

- Root: `C:\86Box`
- 86Box version: 6.0
- Executable SHA-256:
  `DC236A27E5FCCB07D20A60F471E5C1252FBCD82D0505542DD8E4A15C1689E885`
- Manager profile: `<86Box VMs>\Win86SE`
- Direct launch: `C:\86Box\86Box.exe -P "<86Box VMs>\Win86SE"`

86Box 6.0 starts its VM Manager when invoked without `-P`; it does not use the
portable `C:\86Box\86box.cfg` in that mode. The Manager-created profile had an
unbootable 8,589,902,336-byte VHD. That image is preserved as
`Win98HDD.nonbooting-pre-repair.vhd`, and the working 4.2 baseline VHD was
copied into the profile as `Win98HDD.vhd`.

The original profile configuration and NVR directory are preserved in
`velocity9x-pre-s3-backup`. The portable `C:\86Box` config/NVR edits are also
preserved in `C:\86Box\velocity9x-pre-s3-backup`.

## Verified target configuration

The repaired profile now uses:

- Intel YM430TX with a 200 MHz Pentium MMX;
- 128 MB RAM;
- S3 ViRGE/DX as the primary display (`gfxcard = virge_dx_pci`);
- the 86C375 option ROM (`bios = virge375_pci`);
- no active secondary 3Dfx device.

Both ROM trees contain the same `roms\video\s3virge\86c375_4.bin` image with SHA-256
`DF0FBA7D82E734E000E51608D39B2E330AD0D21DBD615A6B83E5CFED1037931D`.

Windows 98 booted, detected `S3 ViRGE-DX/GX PCI (375/385)`, completed Plug and
Play installation, rebooted automatically, and reached the desktop. The Win32
COM1 smoke probe then completed the guest VCOMM-to-86Box-to-host path. 86Box's
File character device buffered the short lines until detach; use the documented
named-pipe capture for live driver diagnostics.

COM1 named-pipe Server mode with pipe name `velocity9x-com1` was subsequently
verified with `scripts/capture-serial-pipe.ps1`. It captured all four ring-0
dynamic-VxD lifecycle records immediately while the VM remained running.

## Guests by family

Each family manifest under `packaging/families/` names the guest that validates
it, so `run-vm-mode-matrix.ps1 -Family <id>` addresses the right machine rather
than the controller's default port.

| Family | Guest profile | Agent port | Notes |
|---|---|---|---|
| `s3-virge` | `Win86SE` | 9869 | The Velocity9x bring-up guest. |
| `s3-trio64` | `Win98SE-Trio64` | 9871 | A clone of the native-S3 guest, so its agent still reports ComputerName `WIN98-S3NATIVE`. Identify it by port, never by name. |
| `matrox-m2` | none | - | `Vm.Emulator = 'none'`: no emulator covers the MGA-2164W, so the VM runner refuses with a real-hardware-only error. |

The stock-driver reference guest `Win98SE-Native-S3` listens on 9870 and is
named by the S3 manifests as `ReferenceProfile`/`ReferencePort`. It carries the
retail S3 driver and is never a Velocity9x install target.

Remote agent 0.5.2 reports `BitsPerPixel` as 0 against the Velocity9x driver
while reporting it correctly against the stock S3 driver. Colour depth is
therefore verified from the guest-side `C:\V9XGDI.INI` result, not from the
agent's `info`.
