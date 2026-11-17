# Doom 3 through the ICD: the ARB path on the HD 4600

Date: 2026-11-15. Status: step 0 done
([census](../decisions/2026-11-15-doom3-demo-census-hd4600.md)).
Startup needed two exe patches: the guide's GlobalMemoryStatusEx rename
(demo offset 0x324D0E), and NOPs over the demo's `idMatX::Test()`
call, which faults on 98. The renderer then gets OPENGL32's generic
pixel format 5, never ours, because ours have no alpha and no stencil.
Nothing coded. Michael asked for
it on 2026-11-15, starting from a Win9x guide that gets the retail
`Doom3.exe` running on 98 SE by zeroing two bytes at file offset
0x374F1E. The guide's machine had a GeForce 7950 GT.

The machine is the HD 4600 (the 4570, 10.0.1.254). Its engine already
runs DirectX 8 pixel shaders and draws D3D fixed-function stages it
cannot otherwise draw (cubes, DOTPRODUCT3) as generated `ps.1.1`
programs. Most of what Doom 3 needs is in the driver already; the ICD
just doesn't reach it yet.

The title is the **Doom 3 demo** (id, 2004; archive.org item
`Doom3_201405`, `Doom 3.exe`, 483,603,646 bytes, MD5
`5172537e1ea114ec857766f6af06a211`), not the retail game. The demo
ships its own `Doom3.exe`, so the guide's offset does not apply as
written. Step 0 finds the same two bytes in the demo's executable.

## Why Doom 3 stops today

Everything in this section is read from id's GPL release of the
engine (`neo/renderer/RenderSystem_init.cpp`,
`R_CheckPortableExtensions`, and `neo/renderer/draw_arb.cpp`). None of
it has been measured on our ICD yet. Step 0 measures it.

- **Four extensions are fatal when missing:** `GL_ARB_multitexture`
  (with at least two units), `GL_ARB_texture_env_combine`,
  `GL_ARB_texture_cube_map` and `GL_ARB_texture_env_dot3`. Our
  `GL_EXTENSIONS` offers `GL_ARB_multitexture` and
  `GL_EXT_compiled_vertex_array` (`gl_icd.c`), so the renderer errors
  out at the second check.
- **Backends.** `r_renderer best` takes ARB2 (`ARB_vertex_program` +
  `ARB_fragment_program`), then NV20, R200 and NV10 (vendor combiner
  extensions), and otherwise **ARB**. ARB is the only backend this plan
  targets. Its interaction pass is multipass:
  - Normal map against the normalisation cube map, combined with
    `GL_DOT3_RGBA_ARB`, written into **destination alpha** with the
    colour mask set to alpha only.
  - Light falloff and projection, from `glTexGen` object-linear planes
    including Q (projective).
  - Then the diffuse map, blended with `GL_DST_ALPHA`.
