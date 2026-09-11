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

`Win98SE-Fast-D3D` on 9878 is not in that table either, for the same reason
and one more: it is not a chip target at all. It is a ViRGE/DX forced into
software Direct3D on the fastest CPU here, kept to measure the rasterizer
rather than a card. See its section below.

`Win98SE-Trio32` on 9875 is not in that table on purpose: it validates a PCI id
the `trio64` chip already binds rather than a chip of its own, so no manifest
`Vm.Targets` row names it. See below.

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

Correction to trap 1, measured on 2026-08-29 creating `Win98SE-Trio32`: the
modal **cannot be answered programmatically**. 86Box 6.0 draws its dialogs in
Qt, so enumerating the process's windows returns class `Q` controls with no
captions - there is no button text to match and no native control to send
`BM_CLICK` to. Either click it by hand, or remove its cause: the check is tied
to the inherited NE2000 `mac`, and a clone given a fresh one came up with the
profile name in its title bar and no modal at all. Answering by hand is still
what regenerates network identity, so if you skip the modal, set the MAC
yourself rather than leaving the parent's.

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

## Win98SE-Trio32 (added 2026-08-29)

The guest that turned "the Trio32 publishes 8811, so we already drive it" from
an option-ROM reading into a boot. Cloned from `Win98SE-Trio64`.

- Profile: `<86Box VMs>\Win98SE-Trio32`
- `machine = ym430tx`, Pentium MMX 200 MHz, 128 MiB, unchanged from its parent
- `gfxcard = s3_trio32_pci`, `[S3 Trio32 PCI] memory = 2` - the real 86C732 is
  a 32-bit-bus part and tops out at 2 MiB, so the parent's 4 is not a figure
  this card can have
- Agent port: host **9875** -> guest 9869; COM1 logs to a file of its own at
  `build\vm-logs\trio32-com1.log`

It is **not** a manifest `Vm.Targets` entry, and should not become one while
the Trio32 is an id sharing the `trio64` chip entry rather than a chip. Run the
matrix against it with an explicit `-Port 9875 -ChipId trio64`.

Measured on the first boot
(`docs\decisions\2026-08-29-s3-trio32-alias-guest.md`):

- The card reports `5333:8811`, the PCI scan matched the `trio64` entry, and
  `V9XHW.INI` reads `Adapter=S3 Trio32/64 86C764` with CR36 decoding the
  configured 2 MiB correctly.
- Eight of the nine declared modes that fit 2 MiB pass with GDI acceleration.
  800x600x32 does not: its BIOS has no VBE `0115h`
  (`docs\issues\2026-08-29-trio32-lacks-vbe-0115.md`).

The guest still reports the parent's ComputerName. Identify it by port.

## Win98SE-Fast-D3D (added 2026-09-11)

The fleet's fastest guest, built to exercise the CPU rasterizer rather than a
chip. Cloned from `Win86SE`, so it arrives with Velocity9x already bound to a
ViRGE/DX and needs no display-driver install.

- Profile: `<86Box VMs>\Win98SE-Fast-D3D`
- `machine = cubx` (ASUS CUBX, 440BX), `cpu_family = celeron_mendocino`,
  `cpu_speed = 533 MHz`, 256 MiB
- `gfxcard = voodoo3_3500_agp` on the **`vbe` package**, installed by Have
  Disk from `C:\V9XVBE`. `86box.cfg.virge-backup` in the same directory is
  the ViRGE/DX configuration it was built with; the two are one `gfxcard`
  line apart and both boot from this image.
- `[Velocity9x] Direct3D=2` in the guest's `SYSTEM.INI`, so
  `V9XHW.INI` reads `Direct3DMode=software` and every Direct3D draw is served
  by the CPU
- Agent port: host **9878** -> guest 9869; COM1 pipe `fast-d3d-com1`

It reports ComputerName `WIN98-86BOX` from its parent. Identify it by port.

### Why this CPU and this card

**The CPU is the fastest this 86Box build has for the job, not the fastest
number it offers.** Build 9001 contains no Pentium III family at all - the
binary has no `katmai`, `coppermine` or `tualatin` string, and its P6 line
ends at `celeron_mendocino`. Two families clock higher and were rejected:
`c3_samuel` reaches 733 MHz (measured: a disk-less POST on `6via90ap` printed
"VIA CyrixIII / CPU Speed: 733MHz"), and the Socket 7 K6 parts reach the low
500s. The Cyrix III core is in-order with a weak FPU, so its extra clock is
not expected to beat a Mendocino's full-speed on-die 128 KiB L2 on this
workload - **expected, not measured**, and the cheap way to settle it is to
point this profile at `6via90ap`/`c3_samuel` and rerun `V9XSOFT`.

**The card is the fastest framebuffer the driver can reach at all**, which
is not the same as the fastest card it has a chip module for.

By *native* family, the ceiling is PCI: the ViRGE/DX this guest was built
with, which also runs the same scene through both of our engines by flipping
`Direct3D` between 1 and 2, and is why its configuration is kept beside the
current one. One correction to an earlier reading of the manifests: build
9001 **does** carry `trio3d2x_agp` with an 8 MiB ROM
(`video/s3virge/TRIO3D2X_8mbsdr.VBI`), so the `8A13` alias's note that "no
86Box profile exists for it" is stale and the s3 family does have an AGP
option. It has not been booted here.

