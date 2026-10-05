# A8U4I5: SiS 6326 3D stall under Final Reality, 2026-10-05

- Machine: A8U4I5, SiS 6326 card 2 (rev 0Bh), desktop 800x600x16; Final
  Reality 1.01 runs at 640x480x16.
- Issue: [2026-10-05 SiS 3D engine stalls in Final Reality](../../issues/2026-10-05-a8u4i5-sis-3d-stalls-in-final-reality.md).

## Final Reality runs

| Files | Boot | Driver | What happened |
|---|---|---|---|
| `B231-*` | 231 | `4128908-dirty` (fog/specular build) | Benchmark ran to the end; the engine timed out at batch 565, triangle 0, and every later draw was dropped. `B231-FR-*.png` are the results screens: the 3D numbers (fill rate 785 Mpixels/s) are not real. `B231-V9XSNAP-AFTER-FR.INI`: `EngineIdleTimeouts=1`, `FlipDeclined=31579` |
| `B256-*` | 256 | nearest for tall textures under Z | Stalled at batch 0, triangle 9 |
| `B257-V9XSIS3D.TXT` | 257 | the same, with every triangle of the batch logged | Stalled at batch 0, triangle 9 |
| `B258-V9XSIS3D.TXT` | 258 | clamp for tall textures under Z | Stalled at batch 0, triangle 11 |

The batch numbers differ because run 231 counted batches from the first
Direct3D client of the boot; the later runs started from a fresh boot.

## SIS3D /phase6: batch 565 / batch 0's first triangle, one change a run

`B232`..`B255-SIS3D-P6-*.TXT`, build `sis3d-20261005-p6a`..`p6h`, every
base moved up 2 MiB. Each switch is described in `sis3d_phase6`'s comment
in `tools/diag/sis6326_3d_win32.c`.

| Variant | Boot | Result |
|---|---|---|
| logged pair (564, 565) | 232 | STALLED |
| `/fb` 565 alone | 233 | STALLED |
| `/fc` Z test and write off | 234 | IDLE |
| `/fd` one column right | 235 | IDLE |
| `/fe` texture 128x128 | 236 | IDLE |
| `/ff` left edge at X = 0 | 237 | machine locked (ping only), no output; not proven to be this run |
| `/fg` frame shifted 16 columns by the bases | 238 | STALLED |
| `/fi` left edge at +1/256 | 239 | IDLE |
| `/fj` `/fg` with the clip from 0 | 240 | STALLED |
| `/fh` negative Y (and one column right) | 241 | IDLE |
| `/fk` one row down | 242 | STALLED |
| `/fl` mid-row, 4 KiB aligned | 243 | STALLED |
| `/fm` no texture-cache clear pulse | 244 | STALLED |
| `/fn` no large cache, no bit 15 | 245 | STALLED |
| `/fo` nearest | 246 | IDLE |
| `/fp` no perspective | 247 | STALLED |
| `/fq` V off the seam | 248 | STALLED |
| `/fr` U off the seam | 249 | STALLED |
| `/ft` clamp | 250 | IDLE |
| `/fu` U and V off the seams | 251 | STALLED |
| `/fv` mirror | 252 | STALLED |
| `/fw` texture 256 wide x 128 high | 253 | IDLE |
| `/fx` texture 128 wide x 256 high | 254 | STALLED |
| `/fy` Z test, no Z write | 255 | STALLED |

## SIS3D /phase6 /file: the whole logged batch

`make_replay.py B258-V9XSIS3D.TXT <out> <variants>` writes the replay
streams in `replay/`; `B259`..`B269-SIS3D-REPLAY-*.TXT` are the results
(build `sis3d-20261005-p6i`, `p6j`). Wait 13 is the one before triangle 12,
i.e. triangle 11 pending; with `only=11`, wait 2.

| Replay | Boot | Result |
|---|---|---|
| `exact` | 259 | STALLED at wait 13, as the driver |
| `only11` (triangle 11 alone) | 260 | STALLED |
| `noz` | 261 | IDLE, all 64 triangles |
| `tq3` (SR3C split 4K/28K) | 262 | STALLED at wait 13 |
| `delay` (100 status reads before each triangle) | 263 | STALLED at wait 13; the status read after the run was idle |
| `only11poll` (120 more 1M-read waits) | 264 | never idle |
| `only11z05`, `only11z099` (every Z 0.5, 0.99) | 265, 266 | STALLED |
| `t11tex64k`, `t11tex2k` (texture moved) | 267, 268 | STALLED |
| `t11z2k` (Z buffer moved) | 269 | STALLED |
