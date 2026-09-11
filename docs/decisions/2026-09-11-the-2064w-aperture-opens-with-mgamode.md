# The 2064W's aperture opens with mgamode, and the card has 8 MiB

Date: 2026-09-11. One BringupKit run on the FX-6300 Debian host, card at
`0000:04:06.0`, boot `195e37e1-bf59-4176-a743-e60810f581ca`. Still no Windows
anywhere.

[The 2026-09-10 record](2026-09-10-the-2064w-is-drivable-by-the-vbe-path.md)
ended with a list of what was still owed. The first item was a mode set, and
two of its findings were explicitly provisional: the aperture took only
2-byte accesses, and installed memory was not measured. This run does all
three, and two of those findings turn out to have been measuring the wrong
thing rather than the card.

## Read this before trusting the older record's citation

**The bundle path was reused.** `runs\handoff-matrox-2064w-full` is cited by
the 2026-09-10 record as boot `adde426f-e25f-48b3-9a25-a9e9fb75468c` with a
`sha256sums.txt` covering 14 files. The directory at that path now holds a
different run: boot `195e37e1-bf59-4176-a743-e60810f581ca`, 15 files, with an
aperture probe and a mode probe the earlier one did not have. The older
evidence at that path is gone.

The parts of this run that decide anything are copied into
[`docs/probe/matrox-2064w-full-2026-09-11`](../probe/matrox-2064w-full-2026-09-11/)
so that the next re-run cannot overwrite them too.

## The aperture was never 2 bytes wide; the mode bit was off

The older record says the framebuffer aperture "accepts only 2-byte accesses
in the card's current mode", with a 1 MiB alias period, and calls that **the
open risk to a DIB on this card, because the engine writes dwords**. It also
says measuring the real window "would need the card in power-graphics mode
with its memory controller configured, which is a mode set and out of scope".

Neither is so. The aperture is gated by a single bit:

> MGA-2064W Specification 10470-MS-0300, Feb 1996, CRTCEXT3 `mgamode<7>`,
> p.4-124, and again at p.4-12 under MGABASE2: "When mgamode = 0
> (CRTCEXT3<7>), the full frame buffer aperture is not available."

The firmware leaves that bit clear. With it set for the duration of the probe
and restored afterwards - CRTCEXT3 `0x00` → `0x80` → `0x00`, restored yes -
32-bit accesses round-trip faithfully at offset 0 and at every probed offset
from 64 KiB to 4 MiB. No clock, no CRTC timing and no memory controller
setting was written, and the probe is the same code that produced the 2-byte
result without the bit, so the difference is in the card rather than in the
measurement.

**The open DIB risk is closed.** Whether the *driver* has to do anything
about it is a separate question this run does not answer, and the first
version of this record overstated it.

`mgamode` appears nowhere in the mode probe: it was read only by the aperture
probe, with the card at rest in mode 3. A VBE linear-framebuffer mode exists
to expose that aperture, so the likeliest reading by far is that the card's
own BIOS sets the bit as part of setting `0117h`, and that the probe saw it
clear because text mode is where it found the card. Nothing in `src/` writes
CRTCEXT3, and on this evidence nothing should: the family is a guarded
candidate whose whole boundary is VBE calls and no chip register writes, and
adding one on an inference would spend that boundary for nothing.

**What would settle it** is one more run: read CRTCEXT3 while the card is in
`0117h`. That is a register read in a mode the kit can already set.

## Installed memory is 8 MiB on this card

The older record declines to call the BIOS's 8 MiB figure a measurement,
because it is exactly the BAR1 window. Three independent readings now agree
that the memory is really there:

- the alias probe wrote a distinct marker at every power of two from 64 KiB
  to 4 MiB and none of them folded back to offset 0, giving an alias period of
  8388608 bytes;
- the mode probe, in `0x117`, has the BIOS report 4096 scan lines at 2048
  bytes, which is 8 MiB;
- 4F00h reports 128 blocks of 64 KiB.

