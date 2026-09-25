# Bars at the top and bottom of 3DMark99's frames at the 2112-byte stride

**Status: OPEN.** MICHAEL-NETBOOK, 945GSE, 1024x576x16, driver with the
unaliased stride (`docs\decisions\2026-09-25-scanning-out-at-2112-on-gen3.md`).

## Symptom

During 3DMark99's game tests the panel shows a band of wrong content along
the top and the bottom of the frame - fragments of rows at the top, a strip
of other content at the bottom (`docs\decisions\2026-09-25-netbook-stride-
2112-3dmark-bars-{1,2}.jpg`). Operator-observed on the panel; the agent's
screenshot cannot see a flipped frame. Reported as appearing with the
2112 stride.

## Ruled out, with the evidence

- **Flip-chain overlap**: it caused the same symptom on boot 13 (buffers
  2048 x 576 apart at a 2112 pitch) and was fixed on boot 14 - the chain is
  now 0x000000, 0x129000, 0x252000 (V9XTRACE `DrawTarget0-2`) - and the
  bars remain.
- **CPU fills at the wrong pitch**: `v9x_cpu_fill` steps rows by the
  destination surface's own lPitch (`blt_cpu.c:109`).
- **The ring flip's pitch**: MI_DISPLAY_FLIP takes the plane's live stride
  register, which read 0x840 at the flips (`FlipStrideLast`).

## Not yet examined

- Whether the bars existed at 2048 and went unnoticed; a 2048 run watched
  for them would settle it.
- The render target's footprint and viewport at 2112: `bind_target` and the
  drawing rectangle, against the surface's rows.
- The Z buffer and mip-tree blocks' placement relative to the third back
  buffer.
- The frame capture (`V9XFRAME.PPM`) fired on the session's first flips
  and recorded a black frame; arming it mid-test (`V9X_DDARMFRAME`) would
  give the presented buffer itself.
