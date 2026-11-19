# Haswell texture pool, Phase 3: the driver maps 32 MiB of RAM at boot, Direct3D samples textures from it, and Car Chase no longer drops to 1 fps

Date: 2026-11-19. Machine: the i5-4570, HD 4600 (`8086:0412`, subsystem
`1028:05A4`), boots 286-287, 2 GiB RAM, desktop 800x600x16. Plan:
[intel-haswell-system-memory-textures.md](../plans/intel-haswell-system-memory-textures.md),
Phase 3. Earlier phases: [0](2026-11-19-haswell-texture-pool-phase0.md),
[2](2026-11-19-haswell-texture-pool-phase2.md). Evidence:
[`../probe/hd4600-texture-pool-phase3-2026-11-19/`](../probe/hd4600-texture-pool-phase3-2026-11-19/).

## What was built

The Gen3/GM965 pool's driver path, taught Haswell:

- **Mini-VDD** (`loader.asm`, `I9XX_POOL_*`): the HD 4600 gated to the
  i5-4570 by device and subsystem (decision 3); its GTT is
  `HSW_ENGINE_OPEN`'s mapping (`V9xHswGttLinear`), not the Gen3 capture's;
  every entry the pool takes is saved, and a failed map puts each back
  (Haswell's entries are junk, Phase 0), with Gen3's uniform check kept for
  Gen3 and Gen4 only; the map's own page check also refuses the 2 MiB below
  BDSM where the GTT lives. PTEs `frame | 1`, uncached (decision 2).
- **DRV** (`intel_pool16.c`): a Haswell branch reads GGC and BDSM as the
  engine open does and builds the layout through `v9x_intel_hsw_pool_layout`
  (Phase 1); the rest (sizing, per-page check, ALLOC, CHUNK, MAP, "kept" on
  ReEnable, `V9X3D OFF`) is the Gen3 path. `intel_hw16.c` calls it on the
  Gen7 path after `HSW_ENGINE_OPEN`; the HD 4600's descriptor gains the
  `engine_pool` hook, so `dd16.c` publishes heap 1, textures only.
- **HAL** (`d3d_hsw.c`): the texture bind takes `v9x_surface_sample_offset`,
  which admits the pool; targets and Z keep stolen memory. Placement
  (`d3d_place.c`) already asks heap 1 first for a texture.

## Results

`INTELGTT.TXT` after the release runner's install and probes (boot 287):
`[IntelPool] Ggc=0209 Bdsm=DD200000 MappedBytes=04000000
PlanOffset=02000000 PlanBytes=02000000 StateBefore=2 Result=kept`: an
Enable earlier in the boot mapped the pool, and later Enables found it.

Every release probe passed on it: V9XD32 at its default DDI and at DDI 7,
6 and 5, V9XGLP, V9XGLIDP, V9XR3DP, V9XWPRS, V9XFBCMP. A snapshot after
them reads `MipTreeLastOffset=0x02000000`, the pool's first byte, with no
mip tree declined: V9XD32's textures were sampled from system memory, and
its texture tests compare drawn pixels exactly.

3DMark 2001 SE, vsync on, two minutes from the start (Car Chase Low, then
High), against run H-on of the vsync record, same machine, no pool:

| | Before (H-on) | With the pool |
|---|---:|---:|
| mip trees refused for room | 185 | **0** |
| texture locks | 18,294 | **3,574** |
| flips | 3,999 | **5,075** |
| 3DMark's meter, Car Chase Low (OBS) | 7, 26, 57, 58, 29, 48 | 58, 14, 58, 43, 58, 47 |
| 3DMark's meter, Car Chase High (OBS) | 34, 1, 1, 18, 1 | 58, 29, 29, 58, 30, 23 |

`MipTreeLastOffset=0x028B5000`, inside the pool. The 1 fps stalls are gone;
the remaining dips are 14-30 fps.

## Not established

- The pool's map at the first Enable of a boot was not captured: the record
  shows `kept`. A boot that refuses would say which rule refused.
- Every other Haswell, Ivy Bridge (HD 4000), and any 64 MiB-stolen board:
  the gate refuses them (`not-gated-device`, or the mini-VDD's subsystem
  check).
- Games beyond 3DMark 2001 SE's first two tests; a full 3DMark run; a DOS
  box; vsync off with the copy target now that the heap has more room.
- Whether the OpenGL ICD's textures can land in the pool and be sampled:
  V9XGLP passed, but where its textures went was not read.
