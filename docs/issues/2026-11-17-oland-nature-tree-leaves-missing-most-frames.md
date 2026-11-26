# Oland: Nature's tree leaves are missing from about two frames in three

Date: 2026-11-17

Machine: W9QI0DAD (Oland XT `1002:6610`), ati package as merged to main
(d3dd1b7d; on the card, the build whose `vi_cp.c` is 1101edcb's), 32
bpp desktop, 128 MiB heap. Status: open, cause not known.

## What the recording shows

`Y:\MWD\videos\obs\2026-11-17 20-01-04.mkv` (Michael), 13 s at 1080p60,
3DMark 2001 SE Nature, the cottage scene. Not in the tree: 3DMark's art.

- The foreground trees are drawn with their leaves in some game frames
  and as bare branches in others. For example, mission time 04:72 is
  bare and 04:85 is leafy: consecutive game frames, the same camera
  position. The butterflies and the rest of the scene are the same in
  both.
- The cadence was measured on the left tree's region (mean luminance
  of each captured frame, leafy about 160 and bare about 192, over the
  first 4 s). The leaves show for 2-3 game frames, then are missing for
  about 6, then show again.
- During a hitch of about 2 s (the mission timer held at 05:65, fps 2),
  the frame held on screen is a bare one. So the bare frames are
  complete, presented frames, not partly drawn ones.
- Frames with leaves also show more sun glare through the canopy.

The Nature frames grabbed earlier that evening (`nature-1`, eight
V9XTRACE grabs) happen to show leafy trees, so the run judged as
correct in [2026-11-17-oland-nature.md](../decisions/2026-11-17-oland-nature.md)
did not catch this. That record's conclusion was drawn from too few
frames.

## Counters over the recorded run

From the snapshot after `nature-1` to one taken after the recording
(`../probe/oland-nature-w9qi0dad-2026-11-17/after-obs-2001/`):
- one Direct3D context, 923 flips, `EngineIdleTimeouts` 0;
- no refusal counted (`M64Policy*` unchanged);
- `DrawsIntoPresented` unchanged at 11;
- 75 textures placed (18 MB), 612 texture locks, 3,223 alpha-test state
  sets;
- 183 draws with no texture handle, 15 of them white;
- 29 % of list triangles culled, the same share as in `nature-1`.

So the leaf draws, whichever they are, were accepted. What they drew is
not known.

## Related

The GeForce 210 showed trees popping in and changing strength between
frames in 3DMark 2000's helicopter test
([2026-11-16-gt218-3dmark2000-helicopter-trees-pop.md](2026-11-16-gt218-3dmark2000-helicopter-trees-pop.md)).
That is a different chip and back end, so shared code in the core is a
candidate. Not established.

## Hypotheses, none tested

- **Alpha test.** The leaves are alpha-tested billboards. The alpha
  reference or function reaching the draw differs between frames: a
  state cached in the core or in a back end's program cache, or the
  reference's float conversion.
- **Texture residency.** The leaf texture is evicted and re-uploaded
  between frames, and a draw samples it mid-upload or from its old
  place. The 75 placements during the run show that textures do move.
- **Culling.** Leaves drawn with culling off are culled by a stale
  cull mode in the core.
- **The application.** 3DMark culls or fades the leaves by level of
  detail. This is unlikely at a fixed camera position, from one frame
  to the next.

## Next

A measurement build that logs the draws of one leafy frame and one bare
frame:
- texture handle and placement;
- alpha test enable, function and reference;
- cull mode;
- whether the draw went through `draw_shader` or the fixed-function
  path;
- the triangle count.

Diff the two frames. A per-frame flip sequence number in the log lines
lets the frames be paired with the recording.

## 2026-11-17, 20:10-20:32: cause found and fixed on the Oland

A draw-logging measurement build showed every frame handing the engine
the same draws and state; only the leaf vertices' diffuse alpha changed.
It fell below 7Fh in about five frames of eight. The Oland's map gave DX5
MODULATE the MODULATEALPHA shader, which multiplies that alpha in;
Direct3D takes the texel's alone. Fixed by keeping the texel's alpha in
the built PS. On boot 93 all seven cottage and bridge grabs show the
trees in leaf, with nothing refused. Record:
[2026-11-17-oland-dx5-modulate-keeps-texel-alpha.md](../decisions/2026-11-17-oland-dx5-modulate-keeps-texel-alpha.md).

Status: fixed on the Oland. The GeForce 210, Evergreen and R600-class
maps fold MODULATE the same way and are not changed or measured.

## 1.0 re-read, 2026-11-26

Closed. Fixed and seen on the Oland. The GeForce 210 took the same
MODULATE texel-alpha fix in 448e2f2d (its Helicopter re-run is in the
re-run list).
