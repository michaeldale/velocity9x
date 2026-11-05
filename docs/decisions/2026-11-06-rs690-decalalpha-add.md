# RS690: DECALALPHA and ADD, and one shader assembler for the TL state

Date: 2026-11-06. Machine: M-GB8KRE (RS690 1002:791F, Win98 SE).
Build: branch `ati-rs690`, 0a0dcd02-dirty. Captures:
`docs/probe/rs690-texblend-2026-11-06/`.

## Decision

The HAL takes two more of Direct3D's DX5 texture blends instead of
refusing them, and publishes them in its caps:

- **DECALALPHA:** rgb = diffuse + (texel − diffuse) × texel.a.
- **ADD:** rgb = texel + diffuse, clamped.

Both keep the diffuse alpha, Direct3D 6's SELECTARG2 for those two
blends. DECALMASK and MODULATEMASK stay refused.

`V9X_D3DPTBLENDCAPS_ADD` (0x80, d3dcaps.h) is added to the ABI header.

The fragment shader for the TL state is now built in one place,
`v9x_rs690_add_shader`, as up to five MAD instructions. It replaces
three separate sets of overrides: texture alpha, specular and fog.

| Step | Instructions |
|---|---|
| combine: REPLACE, MODULATE, untextured | 1 (specular folds in as C) |
| combine: ADD | 1 |
| combine: DECALALPHA | 2: texel − diffuse unclamped, then × texel.a + diffuse |
| specular after ADD or DECALALPHA | 1 |
| fog | 2 (`2026-11-05-rs690-fog-probe.md`) |

- Instruction 0 makes the fragment's alpha. When it is not alone, a later
  last instruction moves that alpha out.
- Any RGB step that needs an alpha operand names that alpha's temp in the
  alpha address word, since SRCn_AAA reads the alpha unit's sources.
- One-instruction programs come out word for word as before. The host
  tests that pin the earlier REPLACE, MODULATE, specular and fog encodings
  pass unchanged.

The plain texture builder (`v9x_rs690_build_3d_texture`) refuses ADD and
DECALALPHA, because it has no room for the longer shader.

## What the probe measured

V9XRS3D, at 640x480x16 and 1024x768x32. Every earlier test still passes.

| Test | What | 16 bpp | 32 bpp |
|---|---|---|---|
| TexAdd | the gradient + diffuse (32, 64, 96) | within 0.33 step | exact |
| TexAddSpecular | the same + specular blue 64 (two instructions) | within 0.55 | exact |
| TexDecalAlpha | texture rows red with alpha y/15, over diffuse (64, 128, 192) | within 0.67 | within 0.47 |

The DECALALPHA texture is ARGB4444 at 16 bpp and ARGB8888 at 32 bpp, so
the alpha takes each of its 16 steps. The gradient texture used
elsewhere has almost no alpha.

## Through the HAL

Boot 73: V9XD32 passed two runs at 32 bpp and one at `/bpp16`, each with
Failures 0 and T15 clean.

V9XD32 draws only DECAL, so **no ADD or DECALALPHA draw has gone through
the HAL yet**. The mapping is host-tested (`test_d3d_rs690_map.c`).

## Not checked

- An application that uses either blend.
- ADD's clamp at the top. The gradient texels plus the diffuse stay
  below 255.
