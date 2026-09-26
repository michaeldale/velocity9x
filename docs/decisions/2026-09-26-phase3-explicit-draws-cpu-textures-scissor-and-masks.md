# Phase 3: explicit draws - the software engine takes CPU textures, a scissor and a colour mask through the render interface, and Direct3D draws the same

Date: 2026-09-26
Plan: `docs/plans/opengl-1.1-icd.md`, Phase 2/3 (the CPU rasterizer's
features reached through the render interface; `accepts()` refusing
before emission)
Guest: 86Box `Win98SE-Fast-D3D` (port 9878, vbe package, `Direct3D=2`
software), boot 597, 1024x768x16 RGB565, package built from the tree this
record is committed with (HAL 126,464 bytes)
Evidence: `2026-09-26-phase3-explicit-draws-soft-V9XR3DP.ini` (render
interface probe), `...-soft-V9XDD.ini` and `...-soft-V9XDD2.ini`
(V9XDDP /mixed, both report parts)

## What changed

`V9X_R3D_DRAW` has an `explicit_state` flag with a scissor, a write mask,
and CPU texture levels with separate colour and alpha ops. The render
interface sets it on every request; the Direct3D front end zeroes it on
every draw. The software engine, on an explicit draw, honours everything
the CPU rasterizer implements - the CPU levels with mip selection, the
whole blend factor set, the alpha test, fog, the scissor, the mask, and
perspective from each triangle's rhw. The hardware engines' `accepts()`
refuse CPU levels, a scissor and a mask, so those arrive as UNSUPPORTED
without anything emitted.

## Measured

- V9XR3DP, every earlier check unchanged (clear, the three depth-tested
  quads, the five refusals). New:
  - a CPU texture whose declared storage is one texel short: `INVALID`;
  - the 4x4 magenta texture, REPLACE, over the whole target with the
    scissor on the top half: top `0xF81F`, bottom still `0x001F` from the
    previous draw - `TexturedScissorOk=1`;
  - white with only red writable over green: `0xFFE0`, yellow -
    `MaskedOk=1`.
- V9XDDP /mixed on the same boot: `Result=COMPLETE`, all three Phase 0.5
  cells pass, and report part 2 is identical to Michael's Phase 0.5
  software-guest report (`2026-09-26-phase05-mixed-soft-V9XDD.INI`) once
  per-process keys (timings, handles, runtime pointers, callback
  addresses) are set aside. Part 1 has no committed reference for this
  guest and was not compared.

## Not established

- Perspective and mip selection on this path: the probe's textured quad
  has equal rhw and one level. The GL texture slice will draw them.
- The hardware engines' refusals of explicit features on a guest: the
  ViRGE run of this probe is on the previous HAL, and the netbook is
  offline.
