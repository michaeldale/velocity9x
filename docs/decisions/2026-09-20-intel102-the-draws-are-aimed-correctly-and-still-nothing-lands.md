# intel102: the draws are aimed correctly, and still nothing lands

2026-09-20, MICHAEL-NETBOOK (945GSE), 3DMark99 Max, build `8d4cf3c-dirty`
(the census, built before its commit was made). Attached:
`2026-09-20-intel102-V9XSNAP.txt`, `2026-09-20-intel102-V9XFRAME.ppm`.

## The census answers, and both predicted outcomes were wrong

intel101 offered two readings: two offsets matching the flip chain (the
application is fine, the fault is downstream), or one offset never presented
(that is the whole answer). There were three.

```
DrawTargetCount=3   DrawTargetOther=0
DrawTarget0 = offset 00120000  draws 59766
DrawTarget1 = offset 00240000  draws 59719
DrawTarget2 = offset 00000000  draws 59164
                               ------
                               178649 = DrawsToBack
```

The mode is 1024x576x16 at pitch 2048, so one surface is 576 * 2048 =
0x120000 bytes **exactly**. The three offsets are three consecutive
full-size surfaces: primary at 0, back buffers at 0x120000 and 0x240000.
`DrawsDisplayedLast=0x00000000` says offset 0 is presented as well.

That is a three-deep flip chain, drawn to and presented in rotation, and an
even three-way split of draws is precisely what it produces. **The
application is targeting its buffers correctly.** Where the draws go is not
the explanation, and the line intel101 opened is closed.

The frame is still 9,216 pixels of one colour, pure black, and all four
coverage records still recheck `now == then`.

## And the number that nearly misled us

intel101 read `D3dTargetOffset=0x00000000` and `DrawsTargetLast=0x00000000`
while the flip chain alternated 0x120000 and 0x240000. Taken at face value
that said every draw landed where nothing is presented from. This run reads
`0x00120000` for both.

So the intel101 values were teardown, exactly as suspected - and acting on
them would have produced a confident wrong answer, because offset 0 is not
a buffer nothing is presented from. It is the primary, and it is presented.
A last value sampled after the benchmark exits remains unusable.

## What this does not establish

`BreadcrumbSubmits=178609` with `BreadcrumbTimeouts=0`, `BreadcrumbLate=0`
and `BreadcrumbOutstanding=0` shows the ring advanced past every batch. It
does **not** show that the drawing commands produced fragments: a breadcrumb
retires whether or not the primitive wrote anything. "The engine executed
the draws" is not in evidence and is not claimed here.

`DrawsNoHandle=30140` - 30,140 draws had no resolvable target handle. It
sits one away from `ScanSampleFrame=30139`, which is noted because it is
striking, not because any mechanism is known to connect them.

The probe logs in this drop (`INTEL3D0.TXT`, `INTELRNG.TXT`) were
unarmed: `Access=no-hardware-writes`, `Result=NO-WRITE` and
`ERRATA-GATED`. They describe nothing about this run.

## The one place something does land

`Cover1` is not empty:

```
Cover1Drawn=18   Cover1Box=776,432 864,456   Cover1Reference=0x00008410
```

Eighteen non-black samples in a box roughly 88x24 pixels at the lower right,
and intel101 had 17 in the same place. So the pipeline is not wholly dead -
a small region is written, repeatably, while 1024x576 of buffer is not.

Supporting state is unremarkable: `DrawsExtentLast=0x04000240` is 1024x576,
the correct extent; `I9xxDepthDraws=178609` with `I9xxDepthSkipped=0` and
`I9xxDepthLastFunc=0` (COMPAREFUNC_ALWAYS, which rejects nothing);
`I9xxDrawsRefused=40` against 178,609 submitted.

## Where this goes

The question is no longer where the draws are aimed. It is why a buffer that
is correctly targeted, and submitted to an engine that retires the batch,
stays black. Two candidates worth separating, and they need different fixes:

- the engine writes to a different physical address than the probe samples -
  a GTT-translated destination against the CPU's aperture-offset view;
- the primitives produce no fragments, and the retired breadcrumb says
  nothing to the contrary.

The 18-pixel box is the better lever than another whole-buffer sample.
Something lands there every run, so what distinguishes the draws that reach
it from the 178,000 that do not is a sharper question than whether the
buffer is black - which has now been asked three times and answered the
same way.
