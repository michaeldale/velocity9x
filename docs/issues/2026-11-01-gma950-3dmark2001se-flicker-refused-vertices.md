# GMA 950, 3DMark 2001 SE: flicker in every test, and batches refused for their vertices

Date: 2026-11-01. Seen by Michael on the netbook's panel (W9QI2BE2, 945GSE,
boot 266), first during the guard-band step 2 run, "noticed flicker in
2001se ... not sure if that was there", in Dragothic, then "looks like it
impacts all tests". Seen again with the guard variant off (mask 0, the
shipped path): "still flicker". Status: open, not investigated beyond the
counters below.

Build `3c736490-dirty` (main's code plus the measurement-only guard band,
off). 3DMark 2001 SE default benchmark, 640x480x16, all game, theoretical
and DX8 feature tests.
Evidence: [`../probe/gen3-guard-band-2026-11-01/step2-3dmark2001-mask0/`](../probe/gen3-guard-band-2026-11-01/step2-3dmark2001-mask0/)
and `step2-3dmark2001/` (the band run, aborted).

## Counters

| | mask 0 (shipped), whole run | band 1,024, first 230 s |
|---|---:|---:|
| submissions | 1,284,048 | 716,123 |
| batches refused by the Gen3 engine | 2,557 (6.4/s) | 1,505 (6.5/s) |
| last refusal reason | 6, `V9X_I9XX_REFUSE_VERTICES` | 6 |
| triangles clipped in the core | 9,064,485 | 0 |
| draws into the displayed buffer, flip waits, timeouts, resets | 0 | 0 |
| score | 678 (recorded, not compared) | aborted |

## Readings

- **Refused batches are dropped geometry**: a refused batch draws nothing
  that frame, and which batches are refused changes as the camera moves.
  That is the shape of the flicker seen, in every test.
- **The guard band is not the cause**: it refuses at the same rate as the
  shipped path, and with the core clipping x and y (mask 0) the refusals
  remain.
- **Not the HD 4600's 0.34.0 fault**: no draw lands in the displayed
  buffer and no draw waits on a flip.
- Reason 6 is the run builders refusing a vertex. With x and y clipped by
  the core, the remaining checks are Z in [0, 1], RHW positive and finite,
  and texture coordinates finite. **Which one is not measured**; the
  counters do not split reason 6.

## Not established

- Whether 0.34.0 or earlier releases flicker here too (not run).
- Which vertex field fails, and which tests refuse most.
- Whether this is UT99's "77 batches a run refused with reason 6"
  ([2026-10-07](../decisions/2026-10-07-gen3-clip-in-core.md)) at a larger
  scale.

## 2026-11-01, later: the field is the texture coordinates

Build `d68b5bf2-dirty`: `d3d_i9xx.c` now classifies a batch refused for a
vertex by repeating the run builders' checks on it, in their order, and
counts the first failure (diagnostics appended within ABI 2026102002:
`I9xxVertexRefusedXy/Z/Rhw/Uv`, the check and the last such vertex's x, y,
z, rhw bits). Boot 267, all eight files hash-matching; V9XD32 `/ddi6`
`/ddi5`, V9XGLP, V9XGLIDP pass, V9XR3DP completes. 3DMark 2001 SE default
benchmark, mask 0, whole run, Michael watching: still flicker, also in the
menu transitions. Evidence:
[`../probe/gma950-3dmark2001se-refused-2026-11-01/`](../probe/gma950-3dmark2001se-refused-2026-11-01/).

| | count |
|---|---:|
| batches refused for a vertex | 2,519 |
| x or y off the target | 0 |
| z outside [0, 1] | 0 |
| rhw not positive and finite | 0 |
| **a texture coordinate not finite** | **2,519** |

The last refused vertex: x 583.43, y 191.13, z 0.9397, rhw 1.2258, so an
ordinary on-screen vertex, not one the core's clipper made (those lie on an
edge). Its tu and tv were not recorded.

Readings:

- **Every refusal is a texture coordinate that is infinite or NaN.** The
  builder refuses those as naming no place on a texture
  (`i9xx_vertex.c`), and the whole batch is lost.
- **The FVF conversion does not invent them**: a coordinate set the vertex
  lacks is written as 0.0f (`v9x_dp2_fvf_convert_set`,
  `v9x_dp2_fvf_texcoords`). So the non-finite values most likely come from
  the application or the runtime's software T&L. Inferred, not measured.

Still not established: the tu/tv bits themselves (NaN or infinity), which
tests send them, and whether the menu-transition flicker is the same
refusals.

## 2026-11-01, later: refusals fixed, flicker not

**Non-finite texture coordinates are now drawn as 0.0f** (`d3d_i9xx.c`,
`v9x_d3d_i9xx_finite_uv`, counted in `I9xxUvZeroed`, appended within ABI
2026102002), for both units, instead of the builder refusing the batch.
The builder's check stays as the second opinion. A coordinate decides only
where the sampler reads in the bound map; where the engine writes is the
position's, still checked. Boot 268, all eight files hash-matching; V9XD32
`/ddi6` `/ddi5`, V9XGLP, V9XGLIDP pass, V9XR3DP completes.
Evidence: [`../probe/gma950-3dmark2001se-uv-zeroed-2026-11-01/`](../probe/gma950-3dmark2001se-uv-zeroed-2026-11-01/).

| 3DMark 2001 SE, whole benchmark, mask 0 | run 1 | run 2 |
|---|---:|---:|
| batches refused | 0 (2,519 before) | 0 |
| coordinates zeroed | 6,776 | 6,762 |
| flips; draws into the displayed buffer; timeouts; resets | 4,556; 0; 0; 0 | 4,557; 0; 0; 0 |

**Michael, watching both runs: "Still flicker".** So the refused batches
were not the flicker (or not all of it). The change stays: it stops
geometry being dropped.

**Frame grabs** (`V9XTRACE -grab:300x10`, armed before the benchmark;
every 300th flip, about every 27 s; boot 268): ten complete frames - Car
Chase, Dragothic, Lobby and the loading screens between tests - none half
drawn, garbled or from another test
(`../probe/gma950-3dmark2001se-grabs-2026-11-01/spread-300/contact-sheet.png`).
Two loading screens are darker than the others, which matches 3DMark's
fade between tests. The grab drains the engine before it reads, so this
rules out a stale or wrong buffer being presented at those flips, and says
nothing about a frame latched before its drawing finished. The full BMPs
were not kept (9.5 MB); the sheet was.

## Parked, 2026-11-01

At Michael's word, on branch `park/gen3-batch-cost`. Hypotheses left:

- **A frame latched before its drawing finished.** Gen3 submits
  asynchronously; whether a flip can reach the panel before the batches
  of its frame have drawn is not measured. The grab cannot see it. A
  synchronous control (`IntelAsyncSubmit=0` if still honoured, or the
  2026-10-01 build) under 3DMark 2001 SE would test it.
- **A regression.** Not run on 0.34.0 or earlier releases.
- Not every flicker need be one cause: the menu transitions flicker too.
