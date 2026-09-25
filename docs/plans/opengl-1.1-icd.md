# OpenGL 1.1 ICD for Velocity9x, on a render core shared with Direct3D

Status: design for approval; nothing coded. Revised 2026-09-26 after two
reviews against the tree; the review notes are at the end.

## Context

Velocity9x ships a DX5-level Direct3D HAL with three engines: the software
CPU rasterizer, S3 ViRGE S3D and Intel Gen3. There is no OpenGL.
PLAN.md:451-453 defers it as "a separate design project"; this plan starts
that project.

Decisions taken with Michael on 2026-09-26:

- **Full ICD** loaded by the system opengl32.dll, not a MiniGL.
  - This supersedes the conclusion of
    `docs/decisions/2026-08-30-virge-opengl-prior-art.md` that a MiniGL was
    the achievable deliverable.
  - Its two ViRGE walls still hold: no triangle setup and no multiplicative
    blend. The plan answers the second with a per-draw software fallback,
    not by dropping GL semantics.
- **Back ends:** software, Intel GMA 950 Gen3 and S3 ViRGE.
- **Correctness milestones:** GLQuake and Quake 2 run correctly on all three
  in Phase 5; Phase 6 completes the remaining GL 1.1 functionality. Work is
  driven by code and rendered output. Public-release timing is decided later.
- **Performance:** measure frame rates and fallback costs, with no minimum
  performance targets yet, including on the ViRGE.
- **Targets:** original Windows 98, Windows 98 SE and Windows ME. Windows 95
  feasibility is measured in Phase 0 on one precisely specified installation;
  support for every Windows 95 variant or all three engines is not assumed.
- **Route to hardware:** a private, versioned, stateless render interface
  exported by V9XHAL.DLL. The alternative was to make the ICD a D3D
  immediate-mode client. It was rejected because it caps GL at the DX5 DDI
  and shares no code.
- **References:** Mesa, ReactOS, Wine and vmdisp9x/mesa9x may be read as
  interface documentation, with facts cited in decision docs. No code is
  copied.

What exists today that shapes the design:

- **Vertices.** The engines consume only screen-space `V9X_D3DTLVERTEX`
  (`include/velocity9x/win9x_ddraw_abi.h:1265`). There is **no transform,
  lighting or clip-space clipping anywhere**.
- **State.** `V9X_D3D_CONTEXT` (`src/display32/d3d/d3d_internal.h:74-178`)
  holds raw D3D enums, and the engines read it directly. They also call back
  into D3D state:
  - texture by handle through `v9x_d3d_context_texture_surface`
    (`d3d_core.c:475-505`)
  - the colour-key table (`d3d_virge.c:1065-1111`)
  - `alpha_force` and `texture_border`
  - `texture_wrap` and `wrap_u`/`wrap_v`
  - zbuffer LCL null tests (`d3d_virge.c:1048`, `d3d_soft.c:997`)
- **Nearly-neutral pieces.**
  - The clipper and fan builder (`d3d_core.c:527-834`) are not host-tested,
    and they still call `v9x_d3d_engine()` for the guard band (`:600,613`)
    and use `v9x_float_to_long`, which comes from the OS-boundary header
    (`ddhal_internal.h:216`).
  - `d3d_cull.c` reads the published caps (`d3d_core.c:741`).
  - `d3d_raster.c` is a pure leaf whose constants already equal D3D's
    (asserted at `d3d_soft.c:106-145`). That is the precedent this plan
    generalises.
- **Fog and specular** are folded into the vertex colour only on the
  execute-buffer path (`d3d_core.c:2051-2054`). The DrawPrimitive paths pass
  vertices through unfolded (`:2217,2418,2591`).
- **One HAL instance.** V9XHAL.DLL is a single shared-arena instance at
  0xB0400000. It relies on DirectDraw's Win16 lock (`d3d_i9xx.c:2010`), and
  `v9x_hal` is set only in DriverInit (`ddhal_core.c:1790`). An ICD that
  linked the same sources would own a second ring and flip state.
- **Drains already exist** and are used by Lock and Blt:
  `v9x_render_drain`/`v9x_blt_drain` (`ddhal_core.c:1292-1397`). The former
  waits on Intel only and otherwise returns success; the latter also calls
  the engine's `wait_idle`, which on the ViRGE covers 3D-done once
  `v9x_engine_3d_launched` has been called (`eng_s3_virge.c:414-419`,
  `d3d_virge.c:1527-1533`). Calling `v9x_render_drain` alone is insufficient
  before a CPU fallback on ViRGE. Phase 2 establishes a shared drain contract
  from `v9x_blt_drain`.
- **The HAL keeps no surface registry.** `V9xHalDestroySurface`
  (`ddhal_core.c:895-909`) clears colour-key and texture-handle entries and
  calls the engine's destroy; Gen3 recognises its own block by a signature in
  the GBL (`d3d_i9xx.c:1274-1281`) and frees it **without draining**
  (`:1283-1287`) while a ring batch may still read it. No DDHAL callback
  fires on SURFACELOST; the HAL sees a mode change only as a DriverInit re-run
  (`ddhal_core.c:1790-1800`).

## Architecture

```
 app ── opengl32.dll (system) ──Drv*──► V9XGL.DLL  (per process, not shared)
                                          GL front end: state, T&L, clip,
                                          assembly, texture objects, pixel ops
                                          DirectDraw client: back/Z/texture
                                          surfaces, clipper, SwapBuffers Blt
                                               │ V9X_R3D_* batches (≤64 tris)
                                               ▼ V9xRenderInterface() (new export)
 DDRAW.DLL ──DDHAL/D3DHAL──► V9XHAL.DLL (shared arena, one instance)
                              d3d_core.c: DX5 DDI front end ─┐
                              r3d/: neutral render core ◄────┘
                                 validate, clip rect, cull, lines/points,
                                 clear, per-draw software fallback
                              engines: soft (d3d_raster) | virge | i9xx
 v9xdisp.drv Control(): QUERYESCSUPPORT/OPENGL_GETINFO → "Velocity9x"
 INF: HKLM,"Software\Microsoft\Windows\CurrentVersion\OpenGLdrivers","Velocity9x",,"v9xgl.dll"
```

Principles:

- **API specifics stay in the front ends.**
  - `d3d_core.c` keeps the DX5 DDI: handles, colour-key table,
    `apply_vertex_color` and D3D flat shading. r3d never folds or flattens.
  - The ICD keeps the GL state machine, and does its own lighting, provoking
    vertex and culling.
  - Engines see only `V9X_R3D_DRAW`.
