# DOS VBE conformance corpus

## Purpose

An external body of evidence about how real VBE BIOSes fail, and what this
driver should do about it. The source is Gona's DOS compatibility matrices at
`https://gona.mactar.hu/DOS_TESTS/` (179 late-PCI and early-AGP chips) and
`https://gona.mactar.hu/DOS_TESTS_VLB/` (about 30 VLB chips), the tables Vogons
cites when someone asks which card behaves in DOS.

Three things come out of it, and they are the three sections below: a taxonomy
of what the classic test programs actually probe, a per-BIOS S3 defect matrix,
and a set of checks this project does not currently have.

**Why an external corpus is worth reading into the repo at all.** Our test suite
grew from the inside out - we wrote checks for the things we broke - and
`docs\issues\2026-08-16-tier0-defects-deferred.md` D5 is what that costs: a
six-mode matrix reporting GDI `PASS` on every cell while the monitor showed
shredded noise, because every check in the suite went through the same pitch,
base and depth the driver had chosen. Gona's method is the opposite one. It is
twenty years of somebody running a fixed corpus of programs on 200 cards and
writing down what appeared on the glass. That is precisely the axis our suite is
blind on.

## Provenance, and what the source is and is not worth

Maintained roughly 2012 to November 2022, on real hardware, by one person. It
carries the strengths and the limits of that.

**What makes it unusually good evidence.** Rows are keyed to **individual video
BIOS revisions**, not to chips - twelve separate S3 ViRGE and Trio64V2 rows
differ only by BIOS - and several rows link the BIOS image that produced them.
Columns are split per resolution, so Quake gets nine and Duke Nukem 3D eight.
The changelog records corrections against earlier entries rather than silently
editing them, including one that overturned a chip-level conclusion after
BIOS-swapping between two cards.

**Where to be careful.**

- One tester, one or two host configurations, and some cells say so
  (`"problems with the ball on one earlier test config but lot other config it
  is OK"`). Host chipset is a confound the table cannot always separate.
- Yellow cells mean "fine on CRT, wrong on LCD", which is a monitor property as
  much as a card property, and the author revised a batch of these in October
  2016 after retesting on a different panel.
- Anything after roughly 2003 is thin, and several late cells are `not tested`.
- The refresh-rate figures (`87Hz`, `154Hz`) are what his setup produced from
  those BIOSes. They are evidence that S3 BIOSes pick high refresh rates at high
  resolutions, not a specification of which.

Treat a single cell as a lead. Treat a pattern that holds across BIOS revisions
of one chip as a finding.

## What each test actually probes

The value here is not the game list, it is that the corpus was assembled to
separate failure modes that all look like "the picture is wrong". This mapping is
the reusable part.

| Test | Symptom vocabulary | What it exercises |
|---|---|---|
| Commander Keen 4/5/6, Keen Dreams | `scroll problem`, `serious display problem`, `flashing` | EGA-era smooth scrolling: display start updated per frame, and whether the latch takes effect mid-scanout |
| Jazz Jackrabbit, Boulderoid, Mario, Prehistorik | `displaced lines at scrolling`, `horizontal scroll problem` | Same panning path, wider and at different pitches. `displaced lines` is the high bits of display start being clobbered or applied late |
| Pinball Fantasies (640x350), Pinball Illusions | `partial graphics`, `wrong colors and dublicated`, `crash`, `size and position problems` | Nonstandard height with a split screen; the scoreboard and playfield are separately scanned regions |
| Flight Unlimited | `+badpaldac`, `+badlinmodes`, `6bitDAC` | Two independent things: the 6-bit/8-bit palette DAC switch, and whether linear-framebuffer modes are trustworthy |
| X-Men: Children of the Atom | `cannot initialize a 640x480 32,000 colour graphics mode` | 5:5:5 versus 5:6:5 mode reporting |
| Terra Nova | `run out of memory trying to allocate 0 bytes` | VBE-reported memory size arithmetic - the same class as tier-0 defect D2 |
| Warcraft: Orcs & Humans | `noise`, `depends on the RAMDAC chip` | RAMDAC analog behaviour under palette churn. Gona states outright this one is caused by the RAMDAC, not the driver |
| Quake 1.08 at 360x200/240/400/480 | `S3VBEFIX` | 360-byte-wide modes: the S3 primary-stream FIFO fetch defect |
| Duke Nukem 3D at 1024x768+ | `flashing`, `87Hz`, `S3REFRSH (for LCD)` | Refresh rate selection at high resolution |
| SimCity | `8x14 font fixer` | Text-mode 8x14 font after graphics use |
| Flight Simulator 5.x SVGA | `rare flashing`, `light hues are white on LCD` | Page flipping, and analog level clipping near white |
| Tomb Raider on PowerVR PCX2 | `uvconfig -m2048` | A card over-reporting usable VRAM to a client that believes it |

