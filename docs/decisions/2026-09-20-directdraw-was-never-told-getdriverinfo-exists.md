# DirectDraw was never told GetDriverInfo exists

> **Corrected 2026-09-20.** The claim that draws "sampled bilinear" is
> withdrawn. `DrawsMagLinear` and `DrawsMinLinear` are incremented where the
> driver submits a batch whose sampler state names a linear filter. That
> establishes the state this driver sent; it does not establish which filter
> produced any pixel, which nothing in this project has measured. Read them
> as "textured submissions carrying linear filter state".
>
> Two things this document treats as outstanding were fixed after it was
> written and should not be read as open: the missing
> `DDHALINFO_GETDRIVERINFOSET` advertisement, and `V9xD3dRenderState`
> discarding blocks longer than 64 states.


2026-09-20. Three changes for the 3DMark99 reports, and two candidates
deliberately left alone. Nothing here has run on hardware.

## The flag that was never set

`DDHALINFO.dwFlags` carried `ISPRIMARYDISPLAY` and nothing else.
DirectDraw calls `GetDriverInfo` only when `DDHALINFO_GETDRIVERINFOSET` is
present, and the constant was defined in
`include\velocity9x\win9x_ddraw_abi.h` and used nowhere.

So `V9xHalGetDriverInfo` has been published and never called, in every build
that ever had it. intel96 measured `DriverInfoCalls=0` in all three
captures, which is what sent me looking. The `GUID_D3DCallbacks2` answer
behind `V9X_C3_SERVE_D3D_CALLBACKS2` has therefore never executed once.

It matters past that path. `GUID_D3DExtendedCaps` is where a DirectX 6
application's texture-size limits, texture-operation caps and simultaneous
texture count come from. With the driver never asked, the runtime supplied
its own defaults, and **what 3DMark99 was told about this device was not
this driver's answer**.

The flag is now set. Nothing else changed in the entry point: it still
declines every GUID but one, with `dwActualSize` zero, which is the
documented "not supported" reply. Turning the channel on commits to serving
nothing, and `DriverInfoCalls` and `DriverInfoLast` will now say what the
runtime asks for - which is what decides whether answering any of it is
worth doing, instead of guessing.

## Long state blocks are clamped, not discarded

```
StateDropReason=5 (count over 64)   StateDropCount=276   (3DMark99)
StateDropReason=5                   StateDropCount=81    (Final Reality)
```

`V9xD3dRenderState` walked at most 64 states and **discarded the whole
block** when an application sent more - every state in it, not the excess.
3DMark99 sends 276.

The cap was the loop's, not the interface's. What replaces it is a bound
that means something: the states must lie inside the execute buffer, whose
size DirectDraw reports in `dwBlockSizeX`.

That field's meaning on this path is not established by measurement, so it
is used **only to make the bound tighter and never to widen it**, with
`V9X_D3D_STATE_MAX` (1024, or 8 KB) as the backstop for a buffer that
reports nothing. `StateExeBytesLast` records what was seen, so the next
capture settles what the field holds rather than the code assuming it. A
value that tracks the block sizes confirms it; a zero says the backstop is
doing the work alone.

`StateMaxCount` and `StateClamped` say whether the new bound ever bites.
Drop reason 5 is retired and its number not reused, so an intel96-era
capture still reads.

## The slow start gets a bracket, not a fix

Nothing has ever measured a duration. `UptimeDriverInit`, `UptimeFirstD3d`
and `UptimeFirstFlip` are `GetTickCount` at three points, and against the
dump's own `DumpUptimeMs` they bound the intervals without timing anything
directly.

`CountDestroySurface=4336` against `CountCreateSurface=806` in intel96's
3DMark99 run is a 5.4-fold asymmetry that nothing explains, and nothing
links it to the startup either. One run with these stamps says where the
time goes before anyone changes anything to make it faster.

## Two candidates left alone, on purpose

**`dpcLineCaps` and `D3DDD_LINECAPS`.** Both empty, and the leading
remaining explanation for the bilinear report now that the other two are
dead. Filling them would be a claim that this device draws lines, and it
does not: `V9xD3dRenderPrimitive` serves `D3DPT_TRIANGLELIST` and the
execute-buffer path serves `D3DOP_TRIANGLE`, and there is no line or point
path at all. Publishing line capabilities would be the advertise-then-ignore
pattern that the `TEXTURESYSTEMMEMORY` comment in `d3d_i9xx.c` exists to
warn against, and an application that drew lines would get nothing.

**`D3DPTEXTURECAPS_SQUAREONLY`.** The bind refuses a non-square texture and
the source says plainly that this is the sampler's assumed rule rather than
`MAP_STATE`'s - the packet's fields hold any dimension.
`D3dTextureRefusedShape=0`, so 3DMark99 obeyed the restriction, possibly by
reducing what it asked for. Relaxing it needs a probe that draws a
non-square map and a look at the result, not a guess.

## The experiment that costs no code

The Intel path publishes `NEAREST | LINEAR`. The ViRGE publishes those plus
`MIPNEAREST`, `MIPLINEAR`, `LINEARMIPNEAREST` and `LINEARMIPLINEAR`. Running
3DMark99 on the Trio3D says whether the bilinear complaint follows the chip
or the capability list, and needs no build at all.

Worth knowing either way: `D3dMipChainLevels=0` - nothing mipmapped exists
on either path - so the ViRGE is already advertising four filters it cannot
honour. Whichever way the complaint goes, the two paths should not disagree.

## What is measured about the complaint itself

Nothing here addresses the picture. intel96 measured 7,631,631 textured
submissions carrying linear filter state in both directions, from the
application's own LINEAR render state - which establishes that the request
reaches the sampler rather than being dropped, and not which filter produced
any pixel. Whether the picture is right was not measured, and the original
wording of this paragraph asserted that it was.
