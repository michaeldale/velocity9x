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

## Next step: bisect by primitive

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
