# A two-unit draw no engine takes is drawn as single-unit passes: the Rage's refused unit-1 modes draw again

Date: 2026-10-08
Machine: A8U4I5, ATI Rage XL PCI (1002:4752), 800x600x16, boots 342-343.
Driver: V9XDISP.DRV 0.12.0 as installed, the `main` HAL with the
composite (7084831's code), and V9XGL.DLL built with this change. The
machine was left on its 0.12.0 HAL and its 0.12.1 ICD.
Evidence: [`../probe/a8u4i5-rage-xl-split-2026-10-08/`](../probe/a8u4i5-rage-xl-split-2026-10-08/)

## Why

Offering GL_SGIS_multitexture on the Rage class
([2026-10-08](2026-10-08-rage-pro-composite-direct.md)) made one thing
worse than not offering it. A unit-1 combine the composite does not have
(REPLACE, BLEND, DECAL) is refused by the engine, the CPU copy is refused
too because the Mach64 has no software fallback, and the batch drew
nothing. Without the extension the application drew it in two passes.

## The change

When a two-unit batch is refused with its CPU copy as well, the ICD draws
it as the passes the application would have drawn
(`v9x_gl_prim_split`, host-tested; `v9x_gl_draw_split` in gl_icd.c).
Pass 0 is unit 0's draw as it stands. Each later pass is unit 1's texture
at unit 1's coordinates, blended to apply unit 1's combine (GL 1.1
table 3.18):

| unit 1 | passes after pass 0 |
|---|---|
| MODULATE | Ct, (DESTCOLOR, ZERO) |
| REPLACE | Ct, unblended |
| DECAL by texel alpha | Ct with At, (SRCALPHA, INVSRCALPHA) |
| BLEND | Ct, (ZERO, INVSRCCOLOR); then Ct modulated by the environment colour as vertex colour, (ONE, ONE) |

Later passes test EQUAL against the depth pass 0 wrote, or repeat its
test when it wrote none, and write no depth. The split is declined, and
the batch refused as before, when the application blends (the passes need
the blender), or an alpha test that could discard has no depth writes to
confine the later passes, or unit 1 changes the alpha a test reads.
Triangles of one batch that overlap without depth writes take unit 1
twice where they overlap; nothing measured that case.

It applies on every engine. Gen3 and the software engine take such
batches whole or through the software fallback, so it does not change
what they draw.

## Measured

V9XGLP's SGIS cases (GL 1.1 table 3.18 values before 565 rounding,
the probe's +-8 tolerance):

| case | before (2026-10-07, boot 332) | split ICD (boot 342) |
|---|---|---|
| Replace (128,255,64) | black | 0x84FF42, Ok |
| Blend (100,0,101) | black | 0x5A0063 (90,0,99), not within 8 |
| Decal (164,178,57) | black | 0xA5B639, Ok |
| Modulate, CoordLeft, CoordRight, Unit1Off | Ok | Ok, unchanged |

ICD: `split=3/6`, `split-declined=0`.

Blend's red is ten below the ideal value, and it is where 16 bits put it.
Unit 0's red is already 198 in the framebuffer (Unit1Off reads 198 for the
same texel). 198 x (1 - 132/255) is 95.5, and the second pass's 565 write
truncates that to 11, which reads 90. An application drawing the two
passes itself on this target gets the same. The tolerance was set for
single-pass results, and this record does not widen it.

Quake 2 demo1 spawn timerefresh, 640x480, `gl_ext_multitexture 1`:
17.9 fps (17.9 before), 329,113 two-unit batches, `split=0/0`, refusals
the same 1,038 `TEXTURE_OP` one-unit batches as before. Its batches
never reach the split. Half-Life was not rerun: it refused no two-unit
batch on the composite HAL.

## Gates

`build-host.ps1` (the planner's tests watched failing against a stub
first), `run-checks.ps1`, passing.