By *any* family, the `vbe` package takes whatever has a VBE 2.0+ BIOS and a
linear framebuffer, which opens the AGP parts no chip module names. The
fastest of those is the Voodoo3 3500, and that is what this guest now runs:
`Adapter=Generic VESA adapter (no chip-specific support)`,
`PciVendorId=121A`, `PciDeviceId=0005`, `ModeSwitching=vbe-lfb`,
`VbeVramBytes=16777216`, `Acceleration=none`, `Direct3DMode=software`.
1024x768x16 sets and draws correctly.

### The AGP aperture reads twice as fast and writes slower

`V9XSOFT` on the two cards, same CPU, same boot-to-boot method, all 24 pixel
and depth hashes identical between them and the RAM column **exactly 1.00 on
every rung** - which is the control that says the only thing that changed is
the aperture:

| VRAM rung | ViRGE/DX PCI | Voodoo3 3500 AGP | |
|---|---|---|---|
| Read | 65.83 ms | 31.46 ms | **2.09x** |
| Bilinear | 152.0 ms | 85.0 ms | 1.79x |
| Depth | 187.5 ms | 106.3 ms | 1.77x |
| Alpha | 227.5 ms | 128.3 ms | 1.77x |
| Point | 44.41 ms | 29.04 ms | 1.53x |
| Small | 5.86 ms | 6.41 ms | 0.91x |
| Gouraud | 7.08 ms | 8.88 ms | 0.80x |
| Write | 5.64 ms | 9.26 ms | **0.61x** |

Reads across the AGP aperture are twice as quick and writes are two thirds
the speed, and every rung falls out of those two facts: the textured and
depth-tested scenes fetch far more than they store and gain 1.5x to 1.8x,
while the untextured fills are pure stores and lose. So the "fastest GPU"
question has no single answer for this renderer - it depends whether the
scene is reading texels or filling pixels.

Against the Pentium MMX 200 Trio64 guest this leaves software Direct3D about
2.5x faster on a video-memory target where the CPU alone bought 1.4x.

### What the first boot measured

`V9XSOFT` at boot 584, against the same binary on `Win98SE-Trio64` (Pentium
MMX 200, Trio64 PCI) earlier the same day. All 24 pixel and depth hashes are
identical between the two machines, which is the cross-check that this is the
same rasterizer and not a different one:

| Target | Small | Gouraud | Point | Bilinear | Depth | Alpha | Read | Write |
|---|---|---|---|---|---|---|---|---|
| RAM | 3.61x | 3.63x | 4.72x | 4.99x | 4.99x | 4.76x | 3.57x | 4.35x |
| VRAM | 3.30x | 2.83x | 1.79x | 1.49x | 1.41x | 1.43x | **0.95x** | 1.72x |

The RAM column is roughly the CPU ratio and a bit more. The VRAM column is the
point of the machine: **a 2.7x faster CPU buys 1.4x on a video-memory target,
and an aperture read is fractionally slower than it was on the Pentium MMX.**
Reading a texel across the PCI aperture costs what the aperture costs, and on
this guest the aperture, not the processor, is now what a textured draw waits
for. The gap between the two columns was about 2x on the Socket 7 guests; here
it reaches 7x for bilinear.

That made this the guest on which `D3DSoftSysMem=1` should have been worth
the most. It was timed here on 2026-09-11 and is worth **nothing** to Final
Reality, because DirectDraw never puts that application's textures in system
memory and advertising the capability does not move them
([record](decisions/2026-09-11-d3dsoftsysmem-buys-final-reality-nothing.md)).
It is off in this profile.

### Building it

Two things cost time and are worth writing down.

**Synthetic keyboard input does not reach the emulated machine on build 9001,
but synthetic relative mouse motion does** - at twice the requested
magnitude. The chipset change from the parent's 430TX raises Win98's
"Add New Hardware Wizard" for the 440BX bridge before the shell starts, so the
agent is not up to drive it; it has to be clicked through from the host with
`mouse_event` while 86Box holds the mouse capture. The driver came from
`C:\WINDOWS\INF\MACHINE2.INF` with no install media.

**The first boot after that wizard's restart came up with no Explorer** - the
agent answered, `DesktopReady` stayed false, and the agent's own reboot verb
could not work because it needs the shell. A hard reset of the emulator
process cleared it and every boot since has been clean.

Swapping to the Voodoo3 afterwards was much easier, because by then the agent
was up and its own `input` verb drives the guest properly. Windows fell back
to `vga.drv` on the card change, and the `vbe` package went on through
Display Properties -> Advanced -> Adapter -> Change -> "Display a list of all
the drivers" -> Have Disk -> `C:\V9XVBE`, which offers exactly one model:
"Velocity9x VBE-generic display (any VESA VBE 2.0+ adapter)". Two notes for
next time: the Have Disk path box starts at `A:\` and does not respond to
Ctrl+A, so clear it with End and a run of backspaces before typing; and the
first mode change after the install raises a Plug and Play Monitor wizard
that blocks the agent's screenshot verb until it is dismissed.
