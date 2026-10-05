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

## SiS 2.28 on the same card (boot 270)

`sis228-b270/`: the device's Enum `Driver` value pointed at SiS's class key
`Display\0009` (`TOSIS.REG`) for one boot, then back to `Display\0010`
(`TOV9X.REG`). `ENUM-SIS6326-BEFORE.REG` is the key before the swap.
`B270-SIS6326-CAP01..20.TXT` are SIS6326 register snapshots taken every 16
s through Final Reality's full benchmark, which SiS's HAL rendered
throughout (`B270-FR-SHOTS.png`). `B270-SIS6326-DESKTOP.TXT` is the same
probe at SiS's 800x600x16 desktop, `B274-SIS6326-VELOCITY9X.TXT` at ours.

SiS's state for Final Reality's Z-tested strip (CAP04, CAP07, CAP10):
enable `00308EA1h` (ours plus dither), Z set `00130500h`, texture
`50030009h` (RGB555, wrap, bilinear, 256x256), with the Turbo Queue on
(SR27 D0h, SR2C 7Eh, SR3C 43h). It also writes 8A10h, 8A20h D24, 8A24h,
8A2Ch and 8A80h-8A88h D23, which the driver never does.

## Replays with SiS's differences (boots 271-283, build `p6l`)

| Replay | Boot | Result |
|---|---|---|
| `only11sisall`, `sisall` (dither, 8A10h, 8A20h D24, 8A24h, 8A2Ch, alpha mode 0, 8A80h-8A88h D23) | 271, 272 | STALLED |
| `only11fmt50`, `fmt50` (RGB555) | 273, 274 | STALLED |
| `only11sr3d` (SR3D A2h, beyond the datasheet) | 275 | STALLED |
| `only11thr` (SR08 8Fh, SR09 0Bh: SiS's arbitration thresholds) | 276 | STALLED |
| `only11sr3e` (SR3E 08h) | 277 | STALLED |
| `only11tq`, `tq` (Turbo Queue on as SiS has it) | 278, 279 | STALLED; status shows the queue live |
| `only11zfail` (Z buffer 0000h: every pixel fails LEQUAL) | 280 | IDLE |
| `only11zffff`, `zffff` (Z buffer FFFFh) | 280, 281 | STALLED |
| `only11noshift` (integer vertices) | 282 | STALLED |
| `only11sisshift` (vertices 2^-15 up-left, as SiS) | 283 | STALLED |

## Real texture contents, and TEND (boots 284-289, build `p6m`)

Boot 284 reinstalled the committed driver (`a141406`). Boot 285 ran a build
that also writes the current batch's texture level 0 and Z buffer to
`V9XSIS3T.BIN` and `V9XSIS3Z.BIN` on a timeout; Final Reality stalled at
batch 0, triangle 0 (`B285-V9XSIS3D.TXT`). The Z buffer was 7FFFh
throughout, the value the replays already filled. The texture held 10,373
distinct texels; it is Final Reality's artwork and is not kept here. SIS3D
gained `B` (MMIO byte write, a new VxD op) and `X` (load a file into VRAM).

| Replay | Boot | Result |
|---|---|---|
| `f5realtex` (boot 285's batch with Final Reality's texture loaded) | 286 | STALLED at wait 2 (triangle 0) |
| `f5tend` (boot 285's batch, TEND after each triangle) | 287 | IDLE, all 64 triangles |
| `f4tend` (boot 258's batch, TEND after each triangle) | 287 | IDLE, all 64 triangles |
| `only11tend` (triangle 11 alone, with TEND) | 288 | IDLE |

`tend-b289/`: Final Reality's full benchmark on the driver writing TEND
after every triangle (boot 289). It rendered every scene; the snapshot
reads `EngineIdleTimeouts=0`, `FlipDeclined=0`, 290,226 primitive calls
(the 17,013 refusals are explained under `fold-b291` below),
and 17,013 batches refused by the mapping (`BatchesEngineRefused`, reason
not counted). Scores as reported: 2D 6.64, 3D 1.85, bus 3.56, overall
3.55; fill rate 14.09 Mpixels/s, robots 8.80 images/s, visual appearance
88.89%.

## Refusal reasons, and the magnification filter (boots 290-291)

`fold-b291/`. Boot 290's build counts SiS mapping refusals by
`V9X_D3D_SIS_REFUSE_*` reason in V9XTRACE's `M64PolicyNN`. Final Reality's
full run (`B290-V9XSNAP-AFTER-FR.INI`): all 17,192 refusals were reason 13,
the texture filter. Boot 291's build folds a mip filter set as the
magnification filter to its within-level half. Final Reality's full run
(`B291-*`) then had `BatchesEngineRefused=0` and `EngineIdleTimeouts=0`.
Scores as reported: 2D 6.60, 3D 1.83, bus 3.50, overall 3.51.

V9XDDP on the same boot (`B291-V9XDD.INI`, `B291-V9XDDT.TXT`) completed
with no idle timeout. `ddp_compare.py` against boot 231 shows no
difference in any compared check, and against SiS's HAL still none that
SiS passes and this fails. Its 5 refusals (`B291-V9XSNAP-AFTER-DDP.INI`):
3 blend (the last DESTCOLOR/ONE), 1 colour key, 1 texture format.
