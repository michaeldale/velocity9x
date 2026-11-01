# Gen3: neither the texture flush nor the repeated state is worth cutting

Date: 2026-11-01

Machine: the netbook (W9QI2BE2, 945GSE / GMA 950, Atom N280 1.66 GHz),
boot 261, build `07693792-dirty` (main plus the omit bits below; the dirt
is that change and untracked evidence folders), all eight intel-gma files
by WININIT rename, every hash matching.

Plan: [intel-gen3-batch-cost.md](../plans/intel-gen3-batch-cost.md), step 1,
run at Michael's request after
[step 0](2026-11-01-gen3-batch-cost-step0-profile.md) found the
submission CPU-bound.
Evidence: [`../probe/gen3-batch-cost-step1-2026-11-01/`](../probe/gen3-batch-cost-step1-2026-11-01/).

## The variants

Two measurement-only bits in the mask `V9XTRACE -omit:<mask>` stores
(`hsw_flush_omit`, outside the trace snapshot; zero at boot and zero is the
shipped batch), read by `v9x_d3d_i9xx_draw_triangles_body`:

- **0x40 TEXTURE**: no leading `MI_FLUSH_READ`.
- **0x80 STATE**: the stipple, state block, constants and program left out
  when they equal what the last draw submission sent and no other
  submission (blit, ring flip, status-page proof) or new session came
  between. The full stream is built and decoded first; the region is cut
  after, the primitives and seal moved down over it. `I9xxStateDwords`
  counts the state actually sent.

Each mask: V9XD32 `/ddi6`, DB3D at 640x480x16 (dispbench `082ccfd`), and
Half-Life mwd5 D3D full screen, launched by the script: one warm-up
timedemo, then V9XTRACE snapshots either side of a second. One run each.

## Correctness

| mask | V9XD32 /ddi6 | DB3D | Half-Life |
|---|---|---|---|
| 0 | PASS | all verified but FillSpecular | ran |
| 0x40 | PASS | same | ran |
| 0x80 | PASS | same | ran |
| 0xC0 | PASS | same | ran |

FillSpecular fails its check in every run, step 0's on `94063db2`
included: a standing failure, not the variants'. Mask 0 also passed V9XD32
`/ddi5`, V9XGLP and V9XGLIDP, and V9XR3DP completed, right after the
deploy. Half-Life's frames were not looked at.

V9XD32 passing under 0x80 is the one new hardware fact: **the 3D state
survives the breadcrumb's `XY_COLOR_BLT` and the two `MI_FLUSH`es between
batches**, as far as V9XD32's read-backs reach. Gen3 has no public 3D
reference saying so.

## Half-Life, per submission (the profiled warm demo)

| | 0 | 0x40 | 0x80 | 0xC0 |
|---|---:|---:|---:|---:|
| timedemo fps | 47.67 | 47.54 | 47.78 | 47.47 |
| whole HAL entry, µs | 45.2 | 45.4 | 46.0 | 46.2 |
| front end | 19.2 | 19.3 | 19.3 | 19.3 |
| build | 10.2 | 10.2 | **12.2** | **12.2** |
| decode | 5.3 | 5.3 | 5.4 | 5.4 |
| ring write | 2.6 | 2.6 | 2.3 | 2.3 |
| ring-space wait (GPU behind) | 5.7 | 5.7 | **4.6** | 4.7 |
| render drain | 2.2 | 2.2 | 2.2 | 2.3 |
| state dwords sent | 63.4 | 62.4 | **19.2** | 18.8 |
| state identical to previous | 75.4 % | 75.4 % | 75.7 % | 74.9 % |

The warm demo repeats its state 75 % of the time; step 0's cold run, 57 %.

## DB3D, cycles (640x480x16)

| row | 0 | 0x40 | 0x80 | 0xC0 |
|---|---:|---:|---:|---:|
| TriBatch.1 | 4,382 | 4,346 | 4,590 | 4,594 |
| TriBatch.64 | 2,025 | 2,012 | 2,200 | 2,205 |
| TriBatch.100.V32 | 1,822 | 1,815 | 2,022 | 2,002 |
| BatchCost.Tiny | 4,419 | 4,348 | 4,589 | 4,726 |
| StateChange.Texture.Static.1 | 4,981 | 5,476 | 5,246 | 5,256 |
| StateChange.Texture.Change.1 | 26,986 | 26,037 | 27,829 | 27,392 |
| StateChange.Blend.Static.16 | 3,072 | 3,051 | 3,288 | 3,273 |
| StateChange.Blend.Change.16 | 3,433 | 3,449 | 3,561 | 3,460 |

Every row is in the INI files; this is a sample. Step 0's run and this
run's mask 0 on the same rows differ by up to 13 % (Depth.Change.16), so
a few percent is inside one run's noise. DB3D's HAL split was not
recorded (its snapshot size check, step 0's record).

## Readings

- **The texture flush costs nothing measurable.** 0x40 is mask 0 within
  noise in DB3D and Half-Life alike. The flush stays: it is what keeps
  Final Reality's rewritten textures from showing stale texels (intel76),
  and leaving it out buys nothing.
- **Cutting the state saves the GPU about 1 µs a submission and costs the
  CPU about 2.** With 70 % of the state dwords gone, the ring-space wait
  fell 1.1 µs and the ring write 0.3 µs; the build grew 2.0 µs (the compare,
  the move of the primitives over the cut, the copy kept for the next
  compare). DB3D's rows are 4 to 11 % slower with it. Half-Life's fps did
  not move either way.
- **A cheaper cut would still save little.** Deciding before the build, so
  nothing is moved, could at best remove the build and decode of the state
  region, a part of 10.2 + 5.3 µs that was not split out, and the 1.1 µs of
  GPU parse. Against the 45 µs a submission costs and 19 µs of it in the
  core's front end, that is not where the time is.
- **Both disputed:** the 10-01 record's "emitting only changes would remove
  about a fifth of the stream ... what would let consecutive same-state
  draws become one submission" is right about the dwords and wrong about
  the time: dwords are cheap on this ring since write-combining.

## Consequence

By the plan's step 1 rule, the plan stops here. Neither bit ships; the
draw path keeps sending the whole state and the flush on every batch. The
omit bits stay in the tree as measurement tools, zero by default, as the
HD 4600's do. The netbook is left on this build with the mask at 0.

## Not established

- One run per mask.
- Half-Life's frames under 0x40 and 0x80 were not inspected; V9XD32 is
  the correctness evidence.
- OpenGL was not run under the variants (its state repeats 8 %, step 0).
- 0x80 does not clear its copy on a mode set or the DOS-box return; that
  would have been step 2's work.