The DOS fix tools are the other half of the taxonomy, because each one names a
single defect class and its resident cost is a measure of how small the fix is:
`VESA12` (192 bytes) degrades a VBE 2.0 report to 1.2 so nothing uses linear
modes or the protected-mode interface; `6bitDAC` (176 bytes) makes the BIOS deny
it can switch the DAC to 8 bits; `VBE15bpp` (224 bytes) presents 16-bpp modes as
15; `UniRefresh` (208 bytes) recomputes CRTC timings from monitor limits;
`VESAFIX -6` disables 8-bit palette support; `S3VBEFIX` (880 bytes) is the S3 set
described below.

That a 192-byte TSR whose entire function is *reporting a lower VBE version*
fixes a large number of cells is the most useful single fact in the corpus. It
says the VBE 2.0 additions - linear framebuffer and the PM interface - are where
BIOSes went wrong, and it is corroborated per-BIOS in the next section.

## The S3 per-BIOS evidence

This is the part that bears on our own hardware. Both S3 chips this driver
drives are in the table, several times each, at different BIOS revisions.

### The load-bearing finding: VBE 2.0 S3 BIOSes are worse than VBE 1.2 ones

Same silicon, different BIOS. Cells that are `OK` on both are omitted.

| Chip and BIOS | Reports | Pinball Illusions 640x480 / 800x600 | Quake 360-wide | 8x14 font |
|---|---|---|---|---|
| Trio64V2/DX ref 1.01.04 (1996-Oct) | VBE 1.2 | `partial graphics` / `partial graphics` | OK | OK |
| Trio64V2/DX ref 2.04.07 (1997-May) | **VBE 2.0** | `wrong colors and dublicated` / **`crash`** | **`S3VBEFIX`** | **`8x14 font fixer`** |
| ViRGE/DX ref 1.01.03 (1996-Dec) | VBE 1.2 | `partial graphics` / `partial graphics` | OK | OK |
| ViRGE/DX Hercules G5h (1997-Nov) | VBE 1.2 | OK / OK | OK | OK |
| ViRGE/DX ref 2.01.07 (1997-Apr) | **VBE 2.0** | `wrong colors and dublicated` / **`crash`** | OK | **`8x14 font fixer`** |
| ViRGE/DX Genoa 3.0 (1997-Jul) | **VBE 2.0** | `wrong colors and dublicated` / OK | OK | **`8x14 font fixer`** |
| ViRGE/DX ref 2.01.16 (1998-Oct) | **VBE 2.0** | `wrong colors and dublicated` / **`crash`** | **`S3VBEFIX`** | **`8x14 font fixer`** |

Three regressions arrive with the VBE 2.0 BIOSes and none of them is a fix:
Pinball Illusions degrades from a drawing fault to a hang, the 8x14 text font
stops being restored, and on the two newest BIOSes Quake's 360-wide modes break.
The cleanest ViRGE/DX in the whole table is the **Hercules G5h, a VBE 1.2 BIOS**,
with nine defect cells against sixteen for the 1998 reference 2.0 BIOS.

**What this is worth to us.** It is direct evidence for a claim we have so far
only made structurally: the BIOS is not a dependable oracle, and *newer is not
safer*. `docs\plans\dynamic-vbe-pipeline.md` is built on asking the BIOS what it
supports and believing the answer. This table is the argument for why that plan's
scan-contradicted-baseline machinery has to exist, and for why a mode the BIOS
lists still needs proving on the glass.

