# Generates docs/plans/opengl-1.1-requirements.md from src/opengl/gl_entrypoints.psd1.
# One row per dispatch slot, classified by name into an OpenGL 1.1 specification
# section, an owning plan phase and module, and the planned test. Evidence is
# "none" for every row until a gate records otherwise. Run from the repo root.

$manifest = Import-PowerShellDataFile -LiteralPath 'src\opengl\gl_entrypoints.psd1'
$entries = $manifest.Entries

# Order matters: the first matching pattern wins.
$rules = @(
    # Display lists (5.4)
    @{ P = '^gl(NewList|EndList|CallLists?|ListBase|GenLists|IsList|DeleteLists)$'; S = '5.4 Display lists'; Ph = '6'; M = 'gl_dlist.c'; T = 'test_gl_dlist.c' }
    # Begin/End and vertex specification (2.6, 2.7)
    @{ P = '^gl(Begin|End)$'; S = '2.6 Begin/End paradigm'; Ph = '4'; M = 'gl_prim.c'; T = 'test_gl_prim.c' }
    @{ P = '^glEdgeFlag(v)?$'; S = '2.6.2 Polygon edges'; Ph = '6'; M = 'gl_prim.c'; T = 'test_gl_prim.c' }
    @{ P = '^glVertex[234](b|s|i|f|d)v?$'; S = '2.7 Vertex specification'; Ph = '4'; M = 'gl_vertex.c'; T = 'test_gl_vertex.c' }
    @{ P = '^glTexCoord[1234](b|s|i|f|d)v?$'; S = '2.7 Vertex specification (texture coordinates)'; Ph = '4'; M = 'gl_vertex.c'; T = 'test_gl_vertex.c' }
    @{ P = '^glNormal3(b|s|i|f|d)v?$'; S = '2.7 Vertex specification (normals)'; Ph = '4'; M = 'gl_vertex.c'; T = 'test_gl_vertex.c' }
    @{ P = '^glColor[34](b|s|i|f|d|ub|us|ui)v?$'; S = '2.7 Vertex specification (colours)'; Ph = '4'; M = 'gl_vertex.c'; T = 'test_gl_vertex.c' }
    @{ P = '^glIndex(s|i|f|d|ub)v?$'; S = '2.7 Vertex specification (colour index)'; Ph = '6'; M = 'gl_vertex.c'; T = 'test_gl_vertex.c' }
    # Vertex arrays (2.8)
    @{ P = '^gl(Vertex|Normal|Color|Index|TexCoord|EdgeFlag)Pointer$'; S = '2.8 Vertex arrays'; Ph = '4'; M = 'gl_varray.c'; T = 'test_gl_varray.c' }
    @{ P = '^gl(EnableClientState|DisableClientState|ArrayElement|DrawArrays|DrawElements|InterleavedArrays)$'; S = '2.8 Vertex arrays'; Ph = '4'; M = 'gl_varray.c'; T = 'test_gl_varray.c' }
    # Rectangles (2.9)
    @{ P = '^glRect(s|i|f|d)v?$'; S = '2.9 Rectangles'; Ph = '6'; M = 'gl_prim.c'; T = 'test_gl_prim.c' }
    # Coordinate transformations (2.10)
    @{ P = '^gl(DepthRange|Viewport)$'; S = '2.10.1 Controlling the viewport'; Ph = '4'; M = 'gl_xform.c'; T = 'test_gl_xform.c' }
    @{ P = '^gl(MatrixMode|LoadMatrix[fd]|MultMatrix[fd]|LoadIdentity|Rotate[fd]|Translate[fd]|Scale[fd]|Frustum|Ortho|PushMatrix|PopMatrix)$'; S = '2.10.2 Matrices'; Ph = '4'; M = 'gl_matrix.c'; T = 'test_gl_matrix.c' }
    @{ P = '^glTexGen(i|f|d)v?$'; S = '2.10.4 Generating texture coordinates'; Ph = '6'; M = 'gl_texgen.c'; T = 'test_gl_texgen.c' }
    # Clipping (2.11)
    @{ P = '^glClipPlane$'; S = '2.11 Clipping'; Ph = '4'; M = 'gl_clip.c'; T = 'test_gl_clip.c' }
    # Raster position (2.12)
    @{ P = '^glRasterPos[234](s|i|f|d)v?$'; S = '2.12 Current raster position'; Ph = '6'; M = 'gl_raster.c'; T = 'test_gl_raster.c' }
    # Colours and colouring, lighting (2.13)
    @{ P = '^glLight(i|f)v?$'; S = '2.13.1 Lighting'; Ph = '4, completed in 6'; M = 'gl_light.c'; T = 'test_gl_light.c' }
    @{ P = '^glLightModel(i|f)v?$'; S = '2.13.1 Lighting'; Ph = '6'; M = 'gl_light.c'; T = 'test_gl_light.c' }
    @{ P = '^glMaterial(i|f)v?$'; S = '2.13.2 Lighting parameter specification'; Ph = '4, completed in 6'; M = 'gl_light.c'; T = 'test_gl_light.c' }
    @{ P = '^glColorMaterial$'; S = '2.13.3 ColorMaterial'; Ph = '6'; M = 'gl_light.c'; T = 'test_gl_light.c' }
    @{ P = '^glFrontFace$'; S = '2.13.1 Lighting (front and back faces)'; Ph = '4'; M = 'gl_prim.c'; T = 'test_gl_prim.c' }
    @{ P = '^glShadeModel$'; S = '2.13.7 Flatshading'; Ph = '4'; M = 'gl_prim.c'; T = 'test_gl_prim.c' }
    # Rasterization (3)
    @{ P = '^glPointSize$'; S = '3.3 Points'; Ph = '6'; M = 'gl_state.c'; T = 'test_gl_scene.c' }
    @{ P = '^gl(LineWidth|LineStipple)$'; S = '3.4 Line segments'; Ph = '6'; M = 'gl_state.c'; T = 'test_gl_scene.c' }
    @{ P = '^glCullFace$'; S = '3.5.1 Basic polygon rasterization'; Ph = '4'; M = 'gl_prim.c'; T = 'test_gl_prim.c' }
    @{ P = '^glPolygonStipple$'; S = '3.5.2 Stippling'; Ph = '6'; M = 'gl_state.c'; T = 'test_gl_scene.c' }
    @{ P = '^glPolygonMode$'; S = '3.5.4 Options controlling polygon rasterization'; Ph = '4'; M = 'gl_prim.c'; T = 'test_gl_prim.c' }
    @{ P = '^glPolygonOffset$'; S = '3.5.5 Depth offset'; Ph = '6'; M = 'gl_xform.c'; T = 'test_gl_scene.c' }
    @{ P = '^glPixelStore(i|f)$'; S = '3.6.1 Pixel storage modes'; Ph = '4'; M = 'gl_pixelstore.c'; T = 'test_gl_pixelstore.c' }
    @{ P = '^glPixelTransfer(i|f)$'; S = '3.6.3 Pixel transfer modes'; Ph = '6'; M = 'gl_pixel.c'; T = 'test_gl_pixel.c' }
    @{ P = '^glPixelMap(fv|uiv|usv)$'; S = '3.6.3 Pixel transfer modes (pixel maps)'; Ph = '6'; M = 'gl_pixel.c'; T = 'test_gl_pixel.c' }
    @{ P = '^glDrawPixels$'; S = '3.6.4 Rasterization of pixel rectangles'; Ph = '6'; M = 'gl_pixel.c'; T = 'test_gl_pixel.c' }
    @{ P = '^glPixelZoom$'; S = '3.6.4 Rasterization of pixel rectangles (zoom)'; Ph = '6'; M = 'gl_pixel.c'; T = 'test_gl_pixel.c' }
    @{ P = '^glBitmap$'; S = '3.7 Bitmaps'; Ph = '6'; M = 'gl_pixel.c'; T = 'test_gl_pixel.c' }
    @{ P = '^glTexImage2D$'; S = '3.8 Texturing (TexImage2D)'; Ph = '4'; M = 'gl_teximage.c'; T = 'test_gl_teximage.c' }
    @{ P = '^glTexImage1D$'; S = '3.8 Texturing (TexImage1D)'; Ph = '6'; M = 'gl_teximage.c'; T = 'test_gl_teximage.c' }
    @{ P = '^glTexSubImage2D$'; S = '3.8.1 Alternate image specification (TexSubImage2D)'; Ph = '4'; M = 'gl_teximage.c'; T = 'test_gl_teximage.c' }
    @{ P = '^gl(TexSubImage1D|CopyTexImage[12]D|CopyTexSubImage[12]D)$'; S = '3.8.1 Alternate image specification'; Ph = '6'; M = 'gl_teximage.c'; T = 'test_gl_teximage.c' }
    @{ P = '^glTexParameter(i|f)v?$'; S = '3.8.2 Texture parameters'; Ph = '4'; M = 'gl_texobj.c'; T = 'test_gl_texobj.c' }
    @{ P = '^gl(BindTexture|GenTextures|DeleteTextures|IsTexture)$'; S = '3.8.8 Texture objects'; Ph = '4'; M = 'gl_texobj.c'; T = 'test_gl_texobj.c' }
    @{ P = '^gl(AreTexturesResident|PrioritizeTextures)$'; S = '3.8.8 Texture objects (residency and priority)'; Ph = '6'; M = 'gl_texobj.c'; T = 'test_gl_texobj.c' }
    @{ P = '^glTexEnv(i|f)v?$'; S = '3.8.9 Texture environments and texture functions'; Ph = '4'; M = 'gl_texobj.c'; T = 'test_gl_scene.c' }
    @{ P = '^glFog(i|f)v?$'; S = '3.9 Fog'; Ph = '4'; M = 'gl_fog.c'; T = 'test_gl_fog.c' }
    # Per-fragment operations and the framebuffer (4)
    @{ P = '^glScissor$'; S = '4.1.2 Scissor test'; Ph = '4'; M = 'gl_state.c'; T = 'test_gl_scene.c' }
    @{ P = '^glAlphaFunc$'; S = '4.1.3 Alpha test'; Ph = '4'; M = 'gl_state.c'; T = 'test_gl_scene.c' }
    @{ P = '^glStencil(Func|Op)$'; S = '4.1.4 Stencil test'; Ph = '6'; M = 'gl_state.c'; T = 'test_gl_state.c' }
    @{ P = '^glDepthFunc$'; S = '4.1.5 Depth buffer test'; Ph = '4'; M = 'gl_state.c'; T = 'test_gl_scene.c' }
    @{ P = '^glBlendFunc$'; S = '4.1.6 Blending'; Ph = '4'; M = 'gl_state.c'; T = 'test_gl_scene.c' }
    @{ P = '^glLogicOp$'; S = '4.1.8 Logical operation'; Ph = '6'; M = 'gl_state.c'; T = 'test_gl_scene.c' }
    @{ P = '^glDrawBuffer$'; S = '4.2.1 Selecting a buffer for writing'; Ph = '4'; M = 'gl_surface.c'; T = 'V9XGLP front/back scene' }
    @{ P = '^gl(ColorMask|DepthMask)$'; S = '4.2.2 Fine control of buffer updates'; Ph = '4'; M = 'gl_state.c'; T = 'test_gl_scene.c' }
    @{ P = '^gl(IndexMask|StencilMask)$'; S = '4.2.2 Fine control of buffer updates'; Ph = '6'; M = 'gl_state.c'; T = 'test_gl_state.c' }
    @{ P = '^gl(Clear|ClearColor|ClearDepth)$'; S = '4.2.3 Clearing the buffers'; Ph = '4'; M = 'gl_state.c'; T = 'test_gl_scene.c' }
    @{ P = '^gl(ClearIndex|ClearStencil|ClearAccum)$'; S = '4.2.3 Clearing the buffers'; Ph = '6'; M = 'gl_state.c'; T = 'test_gl_state.c' }
    @{ P = '^glAccum$'; S = '4.2.4 The accumulation buffer'; Ph = '6'; M = 'gl_state.c'; T = 'test_gl_state.c' }
    @{ P = '^glReadPixels$'; S = '4.3.2 Reading pixels'; Ph = '5, completed in 6'; M = 'gl_pixel.c'; T = 'test_gl_pixel.c' }
    @{ P = '^glReadBuffer$'; S = '4.3.2 Reading pixels (source buffer)'; Ph = '4'; M = 'gl_surface.c'; T = 'V9XGLP front/back scene' }
    @{ P = '^glCopyPixels$'; S = '4.3.3 Copying pixels'; Ph = '6'; M = 'gl_pixel.c'; T = 'test_gl_pixel.c' }
    # Special functions (5)
    @{ P = '^gl(Map[12][fd]|EvalCoord[12][fd]v?|MapGrid[12][fd]|EvalMesh[12]|EvalPoint[12])$'; S = '5.1 Evaluators'; Ph = '6'; M = 'gl_eval.c'; T = 'test_gl_eval.c' }
    @{ P = '^gl(InitNames|PopName|PushName|LoadName|SelectBuffer|RenderMode)$'; S = '5.2 Selection'; Ph = '6'; M = 'gl_select.c'; T = 'test_gl_select.c' }
    @{ P = '^gl(FeedbackBuffer|PassThrough)$'; S = '5.3 Feedback'; Ph = '6'; M = 'gl_select.c'; T = 'test_gl_select.c' }
    @{ P = '^gl(Flush|Finish)$'; S = '5.5 Flush and Finish'; Ph = '4'; M = 'gl_icd.c'; T = 'V9XGLP ordering scene' }
    @{ P = '^glHint$'; S = '5.6 Hints'; Ph = '4'; M = 'gl_state.c'; T = 'test_gl_state.c' }
    # State and state requests (6)
    @{ P = '^gl(Enable|Disable|IsEnabled)$'; S = '6.1.1 Simple queries / 2.11.1 Enable'; Ph = '4'; M = 'gl_state.c'; T = 'test_gl_state.c' }
    @{ P = '^glGet(Boolean|Integer|Float|Double)v$'; S = '6.1.1 Simple queries'; Ph = '4, completed in 6'; M = 'gl_get.c'; T = 'test_gl_get.c' }
    @{ P = '^glGetError$'; S = '2.5 GL errors'; Ph = '4'; M = 'gl_state.c'; T = 'test_gl_state.c' }
    @{ P = '^glGetString$'; S = '6.1.5 String queries'; Ph = '4'; M = 'gl_get.c'; T = 'test_gl_get.c' }
    @{ P = '^glGetPointerv$'; S = '6.1.3 Enumerated queries (vertex array pointers)'; Ph = '4'; M = 'gl_varray.c'; T = 'test_gl_varray.c' }
    @{ P = '^glGetTex(Parameter|LevelParameter|Env|Gen)(i|f|d)v$'; S = '6.1.3 Enumerated queries (texture)'; Ph = '4, completed in 6'; M = 'gl_get.c'; T = 'test_gl_get.c' }
    @{ P = '^glGetTexImage$'; S = '6.1.4 Texture queries'; Ph = '6'; M = 'gl_teximage.c'; T = 'test_gl_teximage.c' }
    @{ P = '^glGet(Light|Material)(i|f)v$'; S = '6.1.3 Enumerated queries (lighting)'; Ph = '6'; M = 'gl_get.c'; T = 'test_gl_get.c' }
    @{ P = '^glGetMap(iv|fv|dv)$'; S = '6.1.3 Enumerated queries (evaluators)'; Ph = '6'; M = 'gl_get.c'; T = 'test_gl_get.c' }
    @{ P = '^glGetPixelMap(fv|uiv|usv)$'; S = '6.1.3 Enumerated queries (pixel maps)'; Ph = '6'; M = 'gl_get.c'; T = 'test_gl_get.c' }
    @{ P = '^glGetPolygonStipple$'; S = '6.1.3 Enumerated queries (stipple)'; Ph = '6'; M = 'gl_get.c'; T = 'test_gl_get.c' }
    @{ P = '^glGetClipPlane$'; S = '6.1.3 Enumerated queries (clip planes)'; Ph = '4'; M = 'gl_get.c'; T = 'test_gl_get.c' }
    @{ P = '^gl(PushAttrib|PopAttrib|PushClientAttrib|PopClientAttrib)$'; S = '6.1.11 Attribute stacks'; Ph = '6'; M = 'gl_state.c'; T = 'test_gl_state.c' }
)

