# Rage Mobility-M: first HAL fill and first hardware Direct3D

Gateway Solo 2150, Velocity9x bound (see
[`../../decisions/2026-09-28-mobility-first-velocity9x-bind.md`](../../decisions/2026-09-28-mobility-first-velocity9x-bind.md)),
`V9XHAL.DLL` from commit `46a2444` (register-offset and block 1 fixes),
installed by WININIT rename. The DRV and VxD are from the `dbd6200` install.
Boot 15.

## DirectDraw engine fill: PASS

`V9XDDF.EXE` (`tools/diag/ddraw_fill_stage_win32.c`) runs one step at a
time, flushing each stage to disk first:

- `/nofill` created DirectDraw and a 64x16 off-screen surface, and asked
  `GetBltStatus(CANBLT)`, which runs the engine's validate and a status
  read. After it, `BUS_CNTL` read `7B33A001`, up from `7333A001`: the HAL
  set `BUS_EXT_REG_EN`, matching the stock driver's value
  (`ATIMM-BEFORE-DDRAW.TXT`, `ATIMM-AFTER-VALIDATE.TXT`).
- The full run's `COLORFILL` 0x07E0, then Lock: all 1,024 pixels matched and
  `Result=PASS` (`V9XDDF-FILL.TXT`).
- The engine, not the CPU fallback, did the fill: `DST_OFF_PITCH` became
  `0202FFFE`. That is a 64-pixel pitch with base `17FFF0`, 16 bytes below
  the surface at `180000`, which is the Phase 2 origin-repair batch
  (`ATIMM-AFTER-FILL.TXT`).
- The HAL snapshot has engine type 4 and caps `0x10`, with zero FIFO
  timeouts, idle timeouts and resets (`V9XSNAP-AFTER-FILL.INI`).

## Direct3D: first hardware triangles, probe INCOMPLETE

`V9XDDP.EXE` found the Velocity9x Direct3D HAL (`D3DHalFound=1`) with the
Mach64 engine's three texture formats, created the device, and passed:
`D3DTrianglePixelOk` (`0xF800`), reverse winding, both halves, sub-pixel,
shape, depth fog, vertex-alpha blend, and vertex alpha curve.

Most of the failures are the policy boundary, as intended. The texture
tests use textures larger than 8x8, and specular Gouraud carries specular
RGB, so the engine refuses those draws. The Z-buffer and mixed-engine
sections were never reached: they still carry the probe's not-run
placeholder `0x88760231`. So Z is untested, not failed.

The run did not finish. During the texture-mip matrix, the agent became
intermittently unreachable while ping still answered. Then the laptop
restarted (boot 16), and the report stops at
`TexM_256_1555_gapped_mipnear` with `Result=INCOMPLETE`
(`V9XDD-INCOMPLETE.INI`). What restarted it, and why the matrix stalled,
are not established.
