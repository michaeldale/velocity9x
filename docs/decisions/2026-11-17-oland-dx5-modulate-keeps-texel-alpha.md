# Oland: Nature's missing leaves were DX5 MODULATE multiplying the vertex's alpha

Date: 2026-11-17. Machine: W9QI0DAD (Oland XT `1002:6610`), boots 88-93.
Issue: [2026-11-17-oland-nature-tree-leaves-missing-most-frames.md](../issues/2026-11-17-oland-nature-tree-leaves-missing-most-frames.md).
Evidence: `../probe/oland-nature-w9qi0dad-2026-11-17/drawlog-1` to `-3`
and `leaves-fix-1`. Frame grabs are not committed: they are 3DMark's art.

## The measurement

A measurement HAL, not kept, logged every draw `d3d_vi.c` was handed in a
window of 40-48 flips. It recorded the state, the texture's place and
format, the render target, texture locks, and, in the third build, each
draw's vertex alpha range and a hash of its positions. The patch is in
the session scratchpad only.

Nature alone, three runs:
- **Every frame hands the engine the same draws.** About 1,317 draws,
  60,350-60,393 triangles, none failing. 541 are alpha-tested:
  - 540 of them carry the leaves: A8R8G8B8 textures, texture op
    MODULATE (DX5), `alpha_diffuse` 0;
  - alpha test GREATEREQUAL 7Fh, blend SRCALPHA / INVSRCALPHA, depth
    test and write on.

  The state words are identical from frame to frame. The two render
  targets alternate (0 and 300000h). No texture is locked during the
  scene.
- **The vertices' diffuse alpha is what changes** (drawlog-3, frames
  20-47). In some frames every leaf draw's vertex alpha is 9Ah or more.
  In the others, 214 of the 541 alpha-tested draws have every vertex
  alpha below 7Fh, down to 0. The pattern is 2-3 frames of the first
  kind, then about 5 of the second: the cadence the recording showed.
- **Why that matters here.** `d3d_vi_map.c` gave MODULATE and
  MODULATEALPHA the same pixel shader, which multiplies all four channels
  by the diffuse's. Its comment said the result "differs from Direct3D
  only where the vertex's alpha is below 1". DX5 MODULATE on a texture
  with alpha takes the texel's alpha alone. So whenever 3DMark's leaf
  vertices carried alpha below about one half, the product fell under
  the alpha test's 7Fh and the leaves were discarded.

What 3DMark means by the leaves' vertex alpha was not established.
Direct3D ignores it in this stage, so it does not reach the picture on
a conforming driver.

Hypotheses this killed:
- alpha state changing between frames: identical;
- texture eviction or relocation: no locks, the same places;
- culling: the triangle counts are the same;
- a partly drawn buffer on screen: the frame held through a 2 s hitch
  was a complete bare one;
- a stale shader instruction cache: every draw already syncs it.

## The fix

- `struct v9x_vi_d3d_ps` gains `alpha_texel`. With op MODULATE, the
  built PS multiplies B, G and R and keeps the texel's alpha.
- `d3d_vi_map.c` sets it for MODULATE on a texture with alpha when the
  core has not marked the stage's alpha as the diffuse's. Such a draw
  takes the built PS when the alpha can reach the picture: through the
  alpha test (already built) or a blend. Otherwise the slot PS serves,
  as before.

Tests:
- `test_gcn_sim.c`, first: MODULATE with `alpha_texel`, diffuse alpha
  0.3, texel alpha 0.75, alpha test GE 0.5. It failed (the pixel was
  killed, alpha 0.225), then passed.
- `test_d3d_vi_map.c`, written after the change, so not seen failing:
  the leaves' state takes the built PS with `alpha_texel`; a blend
  alone does too; neither keeps the slot PS. MODULATEALPHA, a texture
  without alpha, and the core's diffuse-alpha stage do not set it.

Gates: build-host.ps1 and run-checks.ps1 pass.

## On the card (boot 93, the fix without the logger)

Nature alone, ten grabs 20 flips apart (`leaves-fix-1`):
- all seven grabs of the cottage and the bridge show the trees in leaf;
- before the fix, most such grabs were bare (`drawlog-2`, `nature-1`);
- no refusal counted (`M64Policy*` 0 before and after);
- `EngineIdleTimeouts` 0.

Not shown by these grabs: frame-by-frame continuity. An OBS recording
of the run would show it.

## Not done

- **The same shortcut elsewhere.** `d3d_nv50_map.c` (GeForce 210),
  `d3d_eg_map.c` (Evergreen) and `d3d_r6_map.c` also map MODULATE and
  MODULATEALPHA to one modulate. `d3d_mga_map.c` and `d3d_nv3.c` group
  them too, and may be limited by their hardware. The Rage 128's map
  keeps the texel's alpha.
  - The GeForce 210's 3DMark 2000 tree popping
    ([issue](../issues/2026-11-16-gt218-3dmark2000-helicopter-trees-pop.md))
    fits the same cause; not measured.
  - Each back end needs its own change and its own card run.
- **Carrizo and Mullins** share `d3d_vi_map.c` and `vi_shader.c`, so they
  get this change; neither has run it.
- **A white frame** appears in two runs (mission time 17:06 and 21:77),
  with the whole picture washed out. It is probably Nature's sun glare,
  but it was not compared with another card.
