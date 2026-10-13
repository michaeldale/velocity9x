# OpenGL draws on the Voodoo4 engine; subpixel correction puts its texels where both APIs want them

Date: 2026-10-13. Machine: A8U4I5, boots 442-444. Card: Voodoo4 4500 PCI on
Velocity9x. Evidence:
[a8u4i5-voodoo4-gl-2026-10-13](../probe/a8u4i5-voodoo4-gl-2026-10-13/).
Plan: [3dfx-vsa100-d3d.md](../plans/3dfx-vsa100-d3d.md), step 4. Follows
[2026-10-13-voodoo4-mipmaps-and-direct3d-pixel-centres.md](2026-10-13-voodoo4-mipmaps-and-direct3d-pixel-centres.md).

## What was built

- **The describe entry.** Engine 8, `V9X_R3D_ABI_ENGINE_TDFX`, renderer
  "Velocity9x VSA-100". Hardware textures are powers of two, 1 to 256.
  The shape flags cannot say "at most 8:1"; a longer rectangle is refused
  at draw.
- **Explicit draws.** The map takes the render interface's draws. Their
  scissor goes to clipLeftRight/clipTopBottom (half-open, section 8.44).
  An empty scissor, a partial write mask and CPU texture levels are still
  refused.
- **Centres by caller.** Direct3D draws move half a pixel down and right.
  Render-interface draws do not: the CPU rasterizer contract samples at
  +0.5, as the chip does.
- **Subpixel correction** (fbzColorPath bit 26) on every draw.

## The texel phase, and what subpixel correction changed

Half-Life on OpenGL (boot 442) showed the console-font fault Direct3D had
before its fix. Michael: "text issue is back".

V9XGLP gained a TexPhase scene, the OpenGL twin of V9XDDP's: a 16x16
texture drawn 1:1, GL_NEAREST, under glOrtho at offsets of 0, ±0.25 and
±0.5 pixel, reading which texel each pixel took. V9XDDP's scene gained
quarter- and eighth-pixel offsets.

- **Boot 442, no correction:**
  - OpenGL read the next texel along on whole pixels.
  - V9XDDP's eighths put coverage at x+0.5 to x+0.625 and the texture
    coordinates at x+0.9375 to x+1, in the chip's own coordinates.
- **Dead: a fixed texture lead.** Boot 443 moved S/W, T/W and 1/W back by
  half a pixel's gradient, a workaround with a host test. OpenGL's
  columns came right; its rows stayed a texel off. A fixed sampling
  offset cannot do that, and Direct3D's rows had matched its columns.
  The error depended on the triangle's orientation; the ICD's quads are
  flipped vertically.
- **What it was.** The setup unit starts colour, depth and texture
  parameters at the vertex unless fbzColorPath bit 26 asks it to correct
  them to the pixel centre (Napalm section 8.20). Glide sets it; the
  builder did not.
- **Boot 444, correction on, the lead removed:**
  - V9XGLP's TexPhase reads exactly OpenGL's rules at all five offsets,
    rows and columns, edge pixels included.
  - V9XDDP's reads exactly Direct3D's at all eleven, with the half-pixel
    shift kept, matching 3dfx's driver (boot 439).
  - V9XDDP's Gouraud ramp now reads 3dfx's value at all seven samples,
    not five of seven. The colour start had the same fault.
  - No V9XDDP pass key changed. PLodA_K, one perspective level pixel,
    moved off 3dfx's value; WHuge100 moved onto it.

## The gates

- **V9XGLP** (boot 442, again 444): PASS, `Renderer=Velocity9x VSA-100`.
  - Clear, scissor inside and outside, geometry, perspective texture and
    arrays read right.
  - The fog cases fail: fog is not built, for Direct3D either; the map
    refuses it (3 refusals).
- **Half-Life 1.1.1.0, `-gl`, 640x480x16, `timedemo mwd5`, boot 444**
  (driven through the agent):
  - 10.738, 12.522 and 12.515 fps;
  - the console text right ("Couldn't", "SIERRA");
  - no refusals, no timeouts.

  Direct3D on the same card ran 25.7.
- **Quake 2 3.14 demo, `map demo1`, `timerefresh`, boot 444:**
  - 20.73 fps (6.175 s);
  - the base, lightmaps, weapon and HUD drawn right;
  - no refusals, no timeouts.

  This install has no `demo1.dm2` or `demo2.dm2`.

Recorded, not chased. Michael parked performance (2026-10-13): OpenGL runs
at about half of Direct3D on every family, a core ICD cost to be taken on
later.

## Also seen

Half-Life faulted (invalid page fault, EIP 00000007) when closed by
`WCLOSE.EXE` at its main menu (boot 444). Quitting from its console exited
cleanly. Not investigated; it is Half-Life's shutdown, not a draw.

## Not established

- Whether 3dfx's own OpenGL (3dfxvgl) reads V9XGLP's TexPhase the same.
  OpenGL's rules are the reference here.
- Why PLodA_K moved.
- The rectangle short-side S/T unit is still Glide's convention, checked
  only by crisp text.
