# Haswell vertex stage, DDI 7 and the two light models: what the T&L code may set

Date: 2026-10-26. Step 1 of [`../plans/hardware-tnl.md`](../plans/hardware-tnl.md).
Desk reading only; nothing here has executed on the HD 4600.

Each fact carries a source tag:

| Tag | Source |
|---|---|
| **[PRM]** | HSW PRM, `C:\everything\claude\personal\intel driver research\haswell-2013-gen7.5-prm\`. "2b p724" means Vol 2b, printed page 724; V7 is Vol 7. |
| **[crocus]** | Mesa 24.0.9 `crocus_state.c` and `genxml`, at `...\ivybridge-open-source-references\mesa-24.0.9\`. That copy is partial: no `brw_vue_map.c`, `brw_vec4*`, `brw_vs.c` or `src\mesa`. |
| **[SDK]** | Windows SDK 10.0.26100.0 `um\`: `d3dhal.h` (dh), `d3dcaps.h` (dc), `d3dtypes.h` (dt), `ddrawi.h` (di). These still carry the DirectX 7 DDK declarations. |
| **[Mesa-GL]** | Mesa 22.3.0 `main\ffvertex_prog.c` (FFV) and `main\light.c` (LC) at `build\external\audit\mesa\src\mesa\`; MesaFx 7.4.4 `tnl\t_vb_lighttmp.h` (TL). |
| **[recalled]** | Not read in any local source. It must be confirmed before the step that relies on it, or the step treats it as a hypothesis. |

## 1. The vertex shader stage

### 3DSTATE_VS (2b p724-730; `gen75.xml:1094-1131`) [PRM][crocus]

| DW | Bits | Field | Value for this driver |
|---|---|---|---|
| 0 | | 0x7810_0004 | |
| 1 | 31:6 | Kernel Start Pointer, from Instruction Base, 64-byte aligned | the VS kernel slot |
| 2 | 31 | Single Vertex Dispatch | 0 (SIMD4x2) |
| 2 | 25:18 | Binding Table Entry Count | 0 |
| 2 | 16 | Floating Point Mode | 0 IEEE |
| 4 | 24:20 | Dispatch GRF Start Register | 1 [recalled: Mesa's vec4 payload starts at g1] |
| 4 | 16:11 | Vertex URB Entry Read Length, in pairs of 128-bit elements; 0 undefined | ceil(elements / 2) |
| 4 | 9:4 | Read Offset, 256-bit units | 0 |
| 5 | 31:23 | Maximum Number of Threads − 1, GT2 range [0, 279] | 279 (`intel_device_info.c:361`) |
| 5 | 10 | Statistics Enable | 1 |
| 5 | 1 | Vertex Cache Disable | 0 |
| 5 | 0 | VS Function Enable | 1 |

HSW has no "Single Program Flow" bit in DW2.

### The thread payload, SIMD4x2 (V7 p478-480, p525-529) [PRM]

- **g0.** Dword 0 bits 15:0 is the URB handle for vertex 0 (channels 3:0). Dword 1 is the handle for vertex 1 (channels 7:4); the two are equal when the thread carries one vertex. Dword 4 is the binding table pointer; dword 5 holds the scratch offset and FFTID.
- **From the dispatch GRF on:** the push constants first, non-interleaved, eight dwords per GRF. Then the vertex inputs, interleaved: one GRF per attribute, vertex 0 in dwords 0-3 and vertex 1 in dwords 4-7.

### Push constants [PRM][crocus]

- **3DSTATE_CONSTANT_VS:** subopcode 15h, length 7 (2b p766-767; body in 2d p9-11). Read lengths are in GRFs and the four sum to 64 at most. Buffer 0 is relative to Dynamic State Base (depending on INSTPM). Crocus puts a single buffer in slot 3 on HSW (`crocus_state.c:5752-5760`).
- **2b p766:** a 3DPRIMITIVE with zero vertices must precede any 3DSTATE_CONSTANT_*. Crocus does not do this. This driver follows the PRM.
- **2b p939:** CONSTANT_VS must be re-sent after PUSH_CONSTANT_ALLOC_VS and before the next 3DPRIMITIVE. The tree today allocates VS push constants and never sends CONSTANT_VS (`hsw_3d.c:1626-1641`). That is harmless while the VS is disabled. It becomes a breach the moment the VS is enabled.
- **PUSH_CONSTANT_ALLOC_VS** (2b p939-940): offset in bits 20:16, size in bits 5:0, both in KB; the GT2 total is 16 KB. The tree's 8 KB VS / 8 KB PS split is valid. Crocus uses 3/3/3/3 + 4.

### URB (2b p846-847) [PRM][crocus]

- **3DSTATE_URB_VS:** start address in bits 30:25 (8 KB units), entry size in bits 24:16 (64-byte rows, minus 1), entry count in bits 15:0.
- On GT2 the count is 64-1664, and a multiple of 8 when the entry is under 9 rows. The entry must hold the larger of the input and output VUE, and must not overlap the push-constant area.
- Example: header + position + 4 attributes = 6 × 16 bytes = 2 rows, so the field value is 1.

### The VUE (V7 p451-454) [PRM]

- **DW0-3, the header:** DW0 MBZ, DW1 render target array index, DW2 viewport index, DW3 point width.
- **DW4-7:** position, homogeneous clip space.
- **DW8-15:** clip distances, read only when user clip distances are enabled. Not used here.
- **Then:** the attributes, in the order SBE reads them. A VUE the VF writes needs no header when the VS is enabled. A VUE the VS writes must start with a valid header.
- Keeping header, position, then today's attribute order leaves the SBE read offset at 1 (`hsw_3d.c:1687`).

### The thread's end: URB write (V7 p316-323; `brw_inst.h:919-951`, `brw_eu_emit.c:2693-2739`) [PRM][crocus]

| Item | Value |
|---|---|
| SFID | URB = 6 (2a p33; already `intel_hsw_eu.h:114`) |
| Descriptor bits 28:25 | mlen = 1 + n data registers |
| Descriptor bits 24:20 | rlen = 0 |
| Descriptor bit 19 | header present, required |
| Descriptor bit 16 | per-slot offset = 0 |
| Descriptor bit 15 | Complete, ignored for writes |
| Descriptor bit 14 | Swizzle = 1, interleaved: one message carries both vertices |
| Descriptor bits 13:3 | Global Offset, 256-bit units |
| Descriptor bits 2:0 | opcode 0 = WRITE_HWORD |
| EOT | DW3 bit 31 of the send. The destination must be null (V7 p903). |
| Header M0.0 / M0.1 | the two URB handles, copied from g0 |
| Header M0.5 bits 15:8 | channel masks; Mesa ORs 0xff00 |

[recalled] n must be even, and a second write advances the offset by n/2. Six VUE slots (header, position, four attributes) are one write of mlen 7.

### Vertex elements (2d p413-416; 2a p11, p39-40) [PRM][crocus]

- **Formats:** R32G32B32_FLOAT 0x040, R32G32_FLOAT 0x085, B8G8R8A8_UNORM 0x0C0. The last is a D3DCOLOR, delivered to the shader as floats in R, G, B, A order (`hsw_3d.c:1469`).
- **Components:** all four are stored, with no holes. A missing component is STORE_0, except w, which is STORE_1_FP. So XYZ arrives as (x, y, z, 1).
- **Change from today:** drop the STORE_0 header element the VS-off path writes (`hsw_3d.c:1449-1451`). With the VS on, the VF writes inputs only.

### EU encoder additions (`intel_hsw_eu.h`)

The encoder already has SFID URB, CMP, SEL, the conditional modifier in DW0 27:24, Align16 swizzles and the writemask (`hsw_eu.c:264`). The VS adds:

- **DPH** (opcode 0x55, 2b p28): src0.w taken as 1.0. One DPH per row transforms a position with w = 1 implied.
- **Named MATH function codes** (2a p22): INV 1, LOG 2, EXP 3, SQRT 4, RSQ 5, FDIV 9, POW 0xA. They are carried in the same DW0 27:24 field.
- **Named conditional modifiers** (2a p15): .z 1, .nz 2, .g 3, .ge 4, .l 5, .le 6. `sel.ge` with an immediate 0.0 is max(0, x) in one instruction (2b p141).
- **A URB write descriptor helper,** and the header fix-up: an Align1, exec-size-1, NoMask OR of 0xff00 into M0.5.
- **Open:** 2b p134 restricts HSW MATH to Align1, GRF operands and no source modifiers. Mesa enforces the modifier ban only on Gen6, and [recalled] emits Align16 math on Gen7. This driver follows the PRM: math in Align1, one channel group at a time, until a probe shows Align16 math is safe.

## 2. Clipping, setup and attribute selection

### What the tree sets today [tree]

- **CLIP:** non-TL draws get an all-zero 3DSTATE_CLIP (clipper pass-through). TL draws get DW2 `0x80008200`: Clip Enable, ACCEPT_ALL, Perspective Divide Disable (`hsw_3d.c:1131-1145`, `:1674-1678`).
- **SF:** viewport transform off.
- **Viewports:** SF_CLIP_VIEWPORT and SCISSOR_RECT zero.
- **Scissor:** done with the drawing rectangle (`hsw_3d.c:1740-1750`).
- **Clipping:** a CPU guard band (`d3d_hsw.c:1330-1397`).

### 3DSTATE_CLIP for clip-space vertices (2b p741-747; crocus `:2100-2133`, `:6723-6745`) [PRM][crocus]

| DW | Bits | Field | Value |
|---|---|---|---|
| 1 | 20 | Front Winding | as SF |
| 1 | 18 | EarlyCull | 1. The GT3 A/B restriction does not apply to GT2. |
| 2 | 31 | Clip Enable | 1 |
| 2 | 30 | API Mode | 1 = D3D, z clipped to [0, 1] (V7 p549); GL front end maps its depth into that. |
| 2 | 28 | Viewport XY ClipTest | 1 for triangles, 0 for points and lines (crocus) |
| 2 | 27 | Viewport Z ClipTest | 1 |
| 2 | 26 | Guardband ClipTest | 1 |
| 2 | 15:13 | Clip Mode | 0 NORMAL |
| 2 | 9 | Perspective Divide Disable | 0. With it set the hardware cannot clip (p745). |
| 2 | 5:0 | provoking vertex, strip/list, line, fan | to match SF |
| 3 | 27:17, 16:6 | min/max point width, U8.3 | 0.125 / 255.875 |
| 3 | 5 | Force Zero RTA Index | 1 |
| 3 | 3:0 | Max VP Index | 0 |

A vertex with w ≤ 0 gets the NEGW outcode and is clipped in NORMAL mode (V7 p539, p547-549).

### SF_CLIP_VIEWPORT (2d p292-293; V7 p573, p586) [PRM]

- **Layout:** DW0-5 hold m00, m11, m22, m30, m31, m32; DW8-11 hold the guard band X/Y min/max in NDC. 16 dwords per viewport, 64-byte aligned. Pointer: 3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP (2b p820).
- **The transform:** x' = m00·x + m30, and likewise for y and z.
- **Direct3D:**
  - m00 = W/2, m30 = X + W/2
  - m11 = −H/2, m31 = Y + H/2
  - m22 = MaxZ − MinZ, m32 = MinZ
  - The tree's half-pixel `centre_offset` folds into m30 and m31.
- **OpenGL:** the same with the window's y flip and glDepthRange.
- **Guard band:**
  - The field allows ±16K. SF requires every object's bounding box to be within 8K, and clamps vertices to ±32K (V7 p578).
  - Use a band that keeps the span within 8K, the rule the tree already enforces on the CPU.
  - [recalled] upstream Mesa uses 16384 on Gen7+. The local copy lacks `intel_guardband.h`, so that is not checked.
- **CC_VIEWPORT** (2d p64): min and max depth = MinZ and MaxZ; the minimum must be ≥ 0 for UNORM depth.

### 3DSTATE_SF (2b p748-755; crocus `:2017-2076`, `:7104-7142`) [PRM][crocus]

- **DW1 bit 1, Viewport Transform Enable:** must be 1 for clip-space vertices. With it 0, SF rasterises NDC as screen coordinates. Crocus sets it to `!window_space_position`.
- **DW1 bits 9/8/7:** depth offset enables, solid, wireframe and point. **DW4-6:** constant, scale, clamp. For UNORM depth, bias = Constant·r + Scale·MaxSlope, with r the format's step (1/2^16 or 1/2^24), applied by hardware through the depth format field.
  - Crocus passes Constant = units × 2 (`:2058-2063`). The PRM gives no reason for the factor of two, so the GL polygon-offset probe settles it.
- **DW1 bits 6:3:** front and back fill mode.
- **DW2:** cull mode (bits 30:29); Scissor Rectangle Enable (bit 11) = 1. Triangles the guard band accepts extend past the viewport, and crocus always scissors (`:2070`, `:3267-3288`).
- **DW3:** provoking vertex. Direct3D's flat shading takes the first vertex, GL's the last. With the VS on, the CPU no longer copies the provoking colour (`d3d_hsw.c:1314`), so this field, or SBE constant interpolation, does it.

### 3DSTATE_SBE and two-sided colour (2b p801-808; V7 p579, p587-588; crocus `:4526-4667`) [PRM][crocus]

- **Read length,** DW1 bits 15:11: ceil((highest source + 1) / 2), range 1-16. **Read offset,** bits 9:4: 1 skips the header and position.
- **Attribute Swizzle Enable** (bit 21) with a per-attribute Swizzle Select of **INPUTATTR_FACING** (1): reads `[src]` for a front-facing triangle and `[src + 1]` for a back-facing one. src must not be 31; facing follows SF Front Winding.
- **Confirmed:** Gen7.5 hardware selects front or back colour by facing. GL two-sided lighting writes front colour then back colour in adjacent slots. Crocus does exactly this when `light_twoside` is set (`:4537-4549`).

### Scissor (2d p290-291; 2b p723) [PRM]

- SCISSOR_RECT bounds are inclusive U16 values, relative to the drawing-rectangle origin.
- The drawing rectangle always clips, and its origin is added to positions (V7 p575, p594).
- Plan: the drawing rectangle covers the whole target, and the scissor carries viewport ∩ target ∩ user scissor (GL's draw rectangle, D3D's viewport).

### Both paths in one batch [tree][crocus]

`v9x_hsw_workload` re-emits the whole CLIP-to-PS block and every state pointer per draw (`hsw_3d.c:1552-1769`). A pre-transformed draw keeps today's VS-off, ACCEPT_ALL, divide-disabled setup, the same setup crocus uses for window-space positions. A T&L draw emits the VS-on set. Alternating per draw is allowed with no flush between.

**Not checked:** the PIPE_CONTROL rules for changing 3DSTATE_VS, the URB or the drawing rectangle mid-batch. The IVB-only workarounds (depth stall before 3DSTATE_VS, CS stall after the allocation) are not listed on the HSW pages, and the tree already restricts them to IVB (`hsw_3d.c:1642-1661`).

## 3. DDI 7 [SDK]

### Records

The DP2 command header (dh:870-879) is 4 bytes: bCommand, bReserved, then wPrimitiveCount or wStateCount. Each record below is followed by wStateCount payloads of the size shown.

| Op | Value | Payload | Size | Ref |
|---|---|---|---|---|
| VIEWPORTINFO | 28 | dwX, dwY, dwWidth, dwHeight | 16 | dh:904, 1096 |
| WINFO | 29 | dvWNear, dvWFar | 8 | dh:905, 1105 |
| SETPALETTE | 30 | handle, flags, dwSurfaceHandle | 12 | dh:907 |
| UPDATEPALETTE | 31 | handle, wStart, wNum, then entries | 8 + 4n | dh:908 |
| ZRANGE | 32 | dvMinZ, dvMaxZ | 8 | dh:911, 1160 |
| SETMATERIAL | 33 | D3DMATERIAL7: diffuse, ambient, specular, emissive, power | 68 | dh:1167; dt:751-772 |
| SETLIGHT | 34 | dwIndex, dwDataType (0 enable, 1 disable, 2 data + D3DLIGHT7) | 8, or 8 + 104 | dh:1170-1180; dt:817-831 |
| CREATELIGHT | 35 | dwIndex | 4 | dh:1190 |
| SETTRANSFORM | 36 | xfrmType (1 world, 2 view, 3 projection, 16-23 texture), D3DMATRIX | 68 | dh:1196; dt:1143-1158 |
| TEXBLT | 38 | dest handle, src handle, POINT, RECTL, flags | 36 | dh:1217 |
| STATESET | 39 | dwOperation (0 begin, 1 end, 2 delete, 3 execute, 4 capture), dwParam, sbType (1 all, 2 pixel, 3 vertex) | 12 | dh:1137-1155; dt:2072-2078 |
| SETPRIORITY | 40 | surface handle, priority | 8 | dh:1227 |
| SETRENDERTARGET | 41 | hRenderTarget, hZBuffer | 8 | dh:1128 |
| CLEAR | 42 | flags, colour, depth, stencil, then rectangles | 16 + 16n | dh:1235 |
| SETTEXLOD | 43 | surface handle, LOD | 8 | dh:1248 |
| SETCLIPPLANE | 44 | dwIndex, plane[4] | 20 | dh:1183 |

- No opcode 37 is defined. Opcodes 32 and up are "New for DX7" (dh:909-910).
- A SETLIGHT record's size depends on each entry's dwDataType.
- [recalled] CLEAR's wStateCount is the number of rectangles.
- D3DLIGHT7 (dt:817-831) is 104 bytes. dltType: 1 point, 2 spot, 3 directional.

### GetDriverState and surface handles

- **GUID_Miscellaneous2Callbacks** `{406B2F00-3E5A-11D1-B640-00AA00A1F96A}` (di:127).
- **The Win9x DDHAL_DDMISCELLANEOUS2CALLBACKS** (di:848-855), 24 bytes: dwSize, dwFlags, Reserved, CreateSurfaceEx, GetDriverState, DestroyDDLocal. Flags 0x2, 0x4, 0x8 (di:857-859). NT puts AlphaBlt in the third slot; Win9x does not.
- **DDHAL_GETDRIVERSTATEDATA** (di:2737-2750): dwFlags (the ID: 1 texture manager, 2 D3D texture manager, 3 texturing; dt:2068-2070), dwhContext, lpdwStates, dwLength, ddRVal.
- **DDHAL_CREATESURFACEEXDATA** (di:2730-2735); surfaces carry `dwSurfaceHandle` (di:1468).
- [recalled] A DRAWPRIMITIVES2EX driver must export all three Misc2 callbacks. From then on the runtime names textures (D3DTSS_TEXTUREMAP), render targets and TEXBLT sources by CreateSurfaceEx handle, not DX6 texture handle. That makes CreateSurfaceEx the largest piece of step 5, and the step must establish it on the guest before relying on it.

### Vertex buffers

- **No GUID on Win9x.** Driver vertex and command buffers come through `DDHALINFO.lpDDExeBufCallbacks` → DDHAL_DDEXEBUFCALLBACKS: CanCreate, Create, Destroy, Lock and Unlock ExecuteBuffer (di:882-900). The struct is already transcribed (`win9x_ddraw_abi.h:441-455`).
- The request carries DDSCAPS_EXECUTEBUFFER 0x00800000, with DDSCAPS2_VERTEXBUFFER 0x20 or DDSCAPS2_COMMANDBUFFER 0x40 (di:435-446).
- **D3DHALDP2_* flags** (dh:1734-1750): USERMEMVERTICES 0x01, EXECUTEBUFFER 0x02, SWAPVERTEXBUFFER 0x04, SWAPCOMMANDBUFFER 0x08, REQVERTEXBUFSIZE 0x10, REQCOMMANDBUFSIZE 0x20, VIDMEMVERTEXBUF 0x40, VIDMEMCOMMANDBUF 0x80. The SWAP flags let the driver work on a submitted buffer asynchronously and hand back a fresh one. Not used here: the HAL stays synchronous.

### Caps

- **Device caps:** DRAWPRIMITIVES2EX 0x00008000 (dc:483). HWTRANSFORMANDLIGHT 0x00010000, which requires DRAWPRIMITIVES2EX (dc:486).
- **D3DVTXPCAPS** (dc:495-505): TEXGEN 0x01, MATERIALSOURCE7 0x02, VERTEXFOG 0x04, DIRECTIONALLIGHTS 0x08, POSITIONALLIGHTS 0x10, LOCALVIEWER 0x20. This driver claims all but TEXGEN.
- **D3DHAL_D3DEXTENDEDCAPS DX7 tail** (dh:278-316): dwMaxActiveLights at 84, wMaxUserClipPlanes 92, wMaxVertexBlendMatrices 94, dwVertexProcessingCaps 96; total 116, matching the tree's assert.
- **FVF** (dt:1970-1999, 2102-2110):
  - XYZ 0x002, XYZRHW 0x004, XYZB1-5 0x006-0x00E
  - NORMAL 0x010, DIFFUSE 0x040, SPECULAR 0x080
  - TEXCOUNT bits 11:8
  - TEXCOORDSIZE: 2 bits per set at bit 16 + 2i (0 = 2 floats, 1 = 3, 2 = 4, 3 = 1)

## 4. The two light models

Render-state numbers [SDK] (dt:1226-1285):

| State | Value |
|---|---|
| SPECULARENABLE | 29 |
| RANGEFOGENABLE | 48 |
| LIGHTING | 137 |
| AMBIENT | 139 |
| FOGVERTEXMODE | 140 |
| COLORVERTEX | 141 |
| LOCALVIEWER | 142 |
| NORMALIZENORMALS | 143 |
| DIFFUSE / SPECULAR / AMBIENT / EMISSIVE MATERIALSOURCE | 145-148 (D3DMCS 0 material, 1 colour 1, 2 colour 2) |

### Direct3D 7 [recalled: no local source; check against the DX7 SDK documentation or the reference rasterizer before step 5]

- Diffuse = Ce + Ca·Ga + Σ Atten·Spot·(Ca·La + Cd·Ld·max(N·L, 0)); alpha = Cd.a.
- Specular = Σ Atten·Spot·Cs·Ls·(N·H)^P, when SPECULARENABLE. Kept as a separate colour, added after texturing.
- H = norm(L + norm(−Vcam)) with LOCALVIEWER; otherwise norm(L + (0, 0, 1)). The sign is to be checked.
- Directional: L = −direction, Atten = 1.
- Point and spot: d = |P − V|; no light past dvRange; Atten = 1 / (a0 + a1·d + a2·d²).
- Spot, with ρ = (−L)·dir: 1 inside θ/2; 0 outside φ/2; ((ρ − cos φ/2) / (cos θ/2 − cos φ/2))^falloff between.
- Material sources apply only with COLORVERTEX, and fall back to the material when the vertex lacks that colour.
- Vertex fog into specular alpha: d = |Vcam| with RANGEFOG, else Zcam. LINEAR, EXP or EXP2.

### OpenGL 1.1 [Mesa-GL]

- Scene = e + acm·acs; alpha = dcm.a (FFV:915-928).
- Per light: atten·spot·(ambient + max(N·L, 0)·diffuse + specular) (TL:163-187; FFV:1278-1296).
- Specular only when N·H > 0 (TL:209), and summed into the primary colour before texturing (FFV:1077-1078, 1296).
- Attenuation applies to positional lights only, with no range (TL:138-140; FFV:981, 1182).
- Spot: zero below cos(cutoff), else ((−L)·dir)^exponent (TL:143-154; FFV:967-971). A cutoff of 180 disables it (FFV:211); the cosine is computed in LC:184.
- Local viewer: H = norm(L − norm(Veye)) (FFV:1211-1215). Otherwise H = norm(L + (0, 0, 1)) (FFV:1219-1222).
- Two-sided: a back colour from the negated normal and the back material (FFV:1081-1097, 1303-1375).
- GL_COLOR_MATERIAL replaces the selected material terms with the current colour (FFV:197-198; LC:610).
- GL_NORMALIZE renormalises; rescale is GL 1.2.

### What the key must carry, and what the front ends normalise away

Normalised by the front end, so it never reaches the program:

- **Light space.** D3D gives lights in world space, GL in eye space. Both front ends put eye-space positions and directions into the constant block.
- **Directional vector sign.** Both put the toward-the-light unit vector in the constant block.
- **Global ambient type.** It is a constant either way.

Carried in the key, because the program differs:

| Key field | D3D 7 | GL 1.1 |
|---|---|---|
| spot model | cone pair with falloff | hard cutoff, cos^exponent |
| range test | yes | no |
| specular | separate output, gated | summed into the primary colour, always |
| two-sided | never | optional |
| material sources | four terms × {material, colour 1, colour 2} | one term set from the current colour |
| local viewer | state (default TRUE, recalled) | state (default FALSE) |
| normalise | NORMALIZENORMALS | GL_NORMALIZE |
| fog distance | range or Z | \|Ze\| |
| provoking vertex | first | last |

## 5. Corrections to the plan this reading makes

- **Step 5 needs CreateSurfaceEx** (recalled; to confirm). DDI 7 is larger than "GetDriverState plus records": texture, target and TEXBLT handles change meaning.
- **Turning the VS on means more than flipping a bit:**
  - CONSTANT_VS must be sent, preceded by a zero-vertex 3DPRIMITIVE.
  - The STORE_0 header element goes.
  - The URB entry must hold the output VUE.
  - CLIP leaves ACCEPT_ALL, and SF's viewport transform turns on.
  - The scissor is enabled, because the hardware guard band lets triangles past the viewport.
- **EU math:** in Align1, per the PRM, until a probe says otherwise. The other transform and lighting arithmetic (DP4, DPH, MAD, `sel.ge` for max) is Align16.
