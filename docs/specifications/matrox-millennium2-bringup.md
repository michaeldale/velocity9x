# Matrox Millennium II bring-up boundary

> **Historical since 2026-10-09.** The guarded drop-in package, its recovery
> guard and the `matrox-m2` family this page describes were retired; the
> MGA-2164W is now a chip of the INF-installed `matrox` family
> ([plan](../plans/matrox-millennium-family.md)). The measurements below
> stand - in particular that replacing Matrox's mini-VDD with the
> Velocity9x one of the time corrupted surfaces on this board - and are the
> first things to check when a 2164W runs the current path.

Target: MGA-2164W PCI `102B:051B`. The first physical board is subsystem
`1200:102B`, revision `00`, with a TI TVP3026 RAMDAC and 8 MiB WRAM.

## Resource map

The physical Windows 98 Configuration Manager allocation and the MGA-2164W
specification agree:

| Physical range | Size | MGA name | Use |
|---|---:|---|---|
| `E0000000-E0FFFFFF` | 16 MiB | MGABASE2 | Direct framebuffer aperture |
| `E1000000-E1003FFF` | 16 KiB | MGABASE1 | Control aperture |
| `E2000000-E27FFFFF` | 8 MiB | MGABASE3 | ILOAD pseudo-DMA window |

The control aperture must never be confused with the framebuffer. Only the
installed VRAM portion of MGABASE2 is usable; this board reports 8 MiB.

## Proven read-only MMIO

The dynamic `V9XMGAQ.VXD` probe maps only the 16 KiB MGABASE1 allocation and
reads these documented registers:

- `STATUS` at `MGABASE1+1E14h`;
- `VCOUNT` at `MGABASE1+1E20h`;
- `OPMODE` at `MGABASE1+1E54h`.

Two physical samples at 1024x768x24 returned stable `STATUS=00020024h` and
`OPMODE=00000000h`; `VCOUNT` changed from `02BCh` to `00C0h`, proving that the
mapped address is live rather than a cached or unmapped page. No MMIO, I/O or
configuration write occurred.

## Recovery requirement

Physical candidates initially retain the stock filenames `MGAPDX64.DRV` and
`MGAPDX64.VXD` and do not change the `Display\0009` class association. The
validated `C:\V9XSAFE\MGA2` two-boot guard restores byte-identical stock files
before Windows loads on the second boot unless the first candidate boot is
explicitly disarmed.

The guard was validated through boot counters 3-5:

1. unarmed boot remained 1024x768x24 and left `LASTBOOT=PREPARED`;
2. first armed boot created `ARMED.2` and recorded `CANDIDATE-FIRST-BOOT`;
3. second boot recorded `ROLLED-BACK`, removed `ARMED.2`, and both restored
   files passed `FC /B` against their saved copies.

## First guarded activation result

Build `mga2-640x480x8-guard2` was tested on the physical board at boot counter
6. Its inactive Win16 query passed before installation. The DOS-time guard then
installed the candidate atomically and Windows reached a ready 640x480x8
desktop with `V9XBOOT.INI` reporting `Stage=enable-ok`. Both active files were
byte-identical to the staged candidate.

The desktop initially remained navigable, but the automated GDI test did not
complete and its framebuffer writes produced severe scan-line corruption. The
palette test consequently reported `palette-readback-mismatch`. The candidate
was rejected and the guard restored the byte-identical stock pair at boot
counter 7; the machine returned to a ready 1024x768x24 desktop.

This proves PCI matching, VBE mode entry, BAR0 selection, DPMI mapping, DIB
Engine enable and rollback. It does **not** validate the scan-line contract or
drawing. The next candidate must explicitly set and verify VBE function 4F06h
returns a 640-byte logical scan line before exposing the framebuffer to DIBENG.

Evidence is retained under `build/physical-mga-inventory`:

- `MGA2-GUARD2-DESKTOP.BMP` (ready desktop before the rendering stress);
- `MGA2-GUARD2-AFTER-GDI.BMP` (reproducible scan-line corruption).

## Next candidate

Before replacing either stock file, a DOS/VBE inventory must confirm the
board's mode numbers and linear-framebuffer addresses for 640x480x8,
800x600x8 and 1024x768x8. The first display candidate is then limited to
640x480x8, DIB Engine rendering and BIOS/VBE mode entry. It must obtain the
MGABASE2 address from the physical resource contract and contain no drawing
engine, pseudo-DMA, DAC-clock or acceleration writes. Candidate 2 additionally
uses VBE 4F06h to force and verify the 640-byte scan-line length.

