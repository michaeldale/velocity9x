# SetRenderTarget onto the primary chain is accepted and ignored

Filed: 2026-09-05
Status: OPEN, measured on the emulated ViRGE/DX. Not chip-specific: this is the
control machine, on which everything else the probe measures is correct.
Component: `src/display32/d3d/d3d_core.c`, `v9x_d3d_set_target` and whatever
carries the current render target to a draw

## What was measured

The probe points its existing Direct3D device at the back buffer of the
exclusive-mode flipping chain, draws an opaque textured triangle with `COPY`
and no blend, then reads three surfaces:

```
ChainSetTargetHr   0x00000000     SetRenderTarget said yes
ChainHr            0x00000000     every draw said yes
ChainStage         19             every call in the rung completed

ChainWallRaw       0              the back buffer: black
ChainOffscreenRaw  868  (0,27,4)  the *previous* target: the drawn pixel
ChainFrontRaw      495  (0,15,15) the front buffer: neither
```

`ChainOffscreenRaw` is the one that settles it. `(0, 27, 4)` is a green-to-blue
blend at the sample point - it is what the rung drew, and it is on the offscreen
surface the device was pointed at *before* `SetRenderTarget`. The call returned
success and the driver kept drawing where it was.

Nothing is refused, no counter moves, and the application has no way to know.

## Why it took three attempts to see

Worth recording, because the first two readings were both wrong in ways the
third explains.

1. `SetRenderTarget` first returned `0x88760064`: the probe's flip chain had
   never been created with `DDSCAPS_3DDEVICE`, because nothing had rendered 3D
   onto it. Fixed - the chain asks for the cap and falls back if refused
   (`PrimaryChain3dHr`).
2. Then `0x80070057`: the device's current target carried a Z surface and the
   back buffer had none. Fixed by giving the chain its own Z, which is what a
   game has anyway.
3. Then a **crash** with no useful last key, because Windows caches INI writes
   and a fault loses the cached tail. Fixed by `v9x_write_stage`, which writes
   one key and flushes it, so the value in the file is the last stage actually
   entered.

The markers paid for themselves immediately. They also killed the standing
guess in
[`2026-09-04-a-second-d3d-device-on-the-primary-chain-kills-the-caller.md`](2026-09-04-a-second-d3d-device-on-the-primary-chain-kills-the-caller.md)
that the crash was about a large render target: `ChainBackW=640`,
`ChainBackH=480`. The back buffer is small. Whatever that crash was, size was
not it, and with the second device gone it no longer reproduces.

## What it does not yet explain

3DMark 99 renders to a chain on this driver and its picture appears - it scores
313 at 800x600 and 475 at 640x480. So either it creates its device directly on
the back buffer rather than switching an existing device onto it, or it never
switches targets at all. A device created on the back buffer *was* tried here
and drew nothing either, with every call succeeding - but that attempt also had
a second device alive, which this driver does not appear to support, so it
measured two things at once and settles neither.

So this issue is about `SetRenderTarget` specifically, and the black boxes are
still unaccounted for.

## Next

1. The one-device version of the CreateDevice route: release the offscreen
   device and viewport first, then make the device on the back buffer. That
   separates "a device on a chain" from "two devices at once" and is the last
   arrangement untried.
2. Read the driver's own counters across the switch - `D3dDepthOffered`,
   `D3dDepthAccepted` and `D3dDepthReject` are already published and would say
   whether `v9x_d3d_set_target` was even reached with the new surface.
3. Not on silicon until the emulator says something coherent. The Trio3D is
   remote and cannot be power-cycled from here.
