# CPU rasterizer contract: coordinates, coverage, interpolation and fragment order

Status: Phase 2 of the OpenGL plan (`docs/plans/opengl-1.1-icd.md`), host-tested
Scope: `src/display32/d3d/d3d_raster.c`, the software engine's rasterizer and
the render core's fallback for every engine. The Phase 3 render interface
freezes the vertex and draw layouts against this document, so anything a
front end relies on has to be written here first and pinned by a test in
`tests/host/test_d3d_raster.c`.

Where this document says "tested", the test is named. Where it says
"owed", nothing pins the behaviour yet and a front end must not rely on it.

## Coordinate space

- Vertex x and y are 28.4 fixed point (`V9X_D3D_RASTER_SUBPIXEL_BITS`), the
  origin at the target's top-left corner, y increasing down the rows in
  memory. Targets are at most 2048 pixels on a side
  (`V9X_D3D_RASTER_DIMENSION_MAX`, an overflow bound) and every coordinate
  is 0..`V9X_D3D_RASTER_COORD_MAX`; a vertex outside is refused, so the
  caller clips first.
- **OpenGL mapping.** A window coordinate (xw, yw) with yw increasing
  upward maps to surface (xw, height - yw). This is an exact reflection
  about the horizontal midline: GL pixel (i, j), whose centre is
  (i + 1/2, j + 1/2), is surface column i, row height - 1 - j, and its
  centre lands on that row's centre. Sample positions coincide, so nothing
  below changes under the flip. Winding reverses: a triangle that is
  counter-clockwise in window space is clockwise here. The rasterizer culls
  nothing; the GL front end evaluates `GL_FRONT_FACE` and `GL_CULL_FACE`
  in window space, before the conversion (plan, Phase 4).
- **Readback orientation.** Rows are top-down in memory. `glReadPixels`
  returns rows bottom-up, so the ICD reverses them; the rasterizer is not
  involved.

## Coverage

- The sample position is the pixel centre, (x + 1/2, y + 1/2).
- A pixel on a row is covered when its centre lies in the half-open span
  [left, right) between the triangle's two edge crossings on that row, and
  the row is covered when its centre lies in [top, bottom) of the
  triangle's vertical extent. `v9x_d3d_raster_first_centre` is the rule:
  the first pixel whose centre is at or past an edge. A centre exactly on
  a left or top edge is inside; exactly on a right or bottom edge it is
  not. Two triangles sharing an edge therefore cover each pixel along it
  once, with no gap.
  - Tested: the row and column rules by every test that checks a
    margin (`raster_check_untouched_margins`), and the shared diagonal by
    `test_perspective_divides_texture_coordinates`, which checks the same
    column on both sides of the quad's diagonal.
  - Owed: a test that blends a quad's two triangles and checks a pixel on
    the diagonal itself is blended once.
- A triangle whose three vertices share a row, or a span narrower than the
  distance to the next centre, draws nothing.
- **Scissor.** `V9X_D3D_RASTER_TARGET` carries a half-open rectangle
  [clip_left, clip_right) x [clip_top, clip_bottom) in pixels, inside the
  target; rows and columns are cut to it before any fragment is formed, so
  nothing outside is touched, depth included. An empty rectangle draws
  nothing; there is no "off" value, the whole target is the caller's
  choice. `glScissor(x, y, w, h)` in window coordinates is left = x,
  right = x + w, top = height - (y + h), bottom = height - y. Tested:
  `test_scissor_clips_colour_and_depth`.

## Interpolation

- **Colour** (four 0..255 channels): linear in screen space, sixteen
  fractional bits along a span, exact integer interpolation along an edge
  (`v9x_d3d_raster_edge_component` divides before it multiplies). Clamped
  to 0..255 once, before the texture stage. OpenGL 1.1 section 3.5.1
  permits linear colour; Direct3D's flat shading is the front end's
  business (`d3d_core.c`), not the rasterizer's.