### Defects track the BIOS, not the chip

Gona reached this the hard way and recorded it, in the 2019-01-06 and 2019-05-16
changelog entries. Commander Keen breaks on some S3 Vision 868/964/968 cards and
not others; he swapped a SPEA Vision868 BIOS onto a Canopus Vision864 and **the
fault moved with the BIOS**. ELSA, miro, SPEA and Number Nine BIOSes fail;
Canopus and Diamond mostly pass. The VLB table says the same thing about the S3
805 in plainer words:

> `Some cards have problem depending by it's bios. Diamond has problem.`

The ViRGE rows repeat it. `Commander Keen -> serious display problem` appears on
the **Diamond** Stealth 3D 2000 (BIOS 2.03) and Stealth 3D 2000 Pro (BIOS 1.01)
and on the Diamond ViRGE/VX, while the Aristo and Hercules ViRGEs of the same
chips are clean.

**Consequence for our diagnostics, and it is actionable.**
`v9x_s3_publish_diagnostics` in `src\chipsets\s3\common\s3_regs16.c:286` writes
adapter, vendor and device id, clocks and memory - and **nothing that identifies
the video BIOS revision**. On evidence that the BIOS is the variable that
predicts behaviour, a bug report from an untested S3 card currently cannot be
attributed. See improvement S5.

### The two chips we drive, VLB and PCI

The physical Trio64 on the VLB 486 (see the `vlb-486-test-machine` note) is in
the VLB table as `S3 Trio64 (86C764-P) [SPEA V7-MIRAGE P64]`, VBE 1.2, and it is
one of the cleanest rows on the page - four defect cells:

| Cell | Value |
|---|---|
| Wonderland in Paradise 800x600 | `display problem` |
| Flight Simulator 5.x SVGA | `S3VBE20 or UniVBE` |
| Warcraft | `small noise` |
| Flight Unlimited 1024x768 | `OK & S3REFRSH (for LCD)` |

Notably **no Commander Keen defect and no scroll defect**, which makes it a poor
card for finding panning bugs and a good baseline for everything else.

One trap for that machine specifically: **UniVBE 6.70 refuses to detect any VGA
on a pre-PCI system.** Gona states this as a warning on the tools list. On the
VLB 486 the usable versions are 5.3a or 6.53. All SciTech versions have been free
since October 2002; the 6.x free registration code is
`00000-173D626E-02002` and the 5.x code is `00000-816EAD30-20020`.

### S3VBEFIX as a specification

`S3VBEFIX` by Artem Vasilev is a 880-byte TSR that fixes S3 VBE 2.0 BIOSes. Its
feature list is the closest thing available to an errata sheet for the BIOSes our
own family table is calibrated against, and every item maps onto something in
this driver:

| S3VBEFIX feature | Bears on |
|---|---|
| primary stream FIFO fetch fix - "fixes Quake and other apps bugs in 360-wide and text modes" | Why 360-wide modes must be distrusted. Improvement S3 |
| new 320x400 and 320x480 modes at 8/15/16/32 bpp | Modes the BIOS does not list. Our table already excludes the ViRGE 320x200 rows for this reason (`s3_hw16.c:54`) |
| VESA video memory size overriding - "fixing screen tearing in Duke Nukem 3D in 800x600 and higher" | The BIOS misreporting memory. Same class as tier-0 D2 |
| "set display start" - overriding the wait-for-vertical-retrace flag | Improvement S1 |
| force RAMDAC CLUT width to 6 bit per channel | Improvement S4 and test T0-4 |
| VESA banked-modes booster | Not applicable; we drive linear modes |

## Improvements to the native S3 driver

Each is grounded in a specific cell or feature above and in current code. None
has been measured on our hardware - they are candidates with an argument, and the
argument is stated so it can be refused.

### S1 - the flip path programs display start without waiting for retrace

`v9x_set_display_start` in `src\display32\engines\vga_scanout.c:42` writes CR0D,
CR0C and the CR69 low nibble and returns. There is no retrace wait, so a flip
issued mid-scanout takes effect mid-frame and tears; worse, the three registers
are written across three separate index/data pairs, so a scanout that crosses the
update sees a display start assembled from old and new bytes.

