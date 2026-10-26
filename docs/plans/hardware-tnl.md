# Hardware T&L on Haswell, for OpenGL and Direct3D

Status: 2026-10-26. Planning only. Nothing here has been built or measured.
Claims about the tree cite files at `7a810e78`. Claims about the hardware
name the PRM volume to be read before the step that relies on them; none
has been checked on a machine. Some are recalled from Mesa and marked so.

Scope, by Michael's direction (2026-10-26): Haswell only, both APIs, with
the vertex stage doing the work. A CPU T&L back end and a second-core
offload are parked; so are the other Intel generations. The shared code is
shaped so that either could be added later without changing the front ends.

Machine: the i5-4570, HD 4600 (8086:0412), at 10.0.1.254:9869
(`haswell-4570-test-machine`). Boots take more than five minutes, so probes
are planned in batches.

Siblings: [`ddi6-drawprimitives2.md`](ddi6-drawprimitives2.md) (DDI 7 named
as the next step, for Direct3D 9), [`opengl-1.1-icd.md`](opengl-1.1-icd.md)
(the ICD; lighting listed as future work at 867),
[`software-d3d-smp-workers.md`](software-d3d-smp-workers.md).

## The question

Haswell has a programmable vertex stage on its EUs. The tree switches it
off: the VF writes the VUE directly (`hsw_3d.c:1418`). Every vertex is
transformed on the CPU, by the Direct3D runtime or by the ICD's own
`gl_prim.c`, and arrives in screen space.

Can a VS kernel, generated per fixed-function state, take over transform,
lighting, fog and clipping for both APIs on that chip? For Direct3D the
visible result is a DirectX 7 T&L HAL device. For OpenGL it is the ICD's
geometry path moving off the CPU, and OpenGL lighting existing at all.

## What the tree has

- **EU encoder and per-state kernels.** `hsw_eu.c` (921 lines) encodes
  align16 instructions, including DP4, DP3, MAD, MATH and SEND. The PS
  kernel is generated from a draw descriptor
  (`v9x_intel_hsw_ps_kernel_build`, `hsw_eu.c:772`) and cached in slots
  keyed by its signature (`d3d_hsw.c:1062-1133`;
  `2026-10-20-haswell-kernel-slots-768.md`). A VS kernel per T&L key is
  the same machinery with a second builder.
- **VS plumbing, written and disabled.** 3DSTATE_VS, URB_VS and the VS
  push-constant allocation (`hsw_3d.c:49-58`, `:1627-1660`).
- **Direct3D:** DDI 5 and 6 only. DX7 records fall to the runtime's parser
  (`d3d_core.c:4987-5029`). Untransformed FVFs are refused
  (`d3d_dp2.c:570-614`). No GetDriverState and no CreateD3DBuffer.
- **OpenGL:** the ICD transforms with modelview and projection and clips
  in clip space against six frustum planes and four draw-rectangle planes
  (`gl_prim.c:636-805`, `:1119-1165`). It computes fog per vertex and
  polygon offset per triangle (`gl_prim.c:901-960`). It then sends
  screen-space `V9X_R3D_ABI_VERTEX` batches of up to 64 triangles through
  the render interface (`gl_icd.c:1322`, `:2330`; `r3d_abi.h`, version 6).
  `GL_LIGHTING` is stored as state and does nothing. No
  `EXT_compiled_vertex_array`.

## The design

### Layering

```
 OpenGL ICD (gl_prim / gl_varray)      Direct3D DP2 (DDI 7 records)
          |  render interface v7               |  V9X_D3D_ENGINE_OPS
          +------------------+-----------------+
                             v
          T&L key + constant block + vertex layout   (pure C, host-tested)
                             v
          Haswell back end: VS kernel per key, push constants, VF, VS on
```

1. **Two front ends.** Each turns its API's state into the same three
   things: a **key**, a **constant block** and a **vertex layout**. Each
   keeps today's path as the answer to anything the key cannot express.
