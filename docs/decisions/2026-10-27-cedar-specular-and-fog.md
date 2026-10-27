# HD 5450 specular and fog: ALU literals and a third interpolant

Date: 2026-10-27

Status: **measured, one card.** M-BXMFA8's HD 5450 (`1002:68F9`, Cedar),
boots 25 to 27. Follows
[texture rows, 565 alpha and mip chains](2026-10-27-cedar-texture-pitch-and-565-alpha.md).
Captures: `V9XEG3D-p1` to `-p3` in
[`../probe/m-bxmfa8-cedar-d3d-streams-2026-10-27/`](../probe/m-bxmfa8-cedar-d3d-streams-2026-10-27/);
`V9XDD-b26-1`, `V9XDD2-b26-1`, `V9XDDT-b26-1` and `V9XD32-b26-*` in
[`../probe/m-bxmfa8-cedar-d3d-hal-2026-10-27/`](../probe/m-bxmfa8-cedar-d3d-hal-2026-10-27/).

## What the shaders do

The render core leaves the fog factor in the specular dword's alpha for
vertex and table fog alike (`d3d_fog.h`). So one pair of shaders covers
both, with no fog unit:

- the lit VS also fetches the specular dword (offset 20, four normalised
  bytes) into R3 and exports it as the last parameter;
- the lit PS interpolates it into R2, and after the texture op adds its RGB
  to the colour with `CLAMP` (specular), then computes
  `fog + f * (colour - fog)` per channel with `f` = R2.w (fog). Alpha is
  the colour's.

The fog colour enters as ALU literals: source select `0xFD`, the channel
picking the literal, the literal dwords following their instruction group,
padded to an even count and counted in the clause's length (Mesa's
`r600_asm.c`). The subtraction is `ADD` with `SRC1_NEG` (bit 25). Three
parameters need `SPI_VS_OUT_ID_0`'s `SEMANTIC_2` and
`SPI_PS_INPUT_CNTL_2`; the builder now writes semantic n for each input n.

None of this had been encoded before: literals, `NEG`, `CLAMP`, a third
interpolant, and an ALU group that reads one GPR from a previous group.
The host test fixes the encodings. Only the card can say whether they
mean what Mesa's sources say.

## The probe (boot 25)

V9XEG3D `/d3dfog` draws three bands with colour `80402010h` and fog colour
`00FF8000h`. Each band has its own specular dword: `AA102030h`, then
`00000000h` (all fog), then `FF000000h` (no fog, no specular).
`/d3dfogtex` draws the same over the pattern texture, modulated by white.
Each pixel is checked against the formula, ±2 per channel.

| Run | Mode | Result |
|---|---|---|
| p1 | `/d3dfog` | 0 of 512 wrong; band pixels `808A552B`, `80FF8000`, `80402010`, as computed |
| p2 | `/d3dfogtex` | 0 of 512 wrong |
| p3 | `/d3dcolour` (control) | 0 wrong |

`808A552B` is the formula's value to the bit. The literals land in BGR
order, `NEG` subtracts, `CLAMP` saturates, the third interpolant arrives,
and a group can read GPRs written by the group before it.

## The HAL (boot 26)

`d3d_eg_map.c` sends any draw with `SPECULARENABLE` or `FOGENABLE` to the
lit shaders. `SPECULARENABLE` defaults on, so that is most draws. The lit
VSs have shader slots. The lit PS is up to 92 dwords, which is more than a
256-byte slot, and the fog colour is in its code. So `d3d_eg.c` builds it
per draw and writes it into the vertex arena ahead of each batch's
vertices. The caps now offer vertex and table fog, and flat and Gouraud
specular.

V9XDDP, with both halves of its report compared with the GMA 950
netbook's 0.28.0 report (`nb234-V9XDD.INI` and `nb234-V9XDD2.INI`):

- `D3DFogMulOk`, `D3DFogTexOk`, `D3DTableFogOk`, `D3DDepthFogOk`,
  `D3DSpecularGouraudOk` and `D3DSpecularTexOk` pass. The GMA 950 fails
  the two specular checks.
- Both reports carry the same 227 `_Ok` keys. 13 fail here and 20 fail on
  the GMA 950, and every one of the 13 also fails there:
  `ColorKeyHalvesOk`, `D3DZPCompareOk`, `D3DZPWriteMaskOk`, `FlipPixelOk`,
  `MipLadderShapeOk`, `MipTriDegradedOk`, `MixedColorEncodingOk`,
  `MixedDepthEncodingOk`, `MixedOk`, `MixedOrderingOk`,
  `Tex8DecalAlpha4444Ok`, `Tex8DecalAlpha565Ok`, `Tex8X8888HalvesOk`.

V9XD32 passes at 16 and 32 bpp (boot 26).

## The comparison that was half done

On boots 19 to 24, V9XDDP's report seemed to end at
`TexM_64_4444_chain_lin_L` on every run. It was not a crash. The
write-through step log (`V9XDDT.TXT`) holds all 1,675 keys, and V9XDDP
continues in `V9XDD2.INI` once `V9XDD.INI` nears 24 KB. The GMA 950's
file splits at the same size. The earlier record compared the first half
only, and now carries a correction.

## Not checked

- The DX3 execute-buffer path (`V9xD3dExecute`) adds specular and fog to
  vertex colours on the CPU (`v9x_d3d_apply_vertex_color`), then hands
  the engine a draw whose fog and specular states are still on. By the
  code, the lit shaders would apply both a second time. The Rage 128
  engine also reads `fog_enable` from that draw. Nothing here has drawn
  through that path, so this is a reading of the code, not a measurement.
- Throughput: every draw now interpolates three parameters and the lit
  PS travels with each batch. No timing was taken.
