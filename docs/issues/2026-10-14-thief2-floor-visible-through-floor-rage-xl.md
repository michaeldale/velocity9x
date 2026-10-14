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

## Next

1. Arm the DrawPrimitives2 capture (`[Velocity9x] Dp2Capture=1`, with
   `Dp2RingDelayMs` past the menus) for one mission. Read it with
   `tools\diag\dp2ring.py` for ZENABLE, ZFUNC, ZWRITEENABLE and the
   clears.
2. A screenshot of the fault with the in-game brightness raised.
