# The before-and-after pair cannot test the watermark

2026-09-20. `C:\temp\intel95\FinalReality\` holds two snapshots from two
different builds, which was missed on the first reading of the capture:

| file | build | watermark |
|---|---|---|
| `V9XSNAP.INI` | `5d9fbf6-dirty` | never programmed (and `WmWant` zero, the DSPARB error) |
| `V9XSNA1.INI` | `36d0d7f-dirty` | programmed |

`V9XSNAP.INI` is byte-identical to `intel94\V9XSNAP.INI`, so it is a file
left on the disk from the earlier boot rather than a second run of this one.
It is still a genuine before-and-after pair for the watermark change, and it
is the only one taken.

## The underrun runs the wrong way

```
                      before (no watermark)   after (watermark)
PipestatBFirst        0x80000202              0x00000202
PipestatBOr           0x00000202              0x80000202
FwBlc                 0x03060106              0x0314011A
```

The **before** run took a dirty baseline, cleared it, and recorded **no
fresh underrun**. The **after** run took a clean baseline and **underran**.

That is the opposite of the direction the watermark hypothesis predicts, and
it is the first reading taken with the change in force. Earlier this was
reported as "the underrun persisted", which understated it: it did not
persist, it appeared.

## And it cannot be believed, for three reasons

**The workloads are not comparable.** They are not the same run and barely
the same shape of work:

| | before | after |
|---|---|---|
| draws submitted | 252,084 | 576,026 |
| of those, textured | 8,868 (3.5%) | 576,026 (**100%**) |
| Flip calls | 18,283 | 48,268 |
| Lock calls | 2,560 | 64,264 |
| texture creates | 468 | 64,264 |

The before run is the untextured one this capture set was collected to
investigate. Sampling a texture for every draw instead of one in thirty is a
large increase in memory traffic, and memory traffic is what starves a
scanout FIFO. A build that finally textures and then underruns is exactly
what one would expect **whatever** the watermark is set to.

**The watermark under test was wrong anyway.** The after run ended with
plane B at 12, not the 20 it installed first, because the depth came from
`v9x_hal->fb.bits_per_pixel` reading 32 against hardware in 16bpp. The
record of 2026-09-20 has the detail.

**The flip path behaved differently too, and unexplained.**
`FlipStillDrawing` went 1,384,958 to **0** and `FlipWindowClosed` 8,903 to
47,682, with 98.8% of the after run's Flip calls refused for a closed
window. Nothing in the commits between these two builds touches the flip
path. That difference is not accounted for and is not explained by the
workload alone.

## So what would test it

A pair on one build, differing only in whether `FW_BLC` is written, running
the same application at the same settings, with the corrected depth so the
watermark holds at 20 for the whole run. `WmWrites`, `WmWritten` and
`WmDeclined` say whether it did.

Until that exists there is no measurement of the watermark change at all -
neither the "still underran" reported first nor the "underran only after"
this pair appears to show.

## A label that cannot be resolved from the captures

`intel94\V9XSNAP.INI` was handed over as 3DMark99 and is filed in intel95
under Final Reality. Nothing in a snapshot names the application, so the
capture cannot settle it, and the intel94 record has had the claim removed
rather than replaced.

It matters more than a filing detail: `V9XSNA1.INI`, filed under Final
Reality, has 64,264 texture creates and 391,810 render-state calls, while
the capture filed under 3DMark99 has 64,761 and 396,115. Those two look like
the same application; the one filed beside them as Final Reality on the
older build has 468 and 4,071. Which application produced which snapshot is
an open question for the operator and not one to settle by inference.
