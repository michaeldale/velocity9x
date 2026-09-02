# Final Reality on a real Trio3D: 3.21 overall, rendered blind

Date: 2026-09-02
Machine: **A8U4I5**, `10.0.1.172`, physical S3 Trio3D/2X `5333:8A13`,
`<1000 MHz Intel Pentium II>`, 640x480x16 desktop.
Driver: `s3` family, `Build=e7fd47b-dirty`, `Direct3DMode=hardware`.

The first Final Reality run on hardware Direct3D through this driver, and the
first 3D benchmark number of any kind for it.

## Scores

```
Overall            3.21 Reality marks
  2D image proc.   6.25
  3D performance   1.37
  Bus transfer     3.91

3D detail                Raw speed            R marks
  25 pixel               89.21 Kpolys/s        2.85
  Robots                  6.35 images/s        1.64
  Fill rate               4.77 Mpixels/s       1.03
  City scene              8.19 images/s        2.03
  Visual appearance      74.07 %
```

`Visual appearance` carries the caveat this project already recorded: it reads
the same before and after real changes to what is rendered, so it appears to
score the advertised capability set rather than the image. Here that caveat is
absolute - **nobody saw the image.** See below.

## The driver was healthy throughout

Sampled twice over a measured 30.6 second window while the benchmark ran, from
`V9XTRACE.EXE`:

```
CountFlip                  +249    8.15 /s
D3dRenderPrimitiveCalls   +6072  198.66 /s
CountBlt                   +498   16.29 /s
TraceEvents              +22952  750.92 /s

EngineFifoTimeouts = 0    EngineIdleTimeouts = 0    EngineResets = 0
```

Across the whole run: 478,327 render-primitive calls, 723 texture creates, 8
context creates, and **no engine fault of any kind**. `C:\V9XDIAG\V9XTRACE.INI`
was never written, so the display driver recorded no fault either.

That is worth stating against the FIFO worry raised when this chip was bound:
86Box models the Trio3D/2X with 16 command FIFO slots against the ViRGE/DX
arm's 8, and `v9x_wait_fifo(15)` was named first suspect. Half a million
primitives with zero timeouts is not proof the reservation is right, but it is
the strongest evidence available that it is not obviously wrong.

## The screen was black for the whole 3D section

The benchmark rendered and scored with **nothing visible on the monitor**. It
was reported as a freeze, and it was not one: the window inventory showed "FR
Demo Window" and the benchmark dialog both alive, the counters were advancing,
and pressing Escape brought up a complete results page.

**This is unexplained and is not attributed here.**

### What it is not

`FlipPixelOk=0`, which looked like the answer, is not evidence. The probe's own
comment says why:

> `/hold` pauses on each verification color so the emulated scanout can be
> captured from the host: GDI readback only sees the fixed GDI page once real
> flips are in play.

`GetPixel` on the screen DC reads GDI's page, not the scanned-out one, so a
driver doing real hardware flips fails this check *because* the flip works. It
reads 0 on every target measured on 2026-09-02 - the Trio64 guest, the ViRGE
guest, the ATI guest, and both the `vbe` and `s3` drivers on this machine. A key
that is 0 everywhere including on known-good page flipping is not a defect
signal, and reading it as one produced a wrong diagnosis in this session before
the probe's own comment corrected it.

`Flip20Ms=0` alongside it is the same artefact, not a second symptom.

### What is worth trying next

1. **The probe's `/hold` switch**, which exists for exactly this: it pauses on
   each verification colour long enough for the scanout to be captured
   externally rather than through GDI.
2. **A plain VGA monitor.** This machine's display is a **VGA-to-HDMI
   converter** - Display Properties names it "VGA TO HDMI". A converter that
   will not re-lock when the CRTC display start moves every frame, or when a
   fullscreen mode is set, would produce exactly this: a working desktop, a
   black screen during fullscreen flipping, and a correct desktop again
   afterwards. That is a hypothesis with a cheap test and it has not been run.
3. **The same benchmark on an 86Box guest**, where the scanout can be captured
   from the host, to establish whether the black screen follows the driver or
   the machine.

## What this does not establish

- **Nothing about how the picture looked.** The scores are self-reported timings
  from an application that rendered into surfaces nobody inspected. A rasterizer
  drawing garbage at 8 fps would score similarly.
- **Nothing comparative.** No stock-driver run on this card, and Final Reality's
  own comparison database was set to `<none>`.
- **Nothing about the desktop path.** The 2D and bus scores exercise DirectDraw
  and the CPU, not the S3D unit.
