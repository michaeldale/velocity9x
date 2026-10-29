# DDI 8 and pixel shader 1.1 on Haswell: 3DMark 2001 SE's Nature

Status: 2026-10-29. Step 1 done (desk work):
[`ddi8-on-win9x.md`](../specifications/ddi8-on-win9x.md). Its findings
that change the steps below:

- **The runtime never asks today.** On boot 107, DirectX 8 and 9 objects
  were made and `GUID_GetDriverInfo2` (`F828169C`) was not among the 14
  GUIDs asked; no snapshot on any machine has it. vmhal9x sets
  `DDHALINFO_GETDRIVERINFO2` (8) for DDI 8 and this driver does not
  (inferred gate). That flag is set once in DriverInit, before any
  process picks a level, so step 2 must also measure a DDI 7 process
  declining GetDriverInfo2.
- **Vertex and index buffer handles come back.** DDI 8 streams name
  them by CreateSurfaceEx handle; DDI 7 stopped entering them when the
  table filled.
- **Step 5 is bigger than written:** the kill bit, a dummy primitive
  before every data-carrying `CONSTANT_PS`, a new state layout for four
  surfaces and samplers, and shader kernels of about 1.1-2.1 KB against
  768-byte slots.
- **Found on the way:** `3DSTATE_WM` never sets Pixel Shader Kill Pixel,
  which the PRM requires with alpha test (spec section 8). Unobserved on
  hardware; probed in step 5.

Step 2 done 2026-10-29 (boots 111-116,
[`2026-10-29-haswell-ddi8-step2.md`](../decisions/2026-10-29-haswell-ddi8-step2.md)):

- DDI 8 without shaders is what DirectX 8 and 9 programs get on the HD
  4600. V9XD32 passes at the default and `/ddi8`. 3DMark 2001 SE completes
  with 239 million triangles drawn and no fault (1655, recorded, not
  compared).
- The finding that changes later steps: the runtime announces the surfaces
  it makes with local and global halves that are stale after
  CreateSurfaceEx. The driver now keeps its own copy of every mip level's
  halves (the flip chain excepted). Pixel shaders will reach textures
  through those copies.
- Not checked: the pictures, which no capture here can see.

Step 3 done in part 2026-10-29 (boots 117-119,
[`2026-10-29-haswell-ddi8-step3-what-nature-asks.md`](../decisions/2026-10-29-haswell-ddi8-step3-what-nature-asks.md)):

- 3DMark gates Nature on cube textures as well as PS 1.1. Claiming both
  starts it.
- It then validates fixed-function materials, which the two-stage path
  refuses ("failed to validate material").
- Answering every validation one pass ended in a USER.EXE fault before
  any shader was made, so no shader tokens were captured.
- The shaders' text in 3DMark's data archive names what step 4 needs:
  versions 1.0 and 1.1; tex, texkill, texbem, texm3x3pad and texm3x3vspec;
  mov and mul; and co-issue. Cube textures join step 5.

Step 4 done 2026-10-30, host only
([`2026-10-30-haswell-ps11-step4-translator.md`](../decisions/2026-10-30-haswell-ps11-step4-translator.md)):

- The PS 1.0/1.1 parser, a host-only reference interpreter, and the
  Gen7.5 translator, held to each other.
- Ten kernels run in the extended host EU model match the interpreter
  within 2e-4, and kill the same pixels; two deliberate breaks fail.
- Kernels are 512 to 2,544 bytes, so shader kernels need their own region
  in step 5.

Steps 5-7 are not started.
Claims about the tree cite files at `7a2a0504`. Claims about DirectX 8 and
3DMark that are recalled rather than read or measured are marked so; step 1
and step 3 exist to replace them with evidence.

Scope: the HD 4600 (8086:0412), the i5-4570 at 10.0.1.254:9869
(`haswell-4570-test-machine`), runtime DirectX 9.0c. Boots take more than
five minutes, so probes are planned in batches. Other chips are parked
(see the end).