- **Depth** (0..65535): linear in screen space, eight fractional bits.
  Direct3D's `sz` scaled by 65535 and OpenGL's window z after
  `glDepthRange` are both already screen-space, so a reversed depth range
  is nothing more than the caller's values; the compare functions are
  Direct3D's eight, numbered as Direct3D numbers them.
- **Texture coordinates** (16.16, 0 to 33 repeats): when the three
  vertices carry equal `q`, linear in screen space - the affine path every
  draw took before `q` existed, bit for bit
  (`test_perspective_equal_q_is_affine`). When they differ, u * q, v * q
  and q are interpolated linearly and each textured pixel divides them
  back: perspective correction, which OpenGL 1.1 requires for texture
  coordinates and which Direct3D's software engine does not yet claim.
  - `q` is the reciprocal of the homogeneous w as 1.16, normalised by the
    caller so the triangle's nearest vertex is `V9X_D3D_RASTER_Q_ONE` and
    no vertex is below 1 - only ratios within one triangle matter. A
    triangle whose depth ratio exceeds 65536:1 is the caller's to clamp.
  - Projective texturing (a varying texture q): the caller passes
    q = q_tex * (1 / w), normalised, and u = s, v = t; the divide then
    yields s / q_tex. There is one divisor per pixel; the plan's warning
    that one reciprocal-w field cannot serve every interpolation rule is
    met by colour and depth not using it at all.
  - Precision: the per-pixel reciprocal is truncated, costing at most
    1 in 16384 of the coordinate at a triangle's near vertex - a texel at
    33 repeats on a 512-texel axis, a thirty-second of one at a single
    repeat - and the split multiply loses under three units of 16.16.
    `test_perspective_divides_texture_coordinates` checks a coordinate
    1.013 texels past a boundary, two hundred units against that bound.
  - Tested: `test_perspective_divides_texture_coordinates`,
    `test_perspective_equal_q_is_affine`, `test_perspective_refusals`.
- **Fog:** a per-vertex factor, 0..255 with 255 unfogged and 0 the fog
  colour (Direct3D's specular alpha and OpenGL's f), interpolated linearly
  like colour and clamped where read; the caller computes it from its own
  eye-space depth. The draw carries the fog colour, null for off. Applied
  after the texture stage and before the alpha test and the blend, to
  colour only (`test_fog_mixes_toward_fog_colour`,
  `test_fog_order_and_refusals`).