That is exactly the shape of Gona's `displaced lines at scrolling`, which the S3
928 shows in Jazz Jackrabbit, Mario, Pinball Fantasies and both Boulderoid
versions - though not in Commander Keen, so the corpus does not treat the
scrolling tests as interchangeable and neither should we.

The primitive already exists and is already used: `v9x_in_vblank`
(`vga_scanout.c:24`) and the bounded spin loops in
`src\display32\ddhal_core.c:921-933` serve DirectDraw's
`WaitForVerticalBlank`. The flip path simply does not use them.

**Proposed:** an optional bounded wait before the register writes, on the same
spin-count discipline as the existing loops so a card that never asserts vblank
cannot hang the driver. S3VBEFIX offering this as a switch rather than a default
is a hint that the right answer may be per-mode; treat "always wait" as
unproven.

**Do not conflate this with defect D1.** D1 was that this function ran on
non-S3 chips. That is a question of *who* may call it; this is a question of
*when*.

### S2 - the refresh rate reported to DirectDraw is a constant

`src\display16\dd16.c:133` sets `mode->wRefreshRate = 60u` unconditionally.

The corpus says S3 BIOSes do not deliver 60 Hz at high resolution. `87Hz` recurs
across the ATI and S3 rows, the S3 928 rows carry `154Hz`, `140Hz`, `120Hz` and
`97Hz`, and the single most common S3 remedy cell in the whole table is
`S3REFRSH (for LCD)` or `for LCD: UniVBE 6.70 & UniRefresh` at 1024x768 and
1280x1024 - present on Trio32, Trio64, Trio64V+, Trio64UV+, Trio64V2/DX,
ViRGE, ViRGE/VX and ViRGE/DX alike.

Two separate faults sit behind that one line. We **publish a number we never
measured**, and we have **no way to choose one**. The first is the cheaper and
more urgent: reporting 60 when the CRTC is running at 87 is a false statement to
DirectDraw, and on an LCD it is a false statement about a mode the panel may
refuse to display at all.

**Proposed:** derive the figure from the CRTC or from the mode record and
publish that, or publish "unknown" rather than a plausible fiction. Note that
`s3_regs16.c` already decodes the SR10/SR11 PLL and publishes `CoreClockKHz`,
so the machinery for reading real clocks off this chip exists.

**A hazard worth stating.** Anything that ends in "and then set a refresh rate"
can put a mode on a monitor that cannot display it, on a machine whose only
output is that monitor. `S3REFRSH.EXE` does not work on ViRGE/DX, ViRGE/GX,
Trio64UV+, 928PCI or Trio 3D even as a standalone DOS utility, which suggests
the register path is not uniform across the family. Publishing a truthful number
is safe; programming a new one is not, and should not ride along with it.

### S3 - distrust 360-wide modes if the dynamic pipeline starts admitting BIOS modes

Today `v9x_s3_modes` (`src\chipsets\s3\s3_hw16.c:56`) is a hand-audited list and
carries no 360-wide row, so this costs us nothing yet. The dynamic VBE pipeline
changes that premise: it admits what the BIOS lists.

The evidence says the newest S3 BIOSes list 360-wide modes that do not work.
Quake needs `S3VBEFIX` at 360x200, 360x240, 360x400 and 360x480 on **ViRGE/DX
reference 2.01.16 and Trio64V2/DX reference 2.04.07**, and needs nothing on the
VBE 1.2 BIOSes of the same two chips. S3VBEFIX attributes it to primary-stream
FIFO fetch.

**Proposed:** an explicit distrust rule for widths that are not a multiple of 8
on S3 parts, with the reason recorded, so the pipeline's reason codes can say
"declined: S3 360-wide FIFO defect" rather than admitting a mode that will fail
on the glass. This is cheap to state now and expensive to rediscover later.

### S4 - the palette DAC width is never asserted

`flight +badpaldac`, `6bitDAC` and `VESAFIX -6` are all one defect: an
application switches the RAMDAC to 8 bits per channel and the BIOS or the app
then fails to program consistent values, and the picture comes out dark or
wrong. Gona needs `+badpaldac` on Vision864, Vision968 and ViRGE/VX, and
`6bitDAC`-class fixes across many vendors.

