# The state changes were being stepped over

2026-09-20, 86Box `Win86SE` guest (ViRGE/DX), 3DMark 99 Max Pro, full suite,
before and after. Attached: `2026-09-20-virge-3dmark99-fixed-V9XSNA5.txt`.

## The defect

`D3DHAL_DRAWPRIMITIVESDATA` carries its data as, in the DDK's own words,
"interleaved `D3DHAL_DRAWPRIMCOUNTS`, state change pairs, and primitive
drawing commands". `V9xD3dDrawPrimitives` read the counts, and then:

```c
cursor += (DWORD)counts->wNumStateChanges * 2ul * sizeof(DWORD);
```

It added the pairs' width to the cursor and walked on. **Every state change
an application sent this way was discarded**, and 3DMark99 sends them only
this way - `D3dRenderStateCalls` and `D3dExecuteCalls` both read zero across
a complete benchmark.

So the driver published bilinear and trilinear filtering in its caps, the
application asked for a filter, and the request was stepped over. The same
went for the texture handle, the blend, the address mode and the rest.

A second defect sat in the same three lines: a record with more than 64
state changes was rejected outright. The measurement below shows 101, so
that guard would have aborted the record even if the pairs had been read.

## The measurement

Both runs are the full suite at 800x600x16 on the same guest. The second has
`D3dPidDistinct=1`, so its counters are the benchmark's alone.

| | before | after |
|---|---|---|
| `StateMaxCount` | 0 | **101** |
| `FilterMagSeen` | `0x00000000` | `0x00000006` |
| `FilterMinSeen` | `0x00000000` | `0x0000005E` |
| `DrawsNoHandle` | 68,182 | **864** |
| `DrawsHandleUnresolved` | 0 | 9 |
| `D3dRenderPrimitiveCalls` | 24,033 | 22,981 |
| 3DMark score | 353 | 346 |

`FilterMagSeen` `0x06` is `NEAREST` and `LINEAR`. `FilterMinSeen` `0x5E` is
those two plus `MIPNEAREST`, `MIPLINEAR` and `LINEARMIPLINEAR`: the
application asks for mip-mapped and trilinear minification.

The handle count is the plainest evidence. 68,182 draws had no texture bound
because `TEXTUREHANDLE` was among the states being skipped; now 864 do, which
is what an ordinary mix of textured and untextured geometry looks like.

The score moved 353 to 346, about two per cent slower - which is the
direction to expect when the renderer starts binding the textures it was
previously drawing without.

## What the fix is

The switch that applied render states was inline in `V9xD3dRenderState`. It
is now `v9x_d3d_apply_state`, called from both that callback and the
DrawPrimitives loop - one switch, so the two paths cannot drift, which is the
reason it is a function rather than a copy. The loop applies each pair and
records `wNumStateChanges` into `StateMaxCount`.

## What this does not show

`DrawsMagLinear` and `DrawsMinLinear` read zero in both runs, and that is not
a result: both counters are incremented only in `d3d_i9xx.c`, so they cannot
be non-zero on a ViRGE at all. This is the second instrument in two days that
read zero because its path does not exist on the chip under test - the first
was `BltEngineFlipPending`. Whether the ViRGE's sampler honoured the filter
it was finally given is unmeasured.

`DrawsHandleUnresolved` went from 0 to 9. Nine draws named a texture that did
not resolve, which was impossible before because no handle was ever set. It
is small and it is unexplained.

## And a gap this fix widens

`FilterMinSeen` now shows the application asking for `MIPNEAREST`,
`MIPLINEAR` and `LINEARMIPLINEAR`. The ViRGE advertises all three and cannot
honour any: `D3dMipChainLevels` is zero on every capture ever taken, and the
bind drops the mip part of every `MIP*` value because no map has levels.

Before this fix those requests never arrived, so the advertisement was
harmless. They arrive now. The ViRGE and the Intel path also disagree - Intel
publishes `NEAREST | LINEAR` alone - and that disagreement is still
unresolved.
