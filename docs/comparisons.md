# How Velocity9x compares

Two readings of the driver from outside it: what a third-party Direct3D
benchmark sees, and where the driver stands against S3's own retail drivers.
Moved here from the front README on 2026-09-12; the text is unchanged.

## The Direct3D path, as a third party sees it

![Final Reality 1.01 Advanced Options running on Velocity9x, listing the
Direct3D capabilities it detected](images/final-reality-d3d-capabilities.png)

Final Reality 1.01 selects "Direct3D On-board Accelerator" and enumerates the
driver's Direct3D device. The capabilities it lights up — bi-linear filtering,
Z-buffer sorting, mip-mapping, tri-linear mapping, depth fog, specular Gouraud,
vertex alpha, crossfade alpha blending and subpixel accuracy — are the ones the
S3D path actually implements. Additive and multiplicative alpha stay greyed
out, which is correct: the driver declares only `SRCALPHA`/`INVSRCALPHA`
blending, which is the crossfade case and nothing more.

This is a useful sanity check on the capability table, because it is an
independent reading of what the driver advertises rather than the driver
describing itself. It is only a reading of what is *advertised*, though, and
that is a real distinction: "Z-buffer sorting" lit up in this list for weeks
while the driver accepted a depth buffer and then ignored it. Depth testing is
backed by the hardware as of 2026-08-30.

Running the benchmark rather than reading its capability list, on an emulated
ViRGE/DX with 4 MiB, all four 3D tests at five repeats:

| Test | Raw speed | Reality marks |
|---|---|---|
| 25 pixel | 23.35 Kpolys/s | 0.75 |
| Robots | 11.54 images/s | 2.99 |
| Fill rate | 42.09 Mpixels/s | 9.11 |
| City scene | 15.50 images/s | 3.85 |
| **3D performance** | | **1.96** |

Those are modest numbers and they are meant to be read as "the path is real and
survives a third-party workload", not as a performance claim: millions of
triangles go through the S3D engine with depth testing live and not one FIFO
timeout or engine reset. The 25-pixel figure is *down* from 28.54 Kpolys/s,
which is the cost of depth actually being done.

**Serving `DDBLT_DEPTHFILL` is a trade-off, and the numbers above are the side
of it this driver currently takes.** A controlled A/B — same guest, same
session, the only difference being a HAL built without the depth-fill path —
gives 23.42 / 9.41 / 67.66 / 11.38 and the same `3D performance` of 1.97. So
moving the depth clear off the CPU and onto the blitter buys Robots 22% and
City scene 36%, costs Fill rate 38%, and leaves the composite where it was. It
also does *not* recover any of the 25-pixel drop, which is what implementing it
was expected to do.

A third arm — the clear served by the driver but on the CPU rather than the
blitter — gives 23.38 / 9.29 / 62.03 / 11.13 and a composite of 1.92, the worst
of the three. So the gains are the engine's, not the callback's, and the
trade-off cannot be avoided by choosing a different fill path.

There is also reason to doubt the fill-rate figure is about this chip at all.
Published per-cycle rates put a 55 MHz ViRGE 325 at 44 Mpixels/s on
*non-textured* polygons with no Z buffer, 23 with Z, and single figures for
perspective-correct textured pixels; the DX improves only the textured path.
Both numbers in the table above sit above that non-textured ceiling, so 86Box
is not reproducing the real part's fill throughput, and a swing in a figure the
silicon could not produce is not a result about the silicon.

**It is kept anyway, for two reasons that are not about this benchmark.** S3's
own ViRGE driver in the Windows 98 DDK advertises the same capability and
handles the same flag in the same branch as its colour fill, so serving it is
what the vendor's driver for this chip does. And every bias in the environment
these numbers come from favours the arm it lost to: 86Box's framebuffer is host
RAM, so the CPU clear never pays the uncached-aperture cost that dominates CPU
drawing on real cards, and its command FIFO runs on a host thread with no
silicon analogue. This depth-fill A/B has not been repeated on a physical
ViRGE; later physical ViRGE testing does not establish its throughput.
Why the fill rate falls is still not established; see
[docs/decisions/2026-08-30-ddblt-depthfill.md](decisions/2026-08-30-ddblt-depthfill.md).

FR's own `Visual appearance` percentage is not quoted here. It reads the same
value before and after depth testing began working, and the same value again
for FR's built-in ViRGE reference entry, so it appears to score the advertised
capability set rather than the rendered image. See
[docs/specifications/final-reality-101-runbook.md](specifications/final-reality-101-runbook.md).

## How it compares to the retail S3 drivers

For 2D desktop use Velocity9x is close to the retail driver on both chips in
the recorded comparisons. Its hardware Direct3D remains a smaller subset;
Trio64 also offers an opt-in software rasterizer whose speed is unmeasured.

**Where it matches the retail driver**

- The full 8/16-bpp mode matrix, live resolution switching, and — on the ViRGE
  — live colour-depth switching, which retail Windows 9x drivers of this era
  generally do *not* do.
