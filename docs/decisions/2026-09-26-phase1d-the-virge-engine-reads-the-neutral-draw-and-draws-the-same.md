# Phase 1d: the ViRGE engine reads the neutral draw, and the 86Box ViRGE probe reports the same as before the move

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 1d (second engine)
Guest: 86Box `Win86SE` (port 9869), Windows 98 SE, S3 ViRGE/DX in hardware
Direct3D mode, 1024x768x16, boot 635
Evidence: `2026-09-26-phase1d-virge-V9XDD-after.ini` (HAL with `d3d_virge.c`
on `ops->draw`), compared with `2026-09-26-phase1a-virge-V9XDD-after-run1.ini`
and `...-run2.ini` (the HAL before this move; the two commits between
changed nothing on the ViRGE path, whose `draw` was null)

## What moved

`d3d_virge.c`'s entry is `v9x_d3d_virge_draw(const V9X_R3D_DRAW *, const
V9X_R3D_VERTEX *, DWORD)`. Fifty reads of the Direct3D context became reads
of the description by a fixed field map - the S3D setup's target, clip
rectangle, pitch and depth registers from `draw->target` and `draw->depth`,
the texture filter, blend, address, border and wrap words from
`draw->texture`, the blend factors, alpha test, alpha-force instrument and
colour-key enable from their own fields - and the three places that asked
the core for the bound texture's surface read `draw->texture.object`, the
same LCL resolved once per batch. The colour-key rewrite still looks the key
up through the core's table by that object. The per-triangle setup keeps
its `V9X_D3DTLVERTEX` field names behind one cast at the entry, held by the
assertions in `d3d_core.c`. The ops table's `draw_triangles` is null.

## Measured

`V9XDDP.EXE` (859-line report, `Result=COMPLETE`, 9.9 s). Against each of
the two Phase 1a runs, excluding the per-process keys (timings, uptimes, the
runtime's own pointers) and the S3D done-missing census that Phase 1a showed
moves from run to run on the same binary, the difference is **two keys,
`TexHandle` and `TexHandle2`**: the addresses of the HAL's texture-table
entries, which the probe echoes and which move when the DLL's layout
changes. Every result key is the same: device flags, render targets, Z,
the texture matrix's result cells, colour key, blends, the pixel ladder.

## Gates

- Host tests green; `run-checks` passed for all four families.
- 86Box ViRGE as above.
- **Not run:** Final Reality on this guest, and the physical A8U4I5
  (ViRGE/DX). Both are the plan's Phase 1 gate for this engine and remain
  owed before the Phase 1 record closes.

## Standing

Two of three engines are on the neutral draw. Gen3 (`d3d_i9xx.c`) is
next and its gate is the netbook, whose agent timed out today; when it
answers, 3DMark 99 at 1024x576 and the 3D WinBench 98 quality suite are
the comparison, per the plan.
