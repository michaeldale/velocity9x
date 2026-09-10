# The Millennium 2064W puts its framebuffer in BAR1, not BAR0

> **Both open questions were answered on 2026-09-10.** The card's own BIOS,
> executed on an emulated CPU with I/O passed through, advertises `0101h`,
> `0111h`, `0114h` and `0117h` with a linear framebuffer at `FD000000h` -
> which is this card's BAR1 base, so the BIOS confirms the inversion below
> independently - and a guarded `0519` candidate now exists with the per-chip
> BAR index this record said it would need. See
> [the 2064W is drivable by the VBE path](2026-09-10-the-2064w-is-drivable-by-the-vbe-path.md).

A bring-up-kit baseline was taken on an original Matrox Millennium
(`102B:0519`) to see whether the `matrox-m2` family could be extended to it.
The run answers one question with measurement and leaves the decisive one
untouched.

Evidence: `C:\everything\bringupkit\runs\fx6300-matrox-millennium-20260908`
(`baseline.json`, `inventory/pci.txt`, `vga-capture-physical.ndjson`). Host is
an FX-6300 board running Debian 13, kernel 6.12.107, boot id
`db6fb016-9c11-49ff-b5bd-b55125391fff`. The card is the boot VGA device at
`0000:04:06.0`, behind bridge `00:14.4`, IRQ 5.

## The card is a different chip, not a second sample

`102B:0519` rev 01 is the MGA-2064W (Storm plus TI TVP3026). The
`matrox-m2` family targets `102B:051B`, the MGA-2164W. The two are not
interchangeable, and this run is not a second data point for the supported
board.

Subsystem vendor and device both read `0000`. The 2064W does not implement
subsystem identification, unlike the physical Millennium II recorded in
`docs/decisions/2026-08-09-millennium2-physical-baseline.md` with
`SUBSYS_1200102B`. A hardware ID for this card cannot carry a `&SUBSYS_`
suffix.

## The aperture order is inverted between the two chips

Measured, from `inventory/pci.txt`:

| Region | Base | Size | Attributes | Function |
| --- | --- | --- | --- | --- |
| 0 | `FDCFC000` | 16 KiB | non-prefetchable | MGABASE1, control aperture |
| 1 | `FD000000` | 8 MiB | prefetchable | MGABASE2, framebuffer |

`docs/specifications/matrox-millennium2-bringup.md` records the opposite for
`051B`: `E0000000` for the 16 MiB framebuffer, `E1000000` for the 16 KiB
control aperture. Framebuffer first there, control aperture first here.

86Box's device model agrees independently, and says so in its PCI
configuration read at `build/reference/86box/src/video/vid_mga.c:6402`:
offset `10h` is the control aperture for Millennium and Mystique and the
framebuffer for Mystique 220 and later, with offset `14h` the reverse. The
boundary constant is `MGA_1164SG`, and `MGA_2164W` sorts above it. So the
swap is a property of the chip generation, not of one board's resource
allocation.

The consequence for this tree is in `src/display16/runtime.asm:1435`.
`V9XPCIREADBAR0` reads configuration offset `10h` and its header comment calls
it "a chip-agnostic INT 1Ah primitive". It is agnostic only across families
whose framebuffer happens to sit in BAR0. Accepting `0519` requires a per-chip
BAR index carried in the chip's `hw16` object, in the same way the PCI identity
already is. Widening the device list alone would hand the display code the
16 KiB MMIO window as its framebuffer.

## The alignment guard refuses this card, by accident

`V9XPCIREADBAR0` rejects a base that is not 16 MiB aligned, on the reasoning
that such a value is a read that went wrong. `FDCFC000 AND 00FFFFFFh` is
`CFC000h`, so the routine returns 0 and the driver declines before mapping
anything. A naive device-list addition therefore degrades to a refusal rather
than a DIB laid over control registers.

This is worth recording because it is luck, not design: the guard was written
to catch a malformed read and happens to catch a correctly read control
aperture. It also means the guard cannot be relaxed casually. `FD000000`, the
real framebuffer on this board, passes the same test — but only because this
chipset placed it on a 16 MiB boundary, which nothing requires it to do.

## What the run does not establish

The gating unknown is untouched. `matrox-m2` is a VBE mode-set plus a
scan-line pitch hook and no chip register writes; whether the 2064W is drivable
that way depends entirely on its BIOS. The Linux capture path has no real-mode
VBE, so `vbe.controller` and `vbe.modes` came back `unsupported`. Nothing here
says whether this card's BIOS offers `0111`, `0114` or `0117`, or how it
answers 4F06h.

Also absent, and needed before any candidate:

- ROM image: skipped, opt-in not given. The expansion ROM reads as
  `000C0000` virtual and disabled, so no BIOS strings or VBE tables.
- Vendor registers, EDID and the aperture probe: skipped or unsupported.
- Installed VRAM. The 8 MiB figure is the BAR window, not a memory
  measurement. The `matrox-m2` manifest hardcodes 4 MiB for the 2164W and
  nothing here transfers.

The VGA capture is standard 80x25 text: misc output `67h`, CRTC 0 through 2 at
`60h 4Fh 50h`. It shows the board comes up VGA-compatible on this host and
nothing chip-specific. `raw.vga.trust` is null, which is the report's
"Standard VGA trust: None".

One artefact of the kit rather than the card: the decoders that ran were
`s3-law/1` and `s3-identity/1`, which correctly emitted nulls with "Vendor
registers were not captured". There is no Matrox decoder in the bring-up kit,
so enabling the vendor opt-in on a re-run would not have decoded anything
either.

## What this leaves

Extending `matrox-m2` to the 2064W is not a device-list edit. It needs a
per-chip framebuffer BAR index, and before that it needs a DOS-side run on this
board with the VBE and ROM sections enabled, to decide whether the card is
drivable by the VBE path at all.

No code was changed and no gate was run for this document; it records a
measurement.
