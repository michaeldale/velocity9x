# DrawPrimitive into a 640x480 render target terminates the calling process

Filed: 2026-09-02
Status: open, reproduced on two chips, cause not established
Reproduce: `V9XDDP.EXE /bigtarget`

## What happens

Create a 640x480 `DDSCAPS_3DDEVICE | OFFSCREENPLAIN | VIDEOMEMORY` surface,
create a Direct3D HAL device on it, fill it, `BeginScene`, then
`DrawPrimitive` one triangle. **The process dies inside `DrawPrimitive`.**

The same triangle into the probe's usual 64x64 target, in the same run on the
same boot, draws correctly.

## Bisected

The probe writes a `D3DBigStage` key after each step. The last value written is
**3**, which is after `BeginScene` returned and before `DrawPrimitive` returns:

```
D3DBigTargetHr=0x00000000     surface created
D3DBigPitch=1280
D3DBigDeviceHr=0x00000000     HAL device created on it
D3DBigStage=1                 about to fill
D3DBigStage=2                 filled
D3DBigStage=3                 BeginScene returned 0
                              <- nothing further; Result stays INCOMPLETE
```

`D3DBigStage=4`, which would follow `DrawPrimitive`, is never written.

## Reproduced on

| Target | Driver | Result |
|---|---|---|
| A8U4I5, physical S3 Trio3D/2X `5333:8A13` | `s3`, hardware D3D | dies at stage 3 |
| `Win86SE`, 86Box S3 ViRGE/DX | `s3`, hardware D3D | dies at stage 3 |

Byte-identical truncation across repeat runs on the physical machine. **It is
not chip-specific**, which places it in the driver or the runtime rather than in
the Trio3D alias added the same day.

## What it is not

**Not clipping.** The first version of the test drew to y = 503.75 in a
480-high target, so it was both large *and* the first large-target triangle the
clipper had to cut - two variables at once. Redrawn wholly inside the target
(`(8.25,8.25)`, `(503.75,8.25)`, `(8.25,400.75)`), it fails identically. Only
the target size, and with it `DEST_BASE` and the 1280-byte stride, differ from
the passing 64x64 case.

**Not the DEST_BASE alignment hole** recorded in
[`final-reality-101-hardware.md`](../plans/final-reality-101-hardware.md). A
640x480x16 surface and every offset in play here are 8-byte aligned by
arithmetic.

**Not an engine fault.** `EngineFifoTimeouts`, `EngineIdleTimeouts` and
`EngineResets` are all 0 afterwards, and the driver never writes
`C:\V9XDIAG\V9XTRACE.INI`, which it does on a fault. Whatever kills the process
is not the engine timing out or being reset.

## What this does not explain

**It does not explain the Final Reality black screen**, and it would be
convenient to assume it does. FR rendered 478,327 primitives into screen-sized
targets on the same machine and driver without the process dying
([record](../decisions/2026-09-02-final-reality-on-a-real-trio3d.md)). Its
render target is a flippable back buffer, not an `OFFSCREENPLAIN` surface, so
the two paths differ in how the target is allocated and where it lands. The two
symptoms may share a cause or may not; nothing here establishes either.

## Next

1. **Find where it dies.** The driver's own fault handler did not fire, so the
   fault is either outside the HAL or before the handler is armed for that call.
   `V9XTRACE.EXE` immediately after a `/bigtarget` run would say whether
   `D3dRenderPrimitive` was entered at all - the counters distinguish "the
   driver was called and died" from "the runtime died before calling".
2. **Vary the size.** 64x64 works and 640x480 does not; the boundary is
   unknown. 128x128, 256x256 and 320x240 would say whether it is a threshold, a
   stride, or an offset.
3. **Try it in software mode.** `Direct3D=2` uses the same core and clipper with
   a different engine, so a crash there too would move the cause out of
   `d3d_virge.c` entirely.

## The switch, and why

The test is behind `/bigtarget` rather than on by default. On by default it
takes every key after it with it - the depth ladders, the texture tests, the
context cycle - and leaves `Result=INCOMPLETE`, which is a worse instrument for
everyone than the missing coverage was. That regression was introduced and
reverted the same session.
