# HD 3000: 3DMark 2001 SE Nature leaves cut holes in the mountain; 3DMark 2000 Helicopter trees draw as brown blocks

Date: 2026-11-20

Status: **open; reported by the owner, two screenshots, nothing measured.**
M-NZHWBR (i3-2105, HD 3000 `8086:0112`), the build installed for the
Sandy Bridge Direct3D work (merged to main `31ab9aee`). Plan:
[`../plans/intel-sandybridge-direct3d.md`](../plans/intel-sandybridge-direct3d.md).

## The report

Daniel, by chat on 2026-11-20 (05:26 to 05:44 UTC): "3DMark2001 renders
mostly fine, some graphical glitches on the nature test. 3DMark2000:
Helicopter test is very glitchy both with P3 optimization and software.
Adventure renders fine. Quake 3 renders fine." Carmageddon II under Glide
renders fine. He saved two screenshots on the guest desktop; they were
pulled over the relay and converted to PNG:
[`../probe/m-nzhwbr-snb-owner-3dmark-shots-2026-11-20/`](../probe/m-nzhwbr-snb-owner-3dmark-shots-2026-11-20/).
Both are 1024x768, captured by the owner (how is not known).

## What the screenshots show

- **3DMark 2001 SE Nature** (29:53, 20 fps): along the ridge behind the
  trees, jagged patches in the shape of leaf clusters show a flat
  grey-blue where the mountain texture should be. The leaves in front
  draw; the region they cover beyond their own pixels does not.
- **3DMark 2000 Helicopter** (mission time 29.0): the trees on the lower
  slopes, left and centre, draw as brown stepped blocks and smeared
  triangles; the trees further away and on the skyline draw as trees.

## Hypotheses, none tested

1. Nature: alpha-tested leaf pixels that the test discards still write
   depth, so the mountain drawn after them fails the depth test and the
   clear colour stays. Gen4 had a fault of this family
   ([2026-11-11](2026-11-11-gen4-app-alpha-test-depth-write-draws-dropped.md)),
   but that one dropped the draws; this one would draw the wrong pixels.
   The Gen6 pixel kernel's discard and the depth write enable are the
   first things to read.
2. Helicopter: the near trees use a different texture level or format
   from the far ones (the brown is close to the trunk colour), so a
   mip or LOD selection fault on Gen6 is possible. 3DMark 2000's trees on
   other chips were a DX5 MODULATE alpha fault (fixed 2026-11-17); this
   path has that fix.

The owner's description says the Helicopter fault appears with both the
Pentium III and the software transform; that puts it after vertex
processing.

## Next

Run 3DMark 2000 Helicopter and 2001 SE Nature with `V9XTRACE -grab` on
M-NZHWBR, and read the draw states behind the bad pixels.

## 1.0 re-read, 2026-11-26

Stays open for 1.0 and is named in the README's known issues. Nothing
measured since the owner's screenshots; the README's HD 3000 entry names
it.