- **Destination alpha.** Our pixel formats have no alpha bits.
  X8R8G8B8 says so on purpose (`v9x_gl_describe`: "the pad byte is not
  written as alpha"). The ARB path's bump term lands in that byte.
  Without it, interactions are drawn with garbage or full light.
- **Stencil.** Shadows are stencil volumes. No pixel format has a
  stencil buffer (`gl_state.h`), and no engine in the driver
  implements one (`d3d_core.c`: "Stencil has no buffer here").
  `r_shadows 0` lets the rest run while stencil is missing.
- **Optional, not required:** `GL_ARB_vertex_buffer_object`, S3TC,
  `GL_EXT_texture_edge_clamp` / `GL_CLAMP_TO_EDGE`,
  `GL_ARB_texture_env_add`, `GL_EXT_depth_bounds_test`,
  `GL_EXT_stencil_wrap`, anisotropy. Each is printed as found or not
  found, and step 0 lists them.

## What the driver has to build on

- **Two units through the render interface** (`V9X_R3D_DRAW.texture1`,
  ABI 4) and `GL_ARB_multitexture`
  ([opengl-arb-multitexture.md](opengl-arb-multitexture.md)). Unit 1
  carries s and t only; r and q are dropped.
- **Haswell's `draw_shader`** (`d3d_hsw.c`), which runs a parsed
  `ps.1.1` program over four stages that may be cubes or projected
  (`V9X_R3D_SHADER`, 28-float wide vertices). Gen3 has one as well
  (`d3d_i9xx.c`).
- **`v9x_d3d_ps_fixed`** (`d3d_ps.c`), which writes a `ps.1.1` program
  from up to three D3D texture stages: cube sampling, DOTPRODUCT3,
  ADD, MODULATE2X. It is how UT2004's cube stages draw on the 4570.
  An `ARB_texture_env_combine` unit is a D3D texture stage under other
  names: COMBINE_RGB/ALPHA is the op, SOURCEn/OPERANDn are the
  arguments, and DOT3_RGB(A) is DOTPRODUCT3.
- **The ICD has no texgen** (`glTexGen*` are stubs; the state slots
  exist in `gl_state.c`), **no cube targets** and **no combine state**.

So the work is mostly in the ICD and the render interface, plus a new
render-interface draw kind that hands a shader draw to the engine. The
engine work is destination alpha and, last, stencil.

## Steps

### 0. Install the demo and run it as it is (the census) - done

Done 2026-11-15; what it found is in the record. Two findings reorder
the steps below:
- **The ICD is never chosen.** Doom 3 asks for 8 alpha and 8 stencil
  bits and takes the generic format, so step 3 (an alpha format) and a
  stencil claim gate everything else. Step 3 moves ahead of step 1.
- **The extension list is now measured,** not read from source. It
  matches the "Why" section.

Open from step 0:
- The cause of the `idMatX::Test` fault.
- A real `GlobalMemoryStatusEx` shim: the rename makes Doom 3 read
  -16,775,168 MB of RAM and pick Low quality.

The original step 0 text follows.

- Unpack the demo on the 4570 (installed under `C:\D3DEMO`). Find the
  guide's two bytes in the demo's `Doom3.exe`: the guide gives "45 78"
  ("Ex") at the end of an import name. Record which import it is and
  its offset, and patch a copy, keeping the original as
  `DOOM3.ORG`.
- Run it with `+set logFile 2 +set r_mode 0 +set r_fullscreen 0 +set
  r_shadows 0` and read `base\qconsole.log`. That shows each extension
  as found or not, which backend it picks, the fatal error, and
  whether anything before the renderer fails first (DirectX 9.0c,
  memory, sound). V9XGL.LOG's `DrvGetProcAddress` lines name the entry
  points it fetches.
- Record: `docs/decisions/2026-11-15-doom3-demo-census-hd4600.md`.

### 1. Host: GL state for combine, dot3, cube maps and texgen

Test-first in `build-host`, no engine involved:

- `GL_TEXTURE_CUBE_MAP_ARB` as a texture target: six faces, binding,
  `glTexImage2D` on the face targets, completeness, and the queries.
- `glTexEnv` with `GL_COMBINE_ARB`: per unit, COMBINE_RGB/ALPHA,
  SOURCE0-2, OPERAND0-2, RGB_SCALE/ALPHA_SCALE, plus
  `GL_DOT3_RGB(A)_ARB`. Also attribute push/pop and queries.
- `glTexGen{i,f,d}[v]`: OBJECT_LINEAR, EYE_LINEAR and SPHERE_MAP
  planes for S, T, R and Q, evaluated in the vertex pipeline.
- Unit 1 keeps r and q (`V9X_GL_VERTEX.tex1` grows), since cube
  coordinates and projective light coordinates both need them.

The golden pipeline hash must not move for draws that use none of this.

### 2. Render interface ABI 9: the shader draw

- A new draw kind the ICD can submit: up to `V9X_R3D_SHADER_STAGES`
  CPU textures (2D or cube), each with its combine stated as D3D stage
  ops, and wide vertices with four coordinates per stage.
- The render interface validates the draw, has `v9x_d3d_ps_fixed`
  write the program, and passes it to the engine's `draw_shader`
  through a program table of its own, as `d3d_core.c` does for DDI 8.
- An engine without `draw_shader`, or a combine `v9x_d3d_ps_fixed`
  refuses, gets a counted refusal. That is never a silent wrong
  picture. The software engine refuses as well, so no CPU combiner is
  in scope.
- Adding a field is an ABI change: the HAL, ICD, Glide and 3DCIF
  version checks move together, and older DLLs refuse the new HAL.

### 3. Destination alpha on the HD 4600 (first, after step 0)

- First a measurement build only: a format claiming 8 alpha and 8
  stencil bits on 32 bpp, to see that OPENGL32 then picks it for Doom 3
  and which extension check stops it next. Not shipped: it claims bits
  nothing draws.
- An A8R8G8B8 pixel format (`cAlphaBits 8`) whose back buffer keeps
  alpha. Doom 3 also wants 24 depth and 8 stencil bits in the same
  format; until step 6, whether a format without stencil still wins
  over the generic ones is for the measurement build to show. Check how the back buffer is allocated (`gl_surface.c`) and
  whether the engine writes the target's alpha channel today.
- The write mask must reach alpha alone (`V9X_R3D_ABI_WRITE_*`), and
  `GL_DST_ALPHA` / `GL_ONE_MINUS_DST_ALPHA` factors must be honoured,
  not refused.
- Swap ignores alpha. The desktop stays X8R8G8B8.
- Probe: a new V9XGLP section that draws alpha-only and then blends
  `GL_DST_ALPHA`, read back exactly.

### 4. Advertise and probe

- `GL_ARB_texture_env_combine`, `GL_ARB_texture_env_dot3` and
  `GL_ARB_texture_cube_map` in `GL_EXTENSIONS`, only where the
  engine's describe says it has a shader draw. `GL_ARB_texture_env_add`
  comes along at no cost.
- V9XGLP sections, each read back on the 4570:
  - Combine ops, including DOT3 on a constant normal.
  - A cube face lookup per axis.
  - An object-linear texgen quad with projective Q.
- Netbook (Gen3 also has `draw_shader`): the same probes are run, not
  gated.

### 5. Doom 3 without shadows

- The demo's first map at 640x480 with `r_shadows 0` and low quality,
  windowed so screenshots see frames.
- Census the ICD's counters (refusals, stubs called, texture copies
  evicted) and fix what blocks the picture.