The first of those is real MMIO to the card and does not go through the BIOS
at all, which is what makes it worth more than the other two.

**This is a fact about this card, not about the part.** The MGA-2064W shipped
in several memory configurations, so another 2064W may hold 2 or 4 MiB. The
manifest's `VideoMemoryBytes = 2097152` therefore stays where it is: what is
corrected is its stated reason, which was that the figure was unmeasurable,
not the conservative number itself. The runtime heap does not depend on it in
any case - `enable16.c` sizes from the 4F00h total on the VBE path, which is
what `VbeVramBytes` in `V9XHW.INI` reports.

## A mode has been set on this card

`0x117`, 1024x768x16. Entry mode `0x0003` read by 4F03h rather than assumed,
4F02h returned `0x004f`, 4F03h confirmed `0x0117` active, and the card was
put back to mode 3 and verified. Every step through the card's own BIOS with
no register written directly.

The 2026-09-10 record's owed item 1 - "nothing has set one on this card, not
the kit, which refuses to, and not Windows" - is half discharged. The kit now
does. Windows still has not, and no pixel has been drawn.

## 4F06h in bytes is a silent no-op on this BIOS

Setting the logical scan line length:

| Subfunction | Requested | Result |
|---|---|---|
| `BL=02h`, bytes | 2304 | `0x004f`, length unchanged at 2048 |
| `BL=02h`, bytes | 4096 | `0x004f`, length unchanged at 2048 |
| `BL=00h`, pixels | 1152 | `0x004f`, length moved to 2304 bytes |

The byte form returns success and does nothing, including when asked for a
value the pixel form then produces exactly. A driver setting the pitch that
way gets a success code and an unchanged pitch, which presents as a corrupted
display and reads like a driver bug.

**This driver already uses the pixel form**, in both places that call 4F06h:
`v9x_mga2_int10_scan_line(0x4f06u, 0x0000u, ...)` in
[mga2_hw16.c](../../src/chipsets/matrox/millennium2/mga2_hw16.c) and
`v9x_int10_scan_line(0x4f06u, 0x0000u, pixels)` in
[vbe16.c](../../src/display16/hw/vbe16.c). Checked rather than assumed. No
change needed; the hazard is recorded because the next family to call 4F06h
will not have this run in front of it.

`4F06h BL=03h` reported a maximum of 64 bytes per scan line while the current
length was 2048, and left one output register untouched. Its outputs are not
trustworthy on this BIOS.

## Twenty linear-framebuffer modes, not four

The older record names `0101h`, `0111h`, `0114h` and `0117h` because those
are the four the family asks about. The BIOS offers 24 modes, 20 of them with
a linear framebuffer at `0xFD000000`: 8 bpp at 640x400, 640x480, 800x600,
1024x768, 1280x1024 and 1600x1200; 16 bpp at 640x480, 800x600, 1024x768,
1280x1024 and 1600x1200; and 32 bpp at 640x480, 800x600 and 1024x768.

The 800-wide padding the older record found is confirmed and is narrower than
it looked: `0x114` pads 1600 to 1920 and `0x103` pads 800 to 960, but
640-, 1024-, 1280- and 1600-wide modes are not padded at all, and 800x600x32
is not either. So it is 8 and 16 bpp at 800 wide specifically.

Widening the manifest's claimed mode list is a change to a shipped package
and is not made here.

## What this still does not establish

**No Windows, and no pixel.** Everything above is the kit and the card's own
firmware. Nothing has drawn through the driver on this hardware.

**The mode list and the mode set are emulated-int10.** The card's BIOS runs on
an emulated CPU with I/O passed through to real hardware. That is stronger
than a register read and weaker than a machine that booted. The aperture and
alias figures are the exception - they are direct MMIO and involve no BIOS.

**8 MiB is this card.** See above; it does not license a manifest change.

**The run has side effects.** It disturbed framebuffer contents, wrote and
restored CRTCEXT3 bit 7, and cleared the declared bridge's master-abort latch
irreversibly. The VBE info verb also provoked a master abort while running.
