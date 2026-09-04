# A device on the primary chain works; a blend onto it draws nothing

Date: 2026-09-05
Status: measured on the emulated ViRGE/DX, which is the control machine. **Not
yet run on the Trio3D** - it went off the network before the run, as it did the
day before, and cannot be woken from here.

## The variable that was in the way

Three attempts to draw on the exclusive-mode chain's back buffer had failed in
three different ways, and every one of them had a second Direct3D device alive
at the time:

- `SetRenderTarget` onto it succeeded and was **ignored**, the draw landing on
  the previous target
  (`docs/issues/2026-09-05-setrendertarget-is-accepted-and-ignored.md`);
- a second device made on it accepted every call and drew nowhere;
- an earlier form of that crashed outright.

So none of them said whether a device on a chain works at all. The `Solo_*`
rung removes the variable: it runs where the offscreen device has just been
released, with `IDirect3D2` and the chain still alive, so the device it makes is
the only one - which is the arrangement a game is in. It carries its own Z
surface, textures, viewport and vertices, and every call is preceded by a
flushed stage marker.

## Measured

Emulated ViRGE/DX, `SoloStage=22` - the rung ran to its end - and every HRESULT
zero:

```
SoloBackW / SoloBackH   640 / 480
SoloZHr, SoloAttachHr, SoloDeviceHr, SoloViewportHr, SoloSetupHr, SoloHr   all 0

SoloWallRaw             992   (0, 31, 0)    the opaque wall: it landed
Solo_x12 .. Solo_x48    992 x 7             the blended sprite: no mark at all
```

**A device on the chain works.** The opaque textured draw with `COPY` and no
blend put green on the back buffer, read straight back. That settles the
question the three earlier attempts could not: it is *two devices at once* this
driver does not do, not a device on a chain.

**A blend onto the chain does not.** The same alpha-ramp draw that produces a
clean green-to-blue fade on an offscreen target - `RampOk=1` in the same run,
on the same machine, with the same textures - leaves the back buffer exactly as
the wall left it. Seven samples, all 992. `SoloHr` is zero; nothing was
refused, and no counter moved.

## What that is and is not

It is a driver defect on the **control** machine. The emulated ViRGE/DX is
correct in everything else this probe measures - 117 of 117 matrix cells,
`SpriteOk`, `RampOk`, `MipLadderOk`, `AlphaCurveOk` - so this is not the
Trio3D, and not the chip.

It is not, on its face, the black boxes. A blend that draws nothing leaves the
destination standing, and 3DMark 99's sprites sit in rectangles that are
*black*, which is something drawn. But it is the first thing found on the
surface applications actually use, after two rungs that reproduced nothing
offscreen, and the Trio3D has not run it.

## What is owed

1. **The same rung on A8U4I5.** That is the run this document is missing, and
   it is the one that matters: the card is where the black boxes are. The
   machine dropped off the network after about twenty-five minutes idle, the
   same as on 2026-09-04, and waking it needs someone there.
2. Why a blend and not an opaque draw. Both go through the same
   `v9x_d3d_virge_draw`; what differs is `alpha_bits` in the command word and
   the destination read that a blend implies. The engine's destination base for
   a blend is the first place to look, since the chain's back buffer is at an
   offset the offscreen targets never are.
3. `SetRenderTarget` being accepted and ignored is a separate defect and stays
   filed separately. This rung sidesteps it rather than fixing it.

## Gates

check-tree, vga survey safety gate, host tests and family packages
(run-checks). The change is to the probe only; no driver code moved.
