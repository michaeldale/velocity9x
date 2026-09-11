# CR36 and the aperture base, confirmed on a physical ViRGE/DX

Date: 2026-09-11. Two BringupKit runs on the FX-6300 Debian host, card at
`0000:04:06.0`, `5333:8a01`, no kernel driver bound - boots
`8d292e50-3ff0-4a29-85dd-20a857b0100c` and
`b8ee9ffc-d9f9-4970-a6af-324bc6cb567c`. Evidence in
[`docs/probe/virge-dx-registers-2026-09-11`](../probe/virge-dx-registers-2026-09-11/),
which holds the second.

Every register value and aperture reading below is identical across both
boots. This record originally cited only the first; the bundle was then
re-run at the same path, which is how it came to have two.

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

## The bundle's write-up: one claim fixed, one still wrong

Worth recording because the bundle is the citation.

**Fixed.** The first version headlined "This BIOS does not implement 4F06h",
which the raw event contradicted with `"called": false`. The current text is
better than that correction: the calls "did not complete under emulation, so
the call returned no status at all", and it says outright that this is a fact
about running the handler there and **not** evidence the BIOS lacks the
function, with a pointer to the mode-set section as the stronger evidence.
That is the right shape - it is the same error as the 2064W's `mgamode`, a
conclusion drawn from a measurement that was not taken, and it now says so.

**Still wrong: the `BL=02h` prose.** It reads "returns success and leaves the
length alone" and warns that a driver using it "will get a success code and an
unchanged pitch". The table directly above shows status `0x014f`, and
`AH=0x01` is failure, not success. That paragraph is carried over from the
Matrox run, where the byte form really did return `0x004f` and do nothing -
which is the dangerous case. **This BIOS reports the failure honestly**, so on
a ViRGE/DX the byte form is merely useless rather than treacherous. The
paragraph beginning "This took three attempts to establish" is Matrox history
too.

Either way the driver calls the pixel form, so nothing depends on which
reading is right.

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
BDF the 2064W used, so those runs are the same machine with cards swapped.
Nothing here was measured through the driver.

The first boot left the scan-line length at 2304 bytes, unrestored; the second
restores it to 2048, `pitch_restored: true`. An earlier version of this record
reported the unrestored figure as the run's side effect, which is now only
true of the boot it cited.
