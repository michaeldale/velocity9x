# Half the mip chains are not contiguous, and the check is correct

2026-09-20, 86Box `Win86SE` guest (ViRGE/DX), 3DMark 99 Max Pro, full suite,
`D3dPidDistinct=1`. Attached: `2026-09-20-virge-3dmark99-mipgap-V9XSNA7.txt`.

## The breakdown

```
D3dMipChainChecks=65737   D3dMipChainGaps=34360
MipGapShape=0             MipGapOffset=34360      MipGapBounds=0
MipGapExpected=0x003FF9A0 MipGapActual=0x003C6BC0
MipLevelsMax=5
```

Every refusal is the contiguity test, all 34,360 of them. **Not one** was a
wrong shape and **not one** was a bounds failure: the levels always halve
correctly and always fit in video memory. The single reason a chain is
refused is where DirectDraw put it.

And `MipLevelsMax=5`: chains five levels deep are accepted. Roughly half
work - 34,360 gaps in 65,737 checks - so this is placement luck, not a
systematic failure.

The offsets say what kind of placement. The next level sits at `0x003C6BC0`
where the walk expects `0x003FF9A0`: **232,928 bytes BEFORE** it, not after.
The allocator is not padding an ascending layout, it is placing the level
somewhere else in the heap entirely.

## The check stays

The contiguity requirement is the hardware's, it is documented in
`d3d_virge.c`, and it was established by measurement rather than assumption.
The S3D unit takes one `TEX_BASE` and derives every level from it, each
immediately after the one before. Until 2026-09-03 the engine assumed
DirectDraw laid them out that way, on the strength of the emulated ViRGE
passing the probe's mip rung - which it passed because its allocator happened
to place two levels back to back. **A physical Trio3D/2X read black for the
same rung**, because the level-1 fetch went to whatever followed level 0.

So the walk exists to stop exactly this, and a chain with a gap draws from
level 0 with mip selection off - wrong, in that the texture shimmers at
distance, but visibly wrong and from the right texels.

There is no defect here to fix. The check is doing what it was written to do
and what a physical card required.

## What would actually improve it

Making the levels land contiguously is an allocation change, not a check
change: the driver owns the video-memory heap it publishes, and a mipmapped
texture could be given one reservation with the levels placed inside it
rather than each level taking its chances with the ordinary allocator.

That is a design change to surface creation with its own risks - a
reservation that fails, or fragments the heap, is worse than a shimmer - and
it is not something to attempt from a 52 per cent hit rate alone. It is
recorded here as the option, not taken.

## The instrument that made this readable

`mip_chain_levels` and `mip_chain_delta` are reset at the top of every check,
so across 65,737 checks they described one arbitrary check - `D3dMipChainDelta`
still reads `0xFFFFFFFF` in this capture, its initialised value, and
`D3dMipChainLevels` reads 0 while `MipLevelsMax` reads 5. The accumulating
counters are what turned "every chain is rejected" into "every rejection is
the same one reason, and it is placement".

That is the fourth instrument in two days whose zero was an artefact rather
than a measurement, after `BltEngineFlipPending`, `DrawsMagLinear` and
`uptime_driver_init`. The pattern is worth naming: **a counter is only
evidence when the code that increments it ran, and a counter that is reset
per attempt describes the last attempt and not the run.**

## Also in this capture

`DrawsHandleUnresolved` fell from 10 to 2 with no relevant change between the
builds, so it is run-to-run and not a fixed population. Still unexplained,
now known to be variable.
