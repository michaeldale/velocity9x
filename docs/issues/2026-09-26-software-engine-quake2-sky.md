# Gen3 and the software fallback disagree in one Quake 2 frame

Found 2026-09-26 on MICHAEL-NETBOOK. Open. First filed as "the software
engine draws Quake 2's sky darker and without blue"; the evidence below
disputes that title.

## Symptom

In demo1's first room, with square textures sampled by Gen3 and
non-square ones drawn by the software fallback into the same frame, the
sky seen through a hole in the ceiling is grey, a pale sliver shows along
a far ledge, and a bright piece appears at the top right. Region
averages over the same pixels:

| Configuration | Hole | Top right | Ledge |
|---|---|---|---|
| Software engine only (fallback for every texture) | (42, 19, 0) | (5, 2, 0) | (19, 9, 2) |
| Gen3 for square, fallback for non-square | (61, 47, 35) | (45, 31, 21) | (34, 20, 11) |
| Gen3 for every texture | (45, 21, 2) | (10, 3, 0) | (23, 10, 3) |

Gen3 alone and the software engine alone agree (mean absolute difference
over the frame 3.7 / 1.3 / 1.6 per channel); only the mixed frame differs.
Quake 2's own software renderer showing a grey sky there was a false
lead: it lights in monochrome and is no reference for GL colour. Evidence
in `docs/decisions/2026-09-26-phase5-quake2-gen3-hardware-textures.md`
and the files beside it.

## Known

- Quake 2 sends the same draws to both engines (the ICD's state log).
- Every isolated scene tried - REPLACE and MODULATE, LINEAR/CLAMP, far
  quads under Quake 2's frustum, RGB and RGBA - draws the same on both.
- Quake 2 no longer shows it: non-square textures are sampled by Gen3
  since the change that updated this file, so demo1 is drawn by one
  engine. The fallback still runs for batches Gen3 refuses (440 of
  1.23 million in one session).

## Hypothesis (unmeasured)

The ceiling around the hole has non-square textures, so the CPU drew it
and wrote its depth; the sky box, drawn afterwards with LEQUAL, was drawn
by Gen3 against that depth. If the two engines round a depth near 1 to
different 16-bit values, the sky wins where it should lose. Phase 0.5
measured the engines agreeing on depth only for its own values.

## Next

A V9XGLP scene: a far quad drawn by the fallback (a masked draw forces
it), then an equal-depth quad drawn by Gen3 with LEQUAL, at several
depths near 1; read back which one shows.
