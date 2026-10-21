# OpenGL ICD: a second pixel format with 24-bit depth, render ABI 6

2026-10-21, the i5-4570 (HD 4600, 10.0.1.254), boot 59 (the released
0.22.0 set) before and boot 60 (this change) after, driver
`f8318ee3-dirty`: DRV, VxD, HAL, SETP and V9XGL from one build, installed
by WININIT rename and `reset`. Phase 4 Steps 1 and 2 of
[`../plans/32bpp-acceleration-and-3d.md`](../plans/32bpp-acceleration-and-3d.md).
Evidence:
[`../probe/hsw-4570-icd-z24-2026-10-21/`](../probe/hsw-4570-icd-z24-2026-10-21/).

## The change

- **Render ABI 6** (`include/velocity9x/r3d_abi.h`). The describe gains
  `depth32_bits`: the engine's `depth32_bits` where its published Z depths
  include DDBD_32 (24 on Gen3, Gen4 and Gen7.5, 32 on the MGA-2164W), zero
  elsewhere and with no target format. A clear's `depth_value` moves from
  16 bits to the 24-bit full scale. The HAL narrows it to 16 bits for a
  16-bit buffer and widens it for a 32-bit cell
  (`v9x_d3d_state_depth_rescale`, host-tested, failed first), as Clear2
  already scaled its float.
- **The tiled-depth clear took a 16-bit value into a 32-bit cell.** Before
  this change, `v9x_r3d_clear_tiled_depth` (Haswell's Y-tiled Z) passed
  `depth_value & 0xFFFF` to the depth fill, which takes the buffer's own
  full scale. A Z24 buffer cleared to the far plane would have got 0xFFFF
  of 0xFFFFFF, about 0.004. Nothing reached it, because the ICD made only
  16-bit Z. It now gets the rescaled value.
- **ICD.** Format 1 is unchanged (16-bit Z). Format 2 is the same colour
  layout with `cDepthBits` = `depth32_bits`, listed only where that is
  non-zero. DrvSetPixelFormat records the window's depth cell, and the
  drawable makes its Z surface at `dwZBufferBitDepth` 32, as V9XD32 did
  for the Z24 measurements of 2026-10-18. GL_DEPTH_BITS reports the
  drawable's bits. Polygon offset's r is 1/(2^24 - 1) for a 24- or 32-bit
  buffer: the vertex carries depth as a float, which resolves no finer
  step near 1. The clear plan sends depth at 24 bits.
- **Glide** widens its 16-bit clear depth onto the 24-bit scale; its own
  quad keeps the 16-bit value. The R3D probe sends 0xFFFFFF.

Gates: `check-tree.ps1`, `build-host.ps1` (new and changed tests in
`test_r3d_validate`, `test_d3d_state`, `test_gl_state`, `test_gl_get` and
`test_gl_prim`, each failing to build or failing before the code) and
`run-checks.ps1`, all passing.

## What V9XGLP measured

V9XGLP gained `/format:N`, which sets format N instead of the one
ChoosePixelFormat picks. It also gained a depth-precision check: green
at window depth 0.25 + 2^-20, then red at 0.25 under LESS. That is 16
steps of a 24-bit buffer and 0.06 of a 16-bit one, so red can only draw
where the buffer holds more than 16 bits.

| Desktop | Format | Result | DepthBits | DepthFineRead | Resolved |
|---|---|---|---|---|---|
| 800x600x32 | 1 | PASS | 16 | 0x0000FF00 | 0 |
| 800x600x32 | 2 | PASS | 24 | 0x00FF0000 | 1 |
| 800x600x16 | 1 | PASS | 16 | 0x0000FF00 | 0 |
| 800x600x16 | 2 | PASS | 24 | 0x00FF0000 | 1 |

In every run: LifetimeFailures=0, OffsetNearerOk and OffsetFartherOk
equal to 1, and the geometry read-backs the same as boot 30's 16 bpp run.
V9XGL.LOG names the cell: `depth cell=32 bits=24` for format 2.

V9XR3DP negotiates ABI 6 and its 0xFFFFFF far-plane clear reads back as
0xFFFF in its 16-bit buffer (`ClearOk=1`). `TexturedScissorOk=0` is as
in the 0.21.0 run of 2026-10-20, so it is not new.

## What the evidence disputes

- **ChoosePixelFormat does not pick the deeper format.** 98 SE's
  OPENGL32, asked for GLQuake's 24 colour bits and 32 depth bits, chose
  format 1 (16-bit Z) over format 2 (24-bit Z) on a 32 bpp desktop
  (`V9XGLP-boot60-32bpp-chosen.INI`, `ChosenFormat=1`). The hypothesis
  was that it would take the closer depth. So a title that goes through
  ChoosePixelFormat with a GLQuake-style request still gets 16-bit Z, and
  only a title that enumerates formats and sets one itself gets 24. The
  order (16 first) was chosen so that format 1 stays as it was. Whether
  to swap it is a decision, not a measurement. Swapping would give every
  ChoosePixelFormat title Z24 and its bandwidth, if OPENGL32 then picks
  the first match, which has not been measured.
- **Presenting at 32 bpp is wrong on this machine.** It is filed
  separately
  ([`../issues/2026-10-21-haswell-gl-present-at-32-bpp-half-width.md`](../issues/2026-10-21-haswell-gl-present-at-32-bpp-half-width.md)).
  It belongs to Step 1's 32-bit colour, not to the depth format: format 1
  and format 2 show it alike, and 16 bpp shows neither.

## Not measured

The netbook (Gen3) and A8U4I5 (MGA-2164W, 32-bit Z) were unreachable
(connection timeouts). Gen4 and the MGA take the same code path but have
not run it. No game was run.
