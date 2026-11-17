# Oland: 3DMark 2001 SE Nature draws its lake, with nothing refused

Date: 2026-11-17. Step 8 of [`../plans/ati-gcn-ps11.md`](../plans/ati-gcn-ps11.md).
Machine: W9QI0DAD (Oland XT `1002:6610`), the boot that ran the three
clean 3DMark 2001 SE runs, on the package whose `vi_cp.c` is 1101edcb's
(read pointer from writeback; the build string is `0fda6f7a-dirty`).
128 MiB heap, 32 bpp desktop, 3DMark set to compressed textures and
software T&L. Evidence:
[`../probe/oland-nature-w9qi0dad-2026-11-17/nature-1/`](../probe/oland-nature-w9qi0dad-2026-11-17/nature-1/)
(frame grabs not committed: they are 3DMark's art).

## How the frame was got

The plan's gate asks for Nature's Image Quality frame. That test is a
Pro one, and this 3DMark is not Pro: ticking "Image Quality - Game 4"
brings up the purchase notice. "Game 4 - Nature" alone can be selected
in the Custom tab, so it was run alone with `V9XTRACE -grab:80x10`
armed. Eight of the ten grabs are Nature (677 flips in the run). The
last two are left over from an earlier run's Car Chase.

## What the frames show

- **The lake:** dark, bump-mapped ripples, reflecting the sky, with the
  shore rocks and trees behind it. The far water under the cliff has a
  fine speckle that the ripples may account for. It was not judged
  against a reference frame, since none is in the tree.
- **The stream** under the bridge: dark, rippled.
- **Terrain, mountains, trees, grass, the house, the bridge, the
  fisherman and the butterflies** are all textured. The HD 4600 left the
  distant terrain and the mountains white
  ([2026-10-30-haswell-ps11-step6-nature.md](2026-10-30-haswell-ps11-step6-nature.md)).
  Here they are textured.
- Several frames are washed toward white near the sun. That is Nature's
  sun glare, which is in the test by design (recalled, not compared with
  a reference).

On 2026-11-17 at boot 63 (16 MiB heap), Michael's recording showed the
water flat grey-blue in one frame and banded grey speckle in another
([2026-11-17-oland-bc-1d-tiled.md](2026-11-17-oland-bc-1d-tiled.md)).
That run refused 23,760 draws for texture format: DXT uploads that
found no room were placed by DirectDraw, untiled. With the 128 MiB heap
that does not happen, and the water draws. The cliff's smeared lower
edge seen then was not seen in these frames.

## Counters

- The policy counters (`M64Policy00`-`19`) are unchanged across the
  Nature run. No draw was refused for texture format, shape, op or
  anything else.
- Across the whole boot (three full benchmarks, V9XD32 at two depths,
  V9XDDP, Nature) the counters stand at 1 format refusal and 2
  texture-op refusals. Which run they came from was not separated.
- `EngineIdleTimeouts` 0 before and after.
- The context's `ps-summary` reports 0 dropped and 132,401 shader
  triangles (20531h).
- `V9XPS.LOG` holds `ps-refused-draw` lines for a 512x512 Q8W8V8U8 map at
  stage 0 and a 256x256 cube at stage 3 (the water's `texm3x3vspec`),
  reason 10. They lie far from the file's end, and `M64Policy10` is 0 on
  this boot, so they are from an earlier boot's step 5 build, before
  step 6 placed cubes. The log is not cleared between boots.

## Not done

- The Image Quality frame (needs 3DMark Pro).
- Frames from the full benchmark's Nature: the grab window covers Car
  Chase only in a default run.
- A comparison with a reference Nature frame from another card.
