# The S3 pedestal bit: black comes out dark grey

Status: **open, not attempted.** Cause is known and externally confirmed, the
fix is a single bit, and the register index is confirmed on one chip that is not
one of ours. Nothing here has been run on our hardware.

## The symptom

Every S3 card in the Trio64 / Trio 3D / ViRGE families outputs black as a
washed-out dark grey rather than black. On a CRT with a brightness control it
reads as poor contrast and gets dismissed as monitor setup. On an LCD there is
nothing to compensate with, so it is simply wrong, and it is wrong from the POST
screen onward - before any driver loads.

This is the quirk referred to on Vogons as the S3 "high pedestal" BIOS quirk,
and it is the mechanism behind a scattering of cells in
`docs\specifications\dos-vbe-conformance.md` that otherwise look like unrelated
LCD complaints - `some light hues are white on LCD`, `few light hues are white
on LCD`. Raising the black floor compresses the whole range toward white, so
clipping at the top and a grey floor at the bottom are one fault.

## Cause

Not a bug. A deliberate NTSC-era feature, left switched on for VGA output.

Composite video needs black to sit slightly above the blanking level so a
television can tell "black picture" from "the pulse that starts the next line".
The offset that guarantees this is the **pedestal**, a 50 mV lift of the black
level (NTSC setup, conventionally 7.5 IRE). S3 shipped their BIOSes with it
enabled, which is defensible for a card driving a TV and wrong for one driving a
VGA monitor - the monitor has no blanking ambiguity to resolve and simply
renders the lifted floor as grey.

Every S3 part carries the bit. What varies is whether the shipped BIOS sets it.

## The register

Reverse-engineered and published in April 2026 by Bits und Bolts, who found it
with `DEBUG` before patching a BIOS. Verbatim from that write-up:

> Using the S3 data sheets as a map, I used the MS-DOS DEBUG application to talk
> to the chip's sequencer (Index 3C4h, Data 3C5h). After unlocking the chip with
> a specific key (06h), I began hunting through the addresses. On this specific
> Trio 3D 2X, the magic happened at Address 27h.

| | |
|---|---|
| Access | S3 extended **sequencer** registers: index `3C4h`, data `3C5h` |
| Unlock | write `06h` to sequencer index `08h` |
| Register | sequencer index **`27h`** |
| Pedestal on | `08h` - bit 3 set |
| Pedestal off | `00h` |
| Confirmed on | S3 Trio 3D/2X |
| Reported affected | Trio 3D, Trio 3D/2X, Trio64, Trio64V+, TrioV2, ViRGE, ViRGE/DX |
| Reported unaffected | Vision 968 with VRAM |

The author's live `DEBUG` test is the important part of that quotation: **the bit
takes effect immediately at runtime and the screen "instantly snapped from grey
to a perfect, deep black."** A BIOS flash makes it permanent and survives POST,
but it is not required to change the output. That is what makes this a candidate
for a driver rather than only for a hex editor.

He also published patched BIOSes for ViRGE and TrioV2 alongside the Trio 3D/2X
one, which implies the index generalises across the family - but he only claims
to have confirmed `27h` on the Trio 3D/2X, and the phrasing "on this specific
Trio 3D 2X" is doing real work. **Treat `27h` as unverified on ViRGE/DX and
Trio64 until it is read off an S3 datasheet for those parts.**

For reference, the permanent form of the fix is: dump the ROM, find the sequence
where the BIOS unlocks the sequencer and writes `27h`, change the associated
`08h` to `00h`, then add 8 to the image's checksum byte so the sum still ends in
`00h` (his went from `8Bh` to `93h`).

## Why this is cheap for us specifically

`v9x_s3_publish_diagnostics` in `src\chipsets\s3\common\s3_regs16.c:286`
**already does exactly this access pattern**, correctly, to read the SR10/SR11
clock PLL:

