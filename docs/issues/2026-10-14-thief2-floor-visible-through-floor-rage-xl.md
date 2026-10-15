# Thief II on the Rage XL: a floor shows through the floor

Date: 2026-10-14

Status: Open. Reported by Michael from the monitor at A8U4I5 boot 477
(Thief II demo, "The Unwelcome Guest"), after table fog, colour keys and
gamma were in. Not reproduced by the agent: its screenshots were too dark
before gamma worked. After this report the machine stopped answering
ping and was powered off; whether the game had hung it is not known.

## What is known

Counters at boot 475, in the mission (`V9XSNAP`, see
`docs/probe/a8u4i5-rage-xl-gamma-colour-key-2026-10-14/`):

- **Refusals:** none; all 37 refused draws came from V9XDDP's checks.
- **Unrenderable triangles:** none.
- **Depth:** nearly every draw uses it. 155M of 157M `M64Draws` are
  `M64DepthDraws`.
- **Zero-area triangles:** 4% are skipped as zero-area (14.2 fixed-point
  cross product of 0). That is legitimate, but not yet checked against
  this scene.
- **Core work:** 3% are culled as back faces, and the core clipped
  nothing (`R3dListClipped` 0).

The symptom fits depth handling more than missing triangles. Candidates:
- how Thief II clears depth;
- its Z function;
- a W-buffer request (`D3DZB_USEW`), which this driver treats as Z;
- 16-bit depth precision.

## 2026-10-15

- **No lock-up in two more sessions** (boots 479 and 480, about five
  minutes in the mission each). The agent log of boot 477 shows nothing
  after its last command; the lock left no trace.
- **The DrawPrimitives2 capture cannot see this game.** Thief II draws
  through the DirectX 5 DrawPrimitive entry points (`Dp2Calls` 0); DDI 6
  is given only to Direct3D 8 programs by default.
- **The same build's regression pass on A8U4I5** showed nothing new:
  - V9XDDP 207/224, unchanged;
  - Diablo II's town draws, 0.7% of draws refused, as before;
  - the NFS II SE demo's race draws, 5.5% refused against 6.2% before;
  - gamma held in the DAC.

## Next

1. A screenshot of the fault, taken by the agent while Michael stands at
   it. The agent's screenshot reads the framebuffer, and a gamma-brightened
   copy shows the dark scene.
2. A census of the DX5 path's Z state per batch (ZENABLE, ZFUNC,
   ZWRITEENABLE and the clears). No such counter exists yet.
