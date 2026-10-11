# Glide texture memory as bytes

Date: 2026-10-11

Status: proposed. Nothing coded. Follows
[glide-2x-wrapper.md](glide-2x-wrapper.md); found on Carmageddon II,
recorded in
[2026-10-10-carmageddon2-exits-after-intro-gma900.md](../issues/2026-10-10-carmageddon2-exits-after-intro-gma900.md).

## Context

A Voodoo's texture memory is bytes. `grTexDownloadMipMap` writes bytes
at an address; `grTexSource` names an address, a format and an LOD range,
and the TMU reads whatever bytes are there in that format. Nothing on the
card remembers which download put them there.

`GLIDE2X.DLL` (and `GLIDE3X.DLL`, which shares `glide_core.c`) models it
as records instead: a download makes a record of whole texture
(`glide_texmem.c`), a source must match a record's address, even/odd,
LODs, aspect and format exactly (`v9x_glide_texmem_find`), and the DLL
keeps a copy of the texels per record (`V9X_GLIDE_TEXTURE.raw`). A
source with no matching record has no texture, and its draws are
skipped (`textured draw skipped: reason=1`).

NFS II SE never noticed: it downloads each texture to its own slot.
Carmageddon II does not. On the netbook (2026-10-11, boots 140-141) its
menu downloads a 64x64 ARGB4444 texture, a 64x64 RGB565 texture, and
4x4 and 8x8 ARGB4444 glyphs, all to address 0, and then sources earlier
ones again without downloading them. Logged:

```
source miss: addr=00000000 asked=0306630C held=2
source miss: addr=00000000 asked=0302230C held=1
  held record=0305530C (evenodd<<24 small<<16 large<<12 aspect<<8 format)
```

1.46 million textured draws were skipped by 207 s and the menu stayed
black. Keeping a record that a later download covers only in part
(4d62ba1) was not enough: these sources ask for textures covered whole,
or for the same bytes in another format.

## Design

### TMU memory

One byte array per TMU, the size the DLL reports through
`grSstQueryHardware` (2 MiB today, one TMU), allocated at
`grGlideInit` with `v9x_glide_alloc`. A download copies its levels into
the array at their Glide offsets (largest LOD first, each rounded to
`V9X_GLIDE_TEX_GRANULE`, as `v9x_glide_texmem_required` already lays
them out), clipped to the array. Nothing is ever freed or evicted; bytes
are only overwritten.

### Page generations

The array is split into 4 KiB pages, each with a 32-bit generation. A
download increments the generation of every page it writes. 2 MiB is
512 pages: a 2 KiB table.

### Sources and the decoded cache

The existing record table becomes a cache of decoded textures keyed by
(address, even/odd, LODs, aspect, format), not a model of memory. An
entry also keeps, for the pages its bytes span, the generations it was
decoded at; a 64x64 ARGB4444 texture spans two or three pages.

`grTexSource`:

1. Find the entry for the key; make one, oldest out, if none.
2. Compare its saved generations with the pages' current ones.
3. Equal: the entry's `raw` copy, conversion and hardware surface stand.
4. Different, or new: copy the largest level's bytes from the array into
   `raw`, recompute `sum`, set `variant` to 0 and `surface_slot` to 0,
   save the generations. The existing conversion
   (`v9x_glide_texture_convert`) and surface cache
   (`v9x_glide_surface_for`, which finds equal texels by `sum`) then do
   what they do now.

The common case costs a hash lookup and two or three compares, which
matters: NFS II SE calls `grTexSource` tens of thousands of times a
second (census), and the netbook's Atom spent about 16,000 cycles a
triangle in the DLL before table fog was cached.

### What goes

- Overlap eviction in `v9x_glide_texmem_download` and the partial-overlap
  patch `v9x_glide_patch_overlaps` (4d62ba1): the array makes both
  automatic.
- The per-download copy into a record's `raw`: the array holds the bytes.
- `texture->serial` as the staleness test, replaced by generations.

`v9x_glide_texmem_required`, `v9x_glide_level_size`, the texel formats
and the state mapping are unchanged.

### Palettes

P_8 texels decode through the current palette at conversion time, as
now (`v9x_glide_variant` folds the palette sum in). No change.

## Steps

1. **Pure module, host-tested first.** `glide_texmem.c` gains the array
   writer and the generation table; tests: a source after a later
   download over it returns the new bytes; the same bytes sourced in a
   second format decode in that format; an untouched entry is not
   re-copied (count copies); a download past the end is clipped; NFS II
   SE's pattern (distinct slots) never re-copies.
2. **Core.** `v9x_glide_texture_download` writes the array and bumps
   generations; `v9x_glide_texture_source` validates or refills the
   entry. Remove the eviction and the patch.
3. **Census lines.** Count refills and source misses in the periodic
   census, so a game that refills every frame shows up.
4. **Gates.** `run-checks.ps1`.

## Hardware checks

| Machine | Program | Pass |
|---|---|---|
| MICHAEL-NETBOOK (945GSE) | Carmageddon II, Glide | Menu drawn in a HAL frame capture (`V9XTRACE -arm`, after swap 2202); `skipped-textured` near 0 |
| MICHAEL-NETBOOK | NFS II SE, a race | Same frame rate as 2026-10-09 (about 18.5 fps), refills near 0 |
| A8U4I5 (Rage XL) | Diablo II, Rollcage (Glide 3) | Title to town, a race, as at a73cc57 |

Close Carmageddon II by rebooting: `WCLOSE.EXE CARMA2_HW.ICD` leaves the
process holding DirectDraw and the DLL.

### Result, netbook, 2026-10-11 (boots 145-155)

The byte model took skipped textured draws to 0, but the menu still drew
as a lattice of 3-pixel dashes every 8 pixels with no text. A full-size
read of the back buffer at swap 3601 (a temporary dump in the DLL; the
HAL capture samples every eighth pixel) showed it. Hypotheses the runs
killed, one netbook run each:

- Texture coordinates: Glide's 0-256 range gives 1 texel per pixel on
  the text quads and the 8x8 panel tile.
- Depth: forcing the depth test off left the frame unchanged.
- Texture data, upload and pitch: the 8x8 4444 tile read back from video
  memory equals what the game downloaded; the surface pitch is 64 bytes
  and the HAL binds at the surface's own pitch. The 4444 MAP_STATE value
  matches the i915 layout.
- `grLfbLock` writes: a lock-to-unlock diff found no pixel changed.
- `grLfbWriteRegion`, a stub, is now written; the backdrop it draws is
  black, so it changed nothing visible.

The cause was `grTexTextureMemRequired`, a stub that returned 0. The
game places each texture at the last one's address plus that size, so
every texture went to address 0 over the previous one: one download a
frame against about 290 sources, all at 0. Written, the menu draws whole
(text, car, panels; boot 155).

## Not in this plan

- DDERR_SURFACELOST on every lock and flip about 300 s into a
  Carmageddon II run; the DLL never restores its surfaces. A separate
  defect, seen twice, not investigated.
- `guTexSource` (about 100,000 calls a run in Carmageddon II), still a
  stub. The game calls no `guTexAllocateMemory`, so there is no handle
  for it to select, and the menu draws without it.
- A second TMU: the DLL reports one.

## Size

About 300-500 lines changed in `glide_texmem.c` and `glide_core.c`,
plus host tests. One session to code and gate; the hardware table above
takes longer, mostly in reboots (a netbook reboot with Carmageddon II
running takes about six minutes).
