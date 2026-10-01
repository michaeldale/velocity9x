# Intel Gen3: textures in system memory through the GTT (DVMT)

Date: 2026-10-02. Status: proposed; nothing coded. Three decisions below
are Michael's, and a Phase 0 errata decision precedes any GTT write.
Machine: MICHAEL-NETBOOK (945GSE / GMA 950, 2 GiB RAM, 8 MiB stolen).

## Why

The netbook's GPU can only sample what its GTT maps, and the VBIOS maps
only the 8 MiB the BIOS stole from RAM. After the front, back and third
buffers, the depth buffer, the ring and the status page, about 5 MiB is
left for textures. Serious Sam: The First Encounter needs more:

| `auto-demo0001`, 481 s, netbook boot 87 (`ss2`, ICD log) | |
|---|---|
| frames | 1,665 - **3.46 fps** average, 1.0 to 27.6 per ten seconds |
| hardware texture creates / failed (`DDERR_OUTOFVIDEOMEMORY`) / evictions, per 10 s | 2,985 / 1,494 / 1,487 |
| texture upload, per 10 s | 23.4 MB |
| batches drawn by the CPU rasterizer instead, per 10 s | 427 |
| render-interface draw, average over 33,872 calls (HAL snapshot) | 940 us |
| - of which reached the Gen3 engine (29,918 calls) | 41 us |
| - the rest, ~3,950 calls in the front end | about 7.7 ms each |

The failed creates are almost all 256x256 textures with nine levels
(~170 KB). Least-recently-used eviction against a working set larger than
the cache misses on every use, and a texture that cannot be placed is
drawn by the CPU at milliseconds a batch. Serious Sam's frame is the CPU
rasterizer and the texture churn, not the GPU.

The GMA 950 was built for this: Intel's own drivers give it up to 224 MiB
of system memory by mapping RAM pages into the GTT (DVMT). Our GTT has the
room - 65,536 entries for the 256 MiB GMADR, of which the VBIOS points
1,983 at stolen memory and the other 63,553 at one scratch page
([GTT inventory](../decisions/2026-09-12-intel-phase2-gtt-inventory.md)).

## What is proposed

A **texture pool**: N MiB of locked system RAM, its pages written into GTT
entries above the stolen run, declared to DirectDraw as a second video
memory heap that only textures may use. Render targets, depth, the ring
and the status page stay in stolen memory where they are measured.

```
GMADR offset 0          7.7 MiB            7.7 + N MiB          256 MiB
|-- stolen (VBIOS) --|-- pool: RAM pages --|-- scratch page ...--|
   primary, back,       textures only
   Z, ring, HWS
```

### The pieces

1. **Allocation (mini-VDD, ring 0).** At `Device_Init`, `_PageAllocate`
   N MiB `PG_SYS` and `PAGEFIXED`, then read each page's physical address
   (`_CopyPageTable` over the linear range). Contiguity is not needed: the
   GTT is per page. The pages are zeroed once, and the CPU's cache over
   them flushed once (`WBINVD`), before the GPU sees them.
2. **The map (pure policy, host-tested).** A new `i9xx_pool.c` takes the
   stolen run's end, the physical page list and the aperture size, and
   returns the PTE values and the pool's aperture range, refusing:
   overlap with entries 0..1982, the GTT's own storage or any stolen page,
   a physical address above 4 GiB, an entry past the aperture, a page
   listed twice. PTE form for system memory on Gen3: `phys | 1` (valid,
   uncached type), as the VBIOS's own entries are. The validator style of
   `check-intel-gtt-capture.ps1` applies.
3. **The write (mini-VDD).** PTEs through GTTADR (BAR3), the last one read
   back to post them, then the chipset write-buffer question - see Phase 0.
