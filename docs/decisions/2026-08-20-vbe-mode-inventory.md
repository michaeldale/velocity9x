# VBE mode inventory per target

Date: 2026-08-20
Status: **partial - BARRY measured, three targets outstanding**

Stage 0 of `docs\plans\high-depth-dynamic-modes.md`. The gate it sets is: no
baseline 24- or 32-bpp row is committed for a (mode number, depth) pair that
does not appear in a dump from the target that would use it. This record is
where the dumps land.

Taken with `tools\diag\vbe_inventory_dos.c` (query-only: 4F00h, 4F01h, 4F03h,
no mode set), which walks the BIOS's own `VideoModePtr` list and additionally
probes the standard high-colour numbers.

## Targets

| Target | Status |
|---|---|
| BARRY - physical S3 Trio64 86C764, 2 MiB | **measured 2026-08-20** |
| 86Box ViRGE/DX 86C375, 4 MiB (`9869`) | outstanding |
| 86Box Trio64 (`9871`) | outstanding |
| QEMU std-vga (`vbe` family) | outstanding |

The 86Box and QEMU dumps still gate the `s3` and `vbe` baseline rows. BARRY
alone cannot settle them: the `s3` family table is shared by the Trio64 and the
4 MiB ViRGE, and most of what BARRY refuses it refuses for want of memory
rather than for want of support.

## BARRY - physical S3 Trio64, 2 MiB

Collected through the remote agent's shell (`C:\V9XREMOTE\V9XVBE.EXE`), so
inside a Windows 98 DOS box rather than from a clean DOS boot. The answers are
self-consistent with the hardware - 2 MiB reported, pitches exactly
`width * bytes-per-pixel`, and every refusal explicable by memory - so they are
taken as the BIOS's own.

```
Signature=VESA  Version=0102  TotalMemory64K=32  (2 MiB)
VideoModePtr=C000:534F  ModeListCount=18  terminated=yes  overflow=no
```

| Mode | Geometry | bpp | Model | Attr | Linear? | BytesPerScanLine |
|---|---|---|---|---|---|---|
| 0101 | 640x480 | 8 | 4 | 001B | no | 640 |
| 0103 | 800x600 | 8 | 4 | 001B | no | 800 |
| 0105 | 1024x768 | 8 | 4 | 001B | no | 1024 |
| 0107 | 1280x1024 | 8 | 4 | 001B | no | 1280 |
| 0110 | 640x480 | 15 | 6 | 001B | no | 1280 |
| 0111 | 640x480 | 16 | 6 | 001B | no | 1280 |
| 0112 | 640x480 | **32** | 6 | 001B | no | 2560 |
| 0113 | 800x600 | 15 | 6 | 001B | no | 1600 |
| 0114 | 800x600 | 16 | 6 | 001B | no | 1600 |
| 0115 | 800x600 | **32** | 6 | 001B | no | 3200 |
| 0116 | 1024x768 | 15 | 6 | 001B | no | 2048 |
| 0117 | 1024x768 | 16 | 6 | 001B | no | 2048 |
| 0211 | 640x400 | **32** | 6 | 001B | no | 2560 |
| 0118, 0119, 011A, 011B | - | - | - | - | - | **status 014F, not supported** |

Also listed and correctly ignored: 0102/0104/0106 (4-bpp planar) and
0109/010A (132-column text, memory model 0).

### Findings

1. **0x0112 and 0x0115 are 32 bpp on this BIOS, not 24.** This is the question
   Stage 0 exists to answer, and the answer is unambiguous: `BitsPerPixel=32`,
   `BytesPerScanLine = width * 4`, and a reserved channel at `8@24` alongside
   red `8@16`, green `8@8`, blue `8@0`. There is no packed 24-bpp mode anywhere
   in this BIOS's list. **No 24-bpp baseline row may be written for the `s3`
   family on the strength of a VESA number.**

2. **0x0118 is absent, so there is no 1024x768 high-colour row for a 2 MiB
   card.** 1024x768x32 needs 3 MiB. Consistent with 0x011A (1280x1024x16,
   2.6 MiB) and 0x0119/0x011B also refusing. The BIOS declines these rather
   than offering modes it cannot back - which is why the doc's remark that "the
   2 MB Trio64 never even lists oversized modes" holds.

3. **1280x1024 at 8 bpp is available** (0x0107, 1.31 MiB) and 1280x1024 at
   16 bpp is not. A 1280x1024x8 row is justified for the Trio64; the ViRGE dump
   will say whether the 16-bpp one is.

4. **0x0211 is an extended number carrying 640x400x32**, above the standard
   range and therefore invisible to any static table. Small, but it is the first
   direct evidence that walking the list finds modes enumerating fixed numbers
   cannot.

5. **The BIOS is VBE 1.2 and reports no linear framebuffer for any mode.**
   Every mode has attribute bit 7 clear, `PhysBasePtr = 0` and
   `LinBytesPerScanLine = 0` - expected, because the linear framebuffer and the
   `PhysBasePtr` field were both introduced in VBE 2.0.

## Consequence for Stage D: the `s3` scan cannot contribute on BARRY

Two independent refusals, both correct, both before any mode is considered:

- `v9x_vbe_parse_controller_info` requires VBE 2.0 or later. This BIOS reports
  1.2, so the controller is rejected outright.
- `v9x_vbe_scan_accept` requires the linear-framebuffer attribute and a
  physical base above the first megabyte, neither of which a 1.2 BIOS supplies.

So enabling `MiniVddVbeCollect` for `s3` would yield a scan of zero admitted
modes on this card, and the family would fall back to its baseline table - which
is the outcome the plan already said it tolerates indefinitely. Stage D is
therefore **not blocked but pointless on the Trio64**, and its remaining value
is whatever the 86Box ViRGE reports. The 8 modes the Trio64 actually drives
today come from the static table and the family's own CR59/CR5A aperture hook,
not from VBE, and that does not change.

Worth stating plainly, because it is a limit of the design rather than a bug:
dynamic discovery needs a VBE 2.0 BIOS. On a card whose BIOS predates it the
driver can still drive high-colour modes - the mode numbers work, and 0x0112
and 0x0115 are right there in the list - but only from a table someone wrote,
because the BIOS will not describe a linear surface for them.

## Still to decide

Pending the three outstanding dumps:

- whether `s3` gets 640x480x32 (0x0112) and 800x600x32 (0x0115) rows. BARRY
  supports both; the ViRGE has to agree before they are shared by the family.
- whether the ViRGE, at 4 MiB, adds 1024x768x32 (0x0118) - which BARRY would
  then have to refuse at runtime. The `ValidateMode` VRAM check added in
  Stage A is what makes that safe: the row can exist for the family and be
  refused on the card that cannot hold it.
- whether 1280x1024x8 (0x0107) and x16 (0x011A) are offered on the ViRGE.
- what QEMU std-vga reports, which is the `vbe` family's baseline and the only
  target expected to report VBE 3.0 and a linear framebuffer.