- **The HAL interface is stateless.** Every call carries target, depth,
  texture and state, so the HAL holds no per-process GL state and retains no
  client pointer after a call. Its existing device and submission bookkeeping
  remains.
  - Render targets, depth buffers and hardware texture realizations are
    DirectDraw surfaces created by the ICD in its own process. HAL-managed
    allocations go through `V9xHalCreateSurface`, so Gen3 placement
    (`d3d_i9xx.c:917-1289`) applies. Logical texture images remain in system
    memory and are also available to the software sampler when no faithful
    hardware realization is possible.
  - The ICD passes each DirectDraw surface's LCL (`INT->lpLcl`). The existing
    `IsBadReadPtr` guard (`d3d_core.c:862-885`) checks only the INT wrapper;
    neither it nor an address ≥0x80000000 establishes LCL/GBL validity or
    ownership. `v9x_surface_offset` checks only the starting VRAM address.
  - **What the HAL checks, per call:** INT, LCL and GBL readability; caps,
    width, height, pitch and pixel format; `fpVidMem` inside the aperture and
    extent = pitch × rows within VRAM, with overflow-checked arithmetic (the
    pattern at `d3d_soft.c:393-426`, factored out); each mip level's LCL the
    same way; and, if Phase 0.4 measures it usable, ownership through
    `lpSurfMore->lpDD_lcl->dwProcessId`. The check is a **pure function in
    `r3d_validate.c`** over a project-owned `V9X_R3D_SURFACE`, filled from
    the LCL/GBL in the boundary file, with `test_r3d_validate.c` covering
    malformed descriptors and overflow.
  - **What the HAL cannot check:** that the allocation is live or that the
    VRAM range is still this surface's. Calls are synchronous under Win16
    serialization, so no destroy can interleave a call. Staleness is the
    ICD's responsibility, through the generation and surface-lost tracking
    below. The remaining hazard is pending GPU work when a surface is
    destroyed: `V9xHalDestroySurface` drains (shared drain, Phase 2) before
    the Gen3 block is freed. That closes a pre-existing D3D hazard as well and
    is gated on the netbook.
  - CPU texture descriptors carry explicit byte extents, are consumed
    synchronously, with their storage retained for the call, and are checked
    with `IsBadReadPtr` over the declared extent before sampling. A bad
    pointer would fault inside HAL code with the mutex held, which is the
    wedge `INSTALL.md:198` describes.
  - `describe` returns a HAL generation, a counter in the shared block that
    DriverInit increments; submissions carry it. The ICD also invalidates
    cached descriptors on surface loss/restore, even without a HAL generation
    change, and re-resolves offsets before reuse.
- **Pure logic is host-testable,** in the `mtrr.c` pattern. A host end-to-end
  test drives GL calls through r3d into `d3d_raster.c` and hashes pixels.
- **No new INI keys and no new selector values.** GL uses the engine that
  `Direct3D=` already picked. The 16-bit driver answers the OpenGL escape
  only when `engine_caps` has D3D. Otherwise, or with `Direct3D=1`, opengl32
  finds no ICD and serves Microsoft's generic formats.
- **Pixel formats only on a 16 bpp desktop** that the engine can write:
  - 565 or 555 on software and Gen3.
  - **555 only on the ViRGE.** The S3D can write only 1555, and a soft
    fallback on a 565 desktop would mix encodings.
  - Double- and single-buffered, 16-bit Z, no stencil, accum or alpha.
- **Strings.** Vendor "Velocity9x", renderer "Velocity9x <engine>".
  - They must avoid the substrings Quake 2 and GLQuake test for
    (`ref_gl/gl_rmain.c:1162-1190`, `gl_vidnt.c:613-616`): voodoo, rush,
    permedia, glint, glzicd, gdi, pcx2, verite, glide and sgi, and a leading
    3dfx or powervr.
  - `GL_EXTENSIONS` stays under 4 KB.
  - `DrvGetProcAddress` returns NULL for anything unimplemented.

## Phase 0: measurements and records, before code depends on them

**Desk record.** The 2026-09-26 research goes into
`docs/decisions/2026-09-26-opengl-icd-interface-research.md`, with each fact
labelled confirmed or inferred and its source given. It covers:

- **Discovery.** Win9x discovery sends `QUERYESCSUPPORT(0x1101)`, then
  `OPENGL_GETINFO`, which returns an ANSI `{long Version=2; long DriverVersion; char name[262]}`
  (vmdisp9x `control.c:64-119`). The name is a plain REG_SZ value under
  `OpenGLdrivers`.
- **Exports.** 16 mandatory `Drv*` exports, plus `DrvValidateVersion`, which
  returns TRUE.
- **Dispatch table.** `GLCLTPROCTABLE` = `{336; 336 stdcall slots}`, in the
  order given by Mesa `gldrv.h` and ReactOS `icd.h`. Slot 7 is Begin, 305 is
  Viewport, 306 is ArrayElement and 335 is PushClientAttrib. The verified
  list becomes the manifest `src/opengl/gl_entrypoints.psd1` (Phase 3).
- **Pixel formats.** ICD formats are numbered 1..n, before the generic ones.
  They set neither `PFD_GENERIC_*` flag.
- **Win16 lock.** The Win16 mutex is reached through KERNEL32 ordinals 93, 97
  and 98. DDRAW.DLL imports them; this was checked on the DX3 build in
  `build/hellbender-cd`. Microsoft KB125867 ("Understanding Win16Mutex") is
  cited here: its warning is a desktop stall while a Win32 thread holds the
  mutex, and a deadlock only if the holder waits on another thread.
- **The Quake census**, summarised below.
- **Supersession.** Why the 2026-08-30 MiniGL conclusion no longer stands.
- **Semantic baseline.** Use the [OpenGL 1.1 specification](https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf)
  for required behaviour and generic GL for comparison evidence. Record
  requirements separately from behaviour observed in one implementation.
- **Requirements inventory.** Create `docs/plans/opengl-1.1-requirements.md`
  before implementation, expanding the coverage matrix below into all 336
  entry points and cross-cutting semantic requirements. Each row names its
  specification section, owning phase/module, test and current evidence.
  Track temporary stubs, error cases, defaults, queries and implementation
  limits explicitly; a callable entry point is not a completed requirement.

**Guests that do not exist yet (dependency on Michael).** Every 86Box profile
is Windows 98 SE (`docs/vm-environment.md:64-78`). ME has no validated
configuration (`README.md:406`), and Windows 95 exists only on the physical
486 VLB Trio64 without the mini-VDD and without a D3D-capable engine
(`README.md:230-245`). Phase 0 needs, with install media as an external
input: an original Windows 98 (4.10.1998) guest, a Windows ME (4.90.3000)
guest, and one named Windows 95 installation (OSR2 recommended, so the
OpenGL runtime is present). Gates that name these guests cannot be reported
passed until the guests exist and their OS build, OPENGL32.DLL and DDRAW.DLL
versions are recorded.

**Guest measurements**, one decision doc each:

1. **Discovery on original 98, 98SE and ME.** Log every `Control` escape in
   a trace build of v9xdisp.drv, then run a GL app. This confirms or kills
   the vmdisp9x layout (Version 2, ANSI name, NULL input). Record OS build
   and the versions of OPENGL32.DLL and DDRAW.DLL for each installation.
   **98SE measured 2026-09-26**
   (`2026-09-26-98se-opengl32-asks-the-display-driver-through-escape-0x1101-seven-times.md`):
   QUERYESCSUPPORT then OPENGL_GETINFO with an unprimed buffer, seven times
   for one screensaver; the answer ships in `dd16.c`. Original 98 and ME
   wait on their guests.
