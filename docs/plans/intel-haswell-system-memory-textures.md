# Haswell: textures in system memory through the GTT

Date: 2026-11-19. Status: approved; Michael took all four decisions below
as proposed the same day. Phase 0 measured the same day
([record](../decisions/2026-11-19-haswell-texture-pool-phase0.md)). Phase 1
done the same day: `v9x_intel_hsw_pool_layout` (`hsw_config.c`) feeds the
Gen3 policy unchanged, with the GTT's own stolen block excluded; host-tested
on the i5-4570's config and Phase 0's frames. Phase 1 found no need for a
Gen7 PTE encoder (the Gen3 `frame | 1` is Haswell's uncached form below
4 GiB); the "write all, read every entry back" rule for junk entries is
the mini-VDD's, in Phase 2. Phase 2 passed the same day: the pool maps at
GTTMMADR + 2 MiB, the aperture reaches its pages, mode sets leave them, and
every junk entry is restored exactly
([record](../decisions/2026-11-19-haswell-texture-pool-phase2.md)).
Phase 3 passed the same day: the driver maps the pool at boot on the
i5-4570, V9XD32 samples textures from it, and Car Chase refuses no mip
tree and no longer drops to 1 fps
([record](../decisions/2026-11-19-haswell-texture-pool-phase3.md)).
Phase 4, games, next. Requested by
Michael the same day, after the vsync-off work showed 3DMark 2001 SE's Car
Chase short of texture memory on the HD 4600 with vsync on as well
([record](../decisions/2026-11-19-haswell-vsync-off-async-flips.md)).
The Gen3/Gen4 pool this follows:
[intel-gen3-system-memory-textures.md](intel-gen3-system-memory-textures.md),
running on the GMA 950 and every GM965.

## Why

The i5-4570 has 32 MiB of stolen memory; the top megabyte is protected
and the engine reserve and probe region take another 384 KiB, so
DirectDraw's heap ends at 1EA0000h
([stolen layout](../decisions/2026-10-20-haswell-stolen-layout.md)). At
1024x768x32 the primary, back buffer and Z buffer take 9 MiB of that,
leaving about 21.6 MiB for textures. 3DMark 2001 SE, same build, vsync on:

| Car Chase, HD 4600, 2026-11-19 (run H-on) | |
|---|---|
| mip trees refused for room in 2 minutes | 185 |
| texture locks (uploads) | 18,294 |
| 3DMark's fps meter, Low / High | 7-58 / 1-34 |

The meter falls to 1 fps where textures are refused and re-uploaded. Run
E-v0, with 8 MiB less heap, refused 817 and Car Chase stayed at 1-2 fps
throughout, so the test's speed moves with texture room.

Intel's own drivers give this GPU hundreds of MiB through the GTT. Our GTT
has the room: 2 MiB of entries (512 Ki pages), of which the BIOS maps
8,191 linearly over stolen memory; the entries past stolen memory hold
junk with the valid bit set.

## What is proposed

The Gen4 pool's design: N MiB of locked system RAM, its pages written into
GTT entries above stolen memory, published to DirectDraw as heap 1 for
textures only, placed and sampled by the HAL. Render targets, depth, the
rings and the copy target stay in stolen memory.

```
GMADR 0            31 MiB           32 MiB             32 + N MiB     64 MiB
|-- stolen: heap, reserve --|-prot-|-- pool: RAM pages --|-- not mapped
```

### What differs from Gen3/Gen4

1. **The GTT is at GTTMMADR + 2 MiB** (BAR0), 2 MiB of 32-bit entries.
   The mini-VDD already maps it there (`V9xHswGttLinear`, loader.asm).
2. **PTE format** (i915 `hsw_pte_encode`, `HSW_PTE_ADDR_ENCODE`): bits
   31:12 the frame, bits 11:4 physical address bits 39:32, bit 0 valid,
   and cacheability in bits 3:1 and 11. Windows 98 frames are below 4 GiB,
   so 11:4 are zero. The cache field is new: see decision 2.
3. **The last-level cache is shared with the CPU.** An LLC-cached PTE
   (`HSW_WB_LLC_AGE3`) makes GPU reads coherent with the CPU's caches;
   an uncached one (0) does not, and the CPU must not hold the pool's
   lines in its cache. Every CPU write goes through the write-combined
   aperture already, as on Gen3, so uncached is the Gen3 behaviour.
4. **The GTT past stolen memory is junk, not one scratch page.** Gen3's
   map refused unless the range held one uniform value. Here the check has
   to be "the range is not used by anything we own", and the pool's
   entries are written whole and read back.
5. **The HAL's mapping is already 64 MiB**, write-combined through PAT
   (`V9X_HSW_MAP_BYTES`). On a 32 MiB machine, GMADR 32-64 MiB is mapped
   but unused, so a 32 MiB pool fits it without growing the mapping.
   A 64 MiB-stolen board would need the mapping grown.
6. **Haswell's mini-VDD path is its own** (`HSW_ENGINE_OPEN`); the pool
   API is the Gen3 one (`I9XX_POOL_*`) taught Haswell's GTT and PTE
   format, or a parallel `HSW_POOL_*`, whichever keeps loader.asm smaller.

## Decisions (Michael)

1. **Size and place.** Proposed: 32 MiB at GMADR 32 MiB on a 32 MiB-stolen
   machine (the first entry past stolen memory), on machines with at least
   512 MiB of RAM, as on Gen3. No INI key.
2. **Cacheability.** Proposed: uncached PTEs, as the BIOS's stolen entries
   are (Phase 0 reads their cache bits to confirm), with every CPU access
   through the aperture. LLC-cached is faster for the GPU but brings a new
   coherence rule; not now.
3. **Gate.** Proposed: the i5-4570 only (its device and subsystem id,
   checked in the mini-VDD), lifted to every Haswell after the release
   checks pass on it, as the GM965's was. Ivy Bridge (HD 4000) after that,
   one record.
4. **Recovery:** `V9X3D OFF` from DOS turns the pool off, as on Gen3.

## Phases

0. **Measure, write nothing to the GTT** (a Haswell variant of `G3PL`):
   the 8,192 entries at GMADR 32-64 MiB (values, how many valid); the
   cache bits of the BIOS's stolen entries; whether `_PageAllocate` gives
   8,192 fixed pages at `Device_Init` here and their frames (below 4 GiB);
   the machine's RAM. Car Chase's texture bytes per frame from the HAL
   counters, to check 32 MiB covers it.
1. **Policy, host-only:** the PTE encoder and the map check for Haswell in
   `i9xx_pool.c` (or beside `hsw_config.c`), test-first, including that no
   accepted map touches a stolen, protected, reserve or GTT page.
2. **The map, no GPU use:** write the PTEs, a CPU pattern through the
   aperture read back through the aperture and through the RAM's own
   mapping, then the old entries restored.
3. **In the driver, gated:** the mini-VDD maps at boot; `dd16.c`
   publishes heap 1; `d3d_place.c` already places textures there first;
   `d3d_hsw.c`'s texture bind accepts the pool's range. V9XD32 at 16 and
   32 bpp with its textures in the pool, read back exactly.
4. **Games:** 3DMark 2001 SE Car Chase (refusals toward zero, the meter
   against 7-58 / 1-34), Dragothic, UT2004 and Quake 3 for regressions;
   a mode change; vsync off with the copy target.

## Not in scope

- Render targets, Z, rings or the copy target in system memory.
- LLC-cached PTEs (decision 2).
- Growing the 64 MiB mapping, unless a 64 MiB-stolen board needs it.
