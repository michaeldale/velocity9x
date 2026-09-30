# Half-Life's small batches are small DrawPrimitives records, not culled runs

Date: 2026-09-30. Machine: MICHAEL-NETBOOK, 945GSE / GMA 950 `8086:27AE`
revision 03, Windows 98 SE, boot 56. Build `010b2d6-dirty` (in-call merging,
`08b4239`), synchronous submission. Plan:
`../plans/r3d-in-call-batch-merging.md`, Phase 0. Evidence:
`../probe/intel-gma950-r3d-batch-merge-2026-09-30/boot56-halflife-*`.

## Result

The Phase 0 stop rule is met for this scene. Very little is culled in the
list builder, and there is almost exactly one sink batch per list call. The
batches are small because the list calls are small, not because culling
splits them. In-call merging as built has almost nothing to merge in
Half-Life.

## What was run

Half-Life 1.1.1.0 (GOTY, `C:\SIERRA\Half-Life`),
`hl.exe -game valve -d3d -console -condebug +map c1a1`, 640x480, Direct3D.
At the spawn point (`boot56-halflife-c1a1-spawn.png`): `notarget`, then
`timerefresh` four times, at 102.0, 102.5, 102.6 and 102.3 fps
(`boot56-halflife-c1a1-timerefresh-console.png`). The V9XTRACE snapshots
before the first spin and after the last cover 67 s. That window includes
console-up frames between the spins, so it describes this view, not the
128-frame spins alone. `-condebug` wrote no log. `valve\config.cfg` was
unchanged on quit. There was no reboot and no control build: the machine was
not rebooting reliably, and none was needed for a count of what is split.

| Counter (difference) | Value |
|---|---|
| `R3dListCalls` | 1,260,296 |
| `R3dListTrianglesIn` | 3,443,794 |
| `R3dListCulled` | 28,467 (0.83%) |
| `R3dListClipped` | 0 |
| `R3dListSinkBatches` | 1,254,548 (0.995 a call, 2.72 triangles a batch) |
| `Win16D3dDrawPrimsCalls` | 257,087 |
| `Win16D3dDrawIndexedCalls` | 43,371 (1,067,778 triangles, 24.6 a call) |
| refusals, timeouts, abandonments | 0 |

`DpPrimTypeSeen=0x50`: the `DrawPrimitives` records are triangle lists and
fans. `V9xD3dDrawPrimitives` calls the list builder once per record
(`src/display32/d3d/d3d_core.c`, the record loop), and a fan is gathered into
its own list. After the indexed calls' share is taken out, the remaining
~1.22M list calls carry ~2.38M triangles: about two triangles a record and
about 4.7 records a `DrawPrimitives` call. That is one world polygon per
record.

## What this disputes

The plan read boot 31's `TimeEngineDrawCalls=933256` against
`IndexedCalls=128580` as 7.3 engine batches an indexed call, and inferred
that something inside the indexed lists was splitting them. Boot 31's own
snapshot (`2026-09-25-netbook-halflife-alpha-test-V9XTRACE.ini`) has
`D3dRenderPrimitiveCalls=199457` beside the 128,580 indexed calls, and the
two sum exactly to `TimeD3dCallsCalls=328037`. The engine batches were spread
over both kinds of call: 2.85 a call, not 7.3 an indexed call. Most of them
are consistent with small `DrawPrimitives` records, as measured here. The
plan's culling inference was wrong at its source, not only in this scene.

## Limits

One static view, spun in place, in a small room. The boot 31 gameplay run
had the same call mix (199K `DrawPrimitives` against 129K indexed), which
suggests the finding generalises, but no gameplay run of this build has been
counted. Merging's correctness when it does run is still unmeasured, as is
the clipped-fan ordering on a clip-in-core engine.

## Consequence for the plan

Merging inside a single list call cannot reduce these batches. A lever that
stays inside one Direct3D call does exist: merge consecutive records of one
`DrawPrimitives` buffer when a record carries no state changes
(`wNumStateChanges == 0`), flushing on any state change and at the end of
the buffer. That keeps contract points 1-3, since nothing outlives the call
and staging never spans a state change. Whether such runs exist, and how
long they are, is unmeasured. A counter of records with zero state changes,
and of the run lengths between changes, would settle it before any code.
