# DDBLT_DEPTHFILL is one solid fill, and it is a trade-off rather than a win

Date: 2026-08-30
Branch: `d3d-mode-selector`
Plan: [`s3-trio64-voodoo2-hybrid-3d.md`](../plans/s3-trio64-voodoo2-hybrid-3d.md),
mode 3's first deliverable - and worth doing whether or not mode 3 ever exists.

Guest: `Win86SE`, 86Box, `s3` family, ViRGE/DX 86C375, 4 MiB, 1024x768x16,
agent port 9869. Build `5d69a51-dirty`.

## What it cost before

Nothing in `src/` served `DDBLT_DEPTHFILL`, and `DDCAPS_BLTDEPTHFILL` was not
advertised, so DirectDraw cleared a Z buffer by locking it and writing every
word from the CPU through the uncached aperture, once per frame. That is part
of what Final Reality's 25-pixel figure paid when depth testing began actually
happening - 28.54 to 23.62 Kpolys/s
([2026-08-30](2026-08-30-virge-depth-fifo-reservation.md)).

## The implementation is smaller than it looks

A depth clear is a solid fill of a 16-bit surface. `v9x_virge_fill` is already
parameterised on the destination offset, the destination surface's own pitch
and the bytes per pixel, and takes its value from `bltFX.dwFillColor` - which
is a union member sharing its DWORD with `dwFillDepth` (`DDRAW.H:223-229`). So
**no engine code changed at all.** `v9x_depthfill_body` in `ddhal_core.c`
validates the request and hands the existing fill a different offset and a
different width.

Three things it does not do, each deliberate:

- **It does not consult the screen depth.** The colour fill gates on
  `v9x_depth_is_blittable(fb.bits_per_pixel)`; a depth surface is 16-bit
  whatever the desktop is, and applying that gate here would refuse a
  legitimate depth clear on an 8- or 32-bpp desktop for a reason unrelated to
  the surface being filled.
- **It does not hardcode 2 bytes per pixel.** `ddhal_core.c` is chip-neutral
  and the depth width is the engine's, so it asks
  `v9x_d3d_depth_bytes_per_pixel()`, which reads `depth_bits_per_pixel` out of
  the fitted engine's limits. A literal 2 in that file is exactly the mistake
  the comment on that field already records having made once. Zero means the
  chip has no D3D engine, and the fill declines - so a card that cannot have
  depth buffers cannot be asked to clear one.
- **It does not accept any destination.** `DDSCAPS_ZBUFFER` is required and
  `DDSCAPS_SYSTEMMEMORY` refused. A depth fill aimed at the primary would
  otherwise paint the screen with a Z value.

## The constant, which memory gets wrong

`DDBLT_DEPTHFILL` is **`0x02000000`**, from `C:\98DDK\src\display\inc\DDRAW.H`
line 2799. Its neighbours in the flag list suggest `0x00002000` - `DDBLT_DDFX`
is `0x00000800`, `DDBLT_DDROPS` `0x00001000` - and that is what writing it from
memory produces. The `DDBLT_` flags are not densely packed and this one sits
above `DDBLT_ROP`. `DDCAPS_BLTDEPTHFILL` is `0x10000000` (`DDRAW.H:1745`).

Both were read out of the DDK before either was used. That is the whole reason
to check: the wrong flag value would have made the driver decline every depth
fill while claiming a capability it never received a request for, and the
symptom would have been "no change in performance" rather than anything that
looks like a bug.

## Measured

`V9XDDP.EXE` gained `v9x_probe_depth_fill`, which fills a 64x64 16-bit
video-memory Z surface twice and reads the words back:

```
ZDepthFillHr=0x00000000    ZDepthFillRaw=4660        (0x1234, first fill)
ZDepthFill2Hr=0x00000000   ZDepthFill2Raw=43981      (0xABCD, second fill)
                           ZDepthFillCornerRaw=43981 (0xABCD at 63,63)
ZDepthFillOk=1
```

**Two values, not one.** Freshly allocated video memory holds whatever the last
owner left, so a single-value test passes by accident often enough to be
worthless; the second fill has to change what the first one wrote. **Two
positions**, because a rectangle blit with the wrong pitch or height writes the
first word or the first row and nothing else, and reading `(0,0)` alone cannot
see it.

