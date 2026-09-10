# The render-target switch works, and two "driver defects" were one uncleared depth buffer

Date: 2026-09-10. Measured on the emulated ViRGE/DX (`Win86SE`, 86Box, agent
port 9869, boot 576), which is the control machine. Not run on silicon: every
physical machine in the fleet is off the network today.

Two open items are refuted by this run, both by the same cause:

- [`SetRenderTarget onto the primary chain is accepted and ignored`](../issues/2026-09-05-setrendertarget-is-accepted-and-ignored.md).
  The driver's `V9xD3dSetRenderTarget` is **never called**, the runtime
  rebuilds the context instead, and the engine ends up pointed exactly at the
  new target. Nothing was ignored.
- [`A blend onto the primary chain draws nothing`](2026-09-05-a-blend-onto-the-primary-chain-draws-nothing.md).
  It draws correctly with depth testing off, and produced a clean alpha ramp
  on the back buffer in this run.

What produced both symptoms is the probe: it attaches a Z surface, never
clears it, and gives every vertex `sz = 0`. Attaching a Z turns testing on at
`D3DCMP_LESS` with writing on - the DDK's behaviour (`D3DCB2.C:57-66`), which
`v9x_d3d_set_target` copies deliberately - so the first draw writes zero into
the depth buffer and every later fragment at `sz = 0` loses `LESS` against it.
On the chain's back buffer the *first* draw already lost, because the depth
surface DirectDraw had just allocated there read as zeros.

## How the switch actually reaches the driver

`V9X_DDGETCOUNTS`, the probe's compact counter escape, grew the trace ring's
per-id enter count for `V9xD3dSetRenderTarget` plus the context counters, and
the probe now brackets its chain rung with a read. Measured across
`IDirect3DDevice2::SetRenderTarget(backbuffer)`:

```
ChainSetTargetHr        0x00000000    the runtime said yes
ChainSetTargetCalls     0             ... and never called the driver
ChainCtxDestroys        1             it destroyed the context
ChainCtxCreates         1             ... and created another one
ChainDepthOffered       1             the new one carried the chain's Z
ChainDepthAccepted      1             ... which the driver validated

ChainTargetOffsetPre    1228800  pitch 128     the 64x64 offscreen target
ChainTargetOffsetPost    614400  pitch 1280    the chain's back buffer
```

614400 is one 640x480x16 page, and it is the offset the `Solo_*` rung's draws
land on - that rung's opaque wall writes green there and reads back green in
the same run. So the target the engine programmed after the switch is the back
buffer, confirmed by pixels rather than by arithmetic.

The declaration in `v9x_d3d_callbacks2` is correct and complete -
`V9X_D3DHAL2_CB32_SETRENDERTARGET` is published, `GetDriverInfo` serves
`GUID_D3DCallbacks2`, and the entry point is filled in - and this runtime still
does not use it. It reaches the same end by tearing the context down and
building a new one on the new surface, which is a path the driver already
served correctly.

## The two-by-two that named the cause

The chain rung's opaque wall - same texture, same `COPY` blend, same back
buffer, same `DEST_BASE` as the `Solo_*` rung's wall - wrote nothing. Two
things differed, and both belong to the reused device rather than to the
target: the depth state of the rebuilt context, and a viewport still sized
64x64 for the old offscreen target. So the wall was drawn four more times, one
variable at a time, each after a fill of `0x18E3` whose read-back proves the
probe is looking at the memory it filled:

| Depth testing | Viewport | Pixel at (16,12) |
|---|---|---|
| on | 64x64, as inherited | `0x18E3` - the fill: nothing drew |
| **off** | 64x64, as inherited | `0x7FFF` - it drew |
| on | re-set to 640x480 | `0x18E3` - nothing drew |
| **off** | re-set to 640x480 | `0x7FFF` - it drew |

Depth decides it; the viewport is irrelevant. `ChainFillRaw` reads `0x18E3`
before each draw, so "nothing drew" is a statement about the pixel and not
about the read.

## And the same control on the blend

The `Solo_*` rung was extended the same way: with depth off, the wall and then
the alpha-ramp sprite, on the chain's back buffer.

```
SoloWallRaw        992                        depth on:  the wall
Solo_x12..x48      992 x 7                    depth on:  the blend left no mark
SoloWallNoZRaw     992                        depth off: the wall again
SoloNoZ_x12..x48   930 806 682 620 464 341 217  depth off: the ramp
```

That last row is the gradient - monotonic, seven samples - that the
[2026-09-05 record](2026-09-05-a-blend-onto-the-primary-chain-draws-nothing.md)
concluded a blend onto the chain could not produce. The mechanism there is a
step worse than on the wall: the wall draw *writes* `sz = 0` into the depth
buffer, so the sprite that follows loses `LESS` against a value the wall put
there, on a surface that had passed the test moments earlier. That is why the
wall landed and the sprite did not, and why the fault looked specific to
blending.

## What this leaves standing, and what it does not

Standing:

- **Two devices at once still fails.** Untouched here; the `Solo_*` rung
  exists to avoid it.
- **The black boxes in 3DMark 99 are not explained by this.** A depth test
  that discards everything leaves the destination as it was; 3DMark's sprites
  sit in rectangles that are black, which is something drawn.
- **Nothing physical.** The Trio3D has not run any of it, which is where the
  open picture defects are.

Not standing:

- `SetRenderTarget` as a driver defect. The issue is closed as refuted, with
  the measurement.
- A blend onto the primary chain as a driver defect. The 2026-09-05 record
  keeps its two killed hypotheses - the destination base and the stride
  register's low half, both still correctly programmed - but its conclusion is
  withdrawn.

## The instrument, and what it should learn

The probe's rungs now measure both states: as inherited, and with depth off.
Neither is what an application does - a Direct3D application clears its depth
buffer per frame through the viewport, and this driver serves that with
`DDBLT_DEPTHFILL` and advertises `DDCAPS_BLTDEPTHFILL`
([record](2026-08-30-ddblt-depthfill.md)), so the clear is a hardware fill
rather than a CPU pass over the aperture. The probe should clear it too, and
should vary `sz` rather than sending zero for every vertex; both are worth
doing before the next chain measurement, and neither changes what this run
establishes.

A general lesson for the rest of this suite: a rung that attaches a Z surface
and does not clear it is measuring the depth test, whatever else it thinks it
is measuring. Three separate records had a symptom of exactly that shape.

## Gates

`run-checks.ps1` green: check-tree, the VGA survey safety gate, the Watcom
host tests and all four family packages. `build-host-msvc.ps1` cannot run on
this host - no Visual Studio, so no `vswhere.exe`.

The driver change is one 16-bit escape filling six more fields
(`V9X_PROBE_COUNTS`, append-only) and no behaviour. Two DWORDs were first
appended to `V9X_D3D_DIAGNOSTICS` instead and had to be withdrawn: the shared
block's compile-time assert caught them taking `V9X_DD_SHARED` past the 4096
bytes the 16-bit side DPMI-allocates, so the call count is read from the trace
ring's per-id counter, which the block already carried. The guest ran the
rebuilt `V9XDISP.DRV` (45,548 bytes) and `V9XHAL.DLL` (44,032) applied by the
`WININIT.INI` rename route.
