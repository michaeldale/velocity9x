# GDI acceleration corrupts the display on physical S3 Trio64 silicon

Date: 2026-08-27
Status: **open - default-on primitives are unsafe on real hardware**
Severity: high. `GdiAccel` is on by default, and `gdi-fill-copy-overlap` is what
ships.

## What happened

The first run of any GDI-accelerated build on physical S3 silicon. BARRY,
S3 Trio32/64 86C764, 2 MB, Windows 98 SE, 800x600x16, build `gdi-accel-004` with
its shipping defaults (`gdi-fill-copy-overlap`, upload off).

The driver enabled cleanly - `Stage=enable-ok`,
`Surface=pitch=1600 bpp=16 w=800 h=600` - and then the desktop was visibly
corrupted: the image torn and duplicated vertically, a second taskbar rendered
part-way up the screen, and heavy speckle across the whole surface. The `/accel`
harness failed its own comparison.

## The numbers

```
Advertised=23     Enabled=7        Poisoned=0
Calls=1700        Declines=1599    Fills=93       Copies=8      Uploads=0
DeclineNotScreen=1009   DeclineRop=84   DeclineThreshold=367
DeclineGeometry=0  DeclineOverlap=0  DeclineBusy=0  DeclineEngine=0
Compared=FAIL     MismatchOperation=25
MismatchBytes=213372    MismatchX=0   MismatchY=239
MismatchScreen=(191,216,191)   MismatchRef=(0,0,0)
Error=pixel-mismatch    Result=FAIL
```

Three of those matter more than the rest:

- **`Fills=93`, `Copies=8`** - the engine really executed. This is not a case of
  everything declining.
- **`Poisoned=0`** - no bounded wait expired. The spin limits were the standing
  suspicion for a hardware divergence after the build 001 calibration bug, and
  they are **not** the cause here.
- **`MismatchBytes=213372`** at operation 25, against a screen pixel of
  `(191,216,191)` where the reference wanted black. 213 KB is not a stray
  rectangle; it is most of the compared region.

## Why emulation did not catch it

The Trio64 passes **11/11 modes** in 86Box, twice, including this exact
800x600x16. So does the ViRGE. Whatever differs is a property of the real
8514/A engine that 86Box does not model, which is the category this run existed
to probe - and the reason the decision was made to run BARRY before starting
build 005.

The visual signature - a vertically torn and duplicated image rather than
localised garbage - points at addressing rather than at pixel values: the engine
writing on a different stride or from a different base than the CRTC is
scanning out. The Trio64 path addresses both rectangles "through the display
pitch from a common bank base", per the note in
[`s3_engine_regs.h`](../../include/velocity9x/s3_engine_regs.h), and that base
and pitch come from hardware state this driver does not itself program. That is
a hypothesis, not a diagnosis.

## The bisect, and two hypotheses it killed

**Fill alone reproduces it.** `GdiAccelFill=1` with copy and overlap off:
`Enabled=1`, `Fills=34`, `Copies=0`, `Poisoned=0`, `Compared=FAIL` at the same
operation 25, `MismatchBytes=208902` against 213372 for all three primitives.
Copy and overlap are not involved. This is build 001's 8514/A solid-rectangle
path.

**It is not a pixel-depth mismatch.** The vertical yellow/black striping where
solid fills belong suggested the engine addressing at the wrong bytes-per-pixel.
It does not: 800x600x**8** fails identically, `MismatchBytes=195553`, same
operation, same signature.

**It is not the surface base offset.** The Trio64 fill folds the destination
base into the y coordinate (`y = base / pitch + destination_y`), and a non-zero
base on hardware would displace every rectangle. Measured on BARRY:
`LastBase=0x00000000`, `LastPitch=1600` - identical to every emulated guest.

**The register sequence is not wrong.** `src/display32/engines/eng_s3_trio.c`
drives the same registers in the same order with the same constants from the
32-bit DirectDraw HAL, and on this same machine at this same mode it is
**correct**: `BltFillPixelOk=1`, `SrcCopyPixelOk=1`, and all four overlap probes
pass with 64 pixels seen each. Same registers, right answer from 32-bit, wrong
answer from 16-bit.

**The harness is sound here.** The control matters because two of the runs above
could otherwise be read as harness failures: with `GdiAccel=0` on BARRY at
800x600x16, `Enabled=0`, `Fills=0`, `Compared=PASS`, `Result=PASS`. Acceleration
is the cause, and the desktop screenshot with it off is clean.

**Eight fills are enough.** A run with the threshold raised to reject nearly
everything still executed 8 fills in the smoke phase, recorded `FillsDelta=0`
for the measured window, and *still* failed the comparison - the corruption from
those 8 persisted. This is not a rare race; it is close to every fill.

## What is left to explain

The difference is not the registers, the depth, the base, the pitch, or the
timing (`Poisoned=0` throughout). It is something about the **16-bit calling
context** that the 32-bit HAL does not share. Candidates not yet tested, in the
order worth testing:

1. **Concurrent access.** The 32-bit HAL runs under DirectDraw's exclusive
   lock. The 16-bit path runs whenever GDI calls it, and the engine registers
   are global, non-reentrant hardware state. The build 000 drain hooks guard
   CPU-after-engine; nothing guards engine-after-CPU or engine-after-engine
   against a 32-bit caller.
2. **Chip state the 16-bit path never establishes.** `ADVFUNC_CNTL` (0x4AE8)
   and the CR registers that select enhanced-mode addressing are programmed by
   whoever set the mode. The 32-bit HAL may be inheriting a state the 16-bit
   entry does not, and the emulator may not require it at all.
3. **A single controlled fill.** One `PatBlt` at known coordinates, then read
   back the whole screen and report the bounding box of changed pixels. That
   measures the actual transform instead of inferring it, and is the experiment
   to run before any more hypotheses.

Two hypotheses were already lost to reasoning ahead of measurement in this
session. The third item above exists so the next one is not.

## Superseded: the original next step

The per-primitive INI keys exist for this. `GdiAccelFill=1` with copy and
overlap off separates the 8514/A solid-rectangle path from the BITBLT path, and
the two use different registers and different command words. One reboot per
arm.

Do this before anything else, because "GDI acceleration is broken on Trio64"
and "the fill path is broken on Trio64" are very different findings and only one
of them threatens build 001.

## Recovery, and a caution for whoever does this next

`GdiAccel=0` in `[Velocity9x]` of `C:\WINDOWS\SYSTEM.INI` plus a reboot returns
the machine to `GdiAcceleration=none`, and the desktop comes back clean at
800x600x16. Confirmed on boot 76.

The caution is that **recovery depends on the agent staying reachable.** BARRY
was recovered entirely over the network. A blit that hangs the chip rather than
merely mis-addressing it would need someone in front of the machine, so the
bisect wants a person who can reach it physically, or a willingness to leave it
down until one can.

## Related

The ViRGE/DX is untested on real silicon. Only the Trio64 exists physically here,
so nothing in this issue says anything about whether the ViRGE path is affected.
