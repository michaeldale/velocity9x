# Intel GM965 texture pool in the driver: mapped at boot 56, the probes pass, mip trees sample from it

Date: 2026-11-14
Machine: TOMMOHP (`M-KYHT4Q`, Tom Rossell's HP laptop, GM965 GMA X3100
`8086:2A02`, subsystem `103C:30C0`), through remote help, boot 56.
Build: `eae139be-dirty` on the `gm965-texture-pool` branch (dirty by
untracked evidence only), all eight intel-gma files by
`run-release-checks.ps1`, 8 of 8 hashes matching, `reset`.
Gate: `2026-11-14-intel-gm965-texture-pool-decisions.md`.
Evidence: `docs/probe/gm965-texture-pool-driver-2026-11-14/`.

Boot 1 of the ten the gate's conditions ask for.

## What the boot shows

| | |
|---|---|
| `V9XBOOT.INI` | `Stage=enable-ok`; `DpmiMap=04000000:ok`; `MtrrWrite=ok s=3 b=d0000001 m=fc000800` (64 MiB write-combined at `D0000000`) |
| `[IntelPool]` | `Result=kept`, `StateBefore=2`, `PlanOffset=00800000`, `PlanBytes=02000000`: mapped by an earlier Enable in this boot and kept by the one that wrote the record |
| GTT inventory | entries `0`-`77E` stolen memory, `77F`-`7FF` the scratch page, **`800`-`27FF` one linear run from `0450E000`** (the pool, 32 MiB physically contiguous), `2800` up the scratch page |
| heap | unchanged, `vram=7798784` |

## What ran on it

- The release probe set: V9XD32 default, V9XGLP, V9XGLIDP (0 failures),
  V9XR3DP, V9XWPRS, V9XFBCMP pass; then V9XD32 `/bpp16` and `/z24` pass.
- `V9XHEAP.EXE`: textures see `0247C000` (36.5 MiB) where off-screen and
  Z surfaces see `0047C000` (4.5 MiB). 4,096 64x64 textures created, the
  probe's own limit, without a refusal; the highest at `E7989000`, inside
  the pool's range (`E5E07000` up, the address Phase 2's probe mapped it
  at). No off-screen or Z surface above `E5C75000`.
- V9XD32's T21 samples four levels of a mip tree and reads each back
  exactly (`F800`, `07E0`, `001F`, `FFFF`) at both depths. The Gen4 HAL
  places mip trees itself through `d3d_place.c`, which tries the pool
  first; so those levels were sampled from pool memory. That is by the
  placement's order: no address was logged to show it.

## Differences from Gen3 seen here

- DirectDraw places a plain (single-level) texture itself on Gen4, and it
  fills heap 0, stolen memory, before heap 1: V9XHEAP's first texture is
  at the start of heap 0, where the netbook's was at the pool's first
  byte. Small textures therefore still compete with targets and Z for
  the 4.5 MiB until it is full. Not changed.

## Not established

- A GPU read of the pool proved by address, or by a texture known to be
  DirectDraw-placed there.
- Erratum 4's corruption under load: no game, no 3DMark on this build.
- A mode change, a full-screen DOS box, and nine more boots.
- The blue screen of the first Phase 2 probe run remains unexplained.

## Boot 57: 3DMark 2000, a full default run

The same eight files installed again by the runner (8 of 8), `reset`,
boot 57: `[IntelPool] Result=kept`. Boot 2 of ten. 3DMark 2000 v1.1 Pro,
default benchmark (1024x768x16, 16-bit textures, triple buffer), watched
by FlipHandled, V9XTRACE snapshots before and after, ten frames grabbed
(`boot57-3dmark2000/`).

