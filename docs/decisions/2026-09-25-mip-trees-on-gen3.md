# Mipmapping on Gen3: the HAL lays out the chain, and the sampler reads every level

2026-09-25, MICHAEL-NETBOOK (945GSE / GMA 950), Win98 SE, boot 8. DRV, VXD
and HAL from one build (V9XDISP.DRV 104,090 bytes, V9XHAL.DLL 103,936),
deployed together by WININIT.INI rename because the shared-block stamp
moved to 2026092501.

## Why the driver has to place the chain

MAP_STATE carries one address and one pitch for a whole chain; the sampler
finds each level from them by a layout fixed in the part. DirectDraw's heap
creates each level as its own surface with its own pitch - measured here
as 4 KiB-aligned pitches (`MipGapActual=0x00001000`) - so a heap chain can
only ever be sampled at its top level. That is why every texture drew at
level 0 until today, and why the four mip filters were unpublished.

## What changed

- **The layout**, `v9x_d3d_i9xx_layout_miptree` (`d3d_i9xx_target.c`, a
  host-tested leaf): Mesa's i945 layout, which gallium
  `i915_resource_texture.c:462-522` and classic `intel_tex_layout.c:121-186`
  state identically - level 1 below level 0, level 2 to the right of
  level 1, later levels below level 2, widths aligned to 4 texels and
  heights to 2 rows, pitch widened for tiny tops and aligned to 64 bytes.
  The host test's numbers were worked by hand from that code.
- **Placement at CreateSurface.** A video-memory 16-bit mip chain on the
  Gen3 engine gets one block for the whole tree from DirectDraw's own heap
  through `DDHAL32_VidMemAlloc`, found with GetProcAddress, and each level's
  fpVidMem and lPitch are set to its place in it. This is what the Windows
  98 DDK's own ViRGE driver does (`src\display\mini\s3v\S3_DD32.C:3046-3116`,
  built `/DMIP`), with Gen3's layout. The block is rounded up to a page
  because MAP_STATE's address is page aligned; its start is kept in the top
  level's dwReserved1, and lpVidMemHeap is left NULL so DirectDraw does not
  free it. DestroySurface frees the block when the top level goes. New
  engine ops `create_surface`/`destroy_surface`; every other engine leaves
  them null and is unchanged. The user agreed the design (and the new
  exported layout function) before it was written.
- **Bind.** The chain is walked and every level checked to be exactly at
  its layout place and pitch; the verified prefix sets MS4 MAX_LOD (quarter
  levels, gallium `i915_state_sampler.c:291` and classic
  `i915_texstate.c:196`) and SS2's mip filter (NONE 0, NEAREST 1, LINEAR 3,
  gallium `i915_reg.h:773-775`). A chain placed any other way samples its
  top level, as before, and is counted in the `mip_gap_*` counters. The
  decoder compares both fields against the declared chain.
- **The D3DFILTER mapping was half backwards.** The DDK's ViRGE HAL
  (`D3DRENDR.C:124-141`) settles the DirectX 5 names: MIPLINEAR is bilinear
  within the nearest level, LINEARMIPNEAREST point samples blended between
  two levels. This engine had those two the other way round, invisible
  while no map had levels.
- All four `D3DPTFILTERCAPS_MIP*` published. Seven appended diagnostics
  (`MipTreeAllocs` ... `MipDraws`), dumped by V9XTRACE.

## Measured

Host: layout offsets for 256/9, 128/4, 4/3 and 1-level chains and the
refusals; MS4 MAX_LOD and its two bounds; SS2 mip filter and value 2
refused; the decoder refusing each field mismatched. All 39 new assertions
failed against a stub before the implementation. `run-checks.ps1` green;
scene CRCs unchanged (Phase 5 `01A4DE25`, combined `1229FE1F`).

Netbook, the probe's mip ladder (128 to 16, a colour per level,
`...-V9XDD-ladder-pass.ini`): `MipLadderOk=1` - red, green, blue, magenta
at 1.1, 2.2, 4.4 and 8.8 texels a pixel - and `MipTriOk=1`, each
half-level step a mix of exactly its two levels' channels. The first
Gen3 read of a level below the top. The matrix's 256-texel chain cell reads
level 1's colours; its hand-attached ("gapped") chain reads level 0, the
designed fallback.

The first ladder run read level 1 as blue (`...-ladder-fill-overrun.ini`):
the probe filled each level with `lPitch * height` bytes in one run, and on
a shared-pitch tree level 2's row padding IS level 1. The probe now fills a
chain level row by row within its width. **This is a real compatibility
limit, not only a probe bug**: an application that writes a mip level
through its pitch padding - a single memcpy of lPitch * height - will
overwrite the level beside it. Row-by-row writers, the HEL and this
driver's Blt are unaffected. No title has been seen doing it yet.

3D WinBench 98, overrides at DEFAULT (`...-3dwb98-quality.txt`), all on the
Direct3D HAL: 6 Nearest, 7 Linear, 12 Modulate unchanged Capable, and
**8-11, all four mip filters, Capable** (were NotCapable). V9XTRACE after
the run (`...-V9XTRACE-after-3dwb98.ini`): 24 trees allocated, 24 freed,
none declined; 180,890 textured submissions with levels; no draw refused;
the 156 chain gaps are all the probe's hand-attached chains (78 per run).

## What the verdicts did not settle

My verdicts were given on the benchmark's captured frames; the comments
typed into the benchmark are in the export's notes.

- **Level choice on the receding wall is about one level coarser than the
  reference** in all four tests: each band boundary sits where the
  reference's previous one is, and only a thin strip of level 0 remains at
  the near edge (`...-test8-near-edge-zoom.png`). The isotropic ladder
  shows no shift, so the layout and the level count are not the cause. The
  likely explanation is that the hardware computes LOD from the larger
  derivative on a stretched footprint; that is a hypothesis, not measured.
- **Test 10 (point, linear between levels) is compressed at the far end**:
  it reaches only level 3-4 where the reference and this machine's own
  test 8 reach 6-7 (`...-test10-far-end-zoom.png`). Test 11 (trilinear)
  does reach the far levels. Not explained.
- 3D WinBench's Min/Mag interplay with MAX_LOD 0 (Mesa's note at
  `i915_state_sampler.c:322-324`) was not examined.
- A mode change with trees allocated, and DDHELP cleaning up a crashed
  process's trees, were not exercised. The free is skipped when the block
  signature does not match, so the worst case expected is a leaked block
  until the heap is rebuilt; not measured.

## Also seen

During the run the operator saw flicker with the blue background leaking
through, like Final Reality; recorded in
`docs\issues\2026-09-19-the-flicker-state-of-play.md`.
