# The 2064W's own BIOS answers the question, and a guarded candidate now exists

Date: 2026-09-10. Four BringupKit runs on one boot of one machine, and no
Windows anywhere: nothing has set a mode on this card.

[The 2026-09-09 record](2026-09-09-millennium-2064w-bar-ordering.md) left two
things in the way of extending `matrox-m2` to the original Millennium
(`102B:0519`): whether the card's BIOS offers the modes the family sets, and a
per-chip framebuffer BAR index. Both are settled here. The first by measurement
- the BIOS says yes - and the second in code.

## The runs, and what each one was worth

All four are boot `adde426f-e25f-48b3-9a25-a9e9fb75468c` of the FX-6300 Debian
host, card at `0000:04:06.0`, no kernel driver bound, no CRT attached.

| Run | What it added |
|---|---|
| `runs\matrox-2064w-20260910` | The BAR layout on a second boot, `Standard VGA trust: hardware`, and a raw vendor sweep that decoded to nothing - there was no Matrox descriptor in the kit yet |
| `runs\matrox-2064w-shadow-20260910` | The video BIOS, read from the 0xC0000 shadow |
| `runs\handoff-matrox-2064w` | Writable-bit characterisation, the CR11 write-protect, and the absence of vendor register space |
| `runs\handoff-matrox-2064w-full` | The VBE mode list by emulated int10, the config-space and RAMDAC decodes, and the aperture probe |

The last is the one that decides it. Its `sha256sums.txt` covers 14 files and
all 14 verify; the ROM in it hashes identically to the shadow run's.

## The BIOS offers the modes, and says where the framebuffer is

Obtained by executing the card's own BIOS on an emulated CPU with I/O passed
through to the hardware. The bundle labels that `emulated-int10` and says
plainly that it is a different class of evidence from a register read - it is
not pooled with one here either. `card_reinitialised: false`, `status:
passed`, int 10h handler at `c000:1930`, VBE 2.0, OEM `Matrox Graphics Inc.`,
24 modes queried.

The four modes this family sets, all with mode attributes `0x9B` - bit 7, a
linear framebuffer, among them:

| Mode | | Bytes/scanline | PhysBasePtr |
|---|---|---|---|
| `0101h` | 640x480x8, memory model 4 | 640 | `FD000000h` |
| `0111h` | 640x480x16, model 6 | 1280 | `FD000000h` |
| `0114h` | 800x600x16, model 6 | **1920** | `FD000000h` |
| `0117h` | 1024x768x16, model 6 | 2048 | `FD000000h` |

`FD000000h` is this card's BAR1 base. **The BIOS therefore confirms the BAR
inversion independently**, where the 09-09 record had lspci and 86Box's device
model. That is three sources now, and one of them is the card.

**The pitch is padded on three modes**, and the bundle names them: `0103h`
reports 960 where width times bytes would be 800, `0113h` and `0114h` report
1920 where it would be 1600. This family exists to cope with exactly that -
`v9x_mga2_post_mode_set` forces the logical scan-line length through 4F06h and
refuses a BIOS that leaves a different stride - and the measurement says which
mode will exercise it. It also says the two 16-bpp modes the manifest claims
below are already packed: 1280 and 2048 match the driver's table.

`4F15h/01` was called and **not answered**, so there is no EDID by this route
either. Fixed modes are the only option, which is what the family does.

The verb deliberately calls neither `4F02h` nor `4F06h`
(`does_not_call`), so no mode was set and the pitch function itself remains
unmeasured.

## What else the full bundle establishes

**No vendor register space behind the VGA ports.** CR decodes modulo 64, SR
modulo 8, GR modulo 16 - verified across all 256 indexes of each bank, not
sampled. So CR40 is CR00, and there is no S3-style extended-register window on
this chip. The MGA vendor surface is entirely memory-mapped, in the 16 KiB
control aperture at BAR0. That is a point in favour of this family's approach
rather than against it: "VBE mode set plus a pitch hook and no chip register
writes" is the only route to this card that does not require new MMIO work.

**CR11 bit 7 is set: the CRTC is write-protected as the BIOS left it.** Writes
to CR00-CR06 are silently discarded until it is cleared, and CR11 itself is
writable. Nothing in this family touches the CRTC, so it changes no code here;
it is recorded because a driver that ever does will otherwise write six
registers into a hole.

**The framebuffer aperture accepts only 2-byte accesses in the card's current
mode**, with a measured alias period of 1 MiB. The bundle is careful about
what that is: the current VGA mapping, not the DRAM, and measuring the real
window would need the card in power-graphics mode with its memory controller
configured, which is a mode set and out of scope for a probe that only reads
and restores. **This is the open risk to a DIB on this card**, because the
engine writes dwords. It is not evidence against the linear modes the BIOS
advertises; it is evidence that nobody has yet measured the aperture in one of
them.