$rows = @()
$unmatched = @()
foreach ($e in $entries) {
    $hit = $null
    foreach ($r in $rules) {
        if ($e.Name -match $r.P) { $hit = $r; break }
    }
    if ($null -eq $hit) { $unmatched += $e.Name; continue }
    $rows += [pscustomobject]@{
        Slot = $e.Slot; Name = $e.Name; Section = $hit.S; Phase = $hit.Ph; Module = $hit.M; Test = $hit.T
    }
}
if ($unmatched.Count -gt 0) {
    throw ("Unclassified entry points: " + ($unmatched -join ', '))
}

$byPhase = @{}
foreach ($r in $rows) {
    $k = $r.Phase
    if (-not $byPhase.ContainsKey($k)) { $byPhase[$k] = 0 }
    $byPhase[$k]++
}

$out = New-Object System.Collections.Generic.List[string]
$out.Add('# OpenGL 1.1 requirements inventory')
$out.Add('')
$out.Add('Status: Open. Generated 2026-09-26 from `src/opengl/gl_entrypoints.psd1` by')
$out.Add('`scripts/generate-opengl-requirements.ps1`; the cross-cutting section is written by hand there. This')
$out.Add('is the inventory `docs/plans/opengl-1.1-icd.md` asks for in Phase 0: one row')
$out.Add('per dispatch slot, plus the requirements no single entry point owns.')
$out.Add('')
$out.Add('Section numbers are the OpenGL 1.1 specification''s')
$out.Add('(<https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf>). Phase and')
$out.Add('module are the plan''s. The test column names the host test or probe scene')
$out.Add('that is *planned* to cover the row. **Evidence is `none` for every row**')
$out.Add('until a gate records otherwise; a slot that merely exists and returns is')
$out.Add('not evidence, and the plan says so. Update this file when a row gains')
$out.Add('evidence, naming the decision doc that holds it.')
$out.Add('')
$out.Add('Counts by owning phase: ' + (($byPhase.Keys | Sort-Object | ForEach-Object { "phase $_ = $($byPhase[$_])" }) -join '; ') + "; total $($rows.Count).")
$out.Add('')
$out.Add('## Cross-cutting requirements')
$out.Add('')
$out.Add('These are not owned by one entry point. Each has to be closed with its own')
$out.Add('evidence before the Phase 6 gate.')
$out.Add('')
$out.Add('| Requirement | Specification | Phase | Planned evidence | Evidence |')
$out.Add('|---|---|---|---|---|')
$cross = @(
    ,@('Error semantics: first error is retained, `GL_INVALID_OPERATION` inside Begin/End, `GL_INVALID_ENUM`/`GL_INVALID_VALUE` per call, `GL_OUT_OF_MEMORY` behaviour, and no error from a temporary stub being mistaken for conformance', '2.5', '4, completed in 6', '`test_gl_state.c` legal/illegal call tables; the stub tracker below', 'none')
    ,@('Default state for every state variable in the state tables', '6.2 tables 6.5-6.29', '4, completed in 6', '`test_gl_get.c` asserts every default after context creation', 'none')
    ,@('Implementation limits (`GL_MAX_*`, `GL_SUBPIXEL_BITS`, stack depths, `GL_MAX_TEXTURE_SIZE`) describe the implementation including software, not one engine', '6.2 table 6.29', '4', '`test_gl_get.c`; the advertised texture limit is honoured on every engine through software', 'none')
    ,@('Primitive assembly for all ten `glBegin` modes, provoking vertex (last; first for `GL_POLYGON`), edge flags, invalid vertices between Begin/End', '2.6', '4', '`test_gl_prim.c`', 'none')
    ,@('Vertex transformation, clip-space clipping against six frustum and six user planes, perspective divide, viewport and reversed depth range', '2.10-2.11', '4', '`test_gl_clip.c`, `test_gl_xform.c`; reversed range scene against generic GL', 'none')
    ,@('Rasterization contract: pixel centres, lower-left origin versus surface rows, shared-edge coverage, winding after Y flip, scissor', '3.1, 3.5.1, 4.1.2', '2, 4', 'Phase 2 contract doc; adjacent-triangle and boundary pixel tests in `test_d3d_raster.c` and `test_gl_scene.c`', 'none')
    ,@('Perspective-correct interpolation of colour, depth, fog and homogeneous texture coordinates including varying q', '3.5.1', '2', 'projective texture through clipping scene; `test_d3d_raster.c`', 'none')
    ,@('Point and line rasterization: coverage, endpoint rules, connected blended segments, width, stipple, smoothing', '3.3-3.4', '2, 6', '`test_r3d_line.c`, `test_gl_scene.c`', 'none')
    ,@('Texture completeness, mip chain to 1x1, non-square images, borders, all six minification filters, wrap modes, environment functions, alpha decode', '3.8', '2, 4', '`test_gl_teximage.c`, `test_gl_texobj.c`, `test_d3d_raster.c`', 'none')
    ,@('Logical texture images survive surface loss, eviction and engine refusal; no silent downscale or truncation', '3.8.8', '4', '`test_gl_texobj.c`; V9XGLP loss/restore scene', 'none')
    ,@('Pixel pack/unpack: alignment, row length, skip pixels/rows, all format/type pairs, orientation', '3.6.1, 4.3.2', '4, completed in 6', '`test_gl_pixelstore.c`, `test_gl_pixel.c`', 'none')
    ,@('Per-fragment order: scissor, alpha, stencil, depth, blend, logic op, masks, dithering', '4.1-4.2', '2, 4, 6', '`test_d3d_raster.c` order tests; `test_gl_scene.c`', 'none')
    ,@('Front and back buffer selection for drawing and reading, single-buffered formats, behaviour when stencil/accum/alpha planes are absent', '4.2.1, 4.3, 6.1.1', '3, 6', 'V9XGLP front/back scenes; `test_gl_get.c` for absent-buffer queries', 'none')
    ,@('Context and drawable lifetime: one current thread per context, rebind, delete while current, share groups, repeated create/destroy, `FreeLibrary` of opengl32', 'WGL contract', '3, 4, 6', 'V9XGLP lifetime scenes; Quake 2 `vid_restart`', 'none')
    ,@('Display list compile/execute semantics, `GL_COMPILE_AND_EXECUTE`, non-listable commands executed immediately, shared lists', '5.4', '6', '`test_gl_dlist.c`', 'none')
    ,@('Attribute stack groups push and pop exactly the state the tables assign to them', '6.1.11 table 6.30', '6', '`test_gl_state.c`', 'none')
    ,@('`DrvGetProcAddress` returns NULL for every name not implemented; `GL_EXTENSIONS` under 4 KB; vendor/renderer strings avoid the Quake substrings', 'ICD contract; Quake census', '3', '`test_gl_get.c`; V9XGLP string check', 'none')
    ,@('Temporary stubs: every slot whose phase has not landed sets `GL_INVALID_OPERATION` with an ABI-safe return; the list of such slots is kept here and must be empty at the Phase 6 gate', 'plan Phase 3', '3-6', 'the generated stub list, checked by `test_gl_dispatch.c`', 'none; stub list not yet generated')
)
foreach ($c in $cross) {
    $out.Add('| ' + ($c -join ' | ') + ' |')
}
$out.Add('')
$out.Add('## Entry points')
$out.Add('')
$out.Add('| Slot | Entry point | Specification | Phase | Module | Planned test | Evidence |')
$out.Add('|---|---|---|---|---|---|---|')
foreach ($r in $rows) {
    $out.Add("| $($r.Slot) | ``$($r.Name)`` | $($r.Section) | $($r.Phase) | ``$($r.Module)`` | ``$($r.Test)`` | none |")
}
$out.Add('')
$text = ($out -join "`n") + "`n"
[System.IO.File]::WriteAllText((Join-Path (Get-Location) 'docs\plans\opengl-1.1-requirements.md'), $text, (New-Object System.Text.UTF8Encoding($false)))
"rows=$($rows.Count) unmatched=$($unmatched.Count)"
$byPhase.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name): $($_.Value)" }
