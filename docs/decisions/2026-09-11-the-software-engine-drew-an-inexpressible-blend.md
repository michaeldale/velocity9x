# The software engine drew a blend it cannot express, opaque

Date: 2026-09-11. Measured on the 86Box Trio64 guest, agent 9871, boot 346,
against the emulated ViRGE/DX's hardware result from the day before.

Yesterday's session left this as an unexplained loose end: on the Trio64 guest
in software mode the probe's `BlendModulate` cell drew white, it had done so
in every run that day including the pre-change ones, and no texture refusal
was counted for it. Both halves of how it was described were wrong. It is not
a texture cell, and it is not unexplained.

## What the cell is

It fills the render target with `0x03E0`, sets `ALPHABLENDENABLE` with
`SRCBLEND = D3DBLEND_DESTCOLOR` and `DESTBLEND = D3DBLEND_ZERO`, and draws an
**untextured white triangle**. That is the multiplicative pass a lightmap
uses, and the S3D unit cannot express it.

Its own comment states the standard: a correct multiply of white over green
leaves green, a driver that declines the pass leaves green, and a driver that
draws it opaque leaves white. Green is the only acceptable answer. The cell
exists because drawing it opaque is what produced 3DMark 99's saw-toothed
panels on the hardware path.

## The two engines disagreed

```
ViRGE/DX guest, hardware path   BlendModulateRaw  992    0x03E0, unchanged   Ok=1
Trio64 guest, software path     BlendModulateRaw  65535  white               Ok=0
```

The hardware path skips the draw and counts it, and has since 3DMark 99. The
software path drew it opaque, deliberately, on a documented argument: the
factor pair is render state an application may set to anything in the
enumeration, and "a refusal would report failure for a legal draw".

The second half of that is right. The first does not follow from it.
**Skipping the triangles is not refusing the batch** - the HRESULT stays zero
either way, which is exactly what `v9x_d3d_virge_draw` does. And "opaque is
the wrong picture, but it is a picture" does not survive contact with what the
pass is for: the destination is what the frame has already drawn, a correct
multiply by white leaves it alone, and painting it white destroys the frame.

## The fix, and what it measures

The software engine now skips a draw whose factor pair
`v9x_d3d_raster_alpha_valid` refuses, and counts it in the two fields the
ViRGE path already uses - the engines share them because a boot runs one of
them and `V9XHW.INI` records which. Boot 346:

```
BlendModulateRaw   992           the fill, unchanged
BlendModulateOk    1
D3dBlendSkipped    1
D3dBlendLastPair   0x00090001    src 9 DESTCOLOR, dst 1 ZERO
```

The pair is named in the counter, which is the difference between "the driver
declined something" and "the driver declined *this*".

`compare-probe.ps1` over the run before and after bounds the change to two
keys - `BlendModulateOk` and `BlendModulateRaw` - plus `VBlankStatus`, which
samples whether the beam is in blanking at that instant and differs between
any two runs. Nothing else moved. In particular `VtxAlphaCurveOk` stays 1, so
the expressible blends still land, and the system-memory texture from
yesterday still reads 2016.

## What it does not cover

**No host test.** The engine needs the DirectDraw types and is not in the host
build; only the rasterizer is, and the rasterizer was already right - its
`v9x_d3d_raster_alpha_valid` is the check the engine now consults instead of
ignoring. The guest is the test, and the cell that caught it is the
regression.

**Nothing physical, and no application.** A lightmapped scene through software
Direct3D on real silicon has never been drawn. What is established is that the
frame survives the pass now instead of being painted over.

**The four factors are still four.** ONE and SRCALPHA for source, ZERO and
INVSRCALPHA for destination, which is what S3's own driver publishes on this
generation and what `describe_caps` advertises. Anything else now draws
nothing rather than something wrong. Implementing DESTCOLOR is a separate
change with its own evidence, and the caps say plainly that it is not there.

> **Superseded the same day.** DESTCOLOR is now implemented as a source
> factor in the software engine and advertised by its `describe_caps`, so
> the lightmap pass draws the product rather than nothing:
> [the lightmap pass now draws](2026-09-11-the-lightmap-pass-now-draws.md).
> The skip-and-count above is unchanged and still covers every factor pair
> outside the five. The hardware path still has four.

## Corrections to yesterday's record

[The system-memory texture record](2026-09-10-software-d3d-system-memory-textures.md)
calls this "the probe's video-memory `BlendModulate` cell" and lists it as
unexplained. It is neither video-memory-specific nor textured, and it is
explained here. The limits section of that record is amended to point at this
one.

## Gates

`run-checks.ps1` green. Verified on `Win98SE-Trio64` with `Direct3D=2`, which
serves every Direct3D draw from the CPU.