2. **The shared core, pure C, host-tested.**
   - The **key** holds everything that changes the program:
     - the input layout, and which of normal, colour, specular and
       texture coordinates are present;
     - lighting on or off; the count and type of active lights;
     - the light model (D3D or GL; see below);
     - material sources: D3D's `*MATERIALSOURCE`, GL's
       `GL_COLOR_MATERIAL`;
     - specular on or off, local viewer, normalise;
     - two-sided (GL only);
     - fog mode;
     - the texture-coordinate source per unit.

     Equal keys produce equal programs, and the kernel cache is indexed
     by key.
   - The **constant block** holds the numbers: model-view,
     model-view-projection, the normal matrix, light and material
     parameters, fog range and the texture matrices. Its layout is fixed
     by the key.
   - The **reference evaluator** runs a key and constant block over input
     vertices in plain C and produces clip-space positions and lit
     colours. It exists for the host tests and the guest comparisons. It
     is not linked into the HAL or the ICD; see "Parked".
3. **The Haswell back end.** It builds or finds the VS kernel for the key,
   loads the constant block as push constants, points the VF at the
   vertices, enables the VS, and lets the fixed-function clipper and SF
   take the clip-space output.

### The two light models

GL 1.1 and Direct3D 7 differ in ways the key has to name, not paper over.
A front end that cannot express its state as a key falls back to today's
path; it never approximates.

| | Direct3D 7 | OpenGL 1.1 |
|---|---|---|
| Spot falloff | inner and outer cone (theta, phi), falloff power | cutoff angle, `cos^exponent` |
| Attenuation | `1 / (a0 + a1 d + a2 d²)`, plus a range cut-off | the same, no range |
| Two-sided | no | `GL_LIGHT_MODEL_TWO_SIDE`: back colours, selected by facing |
| Specular | D3DRS_SPECULAR, a separate output | added to the primary colour in 1.1 |
| Provoking vertex (flat) | first | last |
| Texture coordinates | pass-through at first | pass-through plus the texture matrix |

These are from the two specifications and must be checked against them in
step 1. Two-sided lighting needs the SBE to select front or back colours by
facing. That Gen7's attribute swizzle can do this is recalled from Mesa,
not read from the PRM. Until it is confirmed, two-sided stays on today's
path.

### OpenGL: the render interface grows a T&L draw

- Version 7 of `r3d_abi.h` adds a describe flag, "the engine runs T&L
  keys", and a draw that carries the key, the constant block,
  object-space vertex arrays with their layout, an index list and the
  primitive. The interface stays stateless and synchronous (`r3d_abi.h`
  header): the HAL copies vertices into batch memory under the Win16
  mutex and keeps no pointer.
- The ICD sends its untransformed vertices this way instead of
  transforming them in `gl_prim.c` when three things hold: the engine
  describes the flag; the state forms a key; and nothing is active that
  needs CPU-side vertices. That means feedback, selection, raster
  position, user clip planes and texgen at first.
- Hardware state moves with it: polygon offset becomes the SF global depth
  offset (today it is computed per triangle on the CPU), cull and front
  face become SF state, the draw-rectangle clip becomes the scissor, the
  depth range becomes the viewport, and the window's y flip goes into the
  viewport transform.
- What it buys: the ICD's per-vertex transform, clip, fog and emit work,
  which today is x87 C on the game's thread, goes to the GPU. OpenGL
  lighting exists on Haswell for the first time. Other chips keep the ICD
  as it is, unlit.
- Not bought: GL 1.1 has no buffer objects and the ICD has no compiled
  vertex arrays, so vertices are still copied once per draw. Adding
  `EXT_compiled_vertex_array`, which Quake 3 uses when it is offered, is a
  follow-up that would let locked arrays stay in GTT memory across the
  passes of a frame.

### Direct3D: DDI 7 and the T&L device

