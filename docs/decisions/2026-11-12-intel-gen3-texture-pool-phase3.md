# Intel Gen3 texture pool, Phase 3: the driver maps 32 MiB of system RAM at boot and the GMA 950 samples textures from it exactly

Date: 2026-11-12
Machine: MICHAEL-NETBOOK (945GSE `8086:27AE` rev 03), boots 318 and 319,
build `b51e96d4-dirty` (this record's commit). Windows sees 1 GiB of RAM
(`MaxPhysPage=40000`).
Plan: `docs/plans/intel-gen3-system-memory-textures.md`, Phase 3 design
(agreed by Michael, "Yes, build it"), with Phase 4's heap and placement
folded in.
Gate: `2026-11-12-intel-gen3-texture-pool-decisions.md`.
Evidence: `docs/probe/gen3-texture-pool-phase3-2026-11-12/`.

## What was built

- **Mini-VDD** (`loader.asm`): `V9XMINI_FN_I9XX_POOL_INFO`, `_ALLOC`,
  `_CHUNK` and `_MAP` (`V9XMAPI.INC` has the contract). The G3PM probe's
  mechanics: the entries must all hold one value before; fixed, zeroed
  `PG_SYS` pages; `WBINVD`; one PTE per page; every PTE read back, with the
  scratch PTE restored and the pages freed on a mismatch. It refuses any
  device but `8086:27AE` and checks each page is present and outside stolen
  memory itself.
- **DRV** (`intel_pool16.c`, LEAFCODE): after the GTT inventory, size the
  pool with `v9x_i9xx_pool_size`, allocate, stream every page through the
  new `v9x_i9xx_pool_check_page`, map; keep a pool a ReEnable finds already
  mapped. `V9X3D OFF` skips it. `[IntelPool]` keys in `INTELGTT.TXT` say
  what happened. The policy now sizes the pool in four-page grains.
- **Shared ABI 2026111201**: `V9X_DD_ENGINE.pool_offset` and `pool_bytes`,
  `V9X_DD_SHARED.heaps[2]`. `dd16.c` publishes heap 1 over the pool,
  textures only, and counts it in `dwVidMemTotal`.
- **HAL**: `d3d_place.c` tries the pool first for a texture that is not a
  render target or Z buffer, then stolen memory, and frees by the heap a
  block came from. Gen3's texture and cube binds use
  `v9x_surface_sample_offset` and bound a footprint by the end of the range
  it starts in. `GUID_DDMoreSurfaceCaps` answers one record per heap.

## Results

| | |
|---|---|
| boot 318 | `Stage=enable-ok`; `[IntelPool] Result=mapped`, 32 MiB at aperture 8 MiB |
| boot 319 (runner's install) | mapped at boot; a later Enable found it mapped: `Result=kept` |
| GTT inventory after the pool | `PASS`; entries `800`-`27FF` one linear run from `03346000`: allocated at boot, the 32 MiB came back physically contiguous |
| `V9XHEAP` | textures see `02587000` (37.5 MiB), off-screen and Z `00587000`; first texture at the pool's first byte (`E61D2000`); 3,201 64x64 textures against 390 before; no off-screen or Z surface in the pool |
| V9XD32, 32 bpp, 16 bpp, `/ddi6`, `/ddi5` | all PASS, `Failures=0`: T6, T22, T21 (mip levels), T16, T19 (pixel shaders, four textures, cube, normal maps), T15 sentinels |
| V9XGLP, V9XGLIDP | PASS; every sub-check identical to the 0.48.0 checks |
| V9XR3DP, V9XFBCMP, V9XWPRS | COMPLETE / PASS / PASS; R3D differs from 0.48.0 in a generation counter only |
| Direct3D 8 `AvailableTextureMem` | `0x02600000` (38 MiB) |
| 3DMark 2001 SE, 640x480, 16-bit colour, 32-bit textures, all 17 tests | completed, 1038 (recorded, not compared) |

The release-check runner did the install and the probe runs (one-machine
list, output outside the tree; its summary is `runner-summary.md`).

## 3DMark 2001 SE's counters (V9XTRACE after the run)

- `MipTreeLastOffset=0x00800000`: the HAL was placing mip trees in the
  pool.
- `MipTreeDeclined=118`, last reason 4 (no room). The 2026-11-07 issue
  (`docs/issues/2026-11-07-gma950-3dmark2001se-dragothic-sky-white.md`)
  measured 7,486 in a run at 16-bit colour with 16-bit textures. This run
  had 32-bit textures, twice the bytes per texel. The builds and settings
  differ, so this is a direction, not a controlled comparison.
- `D3dTextureRefusedBounds=55`: chains the HAL declined for room were
  placed by DirectDraw level by level, and Gen3 cannot sample a level at
  a sub-page offset (the last at `E7D79280`, inside the pool). This path
  existed before the pool; how often it was taken before is not measured.

## What this establishes

The GPU samples system RAM through the GTT correctly on the 945GSE: every
texture probe reads back exactly with its textures in the pool. No hang in
about 25 minutes of probes and a full 3DMark 2001 SE run. The pool
survives a mode change in the driver as Phase 2 found it does in a probe.

## Not established

- Erratum 12 staying quiet under longer load: one 3DMark run, no games.
  Serious Sam's demo, Half-Life and Quake 2 (the plan's list) are owed.
- Whether the Dragothic sky (the issue above) draws now: not looked at.
- A DOS box with the pool mapped (the DOS-box return hard-locked this
  machine before the 2026-11-10 fix; not run).
- Allocation timing: `_PageAllocate` ran at Enable, not `Device_Init`.
- Any other Gen3 chip: the gate keeps the GMA 900, GMA 3150 and G33 without
  a pool until each has its own record.

## State left behind

The netbook runs this build (boot 319), 3DMark 2001 SE's window open. Its
`V9XDDH.INI` still has a `[Velocity9xPoolProbe]` section from the Phase 0
measurement build; nothing reads it but V9XHEAP's report header.
