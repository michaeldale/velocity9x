# Phase 1d: the software engine reads the neutral draw and nothing of the Direct3D context, and the Fast-D3D guest's probe reports the same

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 1d (first engine)
Guest: 86Box `Win98SE-Fast-D3D` (port 9878), Windows 98 SE, Voodoo3 3500 on
the vbe package, `Direct3D=2` (the CPU rasterizer serves every draw),
1024x768x16, boots 592 (before) and 593 (after)
Evidence: `2026-09-26-phase1d-soft-V9XDD-before.ini` (HAL at commit 65c5045,
the batch dispatch present and every `draw` null),
`2026-09-26-phase1d-soft-V9XDD-after.ini` (HAL with `d3d_soft.c` on
`ops->draw`)

## What moved

`d3d_soft.c`'s draw entry is `v9x_d3d_soft_draw(const V9X_R3D_DRAW *, const
V9X_R3D_VERTEX *, DWORD)`. The render target, the depth surface and its
three-part activity test (`v9x_d3d_state_depth_active`), the bound texture's
surface object and sampling state, and the blend factors all come from the
description; `v9x_d3d_context_texture_surface` is no longer called from the
engine, and no `context->` read remains in its code. The ops table's
`draw_triangles` is null and `draw` is set, through the two null placement
entries the append-only rule requires. The texture setup, the coordinate
conversion, the repeat normalisation and every refusal counter are as they
were; the engine's own filter, blend and address normalisations now compare
against `V9X_R3D_*` values that `d3d_state.c` asserts equal to the D3D ones
they replaced.

## Measured

`V9XDDP.EXE` (863-line report, `Result=COMPLETE`) on the HAL before the
move and on the HAL after it, one boot each. Excluding the keys that change
with every process - timings, uptimes, the runtime's own pointers - the two
reports are **identical: zero differences**. The same Direct3D device
(`HwFlags=451`), the same render-target and Z results, the same texture
matrix, the same colour-key, blend and pixel-ladder results. The software
engine is deterministic where the emulated ViRGE's 3D-done census was not
(Phase 1a's record), so this comparison is exact.

## What the deploy taught

The first attempt put the newer 16-bit driver and HAL on this guest with its
older mini-VDD (11,804 bytes, 2026-09-11). The driver asked the VxD for the
VBE API, got none (`V9XBOOT.INI`: `Stage=fail-hardware-aperture`,
`VbeDetail=minivdd-no-api`) and Windows came up on the VGA fallback at
640x480x4, behind an Add New Hardware wizard for the monitor that held the
shell until it was cancelled through the agent's `input` verb (ESC did not
reach it; `move 459,365; click left` did). With the package's VxD renamed in
beside the driver, the next boot enabled at 1024x768x16. On the vbe family
the driver, HAL and mini-VDD are deployed together.

## Gates

- Host tests green (`test_d3d_state.c`, `test_r3d_clip.c`, `test_r3d_cull.c`
  among them); `run-checks` passed for all four families.
- The Fast-D3D guest as above.
- The ViRGE and Gen3 engines are untouched by this commit and keep
  `draw_triangles`; they are the next two moves, each with its own gate.

## Standing

One of three engines is on the neutral draw. The ViRGE engine is next
(86Box `Win86SE` and, for the S3D unit's own behaviour, the physical
A8U4I5); the Gen3 engine waits on the netbook, whose agent timed out today.