- DirectDraw fundamentals: video-memory surfaces, real page flipping off the
  CRTC display-start register, true vertical-blank waits, accelerated fills
  and screen-to-screen copies.
- **Measured, not asserted:** on Ironfield RTS's `BltFast` presentation path the
  retail S3 driver gets 19 FPS and Velocity9x gets 18 — about 6% behind, with the
  same game binary on the same emulated ViRGE. Details and caveats in
  [docs/decisions/2026-08-17-native-driver-benchmark.md](decisions/2026-08-17-native-driver-benchmark.md).

**Where it is behind the retail driver**

- **Partial GDI acceleration.** The retail drivers accelerate desktop blits,
  fills and line drawing through the same 2D engine Velocity9x used to reserve
  for DirectDraw. Of those, **solid rectangle fills and screen-to-screen copies,
  including overlapping ones in all eight directions, are now accelerated on
  both S3 chips** (builds `gdi-accel-001` through `003`) — the operations behind
  a desktop fill and a window scroll or move. Line drawing and colour uploads
  remain software. ViRGE monochrome uploads are implemented but off by default;
  the unreleased checkout adds text on Trio64 and ViRGE, also off by default.
  ATI, VBE and Matrox currently use software GDI because this driver has no
  native 2D backend for them. Accelerated cases retain a DIB Engine fallback,
  bounded polling and a session-long poison latch. Those guards do not prevent
  every hardware bus stall: the physical Trio64 text/DOS-box interaction is
  still awaiting verification of the shipping fix. See [current status](STATUS.md) and
  [docs/decisions/2026-08-26-gdi-accel-000.md](decisions/2026-08-26-gdi-accel-000.md).
- **No hardware cursor.** The retail drivers use the chip's cursor; Velocity9x
  draws a software cursor.
- **Direct3D is a subset.** The hardware path now supports colour-key
  transparency, and the ZRGB1555/RGB565 mismatch was fixed in 0.7.0 by making
  the desktop, GDI and DirectDraw agree on 5:5:5 under hardware Direct3D.
  Texture formats and blend operations remain limited; additive and
  multiplicative blends are unsupported, and Trio3D/2X has additional
  measured restrictions. `SetRenderTarget` onto a flipping chain is also an
  open correctness issue. See [current limitations](STATUS.md) and the
  [5:5:5 fix record](decisions/2026-09-02-a-555-desktop-needs-three-places-to-agree.md).

- **The software rasterizer is opt-in; speed is unmeasured.** Select Software
  on the Velocity9x settings page, or set `Direct3D=2` in
  `SYSTEM.INI`'s `[Velocity9x]` section and restart. This serves Direct3D from
  a CPU rasterizer in the S3, ATI and VBE packages, including cards with no
  3D hardware. The guarded Matrox candidate does not package the HAL. The
  rasterizer draws depth-tested Gouraud triangles with one texture, point or
  bilinear, decal or modulate, from ARGB1555, ARGB4444 or RGB565. It supports texture
  WRAP/CLAMP and vertex-alpha blending with a limited set of factors, verified
  on the emulated Trio64. Texture alpha, perspective correction, mip selection
  and fog remain absent. See the [alpha](decisions/2026-09-02-software-alpha-blending.md),
  [wrap](decisions/2026-09-02-software-texture-wrap.md) and
  [RGB565](decisions/2026-09-02-software-rgb565-textures.md) records.
  No period-machine performance measurement is recorded.
- **Depth gradients are exercised but unverified.** Depth comparison and
  depth-write masking are both pixel-verified. The per-pixel depth slope is
  not: the emulator this is tested on doubles a triangle's start depth but not
  its X gradient, so a sloped test there would measure the emulator rather than
  the driver. Final Reality drives the gradients across sloped scenes without
  faulting, which is not the same as computing the right depth.
- **Fewer modes.** Packed 24-bpp output is not implemented, even when a BIOS
  offers it. The S3 VESA "24-bit" mode numbers measured here are actually
  32 bpp. ATI's baseline stops at 16 bpp. Generic VESA
  adds validated BIOS modes dynamically, including widescreen and True Color
  modes; availability depends on the card's BIOS and framebuffer limits.
- **No hardware acceleration above 16 bpp.** Both S3 blitters decline at 24 and
  32 bpp and the CPU fallback serves those depths, so DirectDraw fills and blits
  are software there. Direct3D is 16-bpp only. This is a limit of this driver
  rather than of the chip: XFree86's S3V driver accelerates 32 bpp by running
  the engine in 16-bpp mode over the same rectangle at double width, which is a
  documented route to lifting it that nothing here has tried
  ([issue](issues/2026-08-30-virge-1024x768x32-xfree86-limit.md)).
- **DirectDraw low-resolution modes are unreliable.** 640x400 is reachable
  from GDI but not from `SetDisplayMode`, and the 320x200/320x240 ModeX path
  reports success then fails in use. Applications configured for those modes
  can crash. See
  [docs/issues/2026-08-15-doom95-low-resolution-modes.md](issues/2026-08-15-doom95-low-resolution-modes.md).

Real-application results, including where the driver is known to fall short,
are recorded under [docs/issues](issues).