We set palettes and the tier-0 matrix reports palette `PASS`, but that check
reads back through GDI - the D5 problem again. Nothing establishes what width
the DAC is actually in.

**Proposed:** publish the DAC width in `V9XHW.INI`, and add T0-4 below. Reading
it is nearly free: `v9x_s3_publish_diagnostics` already opens an S3 sequencer
unlock window at `s3_regs16.c:297-304` and restores it correctly, so an extra
indexed read inside that window costs a few bytes.

### S5 - diagnostics cannot identify the video BIOS

Argued in full above. Defects track BIOS revision; our diagnostics record chip
identity and no BIOS identity, so an untested-card bug report is unattributable
to the one variable that predicts behaviour.

**Proposed:** publish a BIOS revision string. Two sources, and they are not
equivalent - the VBE OEM strings via 4F00h (`OemStringPtr`,
`OemVendorNamePtr`, `OemProductNamePtr`, `OemProductRevPtr`, and
`OemSoftwareRev`), which the mini-VDD already fetches at `Device_Init` for the
controller block, versus the BIOS ROM image at C000h, which is what Gona's row
labels actually name. `struct v9x_vbe_controller_summary`
(`include\velocity9x\vbe_parse.h:26`) currently keeps only `version` and
`total_memory_bytes`, so whichever is chosen, the summary has to carry it.

Prefer the VBE OEM strings: they arrive through a path that already exists and
is already ring-0, and they are what the BIOS asserts about itself.

### S6 - the pedestal bit

Recorded separately in `docs\issues\2026-08-23-s3-pedestal-black-level.md`,
because it is a defect with a known cause and a known fix rather than a
candidate. It is an S3 improvement and belongs on this list by reference: the
unlock window at `s3_regs16.c:297-304` is where the work would go.

## Tier-0 test cases

Tier-0 drives cards nobody has tested, so its suite is the one that most needs
to catch a fault it was not written for. D5 established that the current suite
cannot: every check goes through GDI, so all of them agree with each other
regardless of what the CRTC does.

These are ordered by what they would have caught. T0-1 is a prerequisite for
several of the others, because a check whose oracle is GDI cannot test scanout.

### T0-1 - a scanout oracle that does not go through GDI

**The gap D5 named and left open.** Until one check reads the glass rather than
the framebuffer, a green matrix means "the driver is self-consistent".

The method is already established in this repo and should be reused rather than
reinvented: host-side `PrintWindow` with `PW_RENDERFULLCONTENT` on the 86Box
top-level window, which is occlusion-safe and requires the window **not
minimised**. `CopyFromScreen` is not a substitute. On physical hardware the
equivalent is a photograph or a capture card, and the corpus is a reminder that
this is a legitimate test instrument, not a fallback - Gona's entire dataset is
that measurement.

**Pass criterion:** a known pattern rendered at each admitted mode, captured
host-side, compared against the framebuffer contents. Disagreement is the
finding, and D5 is the fixture: on the Mach64 VT2 at 1024x768x16 the two
disagree completely.

### T0-2 - display-start sweep

Directly targets the corpus's largest symptom class - `scroll problem`,
`displaced lines at scrolling`, `horizontal scroll problem` - and improvement S1.

Render a pattern with a distinguishable row index, then step the display start
through a series of offsets, including at least one crossing each of the CR0D,
CR0C and CR69 byte boundaries, and verify host-side that the image shifts by
exactly the expected number of scanlines with no torn or duplicated rows.

**What it catches that nothing currently does:** the CR69 high nibble being
clobbered rather than preserved, the doubleword rounding refusal at
`vga_scanout.c:47` being wrong in either direction, and the missing retrace
wait. All three are invisible to a GDI-side check because GDI never reads the
display start back.

### T0-3 - palette DAC width assertion

Write a known ramp, read it back **through the DAC ports rather than through
GDI**, and assert the width the driver believes it is in. Pairs with S4.

The existing palette check passes on a card whose display is shredded, so it
does not currently constitute evidence about the DAC.