The rest of the ladder is unchanged on the same run: `D3DZCompareOk=1`,
`D3DZWriteMaskOk=1`, `TexFormatCount=2`, `D3DHalFound=1`, `Result=COMPLETE`,
`Build=` matched, result file deleted first, `exec` exit code 0, one run in the
boot.

## The caps change is inert on a family without Direct3D

`DDCAPS_BLTDEPTHFILL` is added to the word DriverInit publishes for the whole
binary, so every family's HAL now claims it before the 16-bit side narrows
anything. That is a regression risk on three families, and it was checked
rather than reasoned about.

`Win98SE-Trio64`, same build, new HAL installed: `D3DHalFound=0`,
`D3DDeviceCount=3`, `TexFormatCount=0` - the clamp in
`V9xDdCreateDriverObject` reassigns `dwCaps` to `GDI | BLT | BLTCOLORFILL` and
therefore drops the new bit with the rest. DirectDraw stayed fully working:
`BltFillPixelOk=1`, `SrcCopyPixelOk=1`, all four overlap cases,
`RestoreHr=0x00000000`, `Result=COMPLETE`. `FlipPixelOk=0` is the known
pre-existing result.

The `ZDepthFill*` keys are absent from that run, correctly: the probe's depth
test lives inside the D3D block, and a chip with no Direct3D device never
creates a Z surface to fill.

`Win98SE-Mach64VT2` covers the other shape - a family whose chips supply no
engine descriptor at all, so `engine.flags` never gets `V9X_DD_ENGINE_VALID`.
Same result: `Stage=enable-ok`, `Direct3DMode=none`, `D3DHalFound=0`,
`D3DDeviceCount=3`, `TexFormatCount=0`, and DirectDraw working through fill,
copy, overlap and `RestoreHr`. That leaves `vbe` as the only family not booted
with this change; it takes the identical code path to `ati` - null
`fill_engine_descriptor`, zero caps - and differs from it only in which chip
module is linked.

## The pixel test proves less than it looks like it proves

`ZDepthFillOk=1` came back **identical on a HAL built without any of this
work** - no `v9x_depthfill_body`, no `DDCAPS_BLTDEPTHFILL`. Measured, not
supposed: the control build for the A/B below was installed and the probe run
against it reported `ZDepthFillHr=0x00000000`, `ZDepthFillRaw=4660`,
`ZDepthFill2Raw=43981`, `ZDepthFillCornerRaw=43981`, `ZDepthFillOk=1`.

DirectDraw emulates `DDBLT_DEPTHFILL` when the driver declines it, and returns
`S_OK` either way. So the test establishes that **the fill is correct** - the
right value, over the whole rectangle, twice - and says **nothing about who
performed it**. A driver path that returned `DDHAL_DRIVER_NOTHANDLED` on every
call would pass it unchanged.

That is worth stating plainly because the obvious reading of a green pixel test
is "my code ran", and here it is not. The section below settles it by a
different route - counting `Blt` callbacks across the two builds - and the
answer turns out to be that the code does run. That does not redeem the test:
it would have read green either way, and a version of this work that was
entirely inert would have shipped looking verified.

## It is a trade-off, not a win, and the composite does not move

A controlled A/B on `Win86SE`: same guest, same session, same Final Reality
configuration (four 3D tests, five repeats, 2D and bus tests cleared), the only
difference being which `V9XHAL.DLL` was installed. The control was built by
checking the five HAL sources out at the parent commit, so it is this branch
minus the depth fill and nothing else.

| Test | Control, no depth fill | With depth fill | Change |
|---|---|---|---|
| 25 pixel | 23.42 Kpolys/s | 23.35 | -0.3% |
| Robots | 9.41 images/s | 11.54 | **+22.6%** |
| Fill rate | 67.66 Mpixels/s | 42.09 | **-37.8%** |
| City scene | 11.38 images/s | 15.50 | **+36.2%** |
| **3D performance** | **1.97 marks** | **1.96** | flat |

