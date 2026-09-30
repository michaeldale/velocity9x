# Nine in ten of Half-Life's record state changes change nothing

Date: 2026-09-30. Machine: MICHAEL-NETBOOK, 945GSE / GMA 950 `8086:27AE`
revision 03, Windows 98 SE, boot 58. Build `5da6648-dirty` (the only local
change was an untracked release extraction), synchronous submission. It
follows `2026-09-30-halflife-record-runs.md`. Evidence:
`../probe/intel-gma950-r3d-batch-merge-2026-09-30/boot58-halflife-*`.

## What was run

`5da6648` copies the context before applying a drawn `DrawPrimitives`
record's state pairs and compares the copy afterwards. An unchanged context
means the pairs were redundant, because `v9x_d3d_apply_state` writes only the
context and diagnostic counters. The build counts such records and the
batches the record merge would leave if they did not end a run. Nothing is
merged. The operator hard-reset the netbook to take the build.

The measured run is the view used on boots 56 and 57: Half-Life 1.1.1.0,
`hl.exe -game valve -d3d -console +map c1a1`, 640x480 Direct3D, the spawn
point (`boot58-halflife-c1a1-timerefresh-console.png`), `notarget`, four
`timerefresh` spins at 104.0, 104.1, 104.0 and 104.0 fps. The V9XTRACE
window is 18.6 s. `valve\config.cfg` was unchanged on quit.

## Result

| Counter (difference) | Value |
|---|---|
| `DpRecords` | 279,109 (539,338 triangles) |
| `DpStateRecords` | 150,600 (54.0% of records carry pairs) |
| `DpStateRecordsNoop` | 135,942 (90.3% of those change nothing) |
| `DpStateRecordsTexture` | 14,658 (9.7% change the texture handle) |
| `DpRecordRuns` (a pair ends a run) | 202,771 (1.38 records a run) |
| `DpRecordRunsNoopJoined` (only a change ends a run) | 66,861 (4.17 records, 8.07 triangles a run) |
| `R3dListSinkBatches` | 289,958 |
| `R3dListCulled` | 8,446 (0.9%) |
| refusals, timeouts | 0 |

The plain record merge repeats boot 57: 27.4% fewer record batches (27.1%
there). If records whose pairs change nothing may join a run, the cut is
76.0% of record batches: 212,248 fewer, 73.2% of all sink batches in the
window. Batches would average 8.07 triangles instead of 1.93.

The state changes that do change something are few. Every one of them
changes the texture handle: 150,600 − 135,942 = 14,658 records changed the
context, and `DpStateRecordsTexture` is 14,658. Whether some also changed
another field is not counted. Half-Life resends the rest of its state with
most records, and none of it changes.

## A second run, not the same view

The first launch on this boot did not arrive at that view
(`boot58-halflife-first-launch.png`). Its console shows a `c1a0e` save and
decals being loaded, the spins ran at 61-65 fps, and 14.5% of list
triangles were culled. Where it put the player is not established. In its own frames
the same pattern held: 88.3% of state records unchanged, every changing
record a texture change (13,754 of 13,754), and a 31.9% cut for the plain
merge against 75.2% with unchanged records joining. It is kept as
evidence and not used for the headline figures.

## What it means

Comparing state is what makes record merging worthwhile. The plan excluded
it. For Half-Life, the design to consider is: merge consecutive records of
one `DrawPrimitives` buffer, where a record joins the run unless applying
its pairs changes the context. Flush on a real change, at capacity and at
the end of the buffer. Nothing outlives the call, and a batch is still drawn
under exactly one context state. So it keeps the contract's order, lifetime
and one-state rules without holding anything across calls.

This is arithmetic, not a measurement: 212K fewer batches at the plan's
~5 us fixed GPU cost is about 1 s over this 18.6 s window, before build and
write, with pixel cost unchanged. Only a merged build measured against this
one will say. It is also one static view: no gameplay run was counted, and
the merge's correctness on a clip-in-core engine is still unchecked.
