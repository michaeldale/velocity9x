# Haswell texture pool, Phase 2: the pool maps at GTTMMADR + 2 MiB, the aperture reaches its pages, and every junk entry goes back exactly

Date: 2026-11-19. Machine: the i5-4570, HD 4600 (`8086:0412`), boot 285,
2 GiB RAM, intel-gma `0787c4d3` installed. Plan:
[intel-haswell-system-memory-textures.md](../plans/intel-haswell-system-memory-textures.md),
Phase 2: the first GTT writes on Haswell; no GPU register, ring or command
touched. Phase 0: [record](2026-11-19-haswell-texture-pool-phase0.md).
Evidence: [`../probe/hd4600-texture-pool-phase2-2026-11-19/`](../probe/hd4600-texture-pool-phase2-2026-11-19/).

## The tool

`G3PM.VXD` + `V9XG3PM.EXE` (`tools/diag/gen3_pool_map*`), the staged map
probe the GM965 ran ([record](2026-11-14-intel-gm965-texture-pool-phase2.md)),
taught Gen7:

- Haswell 0412/041E and Ivy Bridge 0162/0166 accepted; the GTT at
  GTTMMADR + 2 MiB; GGC decoded as Haswell's (GMS 7:3 in 32 MiB, GGMS 9:8
  in MiB); the GTT's own GGMS bytes below BDSM excluded from the pages, as
  `v9x_intel_hsw_pool_layout` excludes them.
- Every pool entry is saved as found and written back one by one, and the
  write-back is compared entry by entry. Gen3 and Gen4 saved one value and
  refused unless the range was uniform; Phase 0 found Haswell's entries are
  junk, so the uniform check is skipped on Gen7 only.

PTEs are `frame | 1`, the uncached form the BIOS's own entries use.

## Results: PASS at every size, with mode changes

At the plan's entries (`2000h` on, GMADR 32 MiB), each run allocating,
mapping, checking, restoring and freeing:

| Run (`cycles pages stage`) | Result |
|---|---|
| `0 4 1`: PTEs written, read back | PASS, 0 mismatches |
| `0 4 2`: and the aperture range mapped | PASS |
| `0 4 3`: and one dword through it, back through it and the page's own mapping | PASS |
| `0 4 4`: the whole pattern | PASS; restore 0 mismatches, last entry `2538CA43` = Phase 0's `E2003` |
| `0 2048 4` | PASS: write 2 ms, read back through the aperture 125 ms, 0 mismatches |
| `0 0 4`: 8,192 pages, the plan | PASS: write 8 ms, read back 493 ms, 0 mismatches in aperture, RAM and restore |
| `2 0 4`: the plan with two 640x480 mode cycles | PASS: PTEs and pattern 0 mismatches after both cycles, restore 0 |

No run timed out. 8,192 PTEs: about 1.3 million cycles to write and 2.5
million to read back. After all seven runs a read-only `V9XG3PL` found the
pool's span with the same valid count (`1004h`) and hash (`F4B9A0EE`) as
Phase 0: every junk entry back as it was.

## What this settles

- PTEs written at GTTMMADR + 2 MiB stick, and a CPU store through GMADR
  32-64 MiB lands in the system page the PTE names (pattern read back
  through the aperture and through the RAM's own mapping).
- A mode set does not rewrite the pool's entries while they are ours.
- The map and restore are exact over junk entries, so the driver's map can
  save and restore the same way.

## Not established

- The GPU sampling the pool: Phase 3, in the driver.
- The write-through-aperture speed the driver will see: the probe maps the
  pool's aperture range itself (8 ms for 32 MiB here). The HAL's 64 MiB
  mapping is write-combined through PAT, which covers it.
- Allocation and mapping at `Device_Init`.
