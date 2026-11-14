# Intel Gen3 texture pool, Phase 0: 32 MiB allocates, 64 MiB maps, the runtime keeps a texture heap to textures

Date: 2026-11-12
Machine: MICHAEL-NETBOOK (945GSE `8086:27AE` rev 03, 1 GiB RAM as Windows
sees it under `MaxPhysPage=40000`), boots 316 and 317.
Plan: `docs/plans/intel-gen3-system-memory-textures.md`, Phase 0.
Decisions and gate: `2026-11-12-intel-gen3-texture-pool-decisions.md`.
Evidence: `docs/probe/gen3-texture-pool-phase0-2026-11-12/`.

Nothing was written to the GTT. The pool policy (`src/common/i9xx_pool.c`,
host-tested in `tests/host/test_i9xx_pool.c`) was written first and judged
the real page lists below.

## The tools

- **`G3PL.VXD` + `V9XG3PL.EXE`** (`tools/diag/gen3_pool_probe*.{asm,c}`,
  `scripts/build-gen3-pool-probe.ps1`). A dynamic VxD that reads GTT
  entries through BAR3, then `_PageAllocate`s the pool's pages as
  `PG_SYS`, `PAGEFIXED | PAGEZEROINIT`, copies their page table with
  `_CopyPageTable` and frees them. The publisher sizes the pool with the
  driver's policy, sorts the frames and runs `v9x_i9xx_pool_check` on them.
- **`V9XHEAP.EXE`** (`tools/diag/ddraw_heap_probe_win32.c`,
  `scripts/build-ddraw-heap-probe.ps1`). Creates 64x64 surfaces of one kind
  in video memory until DirectDraw refuses, locks each for its address,
  and counts those in heap 1.
- **A measurement DRV, not committed**: `dd16.c` split the top MiB of
  the Gen3 heap off as heap 1, with `ddsCaps` = OFFSCREENPLAIN |
  PRIMARYSURFACE | BACKBUFFER | FLIP | ZBUFFER | 3DDEVICE (`00022254`), on
  the 16-bit copy handed to SetInfo only. Built as `6b3e1bd6-dirty`
  together with the committed 64 MiB map below; DRV, VxD, HAL, SETP and GL
  went on by WININIT rename (hashes match the package), `reset`, boot 317.

## Findings

**1. `_PageAllocate` gives 8,192 fixed pages in 34 ms, at run time.**
Two runs on boot 316, one before and one after the mode cycles below:

| | Run 1 | Run 2 |
|---|---|---|
| free pages before / with the pool / after freeing | `34BB3` / `32BAB` / `34BB3` | `34BA1` / `32B99` / `34BA1` |
| allocation | 34 ms | 34 ms |
| frames | `03331000`-`0E9D8000`, 152 runs, longest 1,024 pages | same range, 153 runs |
| below 16 MiB / not present | 0 / 0 | 0 / 0 |
| `v9x_i9xx_pool_check` | accepted | accepted |

Each allocation took 8,200 pages off the free count (the 8,192 plus the
VMM's own), and freeing gave every one back. The page table's low bits
read `607` (present, writable, user plus the VMM's two allocation bits).
This was a dynamic VxD after boot. Allocation at `Device_Init`, which the
plan proposed, is not measured; with 210,000 pages free at run time there
is no sign it would be harder.

**2. The entries the pool will use all map the VBIOS's scratch page, and a
mode set does not visibly change them.** Entries `7E0`-`281F` (the last 32
of the stolen run's space, then the pool's 8,192) all read `7FFBE001`,
the fill page the Phase 2 inventory found
(`2026-09-12-intel-phase2-gtt-inventory.md`). Span hash `C5F0C605` before
and after `V9XMSW /cycle:2` (two 640x480 / 800x600 round trips, `PASS`).
This does not show that a mode set leaves the entries alone: with every
entry already the scratch value, a VBIOS that rewrote them would look the
same. That question moves to Phase 2, which can read the pool's own PTEs
back after a mode set.

**3. The DPMI host maps 64 MiB of GMADR, and the WC MTRR covers it.** Boot
317: `DpmiMap=04000000:ok`, `MtrrWrite=ok s=2 b=d0000001 m=fc000800` (64 MiB
write-combined at `D0000000`), `Stage=enable-ok`, heap unchanged
(`VddReserve ... vram=8060928`). The map is 64 MiB, not the 48 decision 1
named. A power of two keeps one MTRR over the whole mapping. The pool
stays 32 MiB, from GMADR 8 MiB to 40 MiB.

**4. The 98 SE runtime keeps a texture-only heap to textures.**
`V9XHEAP.EXE`, boot 317, heap 0 `E5AFB000`-`E5F81FFF`, heap 1
`E5F82000`-`E6081FFF`:

| Kind | `GetAvailableVidMem` total | Created | In heap 1 | Highest address |
|---|---|---|---|---|
| Off-screen plain | `487000` (4.53 MiB) | 579 | 0 | `E5F7F000` |
| Z buffer, 16-bit | `487000` | 288 | 0 | `E5F7FF80` |
| Texture | `587000` (5.53 MiB) | 390 | 4 | `E6042000` |
| Off-screen plain, after the textures | `487000` | 579 | 0 | `E5F7F000` |

Each kind stopped at `DDERR_OUTOFVIDEOMEMORY`. The restriction is honoured
by both the allocator and the free-memory report.

**5. DirectDraw placing textures in the pool wastes almost all of it.**
Only 4 of the 390 textures reached heap 1, against its 1 MiB. Four 64-row
textures at the page-wide pitch DirectDraw gives textures on this driver
(`d3d_place.c`: 4,096 bytes, measured 2026-09-25) are exactly 1 MiB. The
HAL has to place pool textures itself, through `DDHAL32_VidMemAlloc` on
heap 1 at the sampler's own pitch, as plan step 5 says. A texture the HAL
leaves to DirectDraw would land in the pool at a 4 KiB pitch.

## Not measured

- Serious Sam's texture working set (the plan's `hwno` counters). It does
  not change the build: the pool is 32 MiB by decision.
- Allocation at `Device_Init` (finding 1).
- Whether a mode set rewrites PTEs above the stolen run (finding 2).

## State left behind

The netbook runs the measurement build, so heap 0 is 1 MiB smaller and
the textures DirectDraw places itself can spill into a 1 MiB heap 1 at a
4 KiB pitch. The previous set is in session 721dbb0a's scratchpad
`nb-before-pool`. The next netbook build replaces it.