- The DX7 records: SETTRANSFORM, SETLIGHT, CREATELIGHT, SETMATERIAL,
  SETCLIPPLANE (zero planes at first), STATESET, SETRENDERTARGET, CLEAR,
  ZRANGE; VIEWPORTINFO used, not skipped. GetDriverState through
  `GUID_Miscellaneous2Callbacks`. `D3DDEVCAPS_DRAWPRIMITIVES2EX`.
- `D3DDEVCAPS_HWTRANSFORMANDLIGHT`, `dwMaxActiveLights` 8, and
  `dwVertexProcessingCaps` for directional, positional and spot lights
  and the local viewer. Texgen and vertex blending are zero.
- Driver vertex buffers (CreateD3DBuffer, LockD3DBuffer and the rest) are
  allocated in GTT memory, and the VF fetches them directly. For geometry
  that a DX7 title keeps in vertex buffers, the CPU never touches a
  vertex after the application writes it. Today every vertex is
  transformed by the runtime, converted by `v9x_dp2_fvf_convert_set` and
  copied.
- Per-context T&L state lives in the context, never in a static
  (`v9xhal-statics-are-global`).
- A new append-only op in `V9X_D3D_ENGINE_OPS` (`d3d_internal.h:488-598`)
  takes the key, the constants, the vertex source and the indices. Only
  the Haswell engine fills it, so only Haswell offers the T&L device.

### Precision

The EU's MATH POW and RSQ are not IEEE exact, and neither the runtime nor
the ICD is the reference evaluator. Agreement is within a stated per-channel
tolerance, never bit-identical. The step 3 record states the tolerance and
what produced it.

## Work order

Progress:

- **Step 1 done (2026-10-26):**
  [`haswell-vertex-stage-tnl.md`](../specifications/haswell-vertex-stage-tnl.md).
  It confirms that Gen7.5 can select the back colour by facing (SBE
  INPUTATTR_FACING), so GL two-sided lighting is in scope. It also adds
  work to two steps:
  - **Step 3:** turning the VS on needs CONSTANT_VS (preceded by a
    zero-vertex 3DPRIMITIVE), CLIP out of ACCEPT_ALL, the SF viewport
    transform, and the scissor.
  - **Step 5:** DDI 7 very likely needs CreateSurfaceEx, because DX7
    names textures and render targets by surface handle. That is
    recalled from the DDK documentation, not read in a header, and step
    5 confirms it on the guest first.
- **Step 2 done (2026-10-26):** `include/velocity9x/r3d_tnl.h` (64-byte key,
  1440-byte constant block = 45 GRFs), `src/display32/r3d/r3d_tnl.c` (the
  key rule), `r3d_tnl_ref.c` (the host-only evaluator) and
  `tests/host/test_r3d_tnl.c` (15 groups, hand-worked; Direct3D cases
  encode the recalled equations). Each front end's state-to-key function
  moved to its own step: GL with step 4, which also has to add the light,
  material and light-model state the ICD does not keep today; Direct3D
  with step 6.
- **Step 3 done for correctness (2026-10-26, boots 84-85):**
  [`2026-10-26-haswell-tnl-step3-vertex-stage.md`](../decisions/2026-10-26-haswell-tnl-step3-vertex-stage.md).
  - Render interface ABI 7: `draw_tnl`, indexed.
  - The VS on through CONSTANT_VS, CLIP and the SF viewport.
  - The ICD captures primitives and keeps the CPU's batch as the fallback.
  - The V9XGLP A/B scene matches within 565 rounding. Half-Life GL and
    Quake 3 draw correctly, with 0 refused.
  - Speed: Half-Life's timedemo is 7% behind the CPU path and Quake 3 at
    par. That is the ceiling while the CPU still transforms underneath.
- **Step 3b (new): beat the CPU path.** Three pieces:
  - capture vertex arrays as arrays, vertices once and indices as given;
  - a visibility test per batch, not per vertex, to skip empty draws
    without the full transform;
  - drop the CPU twin, with a host-tested CPU fallback from the held
    object vertices.

  The decision record says why each is needed.
