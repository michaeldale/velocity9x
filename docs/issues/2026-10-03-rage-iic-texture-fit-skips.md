# Rage IIC: pieces skipped because the perspective texture fit fails

Date: 2026-10-03. Machine: A8U4I5 (Rage IIC AGP `1002:4757`). Status:
open, deferred. Evidence: `docs/decisions/2026-10-03-rage-iic-setup-cost.md`
(Open), `docs/decisions/2026-10-03-rage-iic-opengl-first-runs.md`
(boots 146-148).

## Symptom

Pieces that `v9x_r2_build_piece` skips at the texture-fit stage are not
drawn, so they leave holes:
- 14,617 per 640x480 Half-Life run (setup-cost record);
- 2,589 in the Quake 2 timedemo after the FPU save (1f7ceb1).

The last skipped piece's inputs are recorded (`r2_piece_last`: stage,
status, per vertex x/y, z, and q/tu/tv float bits; `R2Last*` in
`V9XSNA7.INI`). The causes have not been analysed.

## What is known

- The 68,270 Quake 2 skips before 1f7ceb1 were the 24-bit FPU precision
  control word, not range: those inputs fit at every texture size when
  replayed in a host test.
- 98,754 degenerate pieces a 640x480 run are a separate count (no pixel
  covered), not this.

## Next

Capture several `R2Last` sets from Half-Life and Quake 2 runs. Replay
them in a host test (`test_rage2_setup.c`). Classify each as a real limit
of the fit (texel range, gradient field width), which is drawn degraded or
split further, or as a defect, which gets a failing test first.
