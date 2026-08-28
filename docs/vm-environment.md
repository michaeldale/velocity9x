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
| `s3` / `-ChipId virge-dx` | `Win86SE` | 9869 | The Velocity9x bring-up guest. |
| `s3` / `-ChipId trio64` | `Win98SE-Trio64` | 9871 | A clone of the native-S3 guest, so its agent still reports ComputerName `WIN98-S3NATIVE`. Identify it by port, never by name. |
| `matrox-m2` | none | - | `Vm.Emulator = 'none'`: no emulator covers the MGA-2164W, so the VM runner refuses with a real-hardware-only error. |
| `ati` / `-ChipId mach64-vt2` | `Win98SE-Mach64VT2` | 9873 | Cloned from `Win98SE-Native-S3` 2026-08-16, so it too reports ComputerName `WIN98-S3NATIVE`. Identify it by port. |
| `ati` / `-ChipId rage-mobility-m` | none | - | Per-target `Emulator = 'none'`: 86Box emulates no Rage. Real hardware only, at `10.0.1.22`. |

### Cloning a guest profile - four things that will stop it booting

All four were hit creating `Win98SE-Mach64VT2`, and not one produces a useful
error message.

1. **86Box asks "This machine might have been moved or copied"** on the first
   start of a copied profile and blocks on that modal until answered. Until you
   answer, the window title stays `86Box` instead of the profile name, no VM
   display appears, no SLiRP port is forwarded and nothing is written into the
   profile - which together look exactly like a VM that failed to start. Answer
   **I Copied It**; that is also what regenerates network identity so the clone
   cannot collide with its source.
2. **The `uuid` in `86box.cfg` is copied too**, and the Manager's registry at
   `%LOCALAPPDATA%\86Box\vmm.ini` already maps that UUID to the source
   directory. Give the clone a fresh UUID and add a matching `vmm.ini` section
   (`system_name`, `config_file`, `config_dir`; forward slashes in the paths).
3. **`serial1_device = pipe`** carried over from a guest that used a named pipe
   for COM1 blocks startup waiting for a pipe client that does not exist. Set it
   to `file`, and point the COM log at a path of its own so it does not append
   to the source guest's log.
4. **`Start-Process -ArgumentList` does not re-quote**, so a VM path containing
   a space - and `86Box VMs` contains one - is split into two arguments and
   86Box silently opens something else. Pass one pre-quoted string.

The profiles live in `C:\Users\michael\86Box VMs`, directly under the user
profile rather than under `Documents`.

After a card change, Windows 98 boots once into 640x480x4 VGA fallback, asks to
restart, and on the second boot binds its own in-box driver. On the VT2 that is
`DXATI.INF` / "ATI Graphics Pro Turbo PCI (atim64 - VT)" at 1024x768x16, which
makes this guest a stock-driver reference as well as an install target.

The stock-driver reference guest `Win98SE-Native-S3` listens on 9870 and is
named by the S3 manifests as `ReferenceProfile`/`ReferencePort`. It carries the
retail S3 driver and is never a Velocity9x install target.

Remote agent 0.5.2 reports `BitsPerPixel` as 0 against the Velocity9x driver
while reporting it correctly against the stock S3 driver. Colour depth is
therefore verified from the guest-side `C:\V9XDIAG\V9XGDI.INI` result, not from the
agent's `info`.

## Win98SE-BX-Trio64 (added 2026-08-28)

The fleet's only P6-class Win98 guest. Cloned from `Win98SE-Trio64` and
reconfigured onto a 440BX board with a Pentium II, to test whether the
memory-type inspection's MSR path could be exercised in an emulator.

- Profile: `<86Box VMs>\Win98SE-BX-Trio64`
- `machine = 686bx`, `cpu_family = pentium2_deschutes`, `cpu_speed = 350 MHz`
- `gfxcard = s3_trio64_pci`, unchanged from its parent
- Agent port: host 9873 -> guest 9869

It answered the question with a no: 86Box emulates no MTRRs on any CPU, so
this guest reports the same `V9X_MTRR_NO_MTRR` as the Socket 7 ones (see
docs\decisions\2026-08-28-mtrr-stage-a-inspect-only.md).

It is kept regardless. It is the only guest here whose board has AGP and whose
CPU is P6-class, which is the right era pairing for the Voodoo3 work
(docs\plans\3dfx-voodoo3-family.md) in a way the Socket 7 boards are not.

Win98 required one interactive "restart to finish setting up your new
hardware" acknowledgement on first boot after the board change; nothing else.
No install media was needed - the chipset re-detection completed from what was
already on the image.

## Win98SE-BX-Voodoo3 (added 2026-08-28)

Track C Phase 1's guest (`docs\plans\3dfx-voodoo3-family.md`): a Voodoo3 on the
only P6-class board here, cloned from `Win98SE-BX-Trio64` because that guest
was kept for exactly this.

- Profile: `<86Box VMs>\Win98SE-BX-Voodoo3`
- `machine = 686bx`, `cpu_family = pentium2_deschutes`, 350 MHz, 128 MiB
- `gfxcard = voodoo3_3k_agp` (86Box ROM `video/voodoo/3k12sd.rom`)
- Agent port: host **9874** -> guest 9869; COM1 pipe `voodoo3-com1`

The model is a **placeholder pending Phase 0**. The physical card's board model
and bus are unmeasured, so a Voodoo3 3000 AGP was chosen as the era-correct
pairing for the 440BX; 86Box also offers 1000/2000/3500 and Banshee in both
bus variants, and re-pointing `gfxcard` is a one-line change.

Measured on the first boot, which needed no interaction at all - no "moved or
copied" modal, no restart prompt:

- Desktop at 640x480x4, `Standard PCI Graphics Adapter (VGA)`. The parent's
  Velocity9x Trio64 driver does not bind the new card, which is the wanted
  starting state for an install target.
- `HKLM\Enum\PCI` carries
  `PCI\VEN_121A&DEV_0005&SUBSYS_003A121A&REV_01`, class `030000`. That
  confirms the plan's expected identity (3Dfx `121A`, Voodoo3 `0005`) on the
  emulator; the physical card's ids are still Phase 0's job.

Two inherited oddities, neither yet a problem:

- The image carries a stale `3dfxzone.it FastVoodoo2 4.6` driver for
  `VEN_121A&DEV_0002`, from a Voodoo2 add-on the parent profile once had
  configured. This clone's config declares no Voodoo2. It did not bind the
  Voodoo3.
- `serial1_device = pipe` was kept from the parent rather than switched to
  `file` as the cloning notes advise. It did not block startup here.

The guest still reports the parent's ComputerName. Identify it by port.