- **Step 3b done (2026-10-26, boot 87):**
  [`2026-10-26-haswell-tnl-step3b-segmented-batches.md`](../decisions/2026-10-26-haswell-tnl-step3b-segmented-batches.md).
  - The lever was batch count, not the three pieces above. A held T&L
    batch now carries up to 8 state segments that share vertices and
    constants.
  - Half-Life 88.0 vs 85.2, and Quake 3 58.6 vs 57.1, with T&L on vs off.
    Engine draws in Quake 3 fell by 41%.
  - The A/B scene is unchanged within rounding, with segments exercised.
  - The three pieces stay open. Dropping the CPU twin is the next saving.
- **Step 3c done (2026-10-26, boot 88):**
  [`2026-10-26-haswell-tnl-step3c-no-cpu-twin.md`](../decisions/2026-10-26-haswell-tnl-step3c-no-cpu-twin.md).
  - **What changed:** no CPU twin; a per-triangle frustum and facing cull
    in its place; a fallback from the held object vertices; 16 segments
    a batch; an element cache for `glDrawElements`; captured batches
    scissored to the draw rectangle.
  - **Speed:** Half-Life about 95 against 85, and Quake 3 61.1 against
    57.2. Through `glDrawElements` Quake 3 runs 69.1 against 56.1.
  - **Follow-up:** `EXT_compiled_vertex_array` would put Quake 3 on that
    path by default.
- **Step 5 started (2026-10-26, boots 89-92):**
  [`2026-10-26-haswell-ddi7-step5-first-light.md`](../decisions/2026-10-26-haswell-ddi7-step5-first-light.md).
  - **Built:** DDI 7 for programs listed `=7`; CreateSurfaceEx handles;
    the DX7 records walked.
  - **Measured:** V9XD32 `/ddi7` passes T1-T16, including a DX7 device.
  - **Owed:** the SDK samples, DirectX 9's HAL device and one DX7 title.
    Then the default, then step 6.
- **Step 5 done, DDI 7 the Haswell default (2026-10-27, boots 92-97):**
  [`2026-10-27-haswell-ddi7-default.md`](../decisions/2026-10-27-haswell-ddi7-default.md).
  - **Passed:** V9XD32, 3DMark 2000 and 2001 SE, and Half-Life Direct3D,
    at par with DDI 6. DirectX 8 and 9 see a HAL device either way.
  - **Not run:** the DX7 SDK samples, which are not on the machine.
  - **Next:** step 6.

1. **Read before writing.** The output is a spec note, each field to be
   set with the section that licenses it.
   - HSW PRM Vol 7: VS dispatch (SIMD4x2 is recalled from Mesa), the
     payload, URB writes, and the VUE layout the clipper and SBE expect.
   - HSW PRM Vol 2: 3DSTATE_VS, the push constants, the clipper, SF depth
     offset, provoking vertex, and SBE facing selection.
   - DX7 DDK `d3dhal.h`: the DDI 7 records and GetDriverState.
   - The GL 1.1 specification, 2.13: lighting.
2. **The key, the constant block, the reference evaluator.** Tests first.
   - Expected values worked by hand from each API's lighting equations.
   - For unlit geometry, cross-checked against the ICD's own transform in
     `gl_prim.c`, which is already host-tested.
   - Both front ends' state-to-key functions tested here too, including
     the states that must refuse.
3. **A VS kernel on Haswell, position only, through OpenGL.** The render
   interface is our own ABI, and the ICD's current path renders the same
   frame on the same machine, so the first kernel is checked by frame
   diff, with no DDK unknowns involved. Gate: an unlit GL probe scene,
   then Quake 3 (`C:\Q3Ademo`) and Half-Life GL, diffed against the CPU
   path. This step proves dispatch, URB, push constants, the clipper and
   the viewport. Expect hangs.
4. **OpenGL lighting and fog in the kernel.** Gate: a lit GL probe
   (directional, point, spot, colour material, two-sided once step 1
   confirms the SBE) against the reference evaluator's output within the
   tolerance. No game here exercises GL lighting, so the probe is the
   gate.
