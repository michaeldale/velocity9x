# The unblank fix does not fix the flicker

2026-09-19 night, A8U4I5 with the Trio3D/2X, build `595afe4-dirty` (the
installed HAL checked byte for byte against the package), Final Reality
Robots, hardware Direct3D, panel recorded through the capture card at
1080p60.

Recording `Y:\MWD\videos\obs\2026-09-19 21-45-58.mkv`, benchmark 21:46:48 to
21:47:50, so about t+50 s to t+112 s. Attached:
`2026-09-19-trio3d-unblank-fix-recorded-V9XSNAP.txt`,
`2026-09-19-trio3d-flicker-after-the-unblank-fix.png`.

## Measured, against the two baselines

Single-frame luminance dips below three quarters of the local median, over
a twelve-second window of the capture area:

| build | dips / 12 s | median | dip depth |
|---|---|---|---|
| unfixed (`intel87`) | 23 | 102.6 | 63.3 |
| edge write (`intel88`) | 70 | 59.0 | 22.4 |
| **unblank fix (this run)** | **22** | 87.9 | 47.6 |

22 against 23 is no change. And the frames are the same phenomenon, not a
milder one: frame 3902 at t=65.033 holds the sky band alone with everything
below it black, complete scenes either side, exactly as frame 4905 did
before the fix.

So completing the flip at the END of the retrace, and giving the ViRGE draw
path the intel78 wait, does not fix the flicker. The counters said as much
before the video did - `DrawsFlipWaited=0`, `VirgeDrawsFlipPending=0`,
`BltFlipPending=0` - and the video confirms it rather than contradicting
it.

## A claim from this afternoon is withdrawn

`2026-09-19-nothing-the-application-does-races-the-flip.md` reported that
the fix removed the engine resets: `EngineIdleTimeouts` and `EngineResets`
had gone from 1 to 0 and I wrote that completing at the end of the retrace
removed them. This run, same build, reads:

```
EngineIdleTimeouts=1    EngineResets=1
```

Back to 1. So the two zeros were run-to-run variance across two passes, not
an effect of the change, and the claim is withdrawn. One reset per Robots
pass remains a reproducible-ish deviation from what the runbook expects,
with no known cause and no known relation to the flicker.

## What is kept, and on what grounds

The change stays, but only on documentation grounds: changing the start
address and then waiting until the end of the vertical retrace before
touching the hidden page is the standard VGA page-flip discipline, and this
path followed neither half of it. It costs nothing measurable - 268 flips
here against 268 and 279 before it - and the refuse-on-timeout that came
with it is a safety property the review was right to ask for.

It is NOT kept because it helps the flicker. It does not, and nothing in
the driver now claims it does. Reverting it would also be defensible; the
reason not to is that the previous behaviour was wrong by the documented
rule even though the wrong thing was not this fault.

## Where the flicker stands

Every mechanism the driver can see is measured out on both backends:
completion, flip bookkeeping, buffer binding, premature reuse, the
pending-flip exposure on draws, and now on Blt and Lock as well. The panel
still shows a buffer holding only the clear and the sky, once every eight
or nine display frames, on two unrelated chips.

The one thing never measured on either part is the latch: when the scanout
actually begins fetching from an address that has been written. No register
on the Trio3D reports it, no register on the 945GSE reports it, and three
counters that read zero may all read zero because the driver's notion of a
flip being pending is derived from a retrace transition that has never been
shown to coincide with it.

That is the next thing to measure, and it needs an instrument that does not
exist yet on either part.
