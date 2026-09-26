# Phase 5: Serious Sam on Gen3 - scissor, small textures and video memory

Date: 2026-09-26. Machine: MICHAEL-NETBOOK (GMA 950, Win98 SE,
10.0.1.248). Serious Sam: The First Encounter v1.05, OpenGL, 640x480
fullscreen, its own menu demo (first level) left running.

## What was wrong

Michael reported it slow. The ICD's exit counters from his session
(before any change): 683,069 triangles on Gen3, 147,532 refused by Gen3
and redrawn by the CPU fallback, 2,937 calls to two unimplemented entry
points (glGetTexLevelParameteriv, glClearStencil). The ICD also opened
its log for every failed batch.

New logging (this change) made it measurable while the game runs: path
and texture counters every ten seconds, each distinct refused state once
with its texture size, write mask and scissor, and the first failed
batches with their vertices as raw bits. It showed three causes, found
and fixed in this order:

1. **Video memory.** After a minute every new texture copy failed
   (DDERR_OUTOFVIDEOMEMORY; about 5 MB is free on this part) and stayed
   failed, so the textures were drawn by the CPU. The ICD now evicts the
   least recently used copies when one cannot be made, keeps the record
   for the next use, and retries a failed copy after 256 uses instead of
   never.
2. **The scissor.** Serious Sam letterboxes its view with glScissor, and
   Gen3's accepts refuses any draw with a scissor, since its builder
   emits none: every in-game batch went to the CPU. The ICD now clips
   geometry to the scissor box and the drawable as four more clip planes
   in clip space - exact, and perspective-correct, since attributes
   interpolate across them as across the frustum's - and hands the
   interface a full-surface scissor. Host test first.
3. **Small textures.** Serious Sam draws layers with 1x1 textures; Gen3
   took nothing below 8 texels, and the CPU fallback then failed those
   batches (result 8, dropped; cause in the software engine not found).
   Gen3's placement, chain walk, bind and accepts now take maps down to
   one texel (V9X_D3D_I9XX_SAMPLER_SIZE_MIN); the published Direct3D
   minimum stays 8. The floor's recorded reason, a pitch not a multiple
   of four, does not apply to maps laid out by the mip-tree layout,
   whose pitch is padded to 64 bytes.

## Measured

V9XGLP, new scenes on Gen3 and the software engine
(`2026-09-26-phase5-serious-sam-{gen3,soft}-V9XGLP.ini`): 1x1, 2x2,
4x4, 4x1 and 1x4 textures read their colours; a scissor of x 40..80,
y 30..70 over a full-window quad is red at its corners and black one
pixel outside each edge. Every earlier scene reads back as before, and
on Gen3 every probe batch is Gen3's.

Serious Sam's counters, one full session each
(`2026-09-26-phase5-serious-sam-counters.txt`):

| | before | after |
|---|---|---|
| Triangles on Gen3 | 683,069 | 1,782,353 |
| Refused by Gen3, redrawn by the CPU | 147,532 | 0 |
| Dropped (fallback failed) | not counted | 0 |
| Drawn by the CPU for want of a copy | 0 | 241,164 |
| Texture copies made / failed / evicted | - | 51,831 / 25,926 / 25,754 |
| Re-uploaded | - | 365 MB |

No frame rate: the game has no timedemo reachable from the agent, and
the session lengths differ. The frame renders correctly
(`...-frame.png`).

## Not established

- Why the software fallback failed Serious Sam's 1x1 double-modulate
  batches. Gen3 draws them now; the fallback's failure is still there.
- glGetTexLevelParameteriv and glClearStencil are still unimplemented.
- The working set is larger than the part's video memory, so the copies
  thrash. More memory for textures - system pages mapped through the
  GTT, as the aperture allows - is the next lever, and a HAL and
  mini-VDD change.
- The reboot that installed this HAL took about twelve minutes where
  earlier ones took two or three; whether the netbook restarted by
  itself is not known.
