# A census of command words, and what 3DMark 99 is actually made of

Date: 2026-09-04
Status: instrument built and read once on A8U4I5; it narrows both open 3DMark
items and kills one hypothesis about the first

## Why a census

3DMark 99 on the Trio3D/2X has two defects left whose draws no counter
separates from the correct draws beside them: the race scene's ground drawn as
white and grey chevron noise, and one large flat green panel on a corridor wall
(`docs/issues/2026-09-03-3dmark99-on-the-trio3d-after-the-stride-fix.md`).
Every texture is accepted, no blend is skipped, the engine never faults, and
the walls in the same frame are right.

A ring of the last N draws does not help when 58,619 go past and the
interesting ones are in the middle, and aiming a filter at a screen rectangle
means knowing where to aim. But a run uses *few distinct command words* - the
word is assembled from a handful of render states - so counting them costs a
small table, needs no aiming and no sampling, and says what state produced any
population of draws. The texture-size field is masked out of the key and
accumulated as a bitmask instead: it is the one field that varies per texture
and would otherwise split every word into a dozen rows.

`V9X_D3D_DRAW_CENSUS`, 32 slots at the end of the shared block, reported by
V9XTRACE as `Census<nn><field>`.

## What one benchmark run is made of

A8U4I5, boot 27, 800x600x16, after one probe run in the same boot (the
low-count rows below are the probe's; its texture sits at 0x00132800).
31 slots used, `CensusOverflow=0`.

```
   draws  command      what it is                                    sizes
  338176  0x8CC0E047  LIT MODULATE WRAP Z:LESS+w 1555 LINEAR         32..256
   88508  0x94C06047  UNLIT WRAP Z:LESS+w 1555 LINEAR                16..256
   11652  0x8C48E027  LIT MODULATE WRAP Z:LESS ALPHA_ENABLE 4444 LINEAR  64..256
   10993  0x94486027  UNLIT WRAP Z:LESS ALPHA_ENABLE 4444 LINEAR     16..256
    1588  0x8CC0B047  LIT MODULATE WRAP Z:LESS+w 1555 LINMIPLIN      16,64,128
    1165  0x94C03047  UNLIT WRAP Z:LESS+w 1555 LINMIPLIN             16..128
     787  0x80C00007  untextured, Z:LESS+w                           -
     702  0x8CC0A047  LIT MODULATE WRAP Z:LESS+w 1555 LINMIPNEAR     16,128
     320  0x94C02047  UNLIT WRAP Z:LESS+w 1555 LINMIPNEAR            16..128
```

Nine words carry the whole benchmark. Read against the pictures:

- **426,684 opaque textured draws** with a plain LINEAR filter are the walls,
  ceiling, floor and most geometry - the parts that are right.
- **22,645 `ALPHA_ENABLE` ARGB4444 draws** are the sprites and HUD, and match
  `D3dTextureAlphaDraws=23355`. Those are the alpha defect, and
  `2026-09-04-no-encoding-of-the-alpha-field-blends-on-the-trio3d.md` says no
  encoding of that field would have blended them.
- **3,775 mip-filtered draws** are the *only* mip usage in the entire run:
  1,588 + 702 lit and 1,165 + 320 unlit, LINMIPLIN and LINMIPNEAR, ARGB1555,
  sizes 16 to 128. A tiled ground plane is few triangles over a large area and
  minification is exactly where a level below the top gets selected, so this is
  where the noise ground lives - a population of under four thousand draws out
  of 58,619, addressable now.
- **787 untextured Gouraud draws** with depth. The flat green panel is either
  one of these or one of the mip populations; the census does not yet say
  which.

The two texture offsets under the mip words - 0x003FEB00 and 0x003FA080, with
0x003E8180 for the third - all sit in the last 24 KB of a 4 MiB card. Whether
that is where the noise comes from or just where DirectDraw put them, this run
does not say, but it is the first thing the next rung should vary.

## A hypothesis this killed on the way

The obvious guess was that the engine binds only level 0 of a gapped chain (the
guard from 88ea410) while still asking the chip for a mip filter, so the
sampler would read whatever memory follows level 0 - noise, exactly where
minification kicks in. It does not: `v9x_d3d_texture_info` sets
`texture_mipmapped` only when `v9x_d3d_mip_chain_contiguous` succeeds, and
every mip arm of the command word is guarded on that flag, so a gapped chain
falls through to plain LINEAR. The 3,775 mip-filtered draws are draws whose
chains really are contiguous. Whatever is wrong with them is not the guard.

That also means the probe's matrix does not cover them the way it looks like it
does: its trilinear cells on chained and gapped layouts pass under a loosened
rule - "something was drawn on both halves" - rather than on an exact hue, so a
sampler reading a wrong but non-black level 1 passes them. That is the gap in
the probe, and it is the rung to write next.

## Gates

check-tree, vga survey safety gate, host tests and family packages
(run-checks). The census is read on the guest, so its own evidence is the run
above: 31 slots, no overflow, and every row's bits decode to a draw the picture
accounts for.