```
v9x_port_out(0x03c4u, 0x08u);
saved_unlock = v9x_port_in(0x03c5u);
v9x_port_out(0x03c5u, 0x06u);
sr10 = v9x_s3_read_sequencer(0x10u);
sr11 = v9x_s3_read_sequencer(0x11u);
v9x_port_out(0x03c4u, 0x08u);
v9x_port_out(0x03c5u, saved_unlock);
v9x_port_out(0x03c4u, saved_index);
```

Same ports, same `06h` unlock key, index saved and restored around it. Reading
`27h` inside that existing window and publishing it costs an indexed read and a
key - a handful of bytes, no new mechanism, no new failure mode.

## Proposed work, in the order it should happen

**Step 1 - publish it, change nothing.** Add the `27h` value to `V9XHW.INI` as a
diagnostic. This is read-only, cannot regress anything, and immediately tells us
whether the bit is even set on the ViRGE/DX guest and on the physical Trio64. It
also builds the evidence that step 2 needs, on both our chips, before any write
is attempted.

**Step 2 - confirm the index against S3 documentation** for the ViRGE/DX
(86C375) and Trio64 (86C764) specifically. Not optional; see the hazard below.

**Step 3 - clear bit 3 at Enable**, read-modify-write, preserving the other
seven bits, inside the same unlock window. Opt-in, because it changes what the
user sees on a machine they may have already colour-calibrated around: a
`V9X.INI` setting whose default is "leave the BIOS setting alone" is the honest
default, and "the driver silently changed my monitor output" is a bad bug report
to receive.

## Hazard: a mis-indexed sequencer write on an S3 is not benign

The neighbourhood matters. **SR10 and SR11 are the memory-clock PLL** - the
driver reads them a few lines above where this write would go, and
`v9x_s3_virge_decode_clock_pll` exists precisely because they determine MCLK.
Other extended sequencer registers on these parts control memory timing and
clock selection.

So the failure mode of getting the index wrong is not "the brightness does not
change". It is potentially reprogramming a clock on a live display - a black
screen at best, and on the VLB 486 there is no second output to recover through.
This is why step 2 is a gate rather than a nicety, and why step 1 exists at all:
a read tells us the current value without risking anything, and a read that
returns something implausible is itself the signal to stop.

Read-modify-write only bit 3. Never write a whole byte to `27h`.

## This cannot be validated in emulation

86Box models the digital side. Analog black level is not something an emulator
reproduces - there is no signal to measure - so **the 86Box ViRGE/DX guest can
confirm the register reads and writes back, and can never confirm the fix
works.** Proving the output changed needs the physical Trio64 on the VLB 486
with a real monitor, and ideally a photograph in a dark room, which is how the
original author demonstrated it.

That split is worth stating plainly because it inverts our usual order. The
guest is the safe place to test the register access and the unsafe place to draw
a conclusion; the physical card is the only place the actual claim can be
settled.

## References

- Bits und Bolts, "Fixing the S3 Brightness Bug: A Deep Dive into the Pedestal Bit BIOS Mod", 2026-04-23 - `https://bitsundbolts.com/2026/04/23/fixing-the-s3-brightness-bug-a-deep-dive-into-the-pedestal-bit-bios-mod/`
- Hackaday, "Why Some S3 Videocards Have A Brightness Issue", 2026-04-21 - `https://hackaday.com/2026/04/21/why-some-s3-videocards-have-a-brightness-issue/`
- Vogons, "S3 AGP Cards (and possibly others) Too Bright" - `https://www.vogons.org/viewtopic.php?t=43472`
- Tom's Hardware coverage - `https://www.tomshardware.com/pc-components/gpus/enthusiast-fixes-30-year-issue-with-s3-graphics-card-hacking-the-vbios-fixes-black-levels-by-scalpelling-out-the-virge-dxs-pedestal-bit`
- `docs\specifications\dos-vbe-conformance.md` - improvement S6 and the LCD colour cells this explains