- Texture memory is the expected limit: Direct3D reports about 30 MB
  free on the 4570. `image_downSize` and the demo's low preset are the
  knobs, and the ICD's LRU evicts.
- `timedemo demo001` is the measurement. Scores are recorded, not
  chased.

### 6. Stencil (its own plan when reached)

Gen7.5 keeps stencil in a separate W-tiled buffer
(`3DSTATE_STENCIL_BUFFER`). It needs:

- A Z24S8 format.
- Stencil state in `V9X_R3D_DRAW` and the clear.
- Stencil, including `GL_INCR_WRAP`/`DECR_WRAP` if
  `GL_EXT_stencil_wrap` is offered, through the engine.
- D3D's Z24S8 alongside it.

Shadows in Doom 3 are the gate. Large enough to plan on its own once
step 5 runs.

## Not in scope

ARB2 / `ARB_fragment_program` and `ARB_vertex_program`. The NV and ATI
backends. The retail game's other ports (BFG Edition). The software
engine's combiners. Engines other than the HD 4600 and Gen3. Each of
these is its own decision.

## Risks

- The demo build has a second Win98 failure, `idMatX::Test`, skipped
  rather than understood. idMatX's factorisations are Doom 3's physics,
  so the same cause may surface in play.
- RAM is read wrongly through the renamed import (step 0). Doom 3
  sizes caches and quality from it.
- `v9x_d3d_ps_fixed` handles three stages. One ARB interaction pass
  uses two to four units, which step 0's census shows (from
  `RB_ARB_DrawInteraction`, not yet counted).
