# The HD 4600 draws two Direct3D texture stages and makes the pixel-centre move itself

Date: 2026-10-25

Status: implemented, measured on the 4570 (W9QI5A83, HD 4600) at boot 82
against boot 81 (guard band). Ivy Bridge runs the same engine table and
so takes it too, untested.

Evidence: [`../probe/haswell-two-stages-2026-10-25/`](../probe/haswell-two-stages-2026-10-25/);
boot 81 in [`../probe/haswell-guard-band-2026-10-25/`](../probe/haswell-guard-band-2026-10-25/).
The Rage 128's: 5ef364f9 (centre_in_engine), a87c724e (two stages,
[2026-10-24-rage128-two-texture-stages.md](2026-10-24-rage128-two-texture-stages.md)).
It follows [2026-10-25-haswell-guard-band.md](2026-10-25-haswell-guard-band.md),
which made `clip_in_core` 0, the first thing both changes need.

## What changed

- **`centre_in_engine` 1.** The core stops moving every vertex half a
  pixel and staging the triangle to do it. The draw applies
  `draw->centre_offset` to x and y as it copies, before the guard-band
  test and any cut. The indexed path uses the same field. The Rage 128's
  move gained nothing on its own (1604 to 1607); it is here because two
  stages need it.
- **`texture_stages` 2.** The core pairs stages 0 and 1 where one pass is
  exact (`v9x_d3d_state_stages`), and the engine must draw every pairing
  it can make: colour REPLACE, MODULATE, ADD or DECALALPHA, alpha
  FRAGMENT, REPLACE or MODULATE, on either unit. The Haswell kernel
  generator did four stage-0 combines and one stage-1 combine (multiply).
  It now takes a general colour op and alpha op per stage
  (`V9X_HSW_PS_STAGE0_OPS`, `_STAGE1_OPS`, `v9x_hsw_psg_combine`). The
  existing codes stay as they were, so the render interface's kernels
  and their slots do not change.
- **Kernel slots.** There are still 48 slots of 768 bytes. The general
  codes fold onto the named ones (stage 0 modulo six, any stage 1 as on),
  and the slot signature, which carries both codes whole, tells kernels
  that share a slot apart. A program that alternates two such kernels
  pays a drain to rewrite the slot.
- **What does not fit.** DECALALPHA (`BLENDTEXTUREALPHA`) takes four
  instructions a component. Beside fog or specular it overflows a slot's
  160 dwords: in both stages with either flag, or in one stage with both
  flags unless stage 0 is TEXTURE. The host test states that rule over
  all 816 combinations. `accepts` builds the kernel once per signature
  and declines one that does not fit. The core then draws that batch's
  stage 0 alone and counts it (`TwoUnitDeclined`). LRP would do
  DECALALPHA in one instruction, but no three-source instruction has run
  on this chip, and a wedged render engine is the one failure that
  hard-locks the 4570 on `reset`. Not tried.
- **DDI 6 for every program.** An engine with two stages gets every
  program at DDI 6 (Michael's rule for the Rage 128, keyed in the core on
  `texture_stages`). On the HD 4600 that now includes Half-Life and
  3DMark 2000; `=5` in `[Velocity9x.Direct3DDdi]` still opts one out.

## Measured (Haswell, 800x600x16 desktop)

| | Boot 81 guard band | Boot 82 two stages |
|---|---|---|
| V9XD32 /ddi6 | PASS, T12 SKIPPED-ONE-STAGE | PASS; T12 `engine-two-unit` PASS, MODULATE and ADD each pixel exact; T13 two-unit volume PASS, no stray pixel |
| V9XD32 /ddi5 | PASS | PASS, every value equal to boot 81 |
| V9XD32 other values, /ddi6 | | equal to boot 81 |
| DB3D probes | 174 pass | 174 pass |
| DB3D FillMultiTex | refused, single-texture device | 8.08 cycles/px (FillTex16Bilinear 8.07) |
| Half-Life mwd5 D3D, windowed | 65.4 fps at DDI 5 | 66.7 fps at DDI 6 |
| Half-Life mwd5 GL, windowed | 77.5 fps | 78.9 fps |
| 3DMark 2000 default | 2009 at DDI 5 | 2355 at DDI 6 |

Trace after boot 82's runs: 429,181 two-unit batches drawn, 0 declined,
2 pairings refused at ValidateTextureStageState. 931,110
DrawPrimitives2 calls. 476,761 indexed calls drawn whole; declined:
55,207 range, 13,495 past half the band, 249,705 state. No timeouts.

Readings:

- **Two stages are exact where measured.** T12's two triangles,
  MODULATE and ADD, read back pixel for pixel. T13's volume through two
  units drew its centres with nothing stray. DB3D's multi-texture fill
  verified at single-texture speed.
- **The score rose 2009 to 2355.** That is two changes at once: DDI 6
  for every program and the multi-texture fill. They are not separated,
  because the second needs the first. On the Rage 128 the same switch to
  DDI 6 cost about 5% (1694 to 1618), so on this chip it evidently does
  not. One run each; not chased.
- **The pixel-centre move in the engine changed no V9XD32 value,** T10's
  guard-band samples included.

## What the evidence disputes or leaves open

- **249,705 indexed calls declined for state at DDI 6,** against 9,552 at
  DDI 5 on boot 81. The state decline covers both flat shading and a
  refused map, and the counter does not say which. Those calls are
  expanded through the list path, which draws them correctly. The cause
  is not established.
- **The DECALALPHA overflow** declines those batches to stage 0 alone:
  a wrong picture for that one combination with fog or specular, counted
  rather than silent. 3DMark 2000 declined none.
- **Frames not watched,** as for the earlier Haswell records.
- **Ivy Bridge** takes all of this through the shared engine table; not
  run.
