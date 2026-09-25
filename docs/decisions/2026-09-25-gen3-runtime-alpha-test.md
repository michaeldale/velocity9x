# Gen3 runtime alpha test: Half-Life's fences see-through

2026-09-25, MICHAEL-NETBOOK (945GSE / GMA 950), Win98 SE, boot 31.
Half-Life GOTY, 640x480x16, Direct3D renderer. HAL only (109,568 bytes)
by WININIT.INI rename; shared ABI stamp 2026092505 unchanged.

## The defect

With the engine blits in (`2026-09-25-gen3-engine-blits-half-life.md`)
the gaps between the red rails drew solid black where they should show
the scene behind (`...-engine-blits-panel.jpg`). The runtime Gen3 path
never read `ALPHATESTENABLE`, `ALPHAFUNC` or `ALPHAREF`: the S6 alpha
test had been built and measured for the Phase 6 scene only
(`2026-09-16-intel-gen3-alpha-test-and-blend-measured.md`, GREATER
against 0x80, exact), so every transparent texel drew. The counter that
should have said so, `AlphaTestUnexpressed`, was incremented by the ViRGE
engine alone, and read zero here.

The hypothesis this displaced: that the rail textures were among those
refused for shape and drawn untextured. The black was the texture's own
transparent colour, not a vertex colour, and the fix did not touch the
shape rule.

## The change

`v9x_i9xx_alpha_test_bits` (`src\common\i9xx_depth.c`, host-tested) maps
the three states to S6 bits 31:20 through the depth-compare table;
ALWAYS is no test. `v9x_i9xx_build_runtime_state` takes the field, and
the decoder requires the runtime stream's S6 to carry exactly what the
engine declares. The scenes pass zero, and their CRCs are unchanged.

ALPHAREF is D3DFIXED in the DirectX 5 header and a byte from DirectX 6;
up to 255 is read as a byte, above as 16.16. Half-Life sends 0, which is
the same either way; neither reading of a non-zero reference is measured.

## Measured (`...-halflife-alpha-test-V9XTRACE.ini`)

- The operator: "it looks perfect now" - the fences are see-through.
- `AlphaTestSets=20878`, functions GREATER, NOTEQUAL and ALWAYS
  (`AlphaTestFuncSeen=0x160`), reference 0, `AlphaTestUnexpressed=0` -
  now a real zero, since this engine counts it.
- 933,256 draws, none refused; 937,570 breadcrumbs, no timeouts, no
  abandons, no resets; 4,314 of 4,314 blits on the engine.

## Not settled

- `D3dTextureRefusedShape=39478` of 933,256 draws still drew untextured
  for a non-square or out-of-range texture (last refused 4 texels wide).
  No visible fault was reported for them.
- The unexplained reboots: boot 29 began while nothing here had asked for
  one, and this deploy's single reboot request advanced the counter from
  29 to 31.
