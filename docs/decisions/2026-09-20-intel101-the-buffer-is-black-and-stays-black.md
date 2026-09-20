# intel101: the buffer is black and stays black

2026-09-20, MICHAEL-NETBOOK (945GSE), build `d7dea21-dirty`,
`D3dPidDistinct=1`, `CountDriverInit=1`. Attached:
`2026-09-20-intel101-V9XSNAP.txt`, `2026-09-20-intel101-V9XFRAME.ppm`.

## The recheck answers the timing question

Four records, each read at the flip that presented it and again at the very
next flip, with the layout and ownership guards passing:

```
Cover0  offset 00120000  drawn 0   recheck then 0   now 0
Cover1  offset 00240000  drawn 17  recheck then 17  now 17
Cover2  offset 00120000  drawn 0   recheck then 0   now 0
Cover3  offset 00240000  drawn 0   recheck then 0   now 0
```

**`now == then` in all four.** A full flip interval passed, during which the
buffer was the one being scanned out and the application was drawing into
the other, and nothing changed in it.

The pre-registered reading was that `now` above `then` means the engine wrote
after the driver sampled - an early read - and that equal counts are
consistent with the buffer holding what was read without proving it. Equal
is what came back, so **late rendering into these buffers is disfavoured**:
had the engine been about to write, a flip's worth of time was available and
it did not.

The image says the same thing more plainly: 9,216 pixels, one distinct
colour, pure black.

That was the open question after intel100 and it is now narrowed. Against
178,601 draws submitted and 20 refused, nothing is reaching the buffers being
flipped.

## And a number that would explain it, which cannot be trusted yet

```
D3dTargetOffset=0x00000000   DrawsTargetLast=0x00000000
DrawsDisplayedLast=0x00240000
DrawsToBack=178621           DrawsToFront=0
```

The render target reads `0x00000000` while the flip chain alternates
`0x00120000` and `0x00240000`. Taken at face value that is the whole answer:
every draw lands somewhere nothing is ever presented from, and buffers that
are never drawn into stay black.

**It cannot be taken at face value.** Both are LAST values, sampled after
the benchmark exited and the desktop context took over, and intel99 read
`0x00120000` from the same field. A last value cannot tell a run's behaviour
from its teardown, which is a trap this investigation has now fallen into
more than once.

`DrawsToBack=178621` does not settle it either. It counts draws whose target
is not the displayed buffer, which is exactly what correct double buffering
looks like as well.

## The instrument that does settle it

Added in the commit carrying this record: the distinct render-target offsets
are collected as they are drawn to, with a count of draws to each.

- Two offsets matching the flip chain, sharing the draws: the application is
  double-buffering correctly and the fault is downstream.
- One offset that is never flipped to, holding all 178,000 draws: that is
  the answer, and everything above follows from it.

No rendering change should follow from intel101. The next capture tells us
which of those two it is, and they need different fixes.