### T0-4 - refresh rate truth

Assert that the rate published to DirectDraw matches what the CRTC is programmed
to, or that the driver publishes "unknown". Fails today by construction:
`dd16.c:133` is a constant. Pairs with S2.

This is a truthfulness check, not a timing check. It does not require
programming a refresh rate and must not be written so that it does.

### T0-5 - mode-set failure must refuse, not half-succeed

The corpus is full of the failure this guards: `crash`, `black screen`,
`freezes`, `no signal`, `VESA mode switch failed`, and Pinball Illusions moving
from `partial graphics` to `crash` between two BIOS revisions of one chip.

Assert that a mode-set which fails at 4F02h leaves the previous mode intact and
reports failure, rather than leaving the CRTC in a half-programmed state. Fault
injection already exists here -
`docs\decisions\2026-08-16-engine-fault-injection.md` - so this is a new case in
an existing mechanism rather than new machinery.

### T0-6 - text-mode restore including the 8x14 font

`8x14 font fixer` is required on every VBE 2.0 S3 BIOS in the table and on none
of the VBE 1.2 ones. Whatever the BIOS does or fails to do on mode restore, our
DOS-box and mode-restore paths cross it.

Assert that after a graphics mode is set and released, text mode returns with
the 8x14 font intact.

### T0-7 - reported memory versus usable memory

D2 was VRAM arithmetic underflowing when the BIOS under-reports. The corpus
supplies the opposite case as well: the ViRGE rows carry
`if your card has 4MB memory you need to use 2MB: uvconfig -m2048`, a card whose
reported size has to be reduced for a client to work, and Terra Nova's
`run out of memory trying to allocate 0 bytes` is a third-party program hitting
the same class.

Assert that raw reported memory and usable memory are both published and that
usable is floored at `visible_bytes` - the D2 fix - with a case for each
direction of disagreement.

### T0-8 - BIOS identity present in diagnostics

The cheapest case here and the one with the best evidence behind it. Assert that
`V9XHW.INI` carries a BIOS revision for the adapter, or an explicit
`unavailable`. Pairs with S5. Without it, every other result in this suite is
attributed to a chip when the corpus says the BIOS is the variable.

## Deliberately not adopted

**15-bpp / 5:5:5 modes.** `X-Men: Children of the Atom` needs a 32,768-colour
mode and DOS solved it with `VBE15bpp`, a 224-byte TSR that presents 16-bpp
modes as 15. `docs\plans\dynamic-vbe-pipeline.md` **rejects 15-bpp storage and
5:5:5 with a reason code, on purpose**, and this corpus is not an argument to
reverse that: a DOS shim converting mode reports for one game says nothing about
what a Windows display driver should enumerate. Recorded so that the X-Men column
is not mistaken for evidence against a decision already taken.

**Banked VBE modes.** S3VBEFIX's banked-mode booster and the `Chris' 3d SVGA
Benchmark` column both concern banked access. We drive linear modes; the future
banked family is already scoped in the pipeline plan.

**Chasing individual yellow cells.** LCD-only colour faults are partly a monitor
property, and the author revised a batch of them after changing panels. The
pedestal bit is the exception, and it is an exception because its mechanism is
known - see the issue.

## References

- Gona, PCI and AGP video chips DOS compatibility - `https://gona.mactar.hu/DOS_TESTS/`
- Gona, VESA Local Bus video chips DOS compatibility - `https://gona.mactar.hu/DOS_TESTS_VLB/`
- Gona, VLB signal quality on LCD - `https://gona.mactar.hu/DOS_TESTS_VLB/quality.html`
- S3VBEFIX by Artem Vasilev - `https://www.vogons.org/viewtopic.php?f=24&t=48445`
- Vogons, best video cards for DOS / Win 3.x / Win 9x - `https://www.vogons.org/viewtopic.php?t=31703`
- `docs\issues\2026-08-16-tier0-defects-deferred.md` - D2 and D5 in particular
- `docs\plans\dynamic-vbe-pipeline.md` - the BIOS-as-oracle design this corpus tests
- `docs\issues\2026-08-23-s3-pedestal-black-level.md` - improvement S6
