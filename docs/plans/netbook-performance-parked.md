# Netbook and Gateway performance: what landed, and what is parked

Date: 2026-10-02. Status: parked. Gen3 textures up to 1024 is the last
change kept; everything below "Parked" is written up and waiting.

## Landed (0.9.2, unreleased)

| Change | Machine | Measured | Record |
|---|---|---|---|
| Asynchronous Gen3 submission, on by default; the dword-head refusal fixed | netbook | Half-Life D3D `mwd5` 29.06 to 42.42 fps; Quake 2 demo2 19.6-19.8 fps | [async](../decisions/2026-10-01-intel-async-corruption-was-the-dword-head.md) |
| ICD vertex path: plane-limited clipping, inline inside test, vertex ring | all (ICD) | `glVertex*` 2.47 to 1.86 us a vertex, output byte-identical | [vertex path](../decisions/2026-10-01-gl-vertex-path-profile-quake2-netbook.md) |
| ICD: environment colour packed when set, glBegin setup kept while unchanged | all (ICD) | texture describe 1.88 to 0.92 us, glBegin 1.65 to 0.77 us; Quake 2 to 21.3-21.9 fps (timers on) | same |
| Gen3 textures to 1024 texels, from 256 | netbook | every size 256-1024 and full 512/1024 chains exact (V9XTSHP); Serious Sam 1.81 to 2.73 fps | [Serious Sam](../decisions/2026-10-02-serious-sam-netbook-profile.md) |
| Half-Life's D3D sky: an undefined texture address taken as CLAMP | Gateway (shared) | sky correct, address refusals 3,183 to 0 | [sky](../decisions/2026-10-01-hl1-d3d-sky-was-texture-address-zero.md) |
| Mach64 rectangular textures | Gateway | probe exact; Half-Life GL texture churn halved | [rectangles](../decisions/2026-10-01-mach64-rectangular-textures.md) |

Instruments added on the way and kept: the HAL submission profile and
ring counters (ABI 2026100103-106), the ICD's stage, sink and `hwno`
timers, `V9XTSHP`'s Gen3 large-texture section, and the Serious Sam
procedure (`docs/probe/serious-sam-netbook-2026-10-02/ssam-run.ps1`,
`sstimeline.py`).

## Parked

### Write-combining the aperture (Stage B)

[Plan](write-combining-stage-b.md). The ring write through the uncached
GMADR is 9.1% of Half-Life D3D and 5.6% of Quake 2 on the netbook, 40-45
ns a dword. The netbook's MTRR readout accepts (`r=0`); Stage A's 4 MiB
window misses the 7.7 MiB the driver reaches, so the rule must round up
inside the BAR. Owed: the Gateway's readout, and two decisions (a
recovery-only switch against the no-gated-defaults rule; scope).

### Textures in system memory through the GTT (DVMT)

[Plan](intel-gen3-system-memory-textures.md). Serious Sam's working set
is 14.1 MiB against about 3.25 MiB of hardware copies; with textures to
1024 its frame is now the copies' create, evict and upload churn (66% of
wall). A 32 MiB pool of locked RAM in the GTT's 63,553 unused entries,
as a texture-only DirectDraw heap. Owed: Phase 0 measurements, an
erratum-12 gate, pool size and recovery decisions.

### Eviction rule

Not worth changing on the evidence. The recorded use order replayed
through a calibrated cache: LRU 0.474, the best possible (Belady) 0.556;
most-recently-used, favoured by a cyclic model, lost on the machine
(1.91 fps against 2.73). About 13 MiB holds the set. The trace build is
`docs/probe/serious-sam-netbook-2026-10-02/icd-use-trace.patch`, the
replay `evictsim.py`.

### Smaller copies under memory pressure

A stopgap for the pool: hold copies of 170 KB or more at a quarter (top
level dropped). The same replay gives LRU 0.721 at 3.25 MiB. It costs
those textures' sharpness and was not built.

### Other levers found and not taken

- **Lightmap uploads drain the GPU.** Quake 2's Lock for each lightmap
  waits for all queued work: 558 us a Lock, 2.6% of wall.
- **Redundant Gen3 state.** 76-80% of submissions repeat the previous
  state block exactly; emitting only changes would shorten each stream by
  about a fifth.
- **The render interface's per-draw cost** behind the ICD's held batch:
  84 us a draw in Quake 2, 38 of it the uncached ring write.
- **The Gateway's split** is unmeasured: the HAL's time buckets run on
  Gen3 only.

## Open questions

- Half-Life D3D 40.89 and Quake 2 20.6-20.8 fps on boot 88, a few percent
  under earlier runs; not separated from that boot's history or the ICD's
  added counters.
- Serious Sam's game-side benchmark (`dem_bProfile`) printed nothing in
  1.05; the frame count comes from the ICD's log.
- Nobody watched Serious Sam's frames.

## Machine state at parking

- **Netbook** (10.0.1.254, boot 88): full Intel set at ABI 2026100106,
  the HAL with textures to 1024 (`5653a86`), the ICD at `bafa3d1`
  (`hwno` counters), async on by default.
- **Gateway** (10.0.1.22): the ATI set at ABI 2026100105 with the
  texture-address fix (`871ebdc`); a version behind the netbook's ABI.
