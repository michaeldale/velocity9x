# intel96: the watermark is programmed correctly and the pipe still underruns

2026-09-20, MICHAEL-NETBOOK (945GSE), build `3b27391-dirty`. Three runs,
each its own boot with a shutdown between, one application apiece.
Attached: `C:\temp\intel96`.

This is the first capture set that can carry a conclusion. Every earlier
attempt failed on attribution, on workload, or on the watermark not holding
the value under test.

## The captures are clean, and the new fields say so

| | Robots | Full Run | 3DMark99 |
|---|---|---|---|
| `D3dPidDistinct` | 1 | 1 | 1 |
| `boot-enable` events | 1 | 1 | 1 |
| `WmDeclined` | 0 | 0 | 0 |
| `PipestatBFirst` bit 31 | clear | clear | **set** |

One application per capture, one boot per capture, no refused watermark
calculation. The counters are not cumulative across the three: `CountFlip`
is 39,556 / 41,221 / 35,441 and `CountDriverInit` 4 / 4 / 1, which no single
DLL lifetime produces.

## The watermark holds the value under test

Every log entry in all three captures computes `0x0314011A` - plane B 20,
plane A 26, both burst bits, bit 25 preserved - and `WmWritten` is that
value in all three. The `0x030C011A` of intel95, plane B 12 from a depth the
hardware was not in, appears nowhere. **The DSPCNTR fix holds on hardware.**

Both Final Reality runs read `FwBlc=0x0314011A` at the pipestat boundary, so
the whole measurement window ran with the watermark already correct. This is
a clean test, which no earlier capture was.

## And the pipe underruns anyway

```
Robots    PipestatBFirst=0x00000202  PipestatBOr=0x80000202   UNDERRAN
Full Run  PipestatBFirst=0x00000202  PipestatBOr=0x80000202   UNDERRAN
3DMark99  PipestatBFirst=0x80000202  PipestatBOr=0x00000202   none fresh
```

Two Final Reality runs, clean baselines, correct watermark, and bit 31 set
after the boundary in both.

**Programming FW_BLC to i915's value does not prevent the display FIFO
underrun.** The watermark shortfall was real and is fixed; it was not the
cause. The mechanism that predicted the symptom's shape and burstiness
better than anything else in this investigation is now measured and does not
survive the measurement.

3DMark99 is the control that makes it sharper: same watermark, same panel,
15 times the draws, and no fresh underrun at all. Its baseline was already
dirty at `CountDriverInit=1` - something underran before this driver
initialised, on the BIOS's watermark of 6 - so the part does underrun, and
what decides it is not the watermark.

What separates the two applications is not draw count:

| | Robots | 3DMark99 |
|---|---|---|
| draws submitted | 529,072 | 7,828,574 |
| texture creates | 56,326 | **405** |
| render-state calls | 374,780 | 3,399 |
| Lock calls | 56,326 | 6,399 |

Final Reality creates a texture every nine draws and Locks once per create -
the two counts are equal to the unit in the Robots run. 3DMark99 creates 405
textures for 7.8 million draws. Texture upload traffic, not rasterisation,
is what distinguishes the run that underruns from the run that does not.
That is a hypothesis this capture suggests and does not test.

## Something else writes FW_BLC, and the driver puts it back

`WmWrites=3` in both Final Reality captures against four logged entries of
which only the first found a value needing changing. The other two writes
happened after the log filled, so the register had reverted to something
other than `0x0314011A` and was restored.

Both of the 2026-09-20 fixes are doing work here: programming no longer
stops when the diagnostic log fills, and FW_BLC's own value is in the
trigger. It is also evidence that something outside this driver writes that
register on a mode set - which is what the withdrawn "the BIOS never
reprograms the watermarks" claim denied, in the opposite direction.

## Bilinear filtering: the driver does it on every draw

```
FilterMagSeen=0x00000004    DrawsMagLinear=7631631
FilterMinSeen=0x00000004    DrawsMinLinear=7631631
DriverInfoCalls=0
```

Bit 2 is D3DFILTER value 2, LINEAR. The application asked for a linear
filter, and **every one of 3DMark99's 7,631,631 textured draws sampled
bilinear**, magnification and minification alike. The Full Run's
`0x00000006` is NEAREST and LINEAR, so Final Reality sets both.

So "bilinear filtering is not supported" is not a rendering defect. The
driver filters bilinear throughout.

Both candidates named in intel95 are now dead or weakened:

- **The application never asks** - false. It asks, and is served.
- **`GUID_D3DExtendedCaps` is declined** - `DriverInfoCalls=0`. The runtime
  never calls `GetDriverInfo` at all, so nothing is declined and no
  capability reaches the application through it.

What remains is the capability report itself. `dpcLineCaps` is zero but for
its `dwSize` and `D3DDD_LINECAPS` is unset. And the Intel path publishes
`NEAREST | LINEAR` alone where the ViRGE publishes `MIPNEAREST`,
`MIPLINEAR`, `LINEARMIPNEAREST` and `LINEARMIPLINEAR` as well - so an
application deciding "bilinear" from a mip-filter capability would answer
differently on the two chips, which is testable by running the same
benchmark on the Trio3D.

## A defect this capture found on its own

```
StateDropReason=5   StateDropCount=81    (Final Reality)
StateDropReason=5   StateDropCount=276   (3DMark99)
```

Reason 5 is "count over 64". `V9xD3dRenderState` walks at most 64 states and
**discards the entire block** when the application sends more. Blocks of 81
and 276 states were thrown away whole - every state in them, not the excess.

Two to four occurrences per run, so it is not what the pictures show. It is
still wrong: the limit is the loop's, not the interface's, and the response
to exceeding it should not be to apply nothing.

## Counters that read zero for a reason that is not the absence of the fault

`BltEngineFlipPending=0` in all three. The netbook has no
`V9X_ENGINE32_OPS` - only `eng_s3_trio.c` and `eng_s3_virge.c` define one -
so `v9x_engine32()` resolves nothing and every clear here is a CPU blit. The
counter is an instrument for the ViRGE and this capture does not exercise
it.

`BltFlipPending` is 1,010 to 4,372 and `LockFlipPending` up to 3,430, so the
clear does still race a pending flip on the paths that exist here. Unchanged
and unexplained in consequence.

## Where this leaves the flicker

The watermark is off the list. It is programmed correctly now and the pipe
underruns regardless, twice, with clean baselines.

The underrun itself is still standing and still the best candidate: it is
measured, it is on the live pipe, it correlates with the application that
flickers and not with the one that does not, and it starves the scanout part
way down a frame, which is the shape of the recorded frames.

No recording was taken with these runs, so the dip count against the
23-per-12 s and 1.09-per-second baselines is still unmeasured on any build
since the fix.
