# CR36 and the aperture base, confirmed on a physical ViRGE/DX

Date: 2026-09-11. One BringupKit run on the FX-6300 Debian host, card at
`0000:04:06.0`, boot `8d292e50-3ff0-4a29-85dd-20a857b0100c`. `5333:8a01`, no
kernel driver bound. Evidence in
[`docs/probe/virge-dx-registers-2026-09-11`](../probe/virge-dx-registers-2026-09-11/).

Two decoders this driver has relied on since the first S3 work had only the
databook and 86Box's model behind them. Both are now checked against the
card.

## CR36 decodes 4 MiB, and the memory stops at 4 MiB

`CR36` reads `0x12`. Bits 7:5 are 0, which `v9x_s3_virge_decode_memory_size`
maps to 4 MiB.

The card's aperture stops decoding at exactly 4,194,304 bytes: everything
below is distinct storage and the first address at or past it reads all-ones.
That is a decode edge rather than an alias period, so it says where the memory
ends rather than what the mapping repeats at.

The decoder and the silicon agree. `V9X_S3_MEMORY_CODE_SHIFT`/`_MASK` and the
code-0 arm are confirmed; the other arms are not, and still rest on the
databook.

## CR59 and CR5A hold the PCI BAR's base

`CR59` reads `0xD8` and `CR5A` reads `0x00`. BAR0 is at `0xD8000000`, 64 MiB.
So the S3 linear-aperture base registers carry the top bytes of the BAR, which
is what this driver assumes when it programs them.

`CR58` reads `0x03` with bit 4 clear - the aperture disabled - which is the
expected state for a card sitting in text mode.

Chip identity from the same read: `CR2D` `0x8A`, `CR2E` `0x01`, matching the
PCI id.

## The aperture takes 32-bit accesses

`access_width: 4`, `readback_faithful: true`, restored. The DIB engine writes
dwords, so this is the same question that was open on the 2064W, and on this
chip it is clean.

## 4F06h works, and our probe asks at the right time

`v9x_vbe_default_pitch` runs after `v9x_vbe_set_mode`
([enable16.c](../../src/display16/enable16.c)). Its comment argued for that
ordering on reasoning - "a mode set does not settle this" - and this run is
the measurement behind it. In mode `0x117` the 4F06h get returns `0x004f` with
2048 bytes, and the pixel set moves it to 2304.

Only the pixel form is usable, which is what the driver calls: `BL=03h` fails
outright with `0x14F`, and the run's own byte-form attempts also came back
`0x14F`.

## Two claims in the bundle's write-up are wrong

Worth recording because the bundle is the citation, and both would mislead.

**"This BIOS does not implement 4F06h" is false.** The raw event says
`"called": false` - the call was never issued, and the `status: 0` the
write-up reads as a failure is an uninitialised default. The mode probe on the
same card then exercises 4F06h successfully. This is the same error as the
2064W's `mgamode`: a conclusion drawn from a measurement that was not taken.

**The `BL=02h` table's prose is carried over from the Matrox run.** It says
the byte form "returns success and leaves the length alone", but the status
shown is `0x014f`, and `AH=0x01` is failure. On the Matrox BIOS the byte form
really did return `0x004f` and do nothing, which is the dangerous case; this
BIOS reports the failure honestly.

## Also true, and smaller

- VBE **1.2** (`0x102`), OEM `S3 Incorporated. 86C375/86C385`. Below the
  `vbe` family's stated VBE 2.0+ contract, so that package could not claim
  this card on its own terms. Irrelevant in practice - the `s3` family drives
  it natively - but it is the first time the version has been read off the
  card.
- **EDID over `4F15h` returns nothing.** The mini-VDD collects block 0 through
  that call, so on this chip it will come back empty. Consistent with VBE 1.2,
  where `4F15h` does not exist.
- SR decodes modulo 64 and GR modulo 16, exactly, across all 256 indexes.
  There is no vendor register space behind either port pair.

## What this does not answer

**The ZRGB1555-into-RGB565 mismatch is untouched.** That needs the S3D engine
drawing, which a baseline capture cannot reach, and it remains the open
question this chip owes.

**The XFree86 "32 bpp limited to under 1024 pixels wide" claim gets no help.**
The run enumerated 25 VBE modes and queried none of them, so there is no
pitch or linear-framebuffer data in it at all.

**This is not A8U4I5's card.** It is a ViRGE/DX on the kit host at the same
BDF the 2064W used, so the two runs are the same machine with cards swapped.
Nothing here was measured through the driver.

The run left the scan-line length at 2304 bytes, not restored. Harmless with
the card back in mode 3.
