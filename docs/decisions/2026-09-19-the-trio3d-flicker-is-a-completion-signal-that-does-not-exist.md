# The Trio3D flicker is a completion signal that does not exist

2026-09-19, **A8U4I5** with the S3 Trio3D/2X `5333:8A13`, build
`f1b869a-dirty`, Final Reality Robots at 640x480 fullscreen on a
1024x768x16 desktop. The panel was captured through a capture card at
1080p60 by OBS.

Evidence: `2026-09-19-trio3d-flicker-frames-4902-4909.png` (the frames),
`2026-09-19-trio3d-robots-recorded-V9XSNAP.txt` (the counters). The
recording is `2026-09-19 15-48-23.mkv`, 7,743 frames at 60 fps; the operator
pointed at 1:19-1:23.

## What the panel shows

Frame 4905, at t=81.750 s, is a single display frame in which the panel
shows **only the sky band across the top of the picture** - roughly the top
eighth - with everything below it empty. The frames either side are complete
scenes. Mean luminance over the captured area drops from 106 to 44 for that
one frame and returns to 109 on the next.

It is not a one-off. Over the six seconds from 78 s to 84 s the same
single-frame drop recurs every eight or nine display frames, which at 60 Hz
is roughly seven a second:

```
frame 4905  t=81.750  mean= 44.5
frame 4906  t=81.767  mean=108.8
frame 4913  t=81.883  mean= 44.7
frame 4914  t=81.900  mean=109.8
```

This is the "buffer under construction" the issue doc is named for, seen
directly for the first time rather than inferred: the panel is fetching a
buffer into which the scene has been only partly drawn.

## Why the driver let it happen

The Flip path gates on the engine being settled
(`ddhal_core.c`, `v9x_wait_idle`). For the ViRGE family, "settled" is
`v9x_virge_settled` in `engines\eng_s3_virge.c`: the engine's IDLE bit, and -
if a triangle has been launched - the 3D-done bit as well, because the file's
own comment says the idle bit alone is not the answer once the 3D engine is
involved.

On this part the 3D-done bit never arrives. The counters from the run:

```
D3dDoneSeen=0          D3dDoneMissing=64      D3dDoneSkipped=273597
```

`D3dDoneSeen=0` - the bit was not observed once in the entire run.
`D3dDoneMissing=64` is exactly `V9X_DONE_WAIT_GIVE_UP`, so after 64
consecutive misses (4,096 status reads each) `donewait.c` retired the
question for this part, permanently, as it is designed to. Every wait after
that - **273,597 of them** - took the IDLE bit at its word and returned
"settled" with no completion evidence at all.

So Flip's test that "the frame about to be shown has to be DRAWN" is, on the
Trio3D, satisfied by a bit the driver already knows does not mean the
triangles have landed. The flip presents the back buffer whenever the 2D
engine happens to be idle, which is most of the time, and about seven times a
second that moment falls between the sky and the rest of the scene.

That the give-up is deliberate does not make it harmless: it was written so a
part without the bit would degrade to the behaviour that predated the bit,
and what predated the bit is this flicker.

## The emulator has the bit, which is why nothing caught this

The same build on the 86Box ViRGE/DX, same scene, same day:

```
D3dDoneSeen=362995     D3dDoneMissing=479     D3dDoneSkipped=0
```

The emulated ViRGE answers every wait. `donewait.h` says as much in the
comment on the threshold - "the emulated ViRGE/DX answered every one of
them". So the guest can never reproduce this, and every ViRGE regression run
has been on a part where the completion signal works. The fault is only
reachable on silicon that lacks the bit, and the only such part in the fleet
is the Trio3D.

## What this does and does not settle

**It explains the S3 flicker, with a measured chain**: no completion signal,
a documented give-up, 273,597 waits answered by a bit known not to mean
completion, and a photograph of the consequence.

**It does not explain the Intel one.** intel86 measured a working completion
channel on the netbook - self test passed at zero polls, 575,868 breadcrumbs,
no timeout - and `RenderDrainWaits=0`, meaning the drain always found
rendering already finished. The same explanation cannot be carried across.

So the reading that a single shared cause produces the flicker on both cards
is now doubtful, and the review that warned of exactly this was right: two
backends can reach the same appearance by different paths. The Trio3D's path
is now known. The netbook's is not, and the evidence that closed every
mechanism there (intel86) still stands.

Worth noting against the shared reading, too: `v9x_render_drain` in
`ddhal_core.c` returns 1 for every engine that is not Intel Gen3, so the
second, breadcrumb-based gate is Intel-only by construction. The S3 path has
only the engine-idle gate described above. The two families are gated by two
different mechanisms, and only one of them currently works.

## What follows

The fix is a completion signal for the Trio3D that does not depend on the
3D-done bit, and the Intel work already has the shape of one: intel82 to
intel84 failed with three MI store forms, and intel86 succeeded with a GPU
write the CPU can read back. The ViRGE equivalent would be a marker write
through the engine whose arrival the CPU can see, waited on before the flip.
Nothing here says that is cheap on this part, and it should be measured
before it is built.

Until then the behaviour is understood and unfixed, and any Trio3D result
that depends on a frame being complete when it is presented is suspect.
