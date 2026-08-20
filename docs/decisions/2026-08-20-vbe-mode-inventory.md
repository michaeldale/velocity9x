# VBE mode inventory per target

Date: 2026-08-20
Status: **four S3 targets measured; QEMU std-vga outstanding**

Stage 0 of `docs\plans\high-depth-dynamic-modes.md`. The gate it sets is: no
baseline 24- or 32-bpp row is committed for a (mode number, depth) pair that
does not appear in a dump from the target that would use it.

Taken with `tools\diag\vbe_inventory_dos.c` (query-only: 4F00h, 4F01h, 4F03h,
no mode set), which walks the BIOS's own `VideoModePtr` list and additionally
probes the standard high-colour numbers. Raw dumps alongside this file.

## Targets measured

| Target | Card | VRAM | VBE | Listed | Dump |
|---|---|---|---|---|---|
| BARRY, physical | S3 Trio64 86C764 (`5333:8811`) | 2 MiB | **1.2** | 18 | `...-barry.txt` |
| 86Box `:9869` | S3 ViRGE/DX 86C375 (`5333:8A01`) | 4 MiB | **1.2** | 25 | `...-p9869-virge.txt` |
| 86Box `:9870` | ViRGE/DX, native S3 driver | 4 MiB | **1.2** | 25 | `...-p9870-virge-native.txt` |
| 86Box `:9871` | S3 Trio32/64 86C764 (`5333:8811`) | 4 MiB | **1.2** | 22 | `...-p9871-trio64.txt` |
| QEMU std-vga | - | - | - | - | **outstanding** |

`:9870` reports a mode list byte-identical to `:9869`, as expected of the same
emulated card, and is not treated as separate evidence.

## The two findings that matter

### 1. There is no 24-bpp mode on any S3 target. 0x0112/0x0115/0x0118 are 32 bpp.

Every one of them reports `BitsPerPixel=32`, a scan line of exactly `width * 4`,
and a channel layout of red `8@16`, green `8@8`, blue `8@0` with a reserved byte
at `8@24`. Four independent BIOSes agree, including a real card and two
different emulated chips.

So the depth those numbers carry is a per-BIOS fact, as suspected - and for the
whole `s3` family the answer is 32. **No 24-bpp row may be written for `s3` on
the strength of a VESA number.** The plan's Stage A wording ("24-or-32bpp at
640x480/800x600/1024x768") resolves to 32 here.

Also present and correctly ignored: 15-bpp modes on every target (0x0110,
0x0113, 0x0116, and 0x0119 on the 4 MiB cards). `v9x_vbe_scan_accept` and
`v9x_mode_calculate` both refuse them, which is the right answer - the layout
maths cannot express a depth that is not a whole number of bytes.

### 2. No S3 target reports a linear framebuffer. Dynamic discovery cannot work on any of them.

Every mode on every S3 target has attribute `001B` - bit 7 clear -
`PhysBasePtr = 0` and `LinBytesPerScanLine = 0`. That is exactly what a VBE 1.2
BIOS should say, because the linear framebuffer and the `PhysBasePtr` field were
both introduced in VBE 2.0, and all four of these BIOSes report 1.2.

Two filters therefore refuse the whole scan, both correctly by their own terms:

- `v9x_vbe_parse_controller_info` requires VBE 2.0 or later, so the controller
  is rejected before any mode is examined.
- `v9x_vbe_scan_accept` requires the linear attribute and a physical base above
  the first megabyte, neither of which a 1.2 BIOS supplies.

**Consequence: Stage B's dynamic discovery contributes nothing to the `s3`
family, and Stage D is a no-op rather than a risk.** Flipping
`MiniVddVbeCollect` to `$true` would run the walk, admit zero modes and fall
back to the baseline table. The plan already said the architecture tolerates
`s3` staying baseline-static indefinitely; that is now measured rather than
assumed, and it is the permanent answer for these cards rather than a soak
result pending.

The remaining value of the dynamic scan rests entirely on QEMU std-vga, whose
BIOS reports VBE 3.0. That dump is still outstanding and is now the only thing
that can justify Stage B at all.

#### The filter is right for tier-0 and wrong for a family with an aperture hook

Worth separating, because it is a design question rather than a measurement.

These modes are perfectly drivable. The driver runs 0x0111/0x0114/0x0117 on
BARRY today at 1024x768x16, and none of those advertise a linear framebuffer
either. The `s3` family never asks the BIOS where the aperture is: it reads
CR59/CR5A through its `read_aperture` hook and enables linear addressing itself
in CR58 via `v9x_s3_enable_linear_aperture`. The BIOS's opinion about linearity
is irrelevant to it.

So requiring attribute bit 7 and a non-zero `PhysBasePtr` is correct for tier-0,
which has no other way to find the framebuffer, and is the wrong test for a
family that supplies its own aperture. If the scan is ever to be useful on
`s3`, `v9x_vbe_scan_accept` needs to take "the family knows where the aperture
is" as a parameter and relax those two checks when it holds.