2. **Win16 lock.**
   - Dump the imports of the guest `SYSTEM\DDRAW.DLL` on original 98,
     98SE and ME. Record the installed DirectX version and whether
     93/97/98 are named; do not infer runtime versions from the OS name.
   - In a HAL trace build, call `_ConfirmWin16Lock` (#96) inside the
     DrawPrimitives, Blt, CreateSurface and Lock callbacks. The assumption in
     `gdi-acceleration.md:29` has never been measured.
   - **98SE measured 2026-09-26**, both halves
     (`2026-09-26-98se-ddraw-imports-the-win16-mutex-ordinals.md`,
     `2026-09-26-98se-confirmwin16lock-is-one-when-held-and-getprocaddress-refuses-the-ordinals.md`,
     `2026-09-26-98se-directdraw-holds-the-win16-mutex-around-every-hal-callback-measured.md`):
     DDRAW imports 93/97/98; GetProcAddress refuses the ordinals and the
     export walk (`src/common/pe_export.c`) resolves them; the mutex is held
     at every callback sampled, depth 2 inside Lock. The instrument stays in
     the HAL (`src/display32/win16lock.c`). ME and original 98 wait on their
     guests.
3. **Clipped Blt.** Find out what 98SE DDraw hands the HAL when a
   clipper-attached app blits into a window that is partly covered:
   `IsClipped` plus `prDestRects`, split calls, or HEL.
   - The HAL never reads `IsClipped` (grep of `src/`).
   - If it is wrong, windowed D3D is already affected: file an issue in
     `docs/issues/` and fix the HAL before Phase 3.
   - **98SE measured 2026-09-26**
     (`2026-09-26-98se-clipped-blits-arrive-one-hal-call-per-rectangle-and-an-application-can-walk-int-lcl-gbl.md`):
     the runtime splits a clipped blit into one HAL call per visible
     rectangle with `rDest` already clipped; the HAL needs no fix, and its
     new `BltClipped*` counters stay as the check for other runtimes.
4. **INT→LCL from an app.** Add a V9XDDP rung that checks INT and LCL are
   above 2 GB, that `lpGbl->fpVidMem` matches the pointer Lock returns, and
   whether `lpSurfMore->lpDD_lcl->dwProcessId` equals the caller's process
   id (nothing in the tree reads it today; it is the only ownership check
   available).
   **98SE measured 2026-09-26** (same record as 0.3): INT, LCL and GBL all
   above 2 GB, `fpVidMem` equal to the Lock address, and the LCL's own
   `dwProcessId` (at +24, no `lpSurfMore` needed) equal to the caller's.
5. **Mixed-engine colour and Z.** Add a V9XDDP rung that draws hardware,
   then soft, then hardware into shared colour and Z buffers, and compares
   with an all-soft image. Exercise overlapping blended draws, depth
   comparisons/writes, clears and texture updates on both hardware engines.
   Soft stores `sz*65535` (`d3d_soft.c:58`) and the ViRGE uses 1.31 registers
   (`d3d_zfixed.h`). Nobody has shown the two agree. The rung drains through
   DirectDraw Lock, which already calls `v9x_blt_drain` and so waits on the
   selected engine. Agreement in encoding, operation ordering and visibility
   is a prerequisite for fallback.
6. **Generic GL as a reference.** Check that a probe can choose a generic
   format with the ICD installed, and that a controlled scene hashes the
   same twice. **Half measured 2026-09-26** (baseline run in the Phase 0.9
   record): the generic renderer is 24 formats, "GDI Generic" 1.1.0, and
   serves a clear and swap; the hash check waits on a scene. Match colour/depth precision, dimensions and relevant state,
   including dithering; cover both 565 and 555 where available. Record exact
   state/error assertions separately from image comparisons. Define image
   tolerances and their specification-based justification before accepting
   differences; matching generic GL alone is not proof of conformance.
7. **Quake media.** Check which guests have GLQuake 0.97 with the shareware
   pak0 and the Quake 2 demo. Missing media is a dependency on Michael.
   **Checked 2026-09-26:** neither `Win86SE` nor `Win98SE-Fast-D3D` has
   either game (roots and `Program Files` listed; 3DMark 99, Final Reality,
   Doom shareware and Microsoft Games are what is there). Both are needed
   before Phase 5.
8. **Windows 95 feasibility.** Start with one installation and record its
   edition/build, installed DirectX version and OpenGL runtime version.
   - First establish display-driver startup and DirectDraw surface
     allocation, including the target, depth and texture surfaces needed
     by this design. Audit required imports and the Win16-lock mechanism.
   - The existing physical Win95 4.00.950 Trio64 VLB result omits the
     mini-VDD because it did not load (`README.md:244-246`). That result
     does not establish the driver services needed by this ICD. Determine
     whether this path can work without the mini-VDD or requires driver work.
   - Use the Phase 0.9 probe to establish discovery, dispatch and
     clear/present through DirectDraw and the HAL before treating the full
     Phase 3 implementation as Windows 95-compatible.
   - Original Windows 95 needs the OpenGL runtime installed; OSR2 includes
     it. Record the prerequisite rather than silently replacing system DLLs.
     Source: Microsoft KB154877, [OpenGL 1.1 release notes (archived)](https://ftp.zx.net.nz/pub/Patches/ftp.microsoft.com/MISC/KB/en-us/154/877.HTM).
   - Record pass/fail evidence and the required compatibility work in a
     decision doc. Expand the OS/engine matrix only after this result;
     a failed Win95 probe does not block the Windows 98/SE/ME work.
9. **Minimal ICD integration before Phases 1-2.** On 98SE and ME, use a
   disposable probe build to exercise system OPENGL32 discovery, correctly
   typed dispatch, DirectDraw surface creation, clear/present, overlap
   clipping and teardown; repeat on original Windows 98 for its
   compatibility gate.
   - Clear and present need **no HAL change**: `Blt(DDBLT_COLORFILL)`
     already reaches every engine's fill through `V9xHalBlt`
     (`ddhal_core.c:1509-1549`), and SwapBuffers is a Blt.
   - The one private call into the HAL instance is measured by exporting
     the real `V9xRenderInterface` at `abi_version` 0 with `describe` only.
     Version negotiation (Phase 3) retires v0, so nothing is frozen and no
     throwaway export is added and later removed (each is a design change
     under CLAUDE.md, and touches `build-ddraw-hal-dll.ps1:147,197`).
   - The probe also settles whether a call from the GL process sees the same
     HAL data as DirectDraw's: `d3d_i9xx.c:964` calls its cache "per
     process", while the build marks every section shared (`:179-191`).
   - Record the result before investing in the full refactor, and reuse the
     probe for the Windows 95 feasibility check.
   - **98SE measured 2026-09-26**
     (`2026-09-26-98se-opengl32-loads-the-icd-lists-its-format-first-and-creates-every-context-through-layer-0.md`):
     the contract holds. Two things it adds to Phase 3: contexts are created
     through `DrvCreateLayerContext(hdc, 0)`, never `DrvCreateContext`; and
     OPENGL32 calls `glFinish` before `DrvSwapBuffers` and `glFlush` after,
     through the table. `src/opengl/gl_icd.c`, the generator
     `scripts/lib/gl-dispatch.ps1` and `tests/host/test_gl_dispatch.c` are
     the probe and stay as Phase 3's skeleton. The one 16-bit format was
     chosen for GLQuake's request and not for 3D Pipes'.

Repeat the OS-sensitive clipped-Blt, INT-to-LCL, surface restore and mode-change
checks on original Windows 98 and ME as well as 98SE. Keep the GL front end
and render core shared; isolate any measured OS differences at the platform
boundary.

## Phase 1: extract the neutral render core (D3D only, no behaviour change)

New files go in `src/display32/r3d/`, with the prefix `v9x_r3d_`. Each step
below is its own commit with its own gate.

- **1a. Characterise, then move the clipper.**
  - First add `tests/host/test_r3d_clip.c` as characterisation tests of
    today's `v9x_d3d_clip_triangle` and `draw_list` behaviour.
  - Then move `d3d_core.c:527-834` verbatim to `r3d_clip.c`. It takes the
    guard band as a parameter instead of calling `v9x_d3d_engine()`, and uses
    its own fistp helper (as in `d3d_zfixed.c:73-76`). A C cast would pull in
    `__CHP` under `-zl`.
  - Move `d3d_cull.c` to `r3d_cull.c`, with the caps check passed in. Rename
    the entries in `host-sources.ps1:60,75` and in the HAL `$sources` list.
  - **Done 2026-09-26**
    (`2026-09-26-phase1a-the-clipper-moves-to-the-render-core-and-the-virge-draws-the-same.md`):
    host tests and the ViRGE guest gate passed; the netbook gate is pending
    the machine (its agent timed out).
- **1b. `r3d.h` and `ops->draw`.**
  - `V9X_R3D_DRAW` carries:
    - target {offset, pitch, width, height, format, lcl}
    - depth {lcl, offset, pitch, enable, write, func}
    - texture {storage kind, logical format/dimensions, lcl for hardware
      storage, per-level {offset or CPU pointer, byte extent, width, height,
      pitch}, level count, min, mag, mip, address, border, wrap_u, wrap_v}
    - blend {enable, src, dst}
    - alpha test {enable, func, raw ref}
    - combine {colour op, alpha op, env colour}
    - fog {enable, colour}
    - masks and clip rect
    - `color_key_enable` and `alpha_force` (D3D-only, zero from GL)
  - Constants take D3D's numbers where the concept is the same. That keeps
    `src/common/i9xx_depth.c` unchanged. Equality is asserted at compile
    time. GL-only concepts take values outside D3D's range.
  - Combine is a colour op and an alpha op (texture, fragment, modulate,
    decal-lerp, env-blend), because neither API's modes map 1:1. GL MODULATE
    on an RGBA texture is D3D MODULATEALPHA; on an RGB texture it is D3D
    MODULATE.
  - Phase 1's D3D adapter uses a `V9X_R3D_VERTEX` layout identical to
    `V9X_D3DTLVERTEX`, with offset asserts. Fog travels as a factor in
    `specular.a`. This does not freeze the public vertex ABI: Phase 2 must
    establish how projective GL texture coordinates are represented before
    the Phase 3 interface is fixed.
  - **Append** `draw` to `V9X_D3D_ENGINE_OPS`, following the append-only rule
    (`d3d_internal.h:307-318,362-367`). The core calls `draw` when it is
    non-null, and `draw_triangles` otherwise.
  - The front end resolves the texture LCL once per batch.
    `v9x_d3d_context_texture_surface` becomes core-private.
  - The `draws_no_handle` and `draws_handle_unresolved` counters become
    per-batch counts; record that in the gate doc.
- **1c. `d3d_state.c` (pure) translates D3D state to r3d.** Its host test is
  `test_d3d_state.c`. It is called from `v9x_d3d_apply_state`
  (`d3d_core.c:1526`), and the raw fields stay in `V9X_D3D_CONTEXT`.
  - **1b and 1c done 2026-09-26** (commit after Phase 1a): `r3d.h` carries
    `V9X_R3D_DRAW` with D3D's numbers for the shared vocabulary, asserted in
    `d3d_state.c`; `ops->draw` is appended and null in every engine; the
    core builds the description per batch (`v9x_d3d_describe_draw`) and
    dispatches on `draw` when set. The translation is called per batch
    rather than from `apply_state`, since it is the identity and the raw
    fields are the DDI's; the surfaces are resolved by the core.
- **1d. One engine per commit** moves to `draw`: soft, then virge, then i9xx.
  Each is gated on its own machine. The ViRGE colour-key rewrite stays in the
  engine, driven by `color_key_enable` and the texture LCL.
- **1e.** Set `draw_triangles` to null in all three tables. The member stays,
  so the positional initialisers do not shift.
- **check-tree.** Extend the chip-neutral rule (`check-tree.ps1:434`, which is
  hard-coded to `d3d_core.c`) to `r3d/*.c` and `d3d_state.c`. Add the new
  files to `$required`.
- **External symbols.** The new cross-file symbols
  (`v9x_r3d_clip_triangle`, `v9x_r3d_draw_list`, `v9x_r3d_cull_triangle`,
  `v9x_r3d_validate_*`, `v9x_d3d_state_apply`, and the externalised drain
  from Phase 2) are external, which is a design change. They are listed here
  for agreement.
- **Gates, per commit where they apply:**
  - Host: `test_d3d_raster.c` hashes unchanged, and `test_i9xx_3d.c` green.
  - Netbook: 3DMark99 at 1024x576 within noise of 643, and the 3D WinBench 98
    quality suite with the same results.
  - 86Box ViRGE (9869): Final Reality and the V9XDDP D3D rungs unchanged.
  - V9XSOFT: hashes unchanged.

## Phase 2: grow the shared core

Rasterization changes get a failing host pixel test first. ABI, lifetime and
synchronization changes get the corresponding host contract test or guest
probe; pixel hashes alone cannot establish ordering or allocation safety.

- **Rasterization contract, before the public ABI is fixed.** Document the
  GL-to-r3d mapping for lower-left window coordinates versus surface rows,
  sample positions, winding after Y conversion, shared-edge coverage, scissor
  rectangles and readback orientation. Preserve Phase 1's D3D behaviour
  through its adapter. Specify interpolation of depth, colour, fog and
  homogeneous texture coordinates, including varying texture q; either prove
  a mapping to the vertex representation or extend it and use software where
  hardware cannot express it. Do not assume one reciprocal-w field serves
  every interpolation rule. Tests include projective textures through
  clipping, adjacent triangles, viewport/scissor boundaries and reversed Z.
- **Software rasterizer** (`d3d_raster.c`, `d3d_soft.c`):
  - `V9X_D3D_RASTER_TEXTURE` (`d3d_raster.h:264-274`) is one square level,
    4..512, three 16-bit formats, mask-wrapped. It becomes per-level
    {pixels, pitch, width, height} × level count, with per-axis wrap and a
    mip filter. `V9X_D3D_RASTER_TEXCOORD_REPEATS` (`:110-121`) is derived
    from the 512 limit; if the limit rises, it and its test are re-derived.
  - **Formats stay 565/1555/4444.** The ICD converts each logical level to a
    sampler-ready 16-bit copy at upload; `d3d_raster.c` does not decode
    L/A/I/LA or 32-bit. The logical image (for queries, GetTexImage and
    restore) and the sampler copy are separate.
  - CPU-pointer levels are **not** subject to the `D3DSoftSysMem` gate
    (`d3d_soft.c:329-368`): that gate is 16-bit-side policy for DirectDraw
    system-memory surfaces whose extent the HAL cannot see, and the GL
    descriptor carries its extent. `d3d_soft.c` says so in a comment.
  - texture alpha decode, perspective-correct u/v, and mip selection using
    per-level offsets, so it works on Gen3 miptrees (`d3d_i9xx_target.c:190-257`)
  - non-square textures, and sizes down to 1x1
  - the full blend factor set
  - alpha test
  - colour and depth masks
  - combine ops including env-blend
  - the clip rect
  - post-texture fog, applied from `specular.a`
  - Per-pixel helpers stay macros, because the build does no inlining.
- **`r3d_line.c`**: use triangle expansion only where it satisfies the
  required point/line coverage and interpolation rules. GL line endpoint
  handling must avoid double-blending shared endpoints and gaps in connected
  segments, within the specification's permitted rasterization variation.
  Otherwise use a software point/line path. Test horizontal, vertical and
  diagonal segments, connected blended lines, clipped endpoints and points
  at pixel boundaries; add width, stipple and smoothing cases in Phase 6.
- **Clear op** over a rect list: engine fill, or CPU fill, respecting GL
  scissor and write masks. Test partial clears and preservation of masked
  channels and depth independently of ordinary draw state.
- **Shared drain and CPU/GPU ordering.** The all-engine helper is
  `v9x_blt_drain` (`ddhal_core.c:1384-1397`) made external, with three
  changes:
  - it does not bump `blt_flip_pending` (`:1388`), which GL must not inflate;
  - it calls `validate_status` before testing `status_validated`, because
    on the ViRGE the latch is unset before any 2D operation and `wait_idle`
    would be skipped;
  - it returns three states, done / busy / abandoned. Today the Gen3 drain
    returns success after 4,000,000 polls (`d3d_i9xx.c:533-538`), which the
    2026-09-18 independent review already said must not be read as
    correctness. Changing that is a D3D behaviour change with its own
    netbook gate.
  - The software engine falls out for free: `v9x_engine32()` selects by chip
    (`:1414-1431`), so a VBE card has no ops and Intel's breadcrumb is idle.
  - Use the helper for CPU fallback, CPU clear/readback, resource reuse,
    `finish`, and `V9xHalDestroySurface` before a Gen3 block is freed. No
    additional backend idle operation is needed. Establish both GPU-to-CPU
    and CPU-to-GPU visibility for colour, depth and texture storage. Test the
    Phase 0.5 transitions and timeout handling on ViRGE and Gen3.
- **Append `accepts(const V9X_R3D_DRAW *)`**. The core asks *before*
  clipping, and clips with the limits of whichever engine will execute. For
  example, Gen3 draws with `clip_in_core=0` and a 4096 guard band, while soft
  clamps at 2048.
  - A capability refusal, before any submission, selects the software engine
    on the same target/depth surfaces after the shared drain above. Preserve
    logical texture images when hardware texture storage cannot represent
    them. `accepts` has no rendering side effects.
  - A drain that reports busy or abandoned prevents CPU access, is counted
    and returns an explicit failure to the ICD. It must not be reported as
    successful rendering or trigger a replay of potentially executed work.
  - Examples of refused draws: the ViRGE's `ZERO/SRC_COLOR` and
    `ZERO/ONE_MINUS_SRC_COLOR` blends, non-square CLAMP, and fog on Gen3.
  - D3D keeps skip-and-count. Moving D3D onto the fallback is a separate
    decision.
  - CPU coherence on Gen3 relies on MI_FLUSH plus the breadcrumb, which is
    marked unmeasured at `d3d_i9xx.c:2305,2364`. It gets a probe rung before
    Gen3 fallback is relied on.
- **Gen3 non-square textures.** MAP_STATE carries width and height
  separately. `v9x_d3d_i9xx_layout_miptree` is checked against a non-square
  chain in `test_i9xx_3d.c`. The D3D caps keep SQUAREONLY.
- **Gate:** host tests green, and the Phase 1 D3D regression set.

## Phase 3: the render interface, the escape and the ICD skeleton

- **`include/velocity9x/r3d_abi.h`** defines `V9X_R3D_INTERFACE`:
  `abi_version`, `struct_bytes`, `describe` (engine, limits, accepted-state
  mask, strings, HAL generation), `draw`, `clear`, `flush` and `finish`.
  Fix calling conventions, field widths, packing, buffer ownership and size
  asserts. Specify version/size negotiation and reject incompatible requests
  before dereferencing their payloads; the Phase 0.9 v0 (`describe` only) is
  retired here. Finish the Phase 2 rasterization and texture-storage
  contracts before freezing the vertex and draw layouts.
  - Define results for success, invalid descriptors, incompatible ABI, stale
    generation/lost surfaces, unsupported state, allocation failure and
    timeout. Record the ICD's handling of each; distinguish legal GL errors
    from device failures, without inventing a GL 1.1 context-loss error.
  - Validate before emitting, through `r3d_validate.c`. Distinguish a
    capability refusal with no work emitted from a submission failure after a
    prefix may have executed. Report a known submitted prefix when possible,
    and an indeterminate outcome otherwise. Never replay an already submitted
    or uncertain prefix through software: blended pixels would be applied
    twice. Device failures stop the affected operation and require an
    explicit recovery decision.
  - GL batches are chunked at `V9X_D3D_INDEXED_BATCH` (64), including any
    triangles produced by clipping/expansion. An oversized public request is
    rejected explicitly, never silently discarded by Gen3.
  - `flush` submits pending work for progress; successful `finish` establishes
    completion and visibility. A timeout is a failure, not completion. Test
    malformed descriptors, arithmetic overflow, stale generations, batch
    boundaries and failures after partial submission.
- **V9XHAL.DLL export `V9xRenderInterface`.** This is a new external symbol,
  approved by this plan.
  - Every entry fails closed unless `v9x_hal`, DriverInit completion and
    `v9x_d3d_engine()` are all present.
  - It resolves ordinals 93/97/98 by walking KERNEL32's export table
    (`src/common/pe_export.c`, host-tested; already used by the Phase 0.2
    instrument), and asserts each pointer lies inside the KERNEL32 image.
    That adds no imports. `GetModuleHandleA` is already used
    (`d3d_i9xx.c:975`). `GetProcAddress` by ordinal is refused on 98SE.
  - **Lock order:** the ICD's critical section, then the Win16 mutex, never
    reversed, and never a wait on another thread under either. Under the
    mutex the only hardware wait is the shared drain, bounded by `wait_idle`'s
    timeout (`win9x-driver-boundaries.md:116`), and the only CPU work is
    validation plus at most one 64-triangle batch inside the clip rect. CPU
    rasterization under the Win16 lock is already how the D3D software engine
    runs (`d3d_i9xx.c:2010-2012`, `gdi-acceleration.md:29`); what is new is
    the ICD taking the mutex itself. Never call USER, GDI or DDraw while
    holding it; do DirectDraw lifetime operations outside it and revalidate
    on entry. Hold times are measured per batch, including with simultaneous
    GL and D3D clients, not assumed.
  - `build-ddraw-hal-dll.ps1` gets an `export` line near :147 and an export
    presence check beside :197.
- **16-bit driver.** `Control` (`dd16.c:1126-1158`) answers the OpenGL
  escape in the layout measured in Phase 0.1.
  - The answer sits **outside** the Matrox `#ifndef` (`dd16.c:1142`) and is
    gated on D3D in `engine_caps`.
  - This is a module-boundary change, checked against
    `win9x-driver-boundaries.md`.
- **`src/opengl/`** builds `v9xgl.dll`, per process and not shared.
  - **Platform boundary:** `gl_icd.c` and `gl_surface.c` may use Windows and
    DirectDraw headers and are explicitly on the check-tree allowlist.
    Their internal platform header is confined to these files. Other GL
    files consume project-owned types and interfaces and remain host-testable.
  - **`gl_icd.c`** owns:
    - The 17 `Drv*` exports, pixel formats, and contexts.
    - DrvSetContext/DrvReleaseContext keep the ICD's own TLS slot.
    - A critical section guards the ICD globals.
    - Context ownership permits only one current thread per context. Define
      detach/rebind, failed binds, deletion and rebinding to another compatible
      HDC, with pending work flushed at the required boundaries. Keep drawable
      storage/lifetime distinct from GL context state.
    - Establish share-group ownership/refcounts and `DrvShareLists` handling
      before texture objects arrive; add display-list storage in Phase 6.
      Define `DrvCopyContext` state-mask behaviour and failure handling rather
      than leaving mandatory exports as unexplained success stubs.
    - It creates DirectDraw, requires `DDCAPS_3D`, finds the module with
      `GetModuleHandleA("V9XHAL")`, and re-runs `describe` after
      SURFACELOST or a mode change. DriverInit re-runs on a mode change
      (`ddhal_core.c:1800`).
  - **`gl_dispatch.c`**: the static `GLCLTPROCTABLE`, 336 non-null
    `__stdcall` slots, **generated**.
    - One manifest, `src/opengl/gl_entrypoints.psd1`, holds {slot, name,
      prototype} for all 336, verified once against Mesa `gldrv.h` and
      ReactOS `icd.h` in the Phase 0 desk record. `scripts/lib/gl-dispatch.ps1`
      mirrors `scripts/lib/family-matrix.ps1` (`build-host.ps1:42-43`,
      `test_family_matrix.c`): it emits the typedefs and typed stubs for the
      ICD build and the name table for the host test, from both
      `build-host.ps1` (and its MSVC twin) and `build-opengl-icd.ps1`.
    - Every implementation and temporary stub has the exact parameter and
      return types for its slot. A shared untyped stdcall stub is forbidden:
      stack cleanup and return-value conventions differ between functions.
    - A slot whose phase has not landed counts as unimplemented and sets
      `GL_INVALID_OPERATION` where a current context exists, with a defined
      ABI-safe return value. These are development placeholders, not correct
      implementations of those entry points; track every one in the matrix.
    - A host test checks slot order against the manifest. A 32-bit guest test
      exercises representative argument sizes, including float/double
      arguments, and void, integer and pointer returns to catch stack/ABI
      errors.
  - **`gl_surface.c`**:
    - `DDSCL_NORMAL`, a primary plus a clipper (`SetHWnd`), and VRAM back
      and Z surfaces.
    - SURFACELOST leads to Restore or re-create, invalidation of old surface
      descriptors and reconstruction from retained logical texture images.
    - Track drawable resize, minimize/restore, client-to-screen translation
      and single/front/back buffer selection. Wait for dependent work before
      storage is replaced or released, and preserve GL context state.
    - SwapBuffers is a Blt from back to the client rect.
    - Front-buffer drawing clips to `GetClipList`.
- **Build and packaging:**
  - `scripts/build-opengl-icd.ps1`, based on `build-settings-page.ps1:56-140`.
  - `inf.ps1`: file sections :152-156 and :253-257, an AddReg entry with the
    **quoted** key path (`inf.ps1:315-322`), and `Assert-V9xInf`.
  - `build-active-package.ps1`: build, copy, `MANIFEST.TXT` and
    `$expectedPackageFiles`.
  - `V9XCOPY.BAT` and `update-associated-driver.ps1`.
- **Probe: `tools/diag/gl_probe_win32.c` → `V9XGLP.EXE`**, built by
  `scripts/build-gl-probe.ps1`. It renders each scene through the ICD format
  and through a generic format, and writes hashes, BMPs and
  `C:\V9XDIAG\V9XGL.INI`.
- **Gate on 86Box `Win98SE-Fast-D3D` (software):**
  - The probe gets an ICD format.
  - Clear and swap work both windowed and fullscreen.
  - An overlapping window is not drawn over.
  - 100 create/destroy cycles leave VRAM free space unchanged.
  - Context switching between two drawables, detach/rebind across threads,
    rejection of simultaneous ownership, resize/minimize/restore, and mode
    changes preserve the specified context and drawable behaviour.
  - Two GL processes and concurrent GL/D3D activity preserve ordering and
    isolation without deadlock; exercise loss/destruction with work pending.
  - Follow-up Phase 4 tests cover shared texture visibility and deletion;
    Phase 6 tests extend the same lifetime cases to shared display lists.
  - Use the [WGL binding contract](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglmakecurrent)
    and [sharing contract](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-wglsharelists)
    as semantic references, with the Win9x ICD mechanics measured separately.

## Phase 4: geometry pipeline and Quake-path state (host-tested to pixels)

Each pure file in `src/opengl/` has a `tests/host/test_gl_*.c` alongside it.

- **`gl_state.c`, `gl_get.c`**: enables, errors, glGet* and strings.
- **`gl_matrix.c`**: all matrix stacks and operations.
- **`gl_vertex.c`**: current attributes and immediate mode.
- **`gl_prim.c`**: Begin/End assembly for all 10 primitives, polygon mode and
  edge flags.
  - The provoking vertex follows GL: the last vertex, or the first for a
    polygon.
  - Face culling uses the signed area in window space with `glFrontFace`,
    which survives Quake's `glScalef(-1)` on the projection.
  - r3d cull and D3D flat shading are not used.
- **`gl_clip.c`**: homogeneous clipping against the 6 frustum planes and 6
  user planes.
- **`gl_xform.c`**: viewport and depth range.
  - Reversed `glDepthRange` must work, for GLQuake's ztrick.
  - Z is initialised when the context is created.
- **`gl_varray.c`**: vertex arrays, including `glArrayElement` inside
  Begin/End, DrawArrays, DrawElements and InterleavedArrays.
- **`gl_pixelstore.c`**: pack/unpack state and checked row/skip/alignment
  arithmetic land with texture upload, before Phase 5 readback. Honour the
  default alignment as well as explicit settings. Test tightly packed and
  padded rows, narrow RGB images, sub-rectangles and output guard bytes;
  extend format/type coverage with the Phase 6 pixel operations.
- **`gl_texobj.c`, `gl_teximage.c`**:
  - Names that were never generated are allowed.
  - Internal formats 1-4, L/A/I/LA and the sized formats. L takes R.
  - Level images are kept in system memory (the logical image) alongside a
    sampler-ready 16-bit copy, and realised as a DirectDraw texture chain
    when first needed for drawing and when faithfully supported by the
    engine. Keep logical image state separate from hardware residency so
    queries, software sampling, eviction and restore use the same images.
  - TexSubImage on sub-rectangles.
  - Advertised GL limits describe the implementation, including software,
    independently of hardware limits. Preserve accepted dimensions, contents
    and all mip levels down to 1x1; use software for valid textures that the
    hardware cannot represent. Do not silently downscale images or truncate
    mip chains. Uploads outside the advertised GL limit follow GL error rules.
  - Define texture completeness, border handling, proxy queries and allocation
    failure behaviour. Test missing levels, mismatched formats/dimensions,
    every minification filter, 1xN/Nx1 tails and the advertised limit boundary.
  - ViRGE square tiling is an optimization only after tests establish correct
    coordinate scaling, REPEAT seams, filtering and mip selection across the
    whole chain. Cases without a proven faithful mapping use software.
    Include CLAMP/border cases and mutation of shared textures.
- **Lighting and fog.** GL lighting adds its specular term into the colour.
  Fog is computed per vertex into `specular.a`, and engines report through
  `accepts()` whether they can do post-texture fog.
- **`gl_bridge.c`**: GL state to `V9X_R3D_DRAW`.
- **`test_gl_scene.c`**: GL call sequences go through the front end, r3d and
  `d3d_raster` to pixel hashes, checked against the Phase 0.6 reference images
  and the requirements matrix. Use exact assertions for state, errors and
  deterministic scenes; image differences require the previously documented
  tolerance and its reason. Do not widen tolerances simply to pass a backend.
- **Gate:** V9XGLP matches generic GL on software (86Box), then Gen3 (the
  netbook), then ViRGE (86Box 9869 and physical A8U4I5, on a 555 desktop).

## Phase 5: GLQuake and Quake 2 (game-correctness gate)

- **Features first:** `glReadPixels` with GL_RGB, front-buffer draws for the
  loading disc, and `glFinish`.
- **Runs:**
  - GLQuake `timedemo demo1`, with `gl_ztrick 1` and with `gl_ztrick 0`.
  - The Quake 2 demo `timedemo`, windowed and fullscreen, plus `vid_restart`.
  - Quake 2 `gl_log 1` is the call-trace instrument.
- **Per engine, record:**
  - frame rate
  - screenshots compared with generic GL
  - fallback draw counts and reason codes, CPU time, drain/wait time and
    pixels processed (define whether each counter measures candidate,
    covered or written pixels; label estimates explicitly)
  - on the ViRGE, the cost of the software lightmap fallback, and
    `gl_monolightmap AF` / `-lm_4` as the path with no fallback
- One decision doc per machine.
- Frame rates and fallback costs are observations, not pass/fail thresholds.
  This gate establishes game correctness; it does not decide public-release
  timing or replace the remaining GL 1.1 work in Phase 6.
- **Extensions come next, in a sub-plan:** `GL_SGIS_multitexture`, which
  needs a second texture-coordinate set and an explicit ABI revision,
  `WGL_EXT_swap_control` and `GL_EXT_compiled_vertex_array`.

## Phase 6: the rest of OpenGL 1.1

- Full fixed-function lighting: 8 lights, materials, colour material,
  two-sided, local viewer.
- Texgen and the texture matrix.
- Display lists.
- Evaluators.
- Feedback and selection.
- Raster position, Bitmap, DrawPixels, CopyPixels, and ReadPixels for all
  formats and types, with transfer, maps and zoom.
- Remaining pixel format/type coverage using the Phase 4 pack/unpack helpers,
  CopyTex(Sub)Image and GetTexImage.
- Stipple, logic op, polygon offset, point and line width, smoothing and
  dithering, with semantic coverage beyond the Quake scenes.
- All push-attrib groups, and texture priorities.

CPU framebuffer paths go through DirectDraw Lock with the shared all-engine
drain and lifetime contract verified; logical CPU texture images use their
bounded synchronous descriptors. Unsupported hardware state selects software
before emission. Runtime submission failures follow the explicit ABI failure
contract and are not capability fallbacks.

**Gate:** the requirements matrix is complete, with specification-based state,
error and rasterization assertions, V9XGLP reference comparisons and relevant
host tests. All 336 dispatch slots have implemented semantics; no temporary
unimplemented stubs remain. Successful Quake runs alone do not satisfy this gate.

## Requirements coverage matrix

This groups the implementation work; the Phase 0 inventory expands it into
individual requirements. Tests listed here are planned, not passing evidence.

| Requirement | Owning phase | Required evidence |
|---|---|---|
| ICD discovery, exports and calling conventions | 0, 3 | Early clear/present experiment, dispatch order and 32-bit ABI probes |
| Surface bounds, generations and lifetime | 2, 3 | Invalid descriptors, overflow, loss/restore, destruction with work pending |
| Submission ordering, fallback and completion | 2, 3 | ViRGE/Gen3 colour and Z transitions, texture visibility, timeouts and partial failures |
| Context/drawable ownership and WGL operations | 3, 4, 6 | Rebind/thread tests, concurrent processes/D3D, shared textures then lists |
| State, errors, defaults, queries and advertised limits | 4, completed in 6 | Exact assertions for legal/illegal calls and every supported query; no unexplained stubs |
| Transform, clipping and primitive assembly | 2, 4 | Projective coordinates, provoking vertices, winding, shared edges and reversed depth |
| Point/line coverage and rasterization options | 2, 4, 6 | Endpoint/coverage tests, then width, stipple, smoothing and polygon offset |
| Texture objects, images, sampling and environments | 2, 4 | Logical image preservation, completeness, borders, mip tails, filters and limit errors |
| Fragment operations and framebuffer selection | 2, 4, 6 | Alpha/depth/blend/masks, scissored clears, front/back selection, logic op and dithering |
| Pixel pack/unpack and readback | 4, 5, completed in 6 | Row alignment/skips, narrow images, buffer guards, orientation and all formats/types |
| Lighting, texgen and texture transforms | 4, completed in 6 | Per-feature state and rendered tests, including two-sided lighting and texture q |
| Lists, evaluators, feedback, selection and attribute stacks | 6 | State/output assertions, list compile/execute semantics and shared-object lifetime |
| Raster position, bitmap, pixel transfer/copy and zoom | 6 | Valid/invalid raster positions and pixel-operation reference scenes |
| Formats without alpha, stencil, accumulation or auxiliary buffers | 3, 6 | Accurate pixel-format/GL queries and specified behaviour of related calls when buffers are absent |
| GLQuake and Quake 2 integration | 5 | Controlled screenshots, restart/lifetime checks and measured fallback costs on each engine |

## Quake requirements that bind earlier phases (census, 2026-09-26)

- **Textures:** non-square power-of-two sizes with mip chains down to 1x1,
  internal formats 1, 3 and 4, and binding names that were never generated.
- **Blend pairs:** `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`, `ONE/ONE`,
  `ZERO/SRC_COLOR` and `ZERO/ONE_MINUS_SRC_COLOR`.
- **Texture env and alpha test:** `REPLACE` and `MODULATE`, and alpha test
  `GREATER`.
- **Depth:** `LEQUAL` and `GEQUAL` with a reversed range.
- **Primitives:** `GL_POLYGON`, `QUADS`, `TRIANGLES`, fans and strips.
- **Other calls:** `glGetFloatv(GL_MODELVIEW_MATRIX)`, `glScissor` (Quake 2)
  and front-buffer draws.
- **Lifetime:** repeated context create/destroy, and `FreeLibrary` of
  opengl32.

## Risks

- **Win16-mutex deadlock or excessive hold time.** Phase 3 fixes the lock
  order and the bound (drain timeout plus one batch). Code holding it never
  calls USER, GDI or DDraw and never waits on another thread. Measure
  contention and fallback hold time, including simultaneous GL and D3D
  clients.
- **The Phase 1 refactor regresses D3D.** Every commit is gated on pixel
  hashes and scores, on the engine it touches.
- **The INT→LCL cast is not documented for applications.** Phase 0.4
  measures the layout and the ownership field. Pointer readability alone
  cannot validate ownership or lifetime; the HAL validates what it can per
  call, and staleness is the ICD's, through generation and surface-lost
  tracking.
- **Mixed-engine colour/Z and CPU coherence are unmeasured.** Phase 0.5 and
  Phase 2 verify ordering and both directions of visibility. The present
  Intel-only render drain cannot establish ViRGE fallback correctness.
- **Changing the Gen3 abandon-after-timeout from success to failure** is a
  D3D behaviour change. It is gated separately on the netbook.
- **Speed.** Software-rasterizer speed with perspective correction on a
  Pentium-class CPU is measured, not promised. The Gen3 per-batch ring-head
  wait (`intel-gen3-async-submission.md`) is inherited.
- **VRAM on a 4 MB ViRGE.** Front, back and Z at 640x480x16 take about
  1.8 MB. The system-memory copies make eviction cheap.
- **Three target guests do not exist** (original 98, ME, Win95). Their gates
  wait on the guests.

## Files (representative)

- **New:**
  - `src/display32/r3d/{r3d.h,r3d_clip.c,r3d_cull.c,r3d_line.c,r3d_validate.c}`
  - `src/display32/d3d/d3d_state.c`
  - `include/velocity9x/r3d_abi.h`
  - `src/opengl/gl_*.c`, `src/opengl/gl_entrypoints.psd1`
  - `scripts/lib/gl-dispatch.ps1`
  - `tools/diag/gl_probe_win32.c`
  - `scripts/build-opengl-icd.ps1`, `scripts/build-gl-probe.ps1`
  - `tests/host/test_{r3d_clip,r3d_validate,d3d_state,gl_*}.c`
  - `docs/plans/opengl-1.1-requirements.md`, the detailed coverage inventory
  - The Phase 0 decision docs.
- **Modified:**
  - `d3d_internal.h`, `d3d_core.c`, `d3d_soft.c`, `d3d_raster.{c,h}`,
    `d3d_virge.c`, `d3d_i9xx.c`, `ddhal_core.c`
  - `src/display16/dd16.c`
  - `scripts/build-ddraw-hal-dll.ps1`, `scripts/build-host.ps1`,
    `scripts/build-host-msvc.ps1`, `scripts/lib/host-sources.ps1`,
    `tests/host/test_main.c`, `scripts/check-tree.ps1`
  - `scripts/lib/inf.ps1`, `scripts/build-active-package.ps1`,
    `packaging/win98se/V9XCOPY.BAT`, `scripts/update-associated-driver.ps1`
  - `PLAN.md` (OpenGL is no longer deferred), `docs/plans/README.md` and
    `docs/STATUS.md`.

## Verification

- **Every code change:** `./scripts/check-tree.ps1`, `./scripts/build-host.ps1`
  and `./scripts/run-checks.ps1` are green before the commit.
- **Phases 1-2:** the D3D regression set on the netbook, 86Box ViRGE and
  V9XSOFT, with before and after recorded in a decision doc.
- **Phase 3 onward:** V9XGLP runs on:
  - 86Box `Win98SE-Fast-D3D` (software)
  - 86Box `Win86SE` 9869 (ViRGE)
  - A8U4I5 (ViRGE/DX)
  - MICHAEL-NETBOOK (Gen3)
  Each run is compared with generic GL on the same guest.
- **OS compatibility:** the original Windows 98 and ME guests, once they
  exist, run ICD discovery, dispatch, clear/present, window clipping, surface
  restore and mode-change validation. Start with software, then validate the
  applicable hardware paths. Record OS/runtime and engine coverage separately.
  Windows 95 initially runs only the Phase 0 feasibility gate; extend its
  verification matrix according to the resulting decision doc.
- **Phase 5:** GLQuake and Quake 2 timedemos on each engine. Screenshots,
  frame rates, fallback reasons/counts, pixels processed and CPU/drain times
  go in `docs/decisions/`. There is no minimum frame-rate gate.
- **Phase 6:** close every requirements-inventory row with evidence. Keep
  exact semantic assertions separate from image tolerances and record the
  OS/runtime, engine, pixel format and relevant state for each comparison.
- Nothing is reported as working on the strength of reading the code.

## Review notes, 2026-09-26

Two reviewers checked the d7aed01 revision against the tree. The claims that
held: `v9x_render_drain` is Intel-only and `v9x_blt_drain` adds `wait_idle`
(`ddhal_core.c:1292-1397`); `IsBadReadPtr` checks only the INT wrapper
(`d3d_core.c:862-885`); the Win95 result omits the mini-VDD
(`README.md:244-246`). What the review changed, and why:

1. **"Reject stale surfaces" contradicted "the HAL retains no client pointers".**
   The HAL has no surface registry, and `V9xHalDestroySurface` only clears
   colour-key and texture-handle entries. A freed wrapper has been measured
   readable (`d3d_core.c:433-436`). Staleness moved to the ICD; the HAL
   validates layout, format and extent per call, and the one real lifetime
   hazard, Gen3 freeing a placed block without draining
   (`d3d_i9xx.c:1264-1289`, pre-existing), is closed by a drain in
   DestroySurface.
2. **The validator is pure**, in `r3d_validate.c`, over a project-owned
   descriptor that the boundary file fills from the LCL/GBL. Otherwise the
   overflow tests could not be host tests (`win9x-driver-boundaries.md:8-11`).
3. **CPU texture descriptors** are exempt from the `D3DSoftSysMem` gate
   (`d3d_soft.c:329-368`), which is 16-bit-side policy for DirectDraw
   system-memory surfaces, and are checked with `IsBadReadPtr` over the
   declared extent. `V9X_D3D_RASTER_TEXTURE` (`d3d_raster.h:264-274`) grows
   per-level fields, and `V9X_D3D_RASTER_TEXCOORD_REPEATS` (`:110-121`) is
   re-derived if the size limit rises. The ICD keeps a sampler-ready 16-bit
   copy of each level rather than teaching `d3d_raster.c` L/A/I/LA decode.
4. **The shared drain** is `v9x_blt_drain` made external, minus the
   `blt_flip_pending` counter, with `validate_status` called first (the
   ViRGE skips `wait_idle` while the latch is unset). Gen3's drain returns
   success after 4,000,000 polls (`d3d_i9xx.c:533-538`); "timeout is a
   failure" needs a three-state return, which changes D3D behaviour and gets
   its own netbook gate.
5. **Phase 0.9 needs no throwaway export.** `Blt(DDBLT_COLORFILL)` already
   reaches every engine's fill through `V9xHalBlt` (`ddhal_core.c:1509-1549`).
   The private-call experiment exports the real `V9xRenderInterface` at
   `abi_version` 0 with `describe` only; version negotiation retires it.
6. **The dispatch table is generated**, mirroring `scripts/lib/family-matrix.ps1`
   → `build/host/v9x_family_matrix.h` (`build-host.ps1:42-43`,
   `test_family_matrix.c`). One manifest of 336 {slot, name, prototype}
   feeds both the typed stubs and the host slot-order test.
7. **Three guests do not exist:** original Windows 98, Windows ME and any
   Windows 95 with a D3D-capable engine. Every 86Box profile is 98 SE
   (`vm-environment.md:64-78`); ME has no validated configuration
   (`README.md:406`). They are a dependency on Michael, named in Phase 0.
8. **Win16 wording.** CPU rasterization under the Win16 lock is the status quo
   (`d3d_i9xx.c:2010-2012`, `gdi-acceleration.md:29`); what is new is the ICD
   taking the mutex. "Serialize so another process cannot race" was replaced
   by a concrete lock order and bound. KB125867 goes into the desk record.
   The Phase 0.5 rung drains through Lock, which already calls
   `v9x_blt_drain`.