| | Boot 57, pool | Boot 53, no pool, write-combined (2026-11-13) |
|---|---|---|
| Score | **1706** | 1224 |
| Run time, flips | 5.7 min, 7,328 | 6.3 min, 5,687 |
| Texture creates | 1,629 | 317,019 as that snapshot counts them |
| Mip trees placed / declined for room | 1,453 / 236 | 212,155 / 199,452 |
| Last mip tree's offset | `BB4000`, in the pool | `665000`, stolen memory |
| Engine stalls, draws into the presented page | 0, 0 | 0, 0 |
| Batches the engine refused | 78, all reason 13 | 0 |

- **The pool is where the trees go**: the last tree placed is at aperture
  offset `BB4000`, past the 8 MiB stolen run. This is the address the
  boot-56 section lacked.
- **3DMark stops recreating its textures.** Without the pool nearly every
  mip tree was declined for room and the benchmark made 317,019 texture
  creates in a run; with it, 1,629. The score follows (recorded, not
  chased; boot 53's column is a different build and its snapshot's
  baseline was not checked, so the pair is a direction).
- **The ten grabbed frames look right**: textured terrain, trees, the
  helicopter and the HUD, no garbage or missing texture
  (`grabs-sheet.png`). All ten are from the first game test.
- **236 trees were still declined for room** (reason 4) with 32 MiB of
  pool. Not looked into: fragmentation, or trees larger than what is
  free at that moment.
- **78 batches refused, reason 13** (`V9X_D3D_G4_REFUSE_STATE`: the SF or
  WM kernel would not write, or a VS/SF/WM/CC state would not encode),
  of 1,860,541. Boot 53 had none. The texture's address is not part of
  that path, so this is not the pool's memory; the likely reading,
  untested, is a kernel or sampler combination 3DMark only reaches now
  that its trees are placed and mip-filtered. Which draws, and whether
  it shows on screen, is not known. Open.

The seven probe runs after it pass (V9XD32 default and `/z24`, V9XGLP,
V9XGLIDP, V9XR3DP, V9XWPRS, V9XFBCMP).

## Boot 58: the 78 refusals were a drain that gave up after 20 ms

Reason 13 has two sources in `v9x_d3d_g4_write_state`: a state block
that will not encode, and `v9x_d3d_g4_write_sf`/`write_wm` returning 0.
The latter happens only when a WM kernel slot must be overwritten and
`v9x_d3d_g4_drain_all` fails. That called the shared
`v9x_d3d_i9xx_render_drain(1)` once: it polls the status page
`V9X_I9XX_BREADCRUMB_POLLS` (20,000) times and returns BUSY if the
engine is still running. Flip and Lock pass BUSY back to DirectDraw,
which retries; a draw had nobody to retry, so it was refused. With the
pool, 3DMark keeps its trees and submits faster, so the engine is more
often still busy when a new kernel combination arrives.

The change (`d3d_g4.c`): `drain_all` loops while the result is BUSY.
The shared drain still abandons the engine after
`V9X_I9XX_DRAIN_ABANDON_POLLS` without progress, which ends the loop. A
refusal that does follow an abandoned drain is now counted as reason 14
(`V9X_D3D_G4_REFUSE_DRAIN`) instead of 13. The HD 4600's HAL is
separate and not touched; the change reaches every Gen4/Gen5 chip.

Boot 58, the same 3DMark 2000 default run:

| | Boot 57 | Boot 58, the change |
|---|---|---|
| Score | 1706 | 1705 |
| Batches refused (reason 13 / 14) | 78 (78 / 0) | **0** (0 / 0) |
| Drains abandoned | 0 | 0 |
| Batches submitted | 1,860,541 | 1,861,289 |
| Engine stalls, draws into the presented page | 0, 0 | 0, 0 |

The seven probe runs after it pass. Not established: whether the 78
were visible on screen at boot 57; that this was the only route to
reason 13 (the other, an encode failure, would show as 13 again).

## State left behind

TOMMOHP runs the drain change at boot 58 with the pool mapped, 3DMark
2000 open at its score window. `V9XHEAP.EXE` is in `C:\V9XREL`.
