# Colour keys on alpha-format textures: matching texels rewritten into the key

Date: 2026-11-21
Machine: remote-help `M-5CAPM3`, Radeon X1300 `1002:7146`.
Builds: HAL `a2f21261-dirty` on boots 51 (before) and 52 (after), each
V9XHAL.DLL alone by WININIT rename, staging empty and hash matched.
Plan: [`ati-rv515-x1300.md`](../plans/ati-rv515-x1300.md).

## The report

The owner: Incoming (1998, Direct3D) "has missing ui and some effects
missing but the game renders all the textures"; the options menu could
not be used because its text was not drawn.

## Measured, boot 51

Incoming windowed (`-notfullscreen -mouse -primary -nocd`), so the agent's
screenshot sees the frames. The intro drew. After about a minute:

- `M64Draws=4822849`, `M64Refused=245485`, every refusal
  `M64Policy08`: `V9X_D3D_RS690_REFUSE_COLOR_KEY`.
- `D3dColorKeySets=0`: the keys come with the surfaces, not through
  SetColorKey.

The RS690 colour-key work
([`2026-11-06-rs690-colour-key.md`](2026-11-06-rs690-colour-key.md))
took keyed draws on RGB565 textures only. The texture unit's chroma key
compares the texel as stored, alpha bits included, while Direct3D
compares colour only, so a keyed ARGB1555, ARGB4444 or ARGB8888 texture
was refused rather than drawn with keyed texels surviving.

About seven minutes into its attract loop the game raised its own error
box: exception `c0000005` at `465973`, inside INCOMING.EXE. Nothing had
been sent to it. It did not recur on boot 52 (six minutes, closed
normally). Not looked into further.

## The change

Before a keyed draw on a format with bits outside the colour (1555,
4444, 8888, XRGB8888), every texel whose colour matches the key is
rewritten into the key exactly, alpha bits and all; other texels are not
touched. The chroma key, comparing the stored texel, then discards the
texels Direct3D would. The rewritten texels are the discarded ones, so
their alpha is never seen. The rule is `v9x_d3d_rs690_key_texel`
(d3d_rs690_map.c, host-tested); the pass is `v9x_d3d_rs690_apply_key`
(d3d_rs690.c), once per upload through the colour-key table the ViRGE
uses (dirty on Unlock, a Blt into the texture, TextureSwap, SetColorKey),
every level the sampler reads, after `v9x_rs690_3d_begin` has left the
engine idle. The chip key is the key at the texel's width.

The ViRGE's rewrite (d3d_virge.c) also forces every other texel opaque,
because its unit keys through texture alpha; this one leaves them alone.

## Boot 52

- `M64Refused=0` (was 245,485), `D3dColorKeyDraws=790744`,
  `D3dColorKeyRewrites=20`, `M64Unrenderable=0`.
- Screenshots: the main menu's text (START GAME, LOAD GAME, GAME
  OPTIONS, EXIT, the logo), explosions, tracers and glows in the attract
  loop.

## Not checked

- Full screen, and the options pages themselves: asked of the owner.
- A mip level written by its own Unlock is not re-keyed (the dirty mark
  lands on that level's surface, not the keyed texture).
- The RS690 takes the same path, unmeasured.
