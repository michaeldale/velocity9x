# The netbook's untextured 3DMark draws are a fog pass, and Game 1 draws correctly

Date: 2026-10-04. Machines: MICHAEL-NETBOOK (945GSE, 1024x576x16) and
A8U4I5 (Rage XL PCI, 800x600x16) for comparison. Builds `0ce9d59` plus the
census below. Plan: `docs/plans/intel-3dmark99-missing-textures.md`.
Evidence: `docs/probe/netbook-vsync-and-textures-2026-10-04/`.

## Finding

On the netbook, 3DMark 99 Game 1 sends about 39 per cent of its batches
with TEXTUREHANDLE 0. The Rage XL gets 0.8 per cent of the same scene.
Those batches are not lost textures. They are an alpha-blended,
untextured second pass. 3DMark draws it because the Intel device
advertises no fog, and the Mach64 device advertises vertex fog
(`D3DPRASTERCAPS_FOGVERTEX`, `D3DPSHADECAPS_FOGFLAT | FOGGOURAUD` in
`d3d_mach64.c`). A frame captured 0.3 s into Game 1 on the netbook shows
the scene fully textured, with grey haze at the horizon.

## The census

New diagnostics (`NoHandle*` keys, ABI 2026100409) classify each
untextured batch in `v9x_d3d_dispatch_draw`:
- by blend: off, modulating (DESTCOLOR source or SRCCOLOR destination,
  the Rage XL's lost lightmaps), or other;
- by whether its first triangle carries varying texture coordinates;
- by whether it is white.

Game 1 alone, 1024x576:

| Key | Value |
|-----|------:|
| DrawsNoHandle | 19,696 of 50,670 batches |
| NoHandleBlendOff | 258 |
| NoHandleBlendModulate | 0 |
| NoHandleBlendOther | 19,438 |
| NoHandleWithUv | 257 |
| NoHandleWhite | 0 |
| NoHandleLastState | 0x02010605: SRCALPHA / INVSRCALPHA, blend on, MODULATE |
| NoHandleLastColor | 0x00808080: grey, vertex alpha 0 |

Grey with a per-vertex alpha, blended SRCALPHA over INVSRCALPHA, with
constant texture coordinates, is fog applied as a pass: the fog colour,
weighted by the fog factor in alpha. None of it is a modulating pass, so
it does not have the Rage XL's lost-lightmap signature (a modulating
pass with varying coordinates).

## The picture

`V9XTRACE -arm`, issued with 3DMark at its menu and title screens off,
captured the first sampled frame of the race (timer 0:00.3) as
`V9XFRAME.PPM`, 128x72. It shows the textured sky, the track and its
rail, other cars, the speedometer and the timer. Nothing is missing or
washed out. One frame at 128x72 is not the whole run, but it is the
scene the plan's photograph (`2026-09-20-intel98-netbook-3dmark99.jpeg`)
showed nearly white, and this build does not reproduce that.

## What the evidence killed

- The 256-entry texture table, as the netbook's cause (table never full;
  `docs/decisions/2026-10-04-texture-handle-table-full.md`).
- Lost textures from video-memory pressure or churn. Game 2 churns as
  hard and binds a texture to every batch, and the census finds no
  modulating pass.
- The plan's reading that `DrawsNoHandle` measures missing textures on
  this part. It measures the fog pass.

## Not established

- Every frame of Game 1, and Image Quality's Game 1 capture. A8U4I5's
  Image Quality Game 1 frame was washed white for a reason nobody has
  looked at.
- Why the September photograph was white. Many Intel changes landed
  between that build and this one, and nobody bisected them.

## Follow-up worth having

Advertise and implement vertex fog on Gen3. Game 1 would then send one
pass instead of two: about 19,700 fewer batches a run on a machine that
renders it at 14.8 fps. It needs the engine to apply the fog factor, and
a measurement on the netbook, so it is a plan of its own, not a cap
flip.
