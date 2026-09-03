# The probe matrix, and the 3D-done bit it made necessary

Date: 2026-09-03
Status: matrix at 90 of 90 on the emulated ViRGE/DX on two consecutive runs
with no differing keys; the Trio3D has not run it

## Why the probe changed shape

Three Trio3D defects were found today, and every one lived between the points
the probe tested: a texture stride that only mattered above 64 texels, an alpha
bit that only mattered in ARGB4444 under a blend, and a mip level that only
mattered on a chain with a gap. Each was found by writing the rung that would
have caught it, after the picture. The probe now tests the space rather than a
point in it - `docs/probe/README.md` describes the matrix, the state reset
between rungs, and the reference-and-expectations workflow with
`scripts/compare-probe.ps1`.

## What the matrix found on its first run: not a driver fault

The first run read 65 of 90 cells as passing, the second 75, with only five
failing cells in common. Every failing cell had one half reading 0 - the
target's fill - with a clean engine (`EngineFifoTimeouts=0`,
`EngineIdleTimeouts=0`, `EngineResets=0`) and every HRESULT success. A
triangle that was issued, not refused, and not on the target when the target
was read.

86Box explains it. Its ViRGE hands a launched triangle to a render thread
(`build\reference-vid_s3_virge.c`, `queue_triangle` / `render_thread`): the
launch appends to a ring buffer and signals the thread; the thread sets
`s3d_busy` when it wakes. The status register's idle bit is computed from
`s3d_busy`, `virge_busy` and the MMIO FIFO - not from the ring buffer. Between
the launch and the thread waking, the register reads idle with a triangle
pending. The HAL's `v9x_wait_idle`, which DirectDraw's Lock relies on, returned
at once, and the probe read the target before the triangle landed. Real
silicon has no such gap - busy is a consequence of the launch - which is why
this never appeared on the Trio3D and why it appeared on the emulator only once
the probe drew hundreds of triangles back to back.

## The fix, and the version of it that wedged the emulator

The chip has a bit that does not have the gap: SUBSYS_STAT bit 1, 3D-done, set
by the engine when its queued triangles are finished and cleared by writing a
1 to the same bit of SUBSYS_CNTL - the same DWORD, written. 86Box sets it from
the render thread when the ring empties and clears it on that write.
`d3d_virge.c` now clears the bit immediately before every launch and tells the
engine layer a triangle is pending; `v9x_wait_idle` then requires the bit
before it reports idle.

The first version folded the missing bit into the existing idle spin, whose
timeout resets the engine through CR66. On the emulator that produced a reset
storm on the first probe run and a wedged guest with a torn desktop, read off
the host window since the agent had stopped answering. The version kept
separates the two conditions: the idle bit missing is the engine working and
is handled as before; the idle bit present with the done bit missing is waited
out in a bounded spin of its own and, if the bit never comes, accepted as idle
and counted - never reset. An engine that says it is idle is not one to reset,
and if some chip never sets the bit the wait must degrade to what it was.

## Measured

Emulated ViRGE/DX, the driver pair with the bounded wait, probe runs 5 and 6:

```
TexMatrixOk=90 TexMatrixCount=90        both runs; no key differs between them
D3dDoneSeen=232   D3dDoneMissing=0      the bit arrived for every wait that needed it
EngineFifoTimeouts=0  EngineIdleTimeouts=0  EngineResets=0
D3DShapesOk=12  D3DVertexAlphaBlendOk=1  ColorKeyOk=1  Alpha4444Ok=1  Alpha1555Ok=1
MipGapOk=1  Tex128HalvesOk=1  Tex256HalvesOk=1
```

Runs 1 and 2, before the wait: 65 and 75 of 90.

Gates: check-tree, vga survey safety gate, host tests and family packages all
green (run-checks).

## What this does not settle

- The Trio3D has not run this pair. `D3dDoneMissing` is the first counter to
  read when it does: non-zero means the card does not set or clear the bit
  the way the emulator does, and the wait has degraded to the old behaviour,
  which is safe but should be known.
- `D3DTrilinearBlendOk=0` on the emulator predates today and is unchanged;
  the matrix's trilinear cells pass under the looser rule that accepts a blend
  of the two levels' colours.
