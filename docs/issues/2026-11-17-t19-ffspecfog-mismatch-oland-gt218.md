# V9XD32 T19 FfSpecFog mismatches on two vendors' hardware: Oland and GeForce 210

Date: 2026-11-17

Machines: W9QI0DAD with the Oland XT (`1002:6610`, ati package) and,
earlier, the GeForce 210 (GT218, nvidia package). Status: open, not
traced.

## What is seen

T19's FfSpecFog row (fixed-function specular plus fog, drawn through
the core's fixed-stage program under DDI 8) is the only T19 row that
fails on either chip.

| Chip | Desktop | Tolerance | Bad pixels | First bad pixel |
|---|---|---|---|---|
| GT218 | R5G6B5 | 9 | 3 | (12, 52): got 5ADBE7h, expected 5ACFDEh (12 steps) |
| Oland | R5G6B5 | 9 | 3 | (12, 52): got 5ADBDEh, expected 5ACFDEh |
| Oland | X8R8G8B8 | 2 | 17 | (11, 49): got 5AD0E4h, expected 5ACDE4h (green 3 over) |

Sources:
- [2026-11-15-gt218-draw-shader-ddi8.md](../decisions/2026-11-15-gt218-draw-shader-ddi8.md)
- [2026-11-17-oland-draw-shader-ddi8.md](../decisions/2026-11-17-oland-draw-shader-ddi8.md)
- [2026-11-17-oland-cubes-signed-formats.md](../decisions/2026-11-17-oland-cubes-signed-formats.md)
- the latest Oland runs, unchanged:
  `../probe/oland-cp-stall-w9qi0dad-2026-11-17/d32-rptr-wb-desktop16/`
  and `d32-rptr-wb-desktop32/` (`T19FfSpecFogMismatches` 3 and 11h).

## Reading

Two back ends from different vendors fail the same pixel, both too
green. The cause is more likely in what they share than in either back
end:
- the core's fixed-stage program for specular plus fog, which these
  DDI 8 back ends draw as a pixel shader;
- or a rounding that V9XD32's expected value allows on some hardware
  and not on others.

The row passes elsewhere
([2026-11-14-gen3-specular-and-bump-luminance.md](../decisions/2026-11-14-gen3-specular-and-bump-luminance.md)):
- the HD 4600, worst channel 9;
- the GMA 950, worst channel 8.

Both are within the tolerance of 9, the HD 4600 only just. So V9XD32's
expected value is not plainly wrong, but the margin is thin on every
chip measured. Whether the HD 4600 and the GMA 950 draw this row
through the same core program as the Oland and the GT218 was not
checked.

## Not done

- The GT218 at X8R8G8B8: not run, as far as the records show.
- Tracing: compute the expected pixel by hand from the row's inputs,
  compare it with the reference interpreter's output for the core's
  program, then with each chip's output.