Not changed here. It would widen what the scan admits on cards whose BIOS has
told us less, which is the opposite of the direction this plan has been taking,
and there is no evidence yet that it buys anything a static row does not.
Recorded as the open question it is.

## Per-target mode lists

Text and 4-bpp planar modes (memory model 0 and 3) omitted throughout; they are
listed by every target and refused on memory-model grounds.

### BARRY - physical Trio64, 2 MiB, 18 listed

| Mode | Geometry | bpp | bytes/scanline |
|---|---|---|---|
| 0101 | 640x480 | 8 | 640 |
| 0103 | 800x600 | 8 | 800 |
| 0105 | 1024x768 | 8 | 1024 |
| 0107 | 1280x1024 | 8 | 1280 |
| 0110 | 640x480 | 15 | 1280 |
| 0111 | 640x480 | 16 | 1280 |
| 0112 | 640x480 | **32** | 2560 |
| 0113 | 800x600 | 15 | 1600 |
| 0114 | 800x600 | 16 | 1600 |
| 0115 | 800x600 | **32** | 3200 |
| 0116 | 1024x768 | 15 | 2048 |
| 0117 | 1024x768 | 16 | 2048 |
| 0211 | 640x400 | **32** | 2560 |
| 0118, 0119, 011A, 011B | | | **014F, not supported** |

`0x0211` is an extended number outside the standard range carrying 640x400x32,
and **no other target lists it**. It is the only direct evidence so far that
walking the list finds modes enumerating fixed numbers cannot - and it is on the
one target where the scan can never run.

The four refusals are all memory: 1024x768x32 needs 3 MiB, 1280x1024x16 needs
2.5 MiB. The BIOS declines modes it cannot back rather than offering them, which
is the behaviour the plan assumed.

### ViRGE/DX 4 MiB (`:9869`, `:9870`) - 25 listed

Everything BARRY lists except `0x0211`, plus:

| Mode | Geometry | bpp | bytes/scanline |
|---|---|---|---|
| 0100 | 640x400 | 8 | 640 |
| 010D / 010E / 010F | 320x200 | 15 / 16 / **32** | 640 / 640 / 1280 |
| 0118 | 1024x768 | **32** | 4096 |
| 0119 | 1280x1024 | 15 | 2560 |
| 011A | 1280x1024 | 16 | 2560 |
| 0120 | 1600x1200 | 8 | 1600 |

### Trio64 4 MiB in 86Box (`:9871`) - 22 listed

The ViRGE list minus the three 320x200 modes (`010D`/`010E`/`010F`). Same
0x0118, 0x0119, 0x011A and 0x0120 as the ViRGE - so at 4 MiB the emulated
Trio64 offers what the ViRGE does, and BARRY's shorter list is its 2 MiB and its
older ROM, not the chip.

## What this justifies for the shared `s3` table

The `s3` family table is one list shared by the ViRGE and both Trio64s, so a row
has to be judged against the weakest target that will load it - BARRY.

**Safe on every target:**

| Row | Mode | Pitch | Notes |
|---|---|---|---|
| 640x480x32 | 0x0112 | 2560 | 1.23 MiB, fits 2 MiB |
| 800x600x32 | 0x0115 | 3200 | 1.83 MiB, fits 2 MiB |
| 1280x1024x8 | 0x0107 | 1280 | 1.31 MiB, fits 2 MiB; english 508/254 |

**Safe only because `ValidateMode` now refuses them on a small card** - listed by
both 4 MiB targets, absent on BARRY purely for want of memory, so the Stage A
VRAM check is what makes the shared row honest:

| Row | Mode | Pitch | Needs |
|---|---|---|---|
| 1024x768x32 | 0x0118 | 4096 | 3 MiB |
| 1280x1024x16 | 0x011A | 2560 | 2.5 MiB |

**Not safe, and the interesting case:** 1600x1200x8 (`0x0120`, pitch 1600) needs
1.83 MiB and *would* fit BARRY's 2 MiB - but BARRY's BIOS does not list the mode
at all. The VRAM check cannot catch this: it is a BIOS-support absence, not a
memory one, so the row would pass validation and fail at 4F02h, taking the
staged-failure and VGA-fallback path. Either leave the row out, or accept that
one target fails a mode it advertises. **Recommendation: leave it out.** The
same reasoning excludes the ViRGE-only 320x200 modes, which are also a
ModeX-adjacent path the driver does not otherwise support.

This is the asymmetry worth remembering from Stage 0: a shared family table can
be protected against a card with too little memory, and cannot be protected
against a card whose BIOS simply lacks the mode.

## Still outstanding

- **QEMU std-vga**, for the `vbe` family - and now the only target that can
  justify Stage B, since no S3 BIOS will feed the scan.
- The 86Box Mach64 VT2, for `ati`. Not reachable in this session; and read D5 in
  `docs\issues\2026-08-16-tier0-defects-deferred.md` before trusting anything
  that card renders.
