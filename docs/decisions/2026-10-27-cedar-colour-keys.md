# HD 5450 colour keys: a kill in the pixel shader

Date: 2026-10-27

Status: **measured, one card.** M-BXMFA8's HD 5450 (`1002:68F9`, Cedar),
boot 35. Follows [OpenGL on the engine](2026-10-27-cedar-opengl-quake3.md).
Captures: `V9XDD-b35-key`, `V9XDD2-b35-key`, `V9XD32-b35-key-*` and
`V9XSNA7-b35-key` in
[`../probe/m-bxmfa8-cedar-d3d-hal-2026-10-27/`](../probe/m-bxmfa8-cedar-d3d-hal-2026-10-27/).

## What was there

`d3d_eg_map.c` refused every textured draw with COLORKEYENABLE, and the
software fallback drew it. V9XDDP's `ColorKeyOk` passed that way;
`ColorKeyHalvesOk` failed, as it does on the GMA 950.

## The shader

Evergreen's sampler has no colour key, so the pixel shader compares. The
texel is tested as sampled, before the texture op:

- R3 = texel − key, the key as literals;
- R3.x = max(|R3.x|, |R3.y|), then max(R3.x, |R3.z|);
- `KILLGT` with the tolerance as src0 and R3.x as src1.

The encodings are Mesa's `r600_opcodes.h` Evergreen values: `MAX` 3,
`KILLGT` 2Dh. Their neighbour `INTERP_XY` D6h matches the encoder's
measured value. ALU_WORD1_OP2's SRC0_ABS and SRC1_ABS are bits 0 and 1
(`eg_sq.h`).

- **The key.** It is the surface's `ddckCKSrcBltLow`, taken only when the
  surface has `DDRAWISURF_HASCKEYSRCBLT`. Each of B, G and R becomes its
  field's value over its maximum, which is how the sampler converts a
  unorm texel.
- **Tolerance.** Half a step of the finest field: 1/126 for 565, 1/62 for
  1555, 1/30 for 4444, 1/510 for 8888. So a point-sampled texel matches
  its own key and no other. The alpha bit of a 1555 or 4444 key is not
  compared.
- **Filtering.** A bilinear sample that mixes the key with another colour
  is not killed: the comparison is after filtering, as on the Voodoo's
  chroma key, not per texel before it.
- **The draw.** A keyed draw takes the lit shaders, whose PS is built per
  draw and sent with the batch, with one more GPR. DB_SHADER_CONTROL is
  set to 240h, KILL_ENABLE with Z after the shader, as Mesa's
  `evergreen_update_db_shader_control` does. Other draws keep the default
  state's 210h.
- **Tests and caps.** Host tests (`test_eg_shader.c`, `test_eg_3d.c`,
  `test_d3d_eg_map.c`) were written first and failed. The caps now
  include `D3DPTEXTURECAPS_TRANSPARENCY`.

## Boot 35

- V9XDDP, against boot 34 (the merged build): `ColorKeyHalvesOk` 0 → 1.
  The 565 and 1555 halves read 31 (blue) and 2016 (green). Every other
  `_Ok` key is unchanged. `ColorKeyOk` still passes.
- V9XD32 passes at 16 and 32 bpp.
- The HAL snapshot shows no colour-key refusals and the engine valid. The
  refusals left are V9XDDP's texture formats (3) and ops (2) that this
  engine does not offer.

## Not checked

- No game with colour-keyed textures has been run on this card.
- The bilinear fringe was not measured: no V9XDDP check filters a keyed
  texture.
