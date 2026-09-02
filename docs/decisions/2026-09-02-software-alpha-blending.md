# The software engine blends, with the four factors the ViRGE publishes

Date: 2026-09-02
Status: implemented, measured on an 86Box Trio64 in software mode

## Why this and why these four

Rating the driver's Direct3D honestly on 2026-09-02 produced two items that
each independently disqualify most period titles from rendering correctly on
the software engine: no texture WRAP, and no alpha blending at all -
`dwSrcBlendCaps = 0`, `dwDestBlendCaps = 0`. This closes the second.

The hardware path already blends. `v9x_d3d_virge_draw_triangles` has programmed
`cmdALP_BLD_CTL_SrcAlph` for `SRCALPHA`/`INVSRCALPHA` since the ViRGE work, and
`v9x_d3d_virge_describe_caps` publishes it. The gap was the software engine's
alone, and the two are meant to be interchangeable.

Four factors, not eleven, and the four are not arbitrary: they are what S3's own
ViRGE driver publishes on this generation of silicon - `D3DPBLENDCAPS_ONE |
SRCALPHA` for source and `ZERO | INVSRCALPHA` for destination
(98DDK `D3DDRV.C:239-242`). ONE with ZERO is opaque and SRCALPHA with
INVSRCALPHA is ordinary transparency; between them they are what an application
of the period actually asks for. Any other pair is refused by the rasterizer
rather than approximated, because a driver that substituted the nearest factor
it had would draw a plausible wrong picture with nothing anywhere to say so.

## The arithmetic

Alpha is a fourth interpolated channel on `V9X_D3D_RASTER_VERTEX`, 0..255,
carried through `v9x_d3d_raster_edge_at` and the span by the same lerp and the
same sixteen fractional bits the three colour channels use. That is what makes
`D3DPSHADECAPS_ALPHAGOURAUDBLEND` a true claim rather than a flat-alpha engine
wearing the bit.

The blend itself is `(source * sf + destination * df) >> 8` with both weights on
0..256. Weights, not 0..255 factors, and the reason is worth stating because it
is the one place this could have been quietly wrong: dividing by 255 per channel
per pixel is three divides in the inner loop, and shifting by eight instead
needs 255 to map to 256 or a fully opaque fragment comes out one part in 256
short of its own colour. `value + (value >> 7)` is the correction - it adds one
only at 254 and 255, which is exactly where it is needed, and stays monotonic
everywhere else.

`ONE` resolves to a constant 256 and only `SRCALPHA`/`INVSRCALPHA` have to be
recomputed per pixel, so the span resolves both factors once and sets a flag.

The destination is read back from the target and unpacked as RGB565, using the
same `expand5`/`expand6` the texture sampler uses. That is the first time this
engine has read the surface it draws into, and it is a per-pixel read of video
memory in a CPU loop - which is slow, and mode 2's stated trade is "slow,
correct".

## Evidence

Six host tests, and each is aimed at a specific way this could be wrong rather
than at coverage:

- `test_alpha_one_zero_replaces` - ONE with ZERO writes the source over a
  saturated destination and consults neither the alpha nor the destination.
- `test_alpha_full_is_exact` - the weight correction. The colour is chosen to
  make it observable: a saturated channel cannot, since `255*255 + 255*1` is
  255 after the shift either way, so this draws 200 over black, where the two
  arithmetics give 200 and 199 and the packed pixel differs.
- `test_alpha_zero_keeps_destination` - the other end, bit for bit.
- `test_alpha_half_blends` - the probe's own case, half-alpha red over blue.
- `test_alpha_gouraud_varies` - opaque at one vertex, transparent at another,
  requiring a monotone ramp along the row. A flat-alpha engine passes the
  other five and fails this.
- `test_alpha_refusals` - `D3DBLEND_SRCCOLOR` and `D3DBLEND_DESTCOLOR` are
  refused and draw nothing.

Six mutations were introduced into `d3d_raster.c` one at a time and the host
suite was run against each. All six were caught: the weight correction dropped,
the two weights swapped, the destination term dropped, the alpha step zeroed,
the source-factor check removed, and the inverse destination weight not
inverted. The first of these was caught only after the test was rewritten - the
original version of `test_alpha_full_is_exact` used red over white and passed
with the correction removed, which is the failure mode a test suite is supposed
to have and usually does not get checked for.

`./scripts/run-checks.ps1` green.

**WIN98-S3NATIVE, 86Box, S3 Trio64 (5333:8811), `Direct3DMode=software`**,
boot 299:

```
Result=COMPLETE
D3DVertexAlphaStateHr=0x00000000
D3DVertexAlphaBlendRaw=32783      0x800F
D3DVertexAlphaBlendOk=1           was 0
D3DDevice2HwTriSrcBlend=18        0x12 = ONE | SRCALPHA
D3DDevice2HwTriDestBlend=33       0x21 = ZERO | INVSRCALPHA
D3DDevice2HwTriShade=21002        0x520A, was 0x020A
D3DDevice2HwTriTexture=34         0x22, unchanged - no TEXTURECAPS_ALPHA
```

`0x800F` is red 16 of 31 and blue 15 of 31 - half-alpha red over a blue
destination, half of each, which is the answer. The caps arrive at the runtime
through DirectDraw's own enumeration rather than being read back out of the
driver.

Every other `*Ok`, `*Count` and `*Hr` key in the file is byte-identical to the
run before this change. One key moved, and it is the one the change was for.

## What is not claimed

`D3DPTEXTURECAPS_ALPHA` stays absent and the texture blend caps stay
`DECAL | MODULATE`. The sampler still discards a texel's alpha channel, so
`DECALALPHA` and `MODULATEALPHA` - which S3's driver publishes - are a separate
change with their own pixel test. Publishing them now would be the
advertise-then-ignore pattern this driver has paid for twice.

## A note on the instrument

Three probe runs in a row came back `Result=INCOMPLETE` or zero bytes and looked
like a regression from this change. They were not: `v9xctl exec -Detach` returns
as soon as the process starts, and the `get` that followed was reading the INI
while the probe was still writing it. The probe takes about 4.5 seconds. Run it
without `-Detach` and read the result afterwards, or the instrument reports on
its own timing rather than on the driver.
