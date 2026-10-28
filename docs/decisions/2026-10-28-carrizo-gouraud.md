# Carrizo draws Gouraud triangles from Direct3D TL vertices, fetched by the VS through FLAT

Date: 2026-10-28

Status: **measured, one machine, two runs; passes.** M-XP7053's Radeon R7E
(`1002:9874`), the same session as
[the first rectangle](2026-10-28-carrizo-first-rectangle.md). This is step
1 of Phase 4 of [the 3D plan](../plans/ati-carrizo-gfx8-3d.md). Reports
are in
[`../probe/m-xp7053-carrizo-gouraud-2026-10-28/`](../probe/m-xp7053-carrizo-gouraud-2026-10-28/).

## What was built

- **`v9x_vi_shader_tl_vs`.** It takes the render core's 32-byte TL vertex
  (sx, sy, sz, rhw, D3DCOLOR, specular, tu, tv) from a buffer whose
  address is in user SGPRs s0:s1.
  - Two `flat_load_dwordx4`s fetch it from base + 32 x VertexID, with
    `v_add_u32`/`v_addc_u32` carrying into the high dword.
  - The position export is sx, sy, sz, rhw as loaded. The VTE is already
    set to screen-space XY, Z and 1/W.
  - The D3DCOLOR becomes parameter 0: `v_cvt_f32_ubyte2/1/0/3` for R, G,
    B, A, each times a 1/255 literal.
- **`v9x_vi_shader_gouraud_ps`.**
  - `s_mov_b32 m0, s0` (PRIM_MASK; the PS has no user SGPRs), then
    `s_nop 0` for the M0 wait state.
  - `v_interp_p1/p2_f32` on parameter 0's four channels, exported to
    MRT0.
- Every new encoding is checked against LLVM's GFX8 tests (VOP1, VOP2,
  FLAT, VINTRP, SOP1, SOPP). Both shaders are checked word for word
  against hand-assembled words. Every VALU word reads at most one SGPR,
  VCC or literal.
- **`v9x_vi_3d_tl_draw`**, a TRILIST of TL vertices. It shares the
  rectangle's cache sync, target, depth-off, screen-space and interface
  state, now factored into helpers. RSRC1's VGPR granule now comes from
  each shader's count; the VS has 13 VGPRs.
- The tests were written first and failed to build until the code
  existed.
- `V9XCZ3D /gouraud`: two triangles over the whole 64x16 target.
  - Red is 0 at the left edge and FFh at the right; green is 80h, blue
    40h, alpha FFh.
  - Every pixel is checked to +-2 per channel against round(255 (x +
    0.5) / 64).

## What ran

| Run | Draw | Result |
|---|---|---|
| 1 | rectangle, on the refactored builder | 256 pixels `BF4080FF`, nothing outside |
| 2 | `/gouraud` | all 1,024 pixels within +-2 |
| 3 | `/gouraud` again | the same |

Row 0 at x = 0, 8, 16 ... 56, 63 reads
`FF408002 FF408022 FF408042 FF408062 FF408081 FF4080A1 FF4080C1 FF4080E1 FF4080FD`.
That is red 02h to FDh in steps of 20h, the ramp at pixel centres. Cedar
gave the same bytes, and every row matches. The fence landed every time.
A grey capture of the desktop after the runs shows it drawn normally.

## What the evidence says

- **FLAT works as the vertex fetch.** No buffer descriptor (V#) is needed
  to read the HAL's vertices. VMID 0's flat addressing reaches the
  carve-out, as the probe stores already showed.
- **The VTE takes the TL position directly.** The interpolation is right
  across the whole target. The diagonal shared by the two triangles shows
  no seam, and no pixel was missed or drawn twice where it would show.
  Every pixel was within tolerance; a double draw would not show, because
  blending is off and both triangles hold the same colour there.
- **D3DCOLOR's byte order comes out right.** The ubyte2/1/0/3 mapping put
  the vertex red in R; the CB's RGBA memory order is the first rectangle's.

## Not checked

- rhw other than 1, so perspective-correct interpolation has not been
  checked. Cedar's `/d3dpersp` is the model.
- A vertex buffer that crosses a 4 GiB boundary (the carry path).
- Specular, which the VS loads but does not export.
- Depth, textures and the `COMP_SWAP` an ARGB target needs: the next
  steps.
