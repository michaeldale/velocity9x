# A second Direct3D device, on the primary chain's back buffer, kills the caller

Filed: 2026-09-04
Status: OPEN. Reproduced on the emulated ViRGE/DX; not yet tried on silicon,
deliberately. The rung that found it is backed out and not committed.
Component: `src/display32/d3d/d3d_core.c` device and render-target lifetime,
as driven through `IDirect3D2::CreateDevice`

## Why anyone was doing this

3DMark 99's sprites draw in opaque black rectangles on the Trio3D/2X, and two
rungs built to reproduce that offscreen have now failed to
(`../decisions/2026-09-04-an-unlit-blend-loses-its-texture-on-the-trio3d.md`,
`../decisions/2026-09-04-the-ramp-and-the-shape-of-the-blend-fault.md`). Both
found real defects; neither is the black box.

What they have in common is the surface. Every render target this probe has
ever used is an offscreen one it created itself. A game in exclusive mode
blends onto the back buffer of a flipping chain, which differs in where it
lives, how it was allocated, and that its address changes under it on every
flip. That is the gap, and closing it is what this issue came out of.

## Two refusals, then a crash

**One.** `SetRenderTarget` will not accept a back buffer whose chain was
created without `DDSCAPS_3DDEVICE`. The probe's flip chain never asked for it,
because until now nothing rendered 3D onto it: `0x88760064` from the emulated
ViRGE/DX. **This is kept**: the chain is now created with the cap and falls back
to a plain chain if that is refused, with `PrimaryChain3dHr` recording which one
a run got. It costs nothing and it is what an application asks for.

**Two.** With the cap present, `SetRenderTarget(backbuffer)` returns
`0x80070057` - E_INVALIDARG - when the device's current render target has a Z
surface attached and the back buffer has none. That is the arrangement the
probe is in by the time it reaches this point, because the depth block attaches
one to the offscreen target and never detaches it.

**Three.** Giving the back buffer a Z surface of its own and making a device on
it the way the render-target block already does - `CreateSurface(ZBUFFER)`,
`AddAttachedSurface`, `IDirect3D2::CreateDevice(IID_IDirect3DHALDevice,
backbuffer, ...)` - gets three successes and then **kills the process**:

```
ChainZHr=0x00000000
ChainAttachHr=0x00000000
ChainDeviceHr=0x00000000
Result=INCOMPLETE          <- the probe never wrote another key
```

Exit code -1, no further keys, on the emulated ViRGE/DX at 1024x768. The next
calls after `CreateDevice` are `IDirect3D2::CreateViewport`,
`AddViewport`, `SetViewport2`, `SetCurrentViewport` and two `GetHandle`s;
which of them faults is not established.

The guest survives - DirectDraw restored the display mode on process death and
the desktop came back on its own - so this costs a run, not a machine.

## What is suspicious about it

The probe already holds a device and a viewport on an offscreen target when it
does this, so the new device is the **second** one alive. The render-target
block makes four devices in a row and does not crash, but it makes them one at
a time on surfaces it then releases, and never while another device is
current.

It may also be the same fault as
[`2026-09-02-large-render-target-kills-the-caller.md`](2026-09-02-large-render-target-kills-the-caller.md)
reached by a different route: the back buffer is 1024x768, and that issue is
about a large render target taking the caller down. The `Tgt_*` block never
tests the interaction because DirectDraw refuses its 800 and 1024 offscreen
targets at CreateSurface, before the driver is asked.

3DMark 99 does exactly this and does not crash - it scores 313 at 800x600 and
475 at 640x480 on this driver - so whatever the fault is, it is about how *this
probe* arrives at the arrangement, not about a device on a chain being
impossible.

## What was done

The rung is backed out. A probe that dies mid-run is worse than no rung: it
loses every key after the point it reaches, and this one reaches two thirds of
the way through the file. The `DDSCAPS_3DDEVICE` chain creation and
`PrimaryChain3dHr` are kept, since they are correct independently and cost
nothing.

## Next

1. Establish which call faults, with the probe's own stage markers rather than
   by inference - a key written before each of the six calls.
2. Try it with the offscreen device's viewport deleted and the device released
   first, which tells "two devices at once" apart from "a device on a chain".
3. Only then on silicon, and on the emulated ViRGE/DX first every time. The
   Trio3D is remote and cannot be power-cycled from here
   (`../decisions/2026-09-04-the-trilinear-two-pass-and-a-retraction.md` for why
   that matters on this card).
