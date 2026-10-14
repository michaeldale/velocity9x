# 3DCIF VQ textures draw: the codebook is RGB565

Date: 2026-10-14

Status: Measured on A8U4I5 (Rage XL PCI, boot 469, 5:5:5 desktop). Phase 3
of [ati-3dcif-wrapper.md](../plans/ati-3dcif-wrapper.md). Evidence:
`docs/probe/a8u4i5-rage-xl-3dcif-2026-10-14/`.

## The codebook's format

The guide (SDK-C02700 Rev 1.30, "Vector Quantization (VQ) Compression",
and C3D_CODEBOOKENTRY) gives the codebook as 256 entries of four 16-bit
texels: upper-left, upper-right, lower-left, lower-right. It does not say
which 16-bit format the texels are in. The Knight demo's `vqt.cpp` copies
them from the file as they are. Over the five `.vqt` files in SDK 4.1
(`vqt_stats.py`, `vqt-codebook-stats.txt`):

| File | Top bit set | Mean 2x2 green spread, read as 565 / 555 |
|---|---|---|
| Dragon | 422 of 1024 | 0.212 / 0.385 |
| ground1 | 639 of 1024 | 0.159 / 0.439 |
| sky | 364 of 1024 | 0.141 / 0.313 |
| BRICK | 741 of 1024 | 0.336 / 0.483 |
| Mandrill | 532 of 1024 | 0.112 / 0.377 |

Red agrees with green.
- **Dead: ARGB1555.** An alpha bit would be constant or nearly so; here
  the top bit varies with the colour.
- **Dead: XRGB1555.** Read as 555, neighbouring texels of a block differ
  two to three times as much as read as 565.

The codebook is RGB565.

The index map is one byte per 2x2 block, rows of width/2 bytes. Sky is
registered as 1024x256 and its file holds a 512x128 index map.

## The change

- **`cif_texfmt.c`:** `v9x_cif_texfmt_abi_format(VQ)` answers as RGB565
  does: RGB565, or ARGB1555 when keyed. The test changed first and failed
  two checks.
- **`cif_dll.c`:**
  - TextureReg keeps a VQ texture's index map. VQ's two bits a texel
    cover it exactly.
  - On first use, the map is decoded through the codebook with the
    tested `v9x_cif_texfmt_vq_decode`. A keyed copy then goes through the
    RGB565 row conversion, which applies the chroma key. Textures larger
    than the engine takes are decimated, as other formats are.
- **A bug found on the way:** TexturePaletteCreate copied a codebook
  (512 dwords) into a palette of 256, so half of every codebook was
  dropped. Nothing drew VQ before, so nothing showed it. Palettes now hold
  512 dwords.

## Measured

Knight VQ on the Rage XL:

| Build | Frames | Drawn | Refused | Textured draws skipped |
|---|---|---|---|---|
| Before | 609 | 1,284,005 | 0 | 438,437 |
| After | 361 (in 30 s) | 763,344 | 0 | 0 |

All six VQ textures registered as drawable. Michael watched it on the
monitor: "looks good".

Gates: build-host, run-checks.
