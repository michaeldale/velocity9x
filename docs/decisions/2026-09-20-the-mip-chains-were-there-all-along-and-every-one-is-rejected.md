# The mip chains were there all along, and every one is rejected

2026-09-20, 86Box `Win86SE` guest (ViRGE/DX), 3DMark 99 Max Pro, full suite,
`D3dPidDistinct=1`. Attached: `2026-09-20-virge-3dmark99-bounded-V9XSNA6.txt`.
Score 339 against 346 and 353 on the two previous runs.

## Both fixes hold

```
StateMaxCount=101   StateClamped=0
UptimeDriverInit=28358  UptimeFirstD3d=31143  UptimeFirstFlip=65228
```

The bound restored to `V9xD3dDrawPrimitives` never bites: 101 state changes
against a backstop of 1024, so no legitimate record is refused and the
unbounded walk is closed. And the uptime bracket is monotonic for the first
time - 2.8 seconds from driver ready to the first Direct3D context, then
**34 seconds to the first flip**, which is the first number this project has
ever had for the slow start it has been describing without measuring.

## And the fix exposed something that was invisible

```
D3dMipChainChecks=64827   D3dMipChainGaps=33766   D3dMipChainLevels=0
```

Every earlier capture read all three as zero, and that was reported here more
than once as "nothing mipmapped exists". **That was wrong.** The checks were
zero because the bind never ran - `TEXTUREHANDLE` was among the states
`DrawPrimitives` was stepping over, so 68,182 draws had no texture bound and
no chain to inspect.

With the handles arriving, the truth is the opposite of what was recorded:
3DMark99 supplies mip chains, the ViRGE checks 64,827 of them, finds a gap in
33,766, and accepts **zero levels**. Not one chain has ever been usable.

`v9x_d3d_mip_chain_contiguous` requires the levels to be laid out contiguously
behind the top surface. Whether 3DMark99's are laid out some other way, or the
check's arithmetic is wrong, is not established here - only that the outcome
is total.

This matters directly to the filter question. `FilterMinSeen=0x5E` says the
application asks for `MIPNEAREST`, `MIPLINEAR` and `LINEARMIPLINEAR`
minification; the ViRGE advertises all three; the chains arrive; and every one
is refused, so the mip part of every request is dropped for want of levels.

## A correction to make plainly

Two claims in this decision log are withdrawn.

"The ViRGE advertises four filters it cannot honour" - the ViRGE has real
mip-chain code and uses it in the bind. What it does not advertise is
`D3DPTEXTURECAPS_MIPMAP`. That is an inconsistency worth resolving, but it is
not the same as lying about the filters, and it was inferred without reading
`d3d_virge.c`.

"`D3dMipChainLevels=0` on every capture, so nothing mipmapped exists" - the
counter was zero because the code that increments it was unreachable. A zero
from an instrument on a path that never runs is not a measurement, which is
the third time in two days that has been the lesson here after
`BltEngineFlipPending` and `DrawsMagLinear`.

## The scores

| build | 3DMarks | CPU |
|---|---|---|
| states skipped | 353 | 1780 |
| states applied | 346 | 1793 |
| states applied, bounded | 339 | 1764 |

Fourteen points across three runs, one run each, on an emulated ViRGE whose
host is doing other work. Nothing here separates the bound's cost from run to
run variance, and the bound cannot plausibly cost anything at 101 of 1024.
The direction is what the texture binding predicts and the magnitude is not
established.

## Still open

`DrawsHandleUnresolved` is 10, up from 9 and from 0 before handles arrived.
Ten draws named a texture that did not resolve. Small, real, unexplained.

Nothing here is measured on the netbook, where the same skip was discarding
Intel's states and where `DrawsNoHandle` should fall the same way.
