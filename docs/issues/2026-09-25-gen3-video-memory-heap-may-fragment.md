# Gen3: mip trees are declined for want of room, and the heap may be fragmenting

**Status: OPEN, not measured.** Recorded 2026-09-25 for review. Nothing
here has been reproduced on purpose; it is a reading of one run's counters
and one operator report.

## What was seen

MICHAEL-NETBOOK, boot 20, shared ABI 2026092505, 3DMark 99 Max at
1024x576x16, stride 2112, triple buffering, one full run after a fresh boot
(`docs\decisions\2026-09-25-netbook-texture-placement-3dmark1024-V9XTRACE.ini`
against `...-before-V9XTRACE.ini`):

| Counter | Boot 20 | Previous run (fc5f21b) |
|---|---|---|
| `MipTreeAllocs` | 8,973 | - |
| `MipTreeDeclined` | **11,456** | 4,408 |
| `MipTreeDeclinedLast` | 4 (`V9X_D3D_I9XX_MIPTREE_ALLOC`) | - |
| `TexturePlaced` | 35,370 | - (not built) |
| `ZPlaced` | 2 | - |
| `MipTreeFrees` (every placed block) | 44,345 | - |
| `D3dTextureCreates` / `Destroys` | 40,303 / 40,303 | - |
| `D3dTextureRefusedBounds` | 0 | 65,487 |

Reason 4 means `DDHAL32_VidMemAlloc` returned 0: DirectDraw's heap had no
free block of the size asked for at that moment. **Only the last reason is
recorded**, so "all 11,456 were reason 4" is not established; the decision
record for this build says it was, and that is an overstatement.

The operator separately reported, on an earlier boot, later 3DMark tests
showing a blank screen, and asked whether repeated runs in one boot degrade.
Armed frame captures from that boot showed drawn frames in the flipped
buffers (`docs\decisions\2026-09-25-scanning-out-at-2112-on-gen3.md`), so the
blank screens are not yet tied to memory at all.

## What is known not to be the cause

- **A leak in the HAL's own placements.** Every block the Gen3 engine placed
  in this run (trees, Z buffers, plain textures: 44,345) was freed, and
  every texture handle created was destroyed. Three contexts were created
  and destroyed. This covers one run only.

## Why fragmentation is suspected

The heap at this mode is 5,660 KB (8 MiB stolen, less 320 KB the BIOS
keeps, less the driver's 1 MB `V9X_I9XX_GTT_RESERVE_BYTES`, less the
1,188 KB screen). The flip chain and Z buffer take about 4.6 MB of it,
leaving roughly 1 MB for every texture.

`v9x_d3d_i9xx_place_block` (`src\display32\d3d\d3d_i9xx.c`) over-asks by a
page of rows and rounds the start up to 4 KiB, because the heap call takes
no alignment and MAP_STATE needs a page-aligned address. Each placed block
therefore holds up to a page more than it uses, and ends at an arbitrary
offset. For small textures that is proportionally large: an 8x8 texture's
128 bytes cost about 4.2 KB, a 64x64's 8 KB cost 12 KB. Many such blocks,
created and destroyed in a different order, could leave the free space in
pieces too small for a whole mip tree even when the total would do.

## Why it may not be fragmentation

- Declines **rose** once plain textures were placed tightly, which should
  have freed room: each such texture now takes a fraction of the 4 KiB-pitch
  block DirectDraw gave it before. That fits fragmentation, but it fits
  equally well an application that keeps more textures resident once its
  creates succeed, or that tries a mip chain first and falls back to a
  plain texture, counting the same texture twice.
- A decline is not a failed texture. The HAL returns NOTHANDLED and
  DirectDraw allocates the chain itself, one surface per level, which can
  fit where one contiguous tree cannot. `D3dTextureCreateSysmem=0` and
  `RefusedBounds=0` say those textures did end up somewhere bindable. Whether
  they are then sampled with mip levels or level 0 only is not measured.
- The heap may already align its blocks to 4 KiB (the previous run saw
  `MipGapActual=0x1000` between DirectDraw's own levels). If so the page of
  slack is pure waste rather than the price of alignment. Not checked.

## What would settle it

1. **Two runs in one boot.** Fresh boot, snapshot, 3DMark 99 full run,
   snapshot, second full run without closing anything else, snapshot. If
   the second run's `MipTreeDeclined` delta clearly exceeds the first's, or
   later tests go blank only in the second, state is carried across runs.
   If the deltas match, the blank screens have another cause.
2. **Free video memory between runs**, from a probe calling
   `IDirectDraw2::GetAvailableVidMem` for `DDSCAPS_TEXTURE`, before the
   first run and after each. A total that does not return to its starting
   value after 3DMark exits is a leak somewhere (ours or DirectDraw's); a
   total that returns while declines rise is fragmentation.
3. **Count declines by reason**, not only the last one, and record the
   largest size declined. Appended diagnostics; ABI change.
4. **Count placed blocks whose heap start was already page aligned.** If it
   is nearly all of them, the slack can be requested only when needed
   (allocate exact, and retry with the slack only if the start is
   unaligned).

## Remedies to weigh once measured

- Drop the page of slack where the heap already aligns (item 4).
- Sub-allocate small textures from pages the HAL owns, one size class per
  page, so they stop scattering across the heap.
- Give back part of the 1 MB reserve if the ring and sandbox do not need
  all of it (`V9X_I9XX_GTT_RESERVE_BYTES`); the reserve is the cheapest
  megabyte to find, but its sizing has not been reviewed.

## Related

- `docs\decisions\2026-09-25-placing-plain-textures-on-gen3.md`
- `docs\decisions\2026-09-25-mip-trees-on-gen3.md`
- `docs\decisions\2026-09-25-scanning-out-at-2112-on-gen3.md`