The control reproduces the previously recorded run - 23.62 / 9.45 / 67.74 /
11.46, `3D performance` 1.97 - to within 1% on every test, which is what makes
the comparison a measurement rather than two sessions being compared. The
depth-fill column was also run twice (Fill rate 41.09 and 42.09), so neither
side is a single sample.

**The prediction this was supposed to confirm is refuted.** The Final Reality
plan said the 28.54 to 23.62 Kpolys/s drop was the cost of depth work "and
partly of DirectDraw clearing the depth buffer on the CPU every frame", and
that the two were "only separable by implementing the depth fill and
re-running". They are now separated: **25 pixel does not move.** Essentially
none of that 17% was the clear.

**And the fill-rate regression is real, reproducible and unexplained.** A
plausible mechanism is that the clear now goes through the 2D engine and waits
on FIFO slots, so it serialises against queued S3D work, where the CPU pass it
replaced touched no engine at all - a fill-rate test keeps the engine busy and
would pay most for that. That is a hypothesis, not a finding; nothing here
measures it. What is measured is that two scenes gain 22-36%, one loses 38%,
and the composite is unchanged.

This is exactly the failure mode this section of the hybrid-3D plan warns
about: "the honest reading of where the wins are" says a blit costs FIFO
reservation and register setup, and that assuming otherwise is the same class
of mistake as assuming a FIFO could supply 18 slots when it reports 16. The
per-frame clear was argued as structurally cheaper without being measured. It
is cheaper for some scenes and dearer for others.

## Who serves the fill, settled by counting Blt callbacks

The trace ring wraps long before the probe finishes, so the `Blt` entry
carrying `0x02000000` was gone by the time `V9XTRACE.EXE` ran. The counters
answer it anyway, because the probe issues **exactly two** depth fills and
nothing else in it changes between the two builds:

| | `CountBlt` | `CountBltEngine` | `ZDepthFillOk` |
|---|---|---|---|
| Control, no depth-fill path | 7 | 7 | 1 |
| With depth fill | **9** | **9** | 1 |

Two conclusions, and the first is the one the pixel test could not reach.

**Without `DDCAPS_BLTDEPTHFILL`, DirectDraw does not dispatch the depth fill to
the driver at all.** Seven against nine: the driver's `Blt` callback is never
entered for it, so the runtime emulates the whole thing. That is why
`ZDepthFillOk=1` on the control - and it means the control build is a true
"driver does nothing" arm rather than "driver declines and the runtime picks up
the pieces".

**With the cap, the engine serves both.** `CountBltEngine` rises by the same
two as `CountBlt`, and that counter is incremented only where `ops->fill`
returned `V9X_BLT_DONE`. Neither depth fill fell through to `v9x_cpu_fill`.

So the driver path runs, and it runs on the blitter. Both of the questions this
document previously left open are closed, and the answers are the ones the
implementation intended.

**Which makes the fill-rate cost the engine's.** `EngineFifoTimeouts=0`,
`EngineIdleTimeouts=0` and `EngineResets=0` across the run, so the 38% is not
timeouts or recovery - it is the ordinary price of reserving FIFO slots and
writing seven registers per clear, on a part where the CPU pass it replaced
touched no engine state at all. That is consistent with the serialisation
hypothesis above and still does not prove it; what it does rule out is the
engine misbehaving.

## What is not established
- **Why the fill rate falls.** The A/B says it does, reproducibly, and the
  counters now say the engine is doing the work without timing out. What is
  still missing is the mechanism: whether the per-clear FIFO wait genuinely
  stalls queued S3D work, and whether a clear issued through `v9x_cpu_fill`
  instead - the driver's own CPU path, which skips the engine but keeps the
  single callback - would keep the Robots and City gains without the fill-rate
  loss. That is a one-line experiment behind a build switch and the obvious
  next step if the trade is worth trying to win outright.
- **Whether the trade is worth taking.** Two scenes gain 22-36%, one loses
  38%, the composite is flat, and the sample is one benchmark on one emulated
  chip. Nothing here says how a real title's mix of geometry and fill falls
  out, and the decision to keep, revert or make it selectable is not one this
  document settles.
- **Any chip but the ViRGE.** No other family has a D3D engine, so
  `v9x_d3d_depth_bytes_per_pixel()` returns zero everywhere else and the fill
  declines. That is asserted by construction, not measured.
