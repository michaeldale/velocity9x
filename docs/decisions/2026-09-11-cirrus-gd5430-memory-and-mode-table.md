# The GD5430 reads its own memory size, and its BIOS mode table needs reading carefully

Date: 2026-09-11. One BringupKit run on the FX-6300 Debian host
`bringup-target`, Cirrus Logic GD5430 [Alpine], `1013:00a0` rev 47 at
`0000:04:06.0`, no kernel driver bound, boot
`2cce947a-3d63-4cfa-8e6e-0a99a1df4acb`. Evidence in
[`docs/probe/cirrus-gd5430-2026-09-11`](../probe/cirrus-gd5430-2026-09-11/).
ROM: `CL-GD5440 VGA BIOS Version 1.00`, Copyright 1992-1995 Cirrus Logic.

The card is not supported by this driver and no code changes with this record.
Three things in it are worth having before anyone writes a Cirrus family: the
memory size is readable, the aperture is closed until something opens it, and
the BIOS mode table does not mean what the bundle's summary says it means.

## The memory size is in a register, and three readings agree

`SR0F` reads `0x11`, bits 4:3 = `0b10`, which the Alpine manual's
CL-GD5430/'40 column calls a 32-bit bus and 1 Mbyte. Bit 7, the DRAM Bank
Switch Control the manual scopes to the '5434/'36, is clear and does not
change the size on this part.

Two further readings agree without depending on that decode:

- The card's own BIOS reports `total_memory_blocks: 16` over VBE 4F00h, which
  is 1048576 bytes.
- No mode the BIOS advertises needs more than 1048576 bytes, and one needs
  exactly that: `0x106` at 1280x1024x4 planar is 256 bytes per scan line per
  plane, 1024 lines, four planes. `0x105` at 1024x768x8 needs 786432, which
  rules out a 512 KiB part.

This is a different situation from the NVIDIA cards, where nothing read says
how much memory is installed. A Cirrus backend can decode `SR0F` the way
`v9x_s3_virge_decode_memory_size` decodes `CR36` — host-testable arithmetic
over a register the mini-VDD reads, with the same structure and the same kind
of unit test.

`SR06` is what makes those reads trustworthy. It reads `0x12` when the
extension bank is unlocked and `0x0F` when it is locked, so a decode taken
from a locked bank cannot be mistaken for a real one. It reads `0x12` here.

## Installed memory is still not measured, and that is the right call

The aperture probe opened the Cirrus linear window, measured a 131072-byte
alias period, and restored the register — `sr07_before: 0`, `sr07_enabled:
0x10`, `sr07_after: 0`, `cirrus_linear_restored: true`.

That period is not the installed memory and the bundle does not claim it is.
`SR07` bit 0 is clear, so the card is not in extended 256-colour mode, and the
Alpine manual makes that the condition for the aperture to map one-to-one onto
display memory; it wraps otherwise. The card was in 80x25 text mode throughout
— `SR04` bit 3 clear, `GR05` bit 6 clear, `GR06` bit 0 clear — so 131072 bytes
is the period of the text-mode mapping.

Measuring it would need a 256-colour mode set that persists across two verbs,
which nothing in the kit currently does. Until then the 1 MiB figure is three
agreeing reads and no measurement, and the aperture figure is a measurement of
something else. Both are recorded; neither is preferred.

## The aperture is closed after POST, and SR07 only enables it

`SR07` bits 7:4 read `0x0`. Per the Alpine manual on this field for a PCI
part, that makes the chip respond at `Axxx:x`/`Bxxx:x` as a standard VGA — so
a frame buffer BAR returning all-ones and master-aborting is the expected
state, not a fault. Anyone bringing this chip up will see that first and
should not chase it.

Operator statement from the same manual, not measured here: on a PCI part the
field only enables, and the base address stays the one the kernel programmed
into the BAR. If that holds it is a real difference from S3, where `CR59` and
`CR5A` carry the aperture base and this driver programs them
([the ViRGE/DX record](2026-09-11-virge-dx-registers-confirmed-on-silicon.md)).
A Cirrus backend would enable, not relocate.

## The bundle's scan-line pitch warning is wrong on two of its three modes

It names `0x110`, `0x113` and `0x112` as modes where computing width times
bytes-per-pixel gives the wrong stride. Recomputed from the 16 mode records in
`cirrus-vbe-3.ndjson`:

| Mode | bpp | Reported pitch | width x ceil(bpp/8) | |
|---|---|---|---|---|
| `0x110` 640x480 | 15 | 1280 | 1280 | flagged, but agrees |
| `0x113` 800x600 | 15 | 1600 | 1600 | flagged, but agrees |
| `0x112` 640x480 | 24 | 2048 | 1920 | flagged, and genuinely padded |
| `0x102` 800x600 | 4 | 100 | 800 | not flagged |
| `0x104` 1024x768 | 4 | 128 | 1024 | not flagged |
| `0x106` 1280x1024 | 4 | 256 | 1280 | not flagged |
| `0x014`, `0x109`, `0x10a` 132-col | 4 | 264 | 132 | not flagged |

A 15bpp pixel occupies two bytes and the warning computes one, so the two
modes it singles out are the arithmetic working. What actually breaks the
naive product is the planar 4bpp modes, where bytes-per-scanline is per plane,
and `0x112`, padded to a 2048-byte boundary.

The correct statement for a family manifest: pitch is width times
`ceil(bpp/8)` for the packed 8, 15 and 16bpp modes; it is per-plane for 4bpp
planar; and `0x112` is padded. Followed as written, the bundle's version would
have someone guard two modes that are fine and trust six that are not.

The blast radius for this driver is small but not zero. `v9x_vbe_default_pitch`
takes its pitch from the family table through `v9x_selected_mode_geometry`
([ddi.c:823](../../src/display16/ddi.c:823)) rather than computing one, and we
do not drive planar modes — but the table is written by hand from exactly this
kind of BIOS dump.

## 4F06h declined, in text mode, which settles nothing

The get returned `0x014F` on both attempts, and VBE success is `0x004F`. The
write-up is careful that this says nothing about whether the call would answer
in a graphics mode, and no mode-set probe is in the bundle.

That is the same trap the ViRGE/DX record had to correct: asking 4F06h in text
mode is a fact about when it was asked. It is also why `v9x_vbe_default_pitch`
runs after `v9x_vbe_set_mode`. Note that the evidence's own
`scan_line.supported: false` field asserts what the prose declines to —
anyone reading the JSON directly gets the overreach.

EDID was called over 4F15h and did not answer, so this card has no EDID by
that route either.

## What is still unknown

No mode set, no write characterization, no installed-memory measurement, no
DOS survey, and the card has not been in a Windows 9x guest. The ROM is the
shadow at `0xC0000`, which POST may have patched, not the ROM as shipped; no
PROM-window read was done here to compare, though that route worked on the
NV4.

No family manifest is proposed on this evidence. The `SR0F` decoder and its
unit test could be written now, against the manual and this single reading,
but one reading on one card is what the `CR36` arm of the S3 decoder had for a
long time and that is worth remembering before trusting the other rows of the
table.

## Gates

Documentation only; no source file changed. `scripts/check-tree.ps1` run.