Siblings: [`hardware-tnl.md`](hardware-tnl.md) (DDI 7, the VS kernel
builder and the host EU model this reuses; it lists "vertex shaders and
DDI 8" as out of its scope), [`intel-haswell-direct3d.md`](intel-haswell-direct3d.md)
(the engine), [`ddi6-drawprimitives2.md`](ddi6-drawprimitives2.md) (how
DDI levels are chosen per program).

## The goal

3DMark 2001 SE's Game 4, Nature, runs on the HD 4600 and draws what it
should. Its Pixel Shader feature test runs too, as the cheaper gate on the
way.

## What is measured

- **3DMark 2001 SE skips it today.** Boot 50 (2026-10-20): Nature, Pixel
  Shader and Advanced Pixel Shader "not supported by hardware"; Vertex
  Shader ran at 5.7 fps with vertex shader caps 0, so 3DMark runs vertex
  shaders in software on this driver
  ([`2026-10-20-haswell-kernel-slots-768.md`](../decisions/2026-10-20-haswell-kernel-slots-768.md)).
- **DDI 7 is the Haswell default** and DirectX 8 and 9 see a HAL device
  through it (DevCaps `0008A650`)
  ([`2026-10-27-haswell-ddi7-default.md`](../decisions/2026-10-27-haswell-ddi7-default.md)).
  The T&L device exists and is not offered
  (`V9X_D3D_TNL_DEVICE_OFFERED` 0,
  [`2026-10-27-haswell-tnl-step6-d3d-device.md`](../decisions/2026-10-27-haswell-tnl-step6-d3d-device.md)).
- **The pixel kernel is fixed-function.** `v9x_intel_hsw_ps_kernel_build`
  (`hsw_eu.c`) generates SIMD8 kernels from `struct v9x_hsw_ps_desc`
  (`intel_hsw_eu.h:270`): two texture stages, fog, specular. Its kernels
  are cached in slots that are a fixed function of that descriptor
  (`d3d_hsw.c:1065-1140`), 768 bytes each. Two textures at most
  (`d3d_core.c:6254`).
- **The tree has no DDI 8.** No GetDriverInfo2, no `D3DCAPS8`, none of
  the DirectX 8 DP2 records.
- **What can be reused:** the align16/SIMD8 EU encoder (`hsw_eu.c`, DP3,
  DP4, MAD, MATH, SEND), the VS builder (`hsw_vs.c`) as the pattern for a
  generated kernel, and the host EU model (`tests/host/hsw_eu_sim.c`)
  that runs kernels against a C reference.

## What is recalled, not measured

These are the premises of the design. Each is checked at the step named.

1. Nature needs pixel shader 1.1 in hardware and accepts software vertex
   processing. The Vertex Shader result above supports the second half;
   nothing here supports the first beyond 3DMark's own message. *Step 3.*
2. The DirectX 8 runtime offers pixel shaders only to a driver that
   answers GetDriverInfo2 with `D3DCAPS8` (DDI 8); a DDI 7 driver's
   `PixelShaderVersion` is 0 whatever it does. *Step 1.*
3. At DDI 8 the runtime sends vertices through stream records
   (`SETSTREAMSOURCE`, `SETSTREAMSOURCEUM`, `SETINDICES`,
   `DRAWPRIMITIVE`, `DRAWINDEXEDPRIMITIVE` and their `2` forms) rather
   than in the DP2 call's vertex buffer, and names the vertex format by
   `SETVERTEXSHADER` with an FVF code when vertex processing is software.
   So DDI 8 changes the draw path of every DirectX 8 and 9 program, not
   only shader programs. *Step 1.*
4. The `D3DGDI2_TYPE_*` requests are `DXVERSION`, `GETD3DCAPS8`,
   `GETFORMATCOUNT` and `GETFORMAT`, and the format list is how a DDI 8
   driver offers texture, render-target and depth formats. *Step 1.*

A reference for the shape of all four, not for shaders: `C:\everything\vmhal9x`
(MIT, JHRobotics, a DDI 8 HAL for Windows 9x virtual GPUs). Its
`d3d.c` answers GetDriverInfo2 and its `mesa3d_draw6.c` walks every DDI 8
record; its `d3dhal_ddk.h` carries the DDI 8 declarations. It reports
`PixelShaderVersion` 0.0, so it is no help past the handshake. Its code is
read and cited, not copied.

## The design

### Layering

```
 DP2 (DDI 8 records)                         GetDriverInfo2
   streams, indices, draws,                    caps 8, formats
   shader create/set/const                         |
          |  V9X_D3D_ENGINE_OPS (new append-only op: a shader draw)
          v
 PS 1.1 core, pure C, host-tested:
   token parser and validator -> IR -> reference interpreter (host only)
                                   -> Gen7.5 SIMD8 translator
          v
 Haswell back end: kernel per shader, constants, four textures, SBE
```

### The front end: DDI 8 in the core

- **GetDriverInfo2**, in `d3d_core.c` beside the existing GUID answers.
  `D3DCAPS8` is built from the DDI 7 caps plus the engine's limits:
  `PixelShaderVersion` 1.1 and `MaxPixelShaderValue` 1.0 only when the
  engine fills the new shader op; `VertexShaderVersion` 0 and no
  `HWTRANSFORMANDLIGHT`, so vertex processing stays the runtime's;
  `MaxSimultaneousTextures` 4; `MaxStreams` 1 at first, raised if step 3
  shows Nature binds more.
- **The format list** from the formats the Haswell path already samples
  and renders (565, 1555, 4444, 8888, X8888, D16, D24S8), each with its
  operations. No DXT, cube or volume formats until step 3 shows they are
  needed.
- **The records.** Streams and indices are per-context state, never
  statics (`v9xhal-statics-are-global`). Draws resolve the stream into
  the FVF vertices the existing path converts (`v9x_dp2_fvf_convert_set`,
  `d3d_dp2.c`), so a DDI 8 draw without a shader is today's draw.
  `ADDDIRTYRECT`/`ADDDIRTYBOX` are accepted and ignored. Records the
  engine cannot do (patches, `VOLUMEBLT`) go to the parse-unknown
  callback, as unknown DDI 7 records do; caps say they are absent.
- **Who gets DDI 8.** Programs that load `D3D8.DLL` or `D3D9.DLL`, on an
  engine that fills the shader op. That is Haswell only. The per-program
  `[Velocity9x.Direct3DDdi]` override (`=7`) is the escape for a title
  that breaks, as it was for DDI 7. No new INI key or verb
  (`no-gated-defaults-dev-driver`).

### The PS 1.1 core

Pure C in `src/display32/d3d/` (the parser, IR and interpreter) and
`src/chipsets/intel/hsw_ps11.c` (the translator), each with host tests.

- **Parser and validator.** The DirectX 8 token stream: version token,
  instruction tokens with their parameter tokens, destination write masks,
  source swizzles and modifiers (negate, `_bias`, `_bx2`, `_comp`,
  replicate), instruction modifiers (`_x2`, `_x4`, `_d2`, `_sat`),
  co-issue, `def`. Registers `c0-c7`, `r0-r1`, `t0-t3`, `v0-v1`.
  Anything outside 1.1 is refused at create, and the create fails; the
  caps never claim more than the parser accepts.
- **Instructions.** Arithmetic: `mov add sub mul mad lrp dp3 cnd nop`.
  Texture: `tex texcoord texkill`, then whichever of `texbem texbeml
  texreg2ar texreg2gb texm3x2pad texm3x2tex texm3x3pad texm3x3tex
  texm3x3spec texm3x3vspec` step 3 finds Nature or the feature test
  using. The rest are refused until a title needs them, and the record
  says which.
- **Semantics the hardware does not give for free.** PS 1.x registers
  hold at least [-1, 1] and the result is `r0`; `cnd` tests `r0.a > 0.5`
  only; `dp3` replicates to all four channels; `tex` writes `tN` and
  `texcoord` clamps to [0, 1]. The translator clamps explicitly after
  every arithmetic instruction, to [-1, 1], or [0, 1] with `_sat`. These
  are recalled from the DirectX 8 SDK reference and are checked against
  it in step 1.
- **The reference interpreter** runs an IR on float inputs per pixel. It
  is host-only, like `r3d_tnl_ref.c`. The translator's kernels run in the
  host EU model on the same inputs and must agree within a stated
  tolerance (the EU's float against the interpreter's double; 1/255 per
  channel at the output is the starting figure).

### The Haswell back end

- **Kernels per shader, not per descriptor.** A shader's kernel is built
  once at `CREATEPIXELSHADER` into a separate shader region of the
  reserve, keyed by a hash of its tokens and the fixed-function state it
  folds in (fog, specular, alpha test, which are D3D render states the
  1.1 program does not express). Statics are global across processes, so
  the region is shared and a slot's signature is checked before use, as
  the fixed-function slots do.
- **Size.** A 1.1 program is at most 4 texture and 8 arithmetic
  instructions, with co-issue up to 8 more. At SIMD8, one D3D vec4
  instruction is up to four EU instructions, plus interpolation and the
  sampler sends. An estimate of 100 instructions, 1.6 KB, is not measured;
  step 4 measures the largest captured shader and sizes the slot from it.
- **Constants** `c0-c7` as push constants (`3DSTATE_CONSTANT_PS`), so
  `SETPIXELSHADERCONST` costs no kernel rebuild. `def` values are folded
  in as immediates.
- **Four textures**: binding table entries and samplers 1-4, and the SBE
  passing four texture coordinates and two colours (two coordinates
  today). Projected coordinates (`D3DTTFF_PROJECTED`) only if step 3 sees
  them.
- **The fixed-function path is untouched** for draws with no pixel shader
  bound.

## Work order

1. **Read before writing.** A spec note,
   `docs/specifications/ddi8-on-win9x.md`, each field cited:
   - the DDI 8 declarations (`vmhal9x\d3dhal_ddk.h`, cross-checked
     against Microsoft's DDK documentation of the same structures);
   - the GetDriverInfo2 sequence on Windows 98 SE with DirectX 9.0c: who
     asks, in what order, and what makes the runtime treat a driver as
     DDI 8 (premises 2 and 4);
   - the stream and draw records (premise 3);
   - the PS 1.1 token format and instruction semantics from the DirectX 8
     SDK reference;
   - HSW PRM Vol 2: `3DSTATE_CONSTANT_PS`, the SBE for six attributes,
     binding tables beyond three entries.
2. **DDI 8 with no shaders.** GetDriverInfo2, caps 8 with
   `PixelShaderVersion` 0, the format list, the stream records. Host
   tests for the stream-to-FVF resolution. Gate:
   - V9XD32 gains `/ddi8` and passes T1-T18 at it;
   - V9XD32 T17's DirectX 8 and 9 caps show the DDI 8 answer;
   - 3DMark 2001 SE completes at DDI 8, score recorded beside DDI 7's
     1120 (recorded, not chased: `no-3dmark-performance-chasing`);
   - Half-Life Direct3D runs (a DirectX 7 program, so it must be
     unaffected).

   This step carries the risk to every DirectX 8 and 9 title and is
   proved before any shader exists.
3. **What Nature asks for.** A measurement build, not committed as a
   default: caps claim `PixelShaderVersion` 1.1; `CREATEPIXELSHADER`
   stores the tokens and writes them to a log; draws with a shader bound
   are counted and dropped. Run Nature and the Pixel Shader feature test.
   Record:
   - every shader's tokens, disassembled;
   - the render and texture-stage states, formats (textures, render
     targets, cube or volume), stream counts and vertex formats used
     while each shader is bound;
   - whether Nature starts at all with vertex caps 0 (premise 1).

   This decides which texture instructions, formats and stream counts
   are in scope. If Nature needs hardware vertex shaders, stop and bring
   the plan back: the VS builder is the base, but that is a second plan.
4. **The PS 1.1 core, tests first.** Parser, IR, reference interpreter,
   translator. Host tests:
   - hand-worked cases for each instruction and modifier, including the
     clamp and `cnd` rules;
   - every shader captured in step 3, run in the EU model against the
     interpreter on generated inputs, within the tolerance;
   - the kernel size of each, which sets the slot size.
5. **The Haswell back end.** The shader region, push constants, four
   textures, the SBE. Gate:
   - a V9XD32 pixel-shader test (T19) that draws each instruction class
     and compares the frame read back with the reference interpreter per
     pixel, within the tolerance;
   - 3DMark 2001 SE's Pixel Shader feature test runs and, by Michael's
     eye or the capture, looks right.

   Expect hangs; a wedged RCS costs a `reset` and five minutes.

   **Step 5 design, approved by Michael 2026-10-30 (both decisions
   below: the reserve grows to 320 KB; PS 1.1 by default after 5b).**
   Steps 3 and 4 changed
   what step 5 has to do:
   - Nature needs cube textures sampled, not only pixel shaders.
   - A 1.1 shader reads four textures and four coordinate sets of up to
     four components. Today's Direct3D draw (`V9X_R3D_DRAW`) carries two
     textures and the 32-byte pre-transformed vertex.
   - Kernels are 512 to 2,544 bytes against 768-byte slots.

   It is split into four parts, each ending on the 4570.

   *5a. A probe first: `V9XHSWMM /ps11`, no HAL change.* The pattern of
   Phases D and E: one boot answers the hardware questions before the
   driver relies on them.
   - **Each run:** the probe builds step 4's kernels and draws each over
     a small rectangle in its own reserve region. Its four textures have
     known texel patterns and are point sampled. It reads the target back
     and compares it with the reference interpreter, which the probe
     links (it is a Win32 tool, not the HAL), at 8-bit precision plus
     one step.
   - **What it settles:**
     - where setup data lands after seven push-constant registers
       (`CONSTANT_PS` after a zero-vertex `3DPRIMITIVE`, Push Constant
       Enable in `3DSTATE_PS`);
     - the SBE with six attributes;
     - four binding entries and samplers;
     - the SIMD8 sample message with three coordinates;
     - what a predicated EOT write does to killed pixels, and to a
       group of eight all killed;
     - whether `3DSTATE_WM`'s Pixel Shader Kill Pixel bit is needed for
       either (spec section 8).
   - **Writes:** as Phase E's, inside the probe region. A wedged RCS
     needs a power cycle, as Phase D's notes say.
   - **Status, 2026-10-30: done, boot 123.** Six programs over three runs
     (push constants, four textures, cube, texkill, texm3x3vspec on a
     cube, texbem) match the interpreter on every judged pixel, max
     channel error 1. Setup data lands at GRF 4 + 7 with no rounding; the
     single-level linear cube layout and the shared face table hold; a
     predicated EOT with killed pixels retires. Kill Pixel's need is
     unmeasured (no depth in the probe). The VxD now borrows the ring
     where the live reserve puts it; its `HSW_RCS_RING_BELOW` must follow
     the reserve to 320 KB in 5b. Record:
     [`../decisions/2026-10-30-haswell-ps11-step5a-probe.md`](../decisions/2026-10-30-haswell-ps11-step5a-probe.md).

   *5b. Shader draws in the HAL, 2D textures.*
   - **Core** (pure parts host-tested):
     - CREATEPIXELSHADER parses with `v9x_ps11_parse` into a
       per-process shader table, kept beside the handle table, and a
       refused shader fails the record;
     - SETPIXELSHADERCONST is kept per context;
     - stages 0-3's texture handles, BUMPENVMAT00-11, LSCALE, LOFFSET and
       TEXCOORDINDEX are kept from the stage states;
     - a draw under a shader converts its FVF vertices to a wide vertex:
       position, diffuse, specular and four coordinate sets, each as
       four floats, the set for stage N chosen by its TEXCOORDINDEX;
     - it goes to a new append-only engine op, `draw_shader`, with the
       program, constants, four texture records and the stage terms.
   - **Haswell:**
     - a pixel-shader kernel cache of 16 slots of 4 KB at 40000h. The
       reserve grows from 256 to 320 KB, and the mini-VDD's
       `V9X_HSW_RESERVE_BYTES` and `_PAGES` grow with it. A kernel
       that does not fit is refused, and counted;
     - a state layout with five binding entries and four samplers;
     - vertex elements for the wide vertex, and the SBE with six
       attributes;
     - push constants: c0-c7 clamped to [-1, 1] as written, the bump
       matrices and luminance terms;
     - Kill Pixel set for texkill and, fixing spec section 8, for
       alpha test, if 5a shows it matters.
   - **Fog** after the shader: the translator gains vertex fog by the
     specular alpha towards the fog colour, as the fixed-function
     kernels do it, host-tested before it is used.
   - **Caps:** PS 1.1 and four textures. They become the Haswell
     default once the gate passes (`no-gated-defaults-dev-driver`).
     `V9X_D3D_PS_CLAIMED` stays only as the measurement switch for what
     is not yet real: cube maps until 5c (renamed
     `V9X_D3D_CUBE_CLAIMED`).
   - **Status, 2026-10-30: core, engine, fog and T19 done; the
     3DMark/Half-Life checks owed.** Reserve 320 KB (boot 124), the wide
     vertex carries sz and rhw (V9XHSWMM /ps11 still passes, boot 125),
     PS 1.1 claimed by default on an engine with `draw_shader`, and
     V9XD32 T19 passes Push, FourTex, Kill and Bem through Direct3D 8 at
     565 (boot 128). Fog after the shader, vertex and table, is in the
     kernel; T19's PushFog and KillFog pass at 565 and at 32 bpp, where
     every program is within 1 (boot 129). 3DMark makes no shader before
     cube maps. Record:
     [`../decisions/2026-10-30-haswell-ps11-step5b-hal.md`](../decisions/2026-10-30-haswell-ps11-step5b-hal.md).
   - **Gate:**
     - V9XD32 T19: a DirectX 8 device draws the ten step 4 shaders with
       DrawPrimitiveUP over a small target, reads it back with LockRect,
       and compares with the interpreter linked into V9XD32;
     - 3DMark 2001 SE completes with PS claimed;
     - Half-Life Direct3D, which is DirectX 7, is unchanged.

   *5c. Cube textures.* Gen7 samples a cube as one surface: six faces,
   each with its mip chain, at a fixed stride (`SURFTYPE_CUBE`,
   spec section 7). DirectDraw would allocate each face separately.
   - The engine's `create_surface` takes a cube's complex surface and
     places all of it in one block in that layout. Each face and level
     gets its address inside the block.
   - The held copies follow a cube's six face attachments.
   - `SURFACE_STATE` gets the cube type and face enables. The format
     list adds CUBETEXTURE, and the caps add CUBEMAP and MIPCUBEMAP.
   - The cube control mode (PRM 2d p272 against p278) is a 5a-style
     probe question.
   - **Gate:** T19's cube cases; 3DMark's Pixel Shader test runs.
   - **Status, 2026-10-30: cubes placed and sampled; 3DMark stops on bump
     maps.** QPitch with mips is h0 + h1 + 12 j (V9XHSWMM /ps11:3, boot
     129). The runtime makes a video-memory cube only once
     GUID_DDMoreSurfaceCaps answers DDSCAPS2_CUBEMAP, and hands it to
     CreateSurface whole (boot 131); the engine places it and samples it
     as SURFTYPE_CUBE, and T19 Cube passes with error 0 at 565 and 32
     bpp (boot 132). The held copies' mip walk no longer takes a face for
     a level. Cubes are claimed. 3DMark 2001 SE then stops: "device does
     not support bump normal maps" - no bump format is published, which
     5d takes first. Record:
     [`../decisions/2026-10-30-haswell-ps11-step5c-cubes.md`](../decisions/2026-10-30-haswell-ps11-step5c-cubes.md).

   *5d. Nature's materials.*
   - Bump texture formats first: with cubes claimed 3DMark stops in
     `P_D3D::DRV_allocateMap`, "device does not support bump normal maps"
     (boot 132).
   - Log each ValidateTextureStageState refusal under the normal
     answers (boot 119 answered all of them, so which ones fail is not
     known).
   - Then draw what Nature validates, or tell it the truth about the
     rest.
   - **Gate:** Nature starts. Step 6 is then Nature itself.

   **Decisions this asks for:**
   1. Growing the reserve to 320 KB, which changes the mini-VDD. The
      alternative is sharing the 64 KB vertex-kernel cache, which the GL
      T&L path fills.
   2. PS 1.1 on by default after 5b, while cube maps wait for 5c.
      3DMark would still gate Nature on cubes, but other DirectX 8
      programs would get shaders without them.
6. **Nature.** Run Game 4 and record:
   - that it completes, with its fps recorded;
   - the engine counters: shader draws refused, timeouts, resets;
   - 3DMark's image quality frame for Nature, looked at by Michael. No
     reference rasteriser image is on the machine; if he wants one, a
     second card's frame is the comparison.

   Then the full default benchmark, recorded.
7. **Record.** A decision doc: what each step measured, which premises
   held, the tolerance, what is refused and why. The PS path is the
   Haswell default from step 5's gate, with the `=7` override as the
   escape.

## Risks

- **DDI 8 changes every DirectX 8 and 9 program's draw path** (premise
  3). Step 2 proves it without shaders; the `=7` override limits the
  damage to one title at a time.
- **Handshake order on Windows 9x.** The DDI 7 work found that Callbacks3
  and Misc2 are asked only while an object is made, never on reset, and
  that a process ID is not proof of ownership. GetDriverInfo2 is expected
  to have the same traps; step 1 reads the sequence from a log before
  step 2 relies on it.
- **PS 1.x precision.** DirectX 8 hardware ran 1.x in fixed point; this
  runs float and clamps. Shaders that rely on a particular hardware's
  rounding will differ within the tolerance.
- **Speed.** Nature is geometry-heavy and vertex shaders run on the CPU.
  A slow but correct Nature meets the goal; speed is not chased here.
- **Faults inside V9XHAL** from new records reach 3DMark as access
  violations. Every new record validates its sizes against the command
  buffer before reading.

## Parked

- **Hardware vertex shaders** (VS 1.1 on `hsw_vs.c`), unless step 3 says
  Nature needs them.
- **DOT3 and environment bump mapping** as texture-stage ops. 3DMark
  reports both unsupported (boot 50). They are fixed-function, not
  shaders, and the PS 1.1 translator's `dp3` and `texbem` code would serve
  them; a follow-up once this lands.
- **PS 1.2-1.4** and the Advanced Pixel Shader test, if it needs them.
- **Other chips with programmable pixel stages**: Ivy Bridge (same
  kernel, `ivy-bridge-gen7-parked`), the HD 5450 and Carrizo (their own
  ISA; the parser, IR and interpreter carry over).

## Related

- [`hardware-tnl.md`](hardware-tnl.md), [`ddi6-drawprimitives2.md`](ddi6-drawprimitives2.md)
- [`2026-10-17-haswell-eu-and-3d-encoders.md`](../decisions/2026-10-17-haswell-eu-and-3d-encoders.md),
  [`2026-10-20-haswell-kernel-slots-768.md`](../decisions/2026-10-20-haswell-kernel-slots-768.md)
- `docs/specifications/win9x-driver-boundaries.md`, whose Direct3D
  section needs a DDI 8 paragraph when step 2 lands.
