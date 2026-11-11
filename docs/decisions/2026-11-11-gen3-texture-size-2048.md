# Gen3 textures to 2048: 3DMark 2001 SE completes with pixel shaders on the GMA 950

Date: 2026-11-11. Machine: the netbook (W9QI2BE2, GMA 950, 945GSE, 7.7 MB
of stolen memory), boots 306-308. Follows
[`2026-11-11-gen3-ps11-netbook-first-runs.md`](2026-11-11-gen3-ps11-netbook-first-runs.md),
which left Advanced Pixel Shader stopping the run with "Could not create
texture - D3DERR_INVALIDCALL" and no cause found.

## Result

| Boot | Build | 3DMark 2001 SE (640x480, 16-bit colour, 32-bit textures, all tests) |
|---|---|---|
| 306 | 7999npt1: TextureCaps NONPOW2CONDITIONAL added (experiment) | stops in Advanced Pixel Shader, same error |
| 307 | the same, with a d3d8.dll logging proxy | stops in Advanced Pixel Shader; the proxy names the call |
| 308 | bb722048: `texture_size_max` 2048 (this change) | **completes: 528**. Advanced Pixel Shader "Not supported by hardware" |

Boot 301, before `draw_shader`, completed with 489 (Nature and the
shader tests skipped). Boot 308's other results match boots 304-305:
Nature 2.9 fps, Environment Bump Mapping 19.7, Vertex Shader 4.6, Pixel
Shader 10.7, Point Sprites 0.2 MSprites/s. The trace snapshot after the
run counts no refused draw, texture, batch or index and no engine timeout.

## The refused call

The runtime refused the texture itself: the driver logged every creation
it saw as placed. To see the call, a d3d8.dll proxy logged every
resource creation with its arguments and HRESULT. Its last line before
the error:

    CreateTexture 2048x2048 levels 12 usage 0 fmt 21 (A8R8G8B8) pool 1 (managed) -> 8876086c

Gen3 claimed `MaxTextureWidth`/`Height` 1024, so the Direct3D 8 runtime
answered `D3DERR_INVALIDCALL` without asking the driver. On boot 308,
with 2048 claimed, Advanced Pixel Shader created that chain and an
X8R8G8B8 one of the same size, both `S_OK`, then reported itself
unsupported and drew nothing. Why it gave up was not measured. The
chains are 21 MiB each at 32 bits, against 7.7 MiB of stolen memory.

Two proxy traps, for whoever needs one again:
- 3DMark's engine, `e2driver\e2_d3d8_driver_mfc.dll`, did not resolve
  `d3d8.dll` to the copy in the EXE's folder. A second copy in `e2driver\`
  was needed.
- The proxy loads the system d3d8 under another name. Its export
  directory still says `d3d8.dll`, so the copy's internal name was patched
  to `d3dr.dll` as well. Whether the internal name mattered was not
  isolated.

## Hypotheses this killed

- **Non-power-of-two textures.** D3D8FMT showed TextureCaps 0x00034907
  with NONPOW2CONDITIONAL set on boot 306. Advanced Pixel Shader failed
  the same way. Reverted.
- **Dynamic textures.** No engine claims `D3DCAPS2_DYNAMICTEXTURES`, and
  the logged call has usage 0.
- **The render-target format.** The test failed the same way at 32-bit
  colour (boot 305 record), and its render-target textures are R5G6B5 at
  16-bit, which CheckDeviceFormat accepts.

## 2048 sampled (V9XTSHP, boot 308)

The probe gained a 256x2048 single level. Every single level, chain and
large level read back exactly: `Result=PASS`, no refusal, timeout or
reset. A 2048 edge is therefore sampled.

A 2048-wide level was not read back. DirectDraw's heap places this
probe's textures, and it refused a 2048-wide 16-bit level (a 4096-byte
row) with `E_INVALIDARG`. 3DMark's 2048x1024 and 2048x256 maps are placed
by the driver instead, and they ran with no refusal, but nothing judged
their pixels. MS3 carries width and height in fields of the same shape.

The first probe run on boot 308 failed every large level in all 4096
pixels, including the 256x256 that passed in October. The probe expected
the texel at `floor((p + 0.5) * w / dw)`. The hardware read the texel at
`floor(p * w / dw)`, Direct3D's rule with pixel centres at integers, in
all seven captures, exactly. In October Gen3 matched the half-pixel rule
only because its fixed-function vertices were not yet moved to its own
centre convention. The probe now expects Direct3D's rule. The two rules
agree whenever the texture is no larger than the draw, so the earlier
single-level and chain results stand.

## Other gates on boot 308

V9XD32 `Result=PASS` at X8R8G8B8 and R5G6B5. T19 passes; T12, T18 and
T19's DXT cases are skipped as before.

## Evidence

The proxy source, 3DMark's error logs, the proxy log, V9XSNAP.INI,
V9XTSHP.INI and its PPMs, and V9XD32 reports are in session 7b86a0ab's
scratchpad (`nb30[678]-*`, `d3d8log/`, `tshp308/`), not in the repo.
