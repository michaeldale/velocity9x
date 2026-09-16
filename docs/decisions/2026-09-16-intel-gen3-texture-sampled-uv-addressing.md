# The GMA 950 samples a texture, and UV addressing is top-left origin, V down

**Machine**: MICHAEL-NETBOOK, Intel GMA 950 on 945GSE (8086:27AE), Windows 98 SE.
**Build**: `fc9a105`, armed `p6-20260916-fc9a105`, Phase 6.
**Capture**: `C:\temp\intel45` — `INTEL3D0.TXT`, schema 3, `Result=PASS`.
**Date**: 2026-09-16.

## What was measured

The first textured draw this driver has performed. A 32x32 RGB565 texture,
painted by four GPU quadrant blits, sampled across the Phase 5 triangle with
per-vertex texture coordinates spanning the triangle's own bounding box.

Four probes, one per quarter of coordinate space:

| Probe | Pixel | u, v | Read | Quadrant |
|---|---|---|---|---|
| `TexQ0` | 250,160 | < 1/2, < 1/2 | `1C3E1C3E` | 0 |
| `TexQ1` | 400,160 | >= 1/2, < 1/2 | `F325F325` | 1 |
| `TexQ2` | 290,330 | < 1/2, >= 1/2 | `30383038` | 2 |
| `TexQ3` | 350,330 | >= 1/2, >= 1/2 | `07E007E0` | 3 |
| `TexOutside` | 40,400 | outside the triangle | `08420842` | fill |
| `TexCorner` | 0,0 | outside the triangle | `08420842` | fill |

Every probe read the quadrant the build predicted.

## What this establishes

**Texture sampling works on this part under this state block.** That is the
larger half of the result and it was genuinely open: `_3DSTATE_MAP_STATE` and
`_3DSTATE_SAMPLER_STATE` were derived from two reference emitters and had never
been executed. Four distinct quadrant colours arriving at four pixels, and the
fill arriving outside the triangle, is that state block working end to end -
map address, MS3 geometry, MS4 pitch, the sampler's filter and map index, S2's
coordinate declaration, the seven-dword vertex, and the `texld` program.

**UV addressing is top-left origin with V increasing downward.** The blits
place quadrant n at `(n & 1)` half-widths across and `(n >> 1)` half-heights
down from the texture base. `u < 1/2, v < 1/2` read the quadrant at the base,
and `v >= 1/2` read the quadrant one half-height further into memory. So
increasing V moves to higher addresses, which for a linear surface is downward.

Stated as measured rather than as confirmation: the alternative - V increasing
upward, giving a vertically mirrored texture - would have read quadrant 2 at
`TexQ0` and was equally consistent with everything known before this boot.

## What it does not establish

- **Nothing about filtering.** The sampler is nearest with no mips and the
  probes sit well inside their quadrants. A probe on a quadrant boundary would
  ask a different question and none was placed there.
- **Nothing about wrapping.** Clamp-to-edge is set and no coordinate leaves
  `[0, 1]`, so the clamp was never exercised.
- **Nothing about non-power-of-two or tiled textures.** 32x32 linear only.
- **Nothing about the colour conversion.** The quadrant colours are bit
  patterns `XY_COLOR_BLT` writes verbatim and the sampled value reached the
  565 target unchanged. No conversion happened anywhere in this path, so the
  open limitation on the conversion rule outside measured channel values
  (`docs\issues\2026-09-15-intel-565-conversion-outside-measured-values.md`) is
  untouched by this.

## The Phase 5 regression

Scene 0 is the Phase 5 triangle, unchanged and run first. Seven interior probes
`1C3E1C3E`, seven exterior `08420842` - byte for byte what `intel42` and
`intel44` read. The depth-`BUF_INFO` removal, the qword padding, the scene
retirement, the decoder's new texture mode and the per-scene decode all left it
identical.

## Guards and errors

`GLow`/`GUpp` unchanged at `A5A5A5A5`/`00000000` for both scenes.
`TexG0` and `S1TexG` both `00000000`: the page past the texture was not
touched, so no quadrant blit ran long. Both scenes report `PostErrOk=1` with
nine registers and `PostErrFailIndex=FFFFFFFF` - the parser rejected nothing.

## What the capture did NOT carry, and why

`ExpectedApertureReads` and the rest of the read budget are **absent**, and the
capture validator refuses it on that basis. This is a driver defect found by
this boot, not a hardware result.

The budget moved to the top of the Phase 4 replay path earlier the same day,
correctly: a budget written after the thousand reads it bounds is a record
rather than a budget. The Phase 5/6 sequencer then reset the whole `[Intel3D]`
section on entry, which is after the replay - erasing it. The reset is now
guarded to happen once, taken by whichever path writes first.

`DriverApertureReads=0000001E` (30) did survive, because it is written by the
sequencer after the reset. It equals the driver-side term the budget would have
predicted, which is the only cross-check available from this capture.

The consequence for this record: the boot's aperture-read total is **not
measured**. The predicted 1448 stands unverified, and the next Phase 6 boot is
what tests it.
