# Merging state-free DrawPrimitives records would cut Half-Life's batches by about a quarter

Date: 2026-09-30. Machine: MICHAEL-NETBOOK, 945GSE / GMA 950 `8086:27AE`
revision 03, Windows 98 SE, boot 57. Build `5f41300-dirty` (the only local
change was an untracked release extraction), synchronous submission. It
follows
`2026-09-30-halflife-batches-are-small-records-not-culling.md`. Evidence:
`../probe/intel-gma950-r3d-batch-merge-2026-09-30/boot57-halflife-*`.

## What was run

The same view as boot 56: Half-Life 1.1.1.0,
`hl.exe -game valve -d3d -console +map c1a1`, 640x480 Direct3D, the spawn
point, `notarget`, then four `timerefresh` spins at 102.9, 102.9, 103.0 and
102.9 fps (`boot57-halflife-c1a1-timerefresh-console.png`). The V9XTRACE
snapshots before and after cover 17.9 s. `5f41300` counts, for every
`DrawPrimitives` record drawn, the batches a merge of consecutive state-free
records would build (`src/display32/r3d/r3d_runs.h`). Nothing is merged. The
netbook was hard-reset by the operator to take the build, because it hangs
at shutdown. `valve\config.cfg` was unchanged on quit.

## Result

| Counter (difference) | Value |
|---|---|
| `DpRecords` | 269,953 (1.93 triangles, 4.71 a `DrawPrimitives` call) |
| `DpRecordTriangles` | 521,596 |
| `DpRecordRuns` | 196,675 (1.37 records, 2.65 triangles a run) |
| `DpRecordStateBreaks` | 142,181 (72.3% of runs) |
| `Win16D3dDrawPrimsCalls` | 57,307 |
| `R3dListCalls` / `R3dListSinkBatches` | 282,736 / 281,080 |
| `R3dListCulled` | 8,008 (0.99%) |
| refusals, timeouts | 0 |

The list-builder figures repeat boot 56's: 0.99% culled, 0.994 batches a
call, 2.84 triangles a batch.

Merging consecutive state-free records would replace 269,953 record batches
with 196,675: 73,278 fewer, about 26% of all sink batches in the window. The
runs are short, 1.37 records each. Almost every run is started either by a
record that carries state pairs (142,181) or by the start of a buffer. The
second figure, 54,494, is close to the 57,307 `DrawPrimitives` calls. The
64-triangle bound almost never ends a run.

## What it means

The lever exists but is modest: batches would average 2.65 triangles instead
of 1.93. Whether 73K fewer batches is worth anything is arithmetic, not a
measurement. At the plan's figure of about 5 us fixed GPU cost a batch, the
head-wait saving would be about 0.37 s over this window, before build and
write. Pixel cost is unchanged.

The larger limit is the state change. Almost three records in four carry
state pairs, and in Half-Life's world those are most likely texture binds
per polygon. Which pairs they are, and how many set a value the context
already holds, is not counted. The plan excludes comparing state between
batches. If redundant pairs are common, that exclusion is what stands
between 27% and a larger reduction. Measure that before building either.

## Limits

One static view in a small room, as on boot 56. No gameplay run was counted
and no merged build exists, so no performance claim follows.