> **Superseded 2026-09-11, and the reasoning above is wrong twice.** The
> aperture is gated by CRTCEXT3 bit 7 (`mgamode`), which the firmware leaves
> clear; setting that one bit - no mode set, no memory controller
> configuration - makes 32-bit accesses round-trip faithfully to 4 MiB. The
> open DIB risk is closed:
> [the 2064W's aperture opens with mgamode](2026-09-11-the-2064w-aperture-opens-with-mgamode.md).

**Installed memory is not measured.** The BIOS reports 8 MiB through 4F00h,
which is exactly the BAR1 window and so a report rather than a confirmation;
the ROM's own info-block template carries 2 MiB. The bundle records the
disagreement rather than preferring a side, and so does this record.

> **Superseded 2026-09-11.** It is measured now, at 8 MiB, by an alias probe
> that is direct MMIO and does not go through the BIOS. That is a fact about
> this card and not about the part, which shipped in several memory sizes -
> so the manifest's conservative figure stands and only this reasoning is
> retracted.

**The RAMDAC is proved, not assumed**: the probe read the part's ID at BAR0
offset `0x3c00` index `0x3f` and required `0x26` before decoding anything. It
got `0x26` - a TI TVP3026, silicon revision `0x11` - and then decoded the
PLLs: pixel 27.491 MHz, MCLK 50.114 MHz, loop 14.318 MHz, all locked, dot
clock sourced from CLK0. OPTION at config `0x40` decodes field by field
against MGA-2064W Specification 10470-MS-0300, Feb 1996, p.4-13, with
`biosen`=1, `vgaioen`=1 and `interleave`=0. The descriptor warns, correctly,
that `productid` is a board strap and not a memory size.

## The candidate that follows from it

Built, not shipped, and on the same terms as its sibling: guarded file
replacement, no INF, no `V9XHAL.DLL`, `HOST-AUDITED; PHYSICAL ACTIVATION NOT
YET TESTED`.

**A per-chip framebuffer BAR index.** `V9X_HW16_DEVICE` gains
`framebuffer_bar`, appended last so that every existing device initialiser
leaves it zero and keeps reading BAR0 exactly as before. It is per chip
because the ordering is a property of the chip generation, and the family's
`read_aperture` hook learns which chip matched from `v9x_hw16_active_device()`
- the same accessor `dd16.c` and `enable16.c` already use.

**`V9XPCIREADBAR0` becomes `V9XPCIREADBAR`,** taking the index and computing
the configuration offset as `10h + index * 4`. Computed rather than written as
a literal for a reason beyond taste: this family's audit forbids
`mov\s+di,14H` as the signature of an S3 aperture path, and a literal would
have failed its own build. Its guards are unchanged - a base below 16 MiB,
above `FE000000h`, or not 16 MiB aligned is refused - and what changes is
their standing. The 09-09 record noted that the alignment guard rejected this
card's control aperture by luck; the index means correctness no longer rests
on that, and the guard is a backstop. It still is one: if the PCI scan has not
matched, `v9x_hw16_active_device()` falls back to the family's first entry,
which is the 2164W, whose index is 0 - so a 2064W in that state reads
`FDCFC000h`, fails the alignment guard, and refuses at stage 3 rather than
handing the display code a 16 KiB MMIO window.

**The policy backend takes both ids.** `v9x_matrox_millennium2_probe` accepted
`051B` alone; it accepts `0519` too, because the policy is identical for both
and the one difference the driver acts on is hw16 data. The host family-matrix
test caught this within a minute of the manifest gaining the chip, which is
the test doing its job.

**Three modes, not four.** The manifest claims `0101h`, `0111h` and `0117h`
for `0519` and leaves out `0114h`: this BIOS reports 1920 bytes per scan line
where the driver's table asks for a packed 1600, so `post_mode_set` would have
to force it and would refuse the mode if the BIOS declined. Refusing is the
designed outcome and is safe - stage 9, legible - but claiming a mode whose
stride the card has already contradicted is not a claim to make before someone
has set it.

**`VideoMemoryBytes` is 2 MiB, and it is a floor rather than a measurement.**
It covers every mode claimed - 1024x768x16 needs 1.57 MiB - and
under-reporting cannot hand DirectDraw memory the card may not have. The
family ships no HAL in any case.

The package's `MANIFEST.TXT` names both ids and which BAR each takes its
framebuffer from, and the build now fails if either chip's string is missing
from the driver image.

## What is still owed

> **All three were discharged on 2026-09-11**, by a run that reused this
> bundle's directory path:
> [the 2064W's aperture opens with mgamode](2026-09-11-the-2064w-aperture-opens-with-mgamode.md).
> Item 2 did not need a graphics mode after all - one register bit was
> enough. Windows has still not set a mode and no pixel has been drawn.

1. **A mode set.** Nothing has set one on this card - not the kit, which
   refuses to, and not Windows. Everything above is what the card and its
   BIOS say at rest.
2. **The aperture's access width in a linear mode.** The one measurement that
   could still sink this, and it needs the card in a graphics mode.
3. **Installed VRAM**, measured rather than reported.
4. **4F06h**, which the family depends on and no run has called.

The next step is therefore not another Linux capture. It is the DOS survey and
VBE inventory on that box - real int10, which would confirm the published mode
list and answer 4F06h - or, with more risk, the guarded candidate on a Windows
install.

## Gates

`run-checks.ps1` green: check-tree, the VGA survey safety gate, the host tests
- including the family matrix, which now drives the new chip through the
backend - and all four family packages, the Matrox candidate among them.
`update-backend-registry.ps1` regenerated `backend_registry_table.inc` for the
new chip. `build-host-msvc.ps1` cannot run on this host.