4. **CPU access only through the aperture.** The pool's RAM also has the
   VMM's write-back linear mapping; nothing uses it after step 1. Every CPU
   write (texture upload, Lock) goes through the GMADR, as stolen memory's
   already do (`2026-09-14-cpu-writes-to-stolen-memory-need-the-aperture`),
   so no CPU cache line can hold data the GPU reads. The HAL's aperture
   mapping grows from 8 MiB to 8 + N MiB.
5. **DirectDraw (DRV and HAL).** `vmiData.dwNumHeaps = 2`: the second
   `VIDMEM` covers the pool's aperture range, `VIDMEM_ISLINEAR`, with its
   `ddsCaps` restriction excluding everything but textures. The HAL's own
   texture placement (`v9x_d3d_place_*`) learns the pool too.
6. **Mode changes.** The VBIOS programs the whole GTT (Phase 2's
   capture); whether it rewrites the entries above the stolen run at each
   INT 10h mode set is not measured. If it does, the pool's entries are
   reapplied after every mode change. The pages are ours, so
   their contents survive; only the mapping needs restoring.

## Decisions (Michael)

1. **Pool size.** Proposed: 32 MiB fixed (8,192 pages, 0.4 MiB of GTT
   entries), on machines with at least 512 MiB of RAM, otherwise none.
   Phase 0 measures Serious Sam's working set to check 32 MiB is enough.
   No INI key, per the 2026-09-18 rule.
2. **Errata gate.** System memory read by the GPU is a new CPU/GPU memory
   interleave on a part with erratum 12 (309220-0132, "incorrect internal
   buffer flush ... system may hang") and the undocumented chipset flush
   page Linux uses (`2026-09-12-intel-flush-page-lead`). Proposed: a
   decision document of the async gate's shape, authorising Phase 2 and 3
   on the netbook only, with a stop rule.
3. **Recovery.** If the pool misbehaves the symptom is global (textures,
   or a hang). Proposed: `V9X3D OFF` from DOS also turns the pool off, as
   in the write-combining plan's option B; no verb of its own.

## Phases

0. **Measure, write nothing to the GTT.**
   - Serious Sam's texture working set: the ICD's live copies and their
     sizes over the demo (the `hwno` counters, ss3).
   - Whether a mode change rewrites GTT entries above the stolen run:
     a GTT inventory after a mode set, against the boot one.
   - Whether `_PageAllocate` gives 8,192 fixed pages at `Device_Init`
     on 98SE, and their physical addresses (the mini-VDD reports, maps
     nothing).
   - Whether the 98SE DirectDraw runtime honours a texture-only second
     heap: a probe that creates textures and offscreen surfaces against
     a fake second heap over stolen memory.
   - The errata decision.
1. **Policy, host-only.** `i9xx_pool.c` and its tests, test-first,
   including a property pass that no accepted map touches a stolen,
   GTT, ring or status page.
2. **The map, no GPU use.** Allocate, write PTEs, then a CPU pattern
   written through the aperture read back through the aperture and, after
   `WBINVD`, through the RAM's own mapping: the mapping proven without the
   GPU.
3. **The GPU samples it.** A probe in the shape of `V9XTSHP`: a texture in
   the pool, drawn, read back exactly; then sustained, with uploads and
   draws interleaved, async on.
4. **DirectDraw heap and the HAL's placement.** Then Serious Sam's demo:
   failed creates and CPU batches toward zero, frames against 3.46 fps;
   Quake 2, Half-Life and 3DMark 99 for regressions, watched for
   corruption. Mode change and DOS box (which hard-locks this netbook,
   so last and with the operator present).

## Separately, and cheaper

The ICD's eviction is least-recently-used, which against a cyclic working
set larger than the cache misses every time. A frame-aware rule (never
evict a copy used in the current frame; otherwise the most recently used
of the rest) keeps part of the set resident. It does not need the pool and
would help the Gateway's 4 MiB too; it is not this plan.

## Not in scope

- Render targets, depth or the ring in system memory.
- Snooped (cache-coherent) PTE types.
- The Gateway: the Mach64 has local VRAM and no GTT; AGP texturing there
  is a different plan.