- **Specular:** not carried by the rasterizer; the front end adds it into
  the colour (Direct3D's core does; the GL front end will).

## Texture sampling

- Per-axis sizes that are powers of two from 1 to 512
  (`test_texture_non_square_point_sampling`), and a mip chain of up to
  nine further levels, each extent max(1, half the previous), so a
  non-square chain ends in a 1xN or Nx1 tail before 1x1; a chain that
  skips a level is refused, as is POINT or LINEAR without a chain
  (OpenGL's incomplete texture; `test_mip_chain_refusals_and_tails`).
- **Level of detail.** ρ is the largest of the four absolute derivatives
  of the texture coordinates, each scaled to level-0 texels per pixel -
  the bound OpenGL 1.1 section 3.8.5 permits in place of the exact form -
  and λ = log2 ρ, its fraction from a sixteen-entry table (about a
  fiftieth of a level). The y derivatives come from the triangle's plane;
  on the affine path λ is constant along a span and chosen once, on the
  perspective path it is formed per pixel from the derivatives of
  (u·q)/q, never from a span's ends
  (`test_mip_perspective_selects_per_pixel`). POINT (D3D's, GL's
  `*_MIPMAP_NEAREST`) takes level round(λ); LINEAR (`*_MIPMAP_LINEAR`)
  blends levels floor(λ) and floor(λ) + 1 by the fraction; both clamp to
  the chain, and λ ≤ 0 is level 0 alone
  (`test_mip_point_selects_by_scale`, `test_mip_linear_blends_levels`).
  NONE samples level 0 whatever the chain holds.
- **Point:** texel index = floor(u * width). Under WRAP the index is
  taken modulo the size, so the coordinate tiles; under CLAMP a coordinate
  at or past 1 takes the last texel and one below 0 the first, per axis.
- **Linear:** texel centres sit at (i + 1/2) / width, the OpenGL and
  Direct3D convention; the four weights come from the fractional distance
  to the neighbouring centres. The four alphas are blended with the same
  weights when the alpha combine consumes them or DECAL uses them as its
  colour lerp factor.
- **Texel alpha:** ARGB1555's bit is 0 or 255, ARGB4444's nibble is
  replicated (17 * n), RGB565 is opaque. `V9X_D3D_RASTER_TEXTURE.alpha`
  says whether it is ignored, replaces the vertex alpha, or multiplies it
  (`test_texture_alpha_replace_and_modulate`).
- **Colour combine:** REPLACE takes the texel; MODULATE multiplies texel and
  fragment; DECAL on an alpha-bearing texture lerps fragment toward texel by
  texel alpha; GL BLEND lerps each fragment channel toward the corresponding
  environment-colour channel by that texel-colour channel. The front end
  chooses the independent colour and alpha operations from the API mode and
  logical texture format: in particular GL MODULATE on RGBA and D3D
  MODULATEALPHA multiply alpha, while legacy D3D MODULATE leaves it alone
  (`test_texture_colour_combine_ops`,
  `test_texture_alpha_replace_and_modulate`).

## Fragment order

For each covered pixel, in this order:

1. Scissor - already applied to the rows and columns.
2. Depth test: compare only, against the bound depth buffer.
3. Texture sample and colour combine; then fog, if the draw carries a
   colour; then the fragment alpha, resolved only when something consumes
   it: the vertex alpha, then the texel's under REPLACE or MODULATE.
4. Alpha test (`V9X_D3D_RASTER_ALPHA_TEST`, compare and 0..255
   reference). A fragment that fails writes nothing - neither colour nor
   depth (`test_alpha_test_gates_colour_and_depth`).
5. Depth write, if the buffer's write flag is set.
6. Blend. The five legacy pairs (source ONE, SRCALPHA or DESTCOLOR with
   destination ZERO or INVSRCALPHA) run on 0..256 weights so that opaque is
   exact; every other pair of the eleven `V9X_D3D_RASTER_FACTOR_*` runs on
   the general path, each factor resolved per channel to 0..255, each
   product divided by 255 exactly, the sum saturated. No target carries
   alpha, so DESTALPHA is ONE and INVDESTALPHA and SRCALPHASAT are ZERO.
   The two paths may differ by one level in the last place on a pair
   both can express; no such pair exists, since a pair is on exactly one
   path. (`test_alpha_*`, `test_factor_*`, `test_blend_reads_target_format`.)
7. Colour mask and store: with all three channel flags set the packed
   pixel is stored whole; with any clear, the store keeps the other bits
   of what was there, including XRGB1555's unused top bit. A draw with
   every channel masked still writes depth
   (`test_colour_mask_keeps_channels`).

OpenGL orders these scissor, alpha test, stencil, depth, blend, mask. The
order above is result-equivalent: the depth compare does not depend on the
alpha test's outcome, depth is written only for a fragment that passed
both, there is no stencil, and no target has alpha to blend into.

## Owed before Phase 3 freezes the layouts

- **Unit points and lines:** `r3d_line.c` assigns a point to `floor(x),
  floor(y)`. Lines use an integer Bresenham walk after Liang-Barsky clipping,
  own the first fragment and exclude the application's final fragment. A
  clipped high endpoint is included when it is not the application's endpoint.
  Attributes are evaluated at each covered pixel centre by projection onto
  the original segment. This makes adjacent strip segments meet without a
  doubled blend or a gap (`test_r3d_line.c`). Width, stipple and smoothing
  remain Phase 6 work.
- The clear operation's interaction with the scissor and the mask.