Candidate 3 confirmed that enforcing the pitch does not fix first-write
corruption. Candidate 4 reproduced it at 640x480x16, proving the failure is not
limited to the TVP3026 palette path. Candidate 5 removed the inherited S3/VBE
bit-15 no-clear request. It reached a visually correct desktop, but the first
stock-validated GDI surface operations still produced a repeated, scan-line
corruption pattern. The same surface probe completed without corruption under
the restored stock Matrox driver, so this is a Velocity9x surface-write defect.

Candidate 6 tested `OPMODE.dirDataSiz=01` at 16 bpp. It did not correct the
geometry and changed only the corrupt color presentation. The MGA-2164W
definition shows why: `dirDataSiz` selects big-endian byte swapping; little
endian x86 must leave the field at `00` for every pixel depth. Candidate 6 was
rejected and the guard restored the stock driver at boot counter 17.

Candidate 7 keeps the conservative VBE mode/origin path and little-endian
`OPMODE=00`. Its isolated change is explicit initialization of the 48-byte
DIB Engine screen PDevice, following the working vmdisp9x implementation:
640x480 geometry, 1,280-byte delta and width, 16 bits per pixel, BAR0 selector,
zero surface offset, 5:6:5 format, and explicit bitmap-info/callback pointers.
The package build and S3 regression pass locally.

Candidate 8 packages the same display path with a NUL-safe, idempotent
`AUTOEXEC.BAT` guard installer. After correcting a cloned 86Box guest's stale
S3 display association and setting the requested mode to 640x480x16, the
strict `102B:051B` backend reached `Stage=enable-ok` and passed the generic GDI
surface probe on two consecutive boots. The guard was disarmed only after the
first pass.

The physical inactive query also passed, and the guarded candidate reached a
ready 640x480x16 desktop at boot counter 19 with `Stage=enable-ok`. The first
framebuffer capture was already visibly repeated and striped, however, and the
generic GDI probe returned `Result=FAIL` with exit code 3. Candidate 8 was
rejected. The still-armed guard restored the byte-identical stock pair on boot
counter 20, removed both armed markers and returned the machine to a clean
1024x768x24 desktop. See
`docs/decisions/2026-08-11-millennium2-86box-candidate8.md` for guest evidence
and the physical failure summary above.

The accepted stock-mini-VDD boundary installs only the
Velocity9x display DRV and uses `KEEPVXD.TAG` to make the guard stage the
target's validated stock `MGAPDX64.VXD`. The mixed pair passed first in 86Box,
then reached clean 640x480x16 physical desktops and generic GDI `PASS` results
at boot counters 21 and 22. The installed 9,726-byte candidate DRV and
75,704-byte stock VXD matched their staged SHA-256 hashes, and the guard was
disarmed after the confirmation pass. This proves the prior physical
surface-write corruption was introduced by replacing the board-specific
Matrox mini-VDD, not by the DIB Engine screen contract alone.

## Ready package

Build `mga2-ready-1024x768x16` expands the accepted DRV to 640x480x16,
800x600x16 and 1024x768x16. The ready package is forced to 1024x768x16 with a
2,048-byte pitch and preserves the stock Matrox mini-VDD. It passed the
inactive probe, boot and generic GDI test in 86Box, then reached a clean
1024x768x16 physical desktop and `Result=PASS` at boot counter 24. The guard
was disarmed after success. The reproducible package is
`build/matrox-candidate`.

## S3 feature-parity snapshot

As of 2026-08-11, the ready Millennium II package is a stable software-GDI
display driver, but it does not yet match the complete Velocity9x S3 feature
set.

| Feature | Millennium II | S3 |
|---|---|---|
| Windows 98 boot and stable desktop | Yes | Yes |
| Software GDI drawing | Yes | Yes |
| Advertised resolutions | 640x480, 800x600 and 1024x768 at 16 bpp | 640x480, 800x600 and 1024x768 at 8 and 16 bpp |
| Live same-depth resolution switching | No; the ready package is fixed at 1024x768x16 | Yes |
| DirectDraw HAL, VRAM surfaces, page flipping and vertical-blank status | No | Yes |
| Velocity9x mini-VDD | No; the accepted package preserves stock `MGAPDX64.VXD` | Yes |
| Hardware 2D acceleration | No | No |

The remaining parity work should be approached in this order:

1. Add reliable live switching between the three existing 16-bpp modes.
2. Implement the Matrox-specific DirectDraw surface, flip and vertical-blank
   paths.
3. Add and qualify 8-bpp modes and palette handling if they remain a project
   requirement.
4. Replace the stock Matrox mini-VDD only as a separate, guarded hardware
   bring-up effort; it is not required for the first three items.

Until those items are complete, describe the Millennium II target as
"ready software-GDI support," not as S3 feature parity.