5. **DDI 7 without T&L, Haswell.** GetDriverState, DRAWPRIMITIVES2EX, the
   DX7 records that do not transform. Gate: V9XD32 at a new `/ddi7`, the
   DX7 SDK samples on the plain HAL device, and whether DirectX 9.0c now
   offers a HAL device (recorded either way). This step can run alongside
   steps 2-4.
6. **The Direct3D T&L device.** Caps, the transform records to the key,
   driver vertex buffers in GTT memory, the D3D light model in the kernel.
   Gate: the DX7 SDK lit samples and a lit probe, diffed against the
   plain HAL device (the runtime's T&L) within the tolerance; V9XD32 T&L
   lines; one DX7 title that offers a T&L device, played.
7. **Record.** A decision doc covering what the probes measured, the
   tolerance, which games took the new paths, and what still falls back
   and why.

## Decision 1 (Michael, taken 2026-10-26): on by default

The GL path from step 3 and the T&L device from step 6 become what the
Haswell build does as soon as each gate passes, with no INI key or verb to
turn them on (`no-gated-defaults-dev-driver`). Every other chip is
unchanged. If a Direct3D title turns out to prefer the T&L device and break
on it, the existing per-program DDI override is the escape, not a new
switch.

## Parked

- **The CPU back end and a second core.** The reference evaluator is
  already a CPU T&L back end in all but its wiring. Running it in the HAL
  only pays where it does not compete with the runtime's T&L on the same
  core, which means a spare core. Design notes from 2026-10-26, kept for
  if this is picked up:
  - Glidulator's `GLSMP.VXD` shows INIT/SIPI and ring 0 jobs work on a
    Haswell under Windows 98 (`C:\everything\Glidulator`,
    `docs/technical-reference.md` section 5).
  - A from-scratch VxD for this driver could be simpler in three ways:
    workers restricted to the shared arena above 2 GiB, so the page
    tables are built once and nothing is remapped under a worker;
    MONITOR/MWAIT idle instead of spinning; INIT/SIPI targeted from the
    MADT instead of broadcast.
  - Glidulator's measured traps become requirements: Windows 98's
    disabled local APIC, parking stopped cores in ROM for SMM's sake,
    one worker per physical core.
- **Ivy Bridge (Gen7).** The same kernel with Ivy Bridge's workarounds, once
  the machine's IGD config read is fixed (`ivy-bridge-gen7-parked`).
- **Gen4 and Gen5** (GM965, Ironlake). They have vertex stages, driven by
  `g4_eu.c`. Their clip unit runs an EU kernel rather than fixed-function
  hardware (recalled from Mesa's `brw_clip`), which means a second kernel
  to write.
- **`EXT_compiled_vertex_array`** in the ICD (see the OpenGL section).

## Not in scope

- Vertex shaders and DDI 8. The VS kernel builder is the natural base for
  them later.
- Texgen, texture transforms in Direct3D, vertex blending, user clip
  planes, GL feedback and selection on the GPU. These keep today's path or
  report zero caps.
- Any chip without a vertex stage.

## Related

- [`ddi6-drawprimitives2.md`](ddi6-drawprimitives2.md),
  [`opengl-1.1-icd.md`](opengl-1.1-icd.md),
  [`software-d3d-smp-workers.md`](software-d3d-smp-workers.md)
- [`2026-10-17-haswell-eu-and-3d-encoders.md`](../decisions/2026-10-17-haswell-eu-and-3d-encoders.md),
  [`2026-10-20-haswell-kernel-slots-768.md`](../decisions/2026-10-20-haswell-kernel-slots-768.md)
- `include/velocity9x/r3d_abi.h`: the render interface; this plan makes it
  version 7.
- `docs/specifications/win9x-driver-boundaries.md`. Its Direct3D section
  still says "no transforms, lighting, clipping" (stale since 2026-08-11)
  and needs updating when step 6 lands.
