# DDI 8 on Windows 9x, pixel shader 1.1, and where each lands on Haswell

Step 1 of [`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md).
Desk work, 2026-10-29: headers, documentation, the HSW PRM and the
snapshots already in `docs/probe`. Nothing here was run on a machine for
this note. Each claim is marked:

- **header**, **DDK comment**, **MS doc** or **PRM**: read in that source;
- **measured**: in a probe file named beside it;
- **vmhal9x** or **Wine**: what an implementation does, which is evidence
  of what works, not of what is required;
- **inferred**: reasoned from the above, to be checked at the step named.

## Sources

| Short | What |
|---|---|
| DDK8 | `C:\everything\vmhal9x\d3dhal_ddk.h`, `ddrawi_ddk.h`: a DX9-era DDK `d3dhal.h`/`ddrawi.h` copy, the only local source of the DX8 DP2 payload structs |
| MW | MinGW-w64 headers under `C:\everything\smart-clock\.pio-tools\ziglang\lib\libc\include\any-windows-any\` (`d3d8types.h`, `d3d8caps.h`, `ddrawi.h`, `ddk\d3dhal.h`) |
| VM | vmhal9x (`C:\everything\vmhal9x`, MIT): a DDI 8 HAL for Windows 9x virtual GPUs, `PixelShaderVersion` 0. Read and cited, not copied |
| ML | Microsoft Learn, `learn.microsoft.com/en-us/windows/win32/direct3dhlsl/`, the ps_1_X reference pages (they cover 1.1-1.4) |
| Wine | `wine-mirror/wine` master: `dlls/wined3d/shader_sm1.c`, `glsl_shader.c`, `dlls/d3d8/tests/visual.c` |
| PRM | HSW PRM in `C:\everything\claude\personal\intel driver research\haswell-2013-gen7.5-prm\`, page numbers as printed |

The Windows 98 DDK (`C:\98DDK`) has no DP2 and no DX8 types; it is not a
source for anything here. No DirectX 8 SDK or DDK documentation is on this
machine.

## 1. What the runtime does today, measured

Every GetDriverInfo call is counted by GUID Data1 in the HAL's
diagnostics (`d3d_core.c:6342`, 16 slots).

**Measured**, `docs/probe/haswell-flush-variants-2026-10-28/b107/omit-0/arm.ini`
lines 372-389: boot 107, one process, V9XD32. Its T17 created DirectX 8
and 9 objects and read HAL caps (DevCaps `0008A650`, T17 lines of the
step 6 runs). The runtime asked 14 distinct GUIDs, none of them
`F828169C`:

| Data1 | GUID (MW `ddrawi.h`, DDK8 `ddrawi_ddk.h`) |
|---|---|
| D7B70EE0 | not in either header |
| EFD60CC0 | MiscellaneousCallbacks |
| 406B2F00 | Miscellaneous2Callbacks |
| EFD60CC1 | VideoPortCallbacks |
| B1122B40 | MotionCompCallbacks |
| EFD60CC2 | ColorControlCallbacks |
| 0BA584E1 | D3DCallbacks2 |
| DDF41230 | D3DCallbacks3 |
| 2E04FFA0 | D3DParseUnknownCommandCallback |
| 80863800 | KernelCallbacks |
| 93869880 | ZPixelFormats |
| 7DE41F80 | D3DExtendedCaps |
| FFAA7540 | KernelCaps |
| 3B8A0466 | DDMoreSurfaceCaps |

No snapshot in `docs/probe`, on any machine, records `F828169C`.

So with the driver as it is, DirectX 8 and 9 never ask the GetDriverInfo2
question at all. They build DX8 caps from the DDI 7 answers, and
`PixelShaderVersion` is 0.

**The likely gate.** `DDHALINFO_GETDRIVERINFO2` = `00000008h`, "indicates
driver support GetDriverInfo2 variant of GetDriverInfo. New for DX 8.0"
(VM `vmhal9x.c:55-58`, which defines it itself; the flag is in neither
MW's nor the 98 DDK's `ddrawi.h`). vmhal9x sets it in its HAL info flags
only when it runs at DDI 8 (`vmhal9x.c:322-326`). This driver sets
`ISPRIMARYDISPLAY | GETDRIVERINFOSET` only (`ddhal_core.c:2558`).
**Inferred:** the runtime asks GetDriverInfo2 only when that bit is set.
Step 2's first boot settles it.

**A consequence for the design.** `DDHALINFO.dwFlags` is set once, in
DriverInit, before any process has chosen a DDI level (`ddhal_core.c:2558`,
and the comment above it on why nothing is chip-selected there). The DDI
level is chosen per process (`v9x_d3d_ddi_for_object`). So the flag is on
for every process or none. A process that should stay at DDI 7 must get
GetDriverInfo2 declined with `DDERR_CURRENTLYNOTAVAIL` (section 2). That
the runtime then falls back to a DDI 7 device cleanly is **not known**;
step 2 measures it with a program listed `=7`.

## 2. The GetDriverInfo2 handshake

- **GUID:** `GUID_GetDriverInfo2` is defined as `GUID_DDStereoMode`
  (DDK8 `d3dhal_ddk.h:1398`), `F828169C-A8E8-11D2-A1F2-00A0C983EAF6`
  (MW `ddrawi.h:105`).
- **Telling it from a stereo query:** `lpvData` points at
  `DD_GETDRIVERINFO2DATA`, and its second dword `dwMagic` is
  `D3DGDI2_MAGIC` = `FFFFFFFFh` for GetDriverInfo2. That dword is
  `DD_STEREOMODE.dwHeight` otherwise (DDK comment, `d3dhal_ddk.h:1258-1268`,
  `:1406`).
- **Header** (`d3dhal_ddk.h:1366-1389`, MW `ddk\d3dhal.h:625-630`, the
  same): `dwReserved` (not to be touched), `dwMagic`, `dwType`,
  `dwExpectedSize`. 16 bytes.
- **Sizes and results** (DDK comment, `d3dhal_ddk.h:1270-1352`):
  - the outer `DD_GETDRIVERINFODATA.dwExpectedSize` is unused; the size
    is `pgdi2->dwExpectedSize`;
  - the answer is written over the header at `lpvData`, `dwActualSize`
    set to the bytes written, `ddRVal` `DD_OK`, return
    `DDHAL_DRIVER_HANDLED`;
  - an unknown type: `dwActualSize` 0, `ddRVal`
    `DDERR_CURRENTLYNOTAVAIL`, still `DDHAL_DRIVER_HANDLED`.
- **Types** (`d3dhal_ddk.h:1413-1427`): `GETD3DCAPS8` 1, `GETFORMATCOUNT`
  2, `GETFORMAT` 3, `DXVERSION` 4. 10h-23h are DX9 types and
  `DEFERRED_AGP_AWARE`/`FREE_DEFERRED_AGP` (18h, 19h) are marked "NT
  only"; all are declined.
- **Which a driver must answer:** no header says. The DDK comment says
  the mechanism is "currently only used for" DX8 caps. vmhal9x answers
  `DXVERSION`, `GETFORMATCOUNT`, `GETFORMAT` and `GETD3DCAPS8` and declines
  the rest (`d3d.c:208-457`). This driver will answer the same four.
- **Payloads** (`d3dhal_ddk.h:1436-1481`):
  - `DD_DXVERSION`: header, `dwDXVersion` (in), `dwReserved`;
  - `DD_GETFORMATCOUNTDATA`: header, `dwFormatCount` (out), `dwReserved`
    (vmhal9x's comment: the DX9 runtime passes `900h` there,
    `d3d.c:241`);
  - `DD_GETFORMATDATA`: header, `dwFormatIndex` (in, guaranteed below the
    count), `DDPIXELFORMAT format` (out);
  - `GETD3DCAPS8`: a bare `D3DCAPS8` over the header.
- **Expected traps, from the DDI 7 work**
  ([`../decisions/2026-10-27-haswell-ddi7-default.md`](../decisions/2026-10-27-haswell-ddi7-default.md)):
  Callbacks3 and Misc2 are asked only while an object is made, never on
  reset, and a process ID is not proof of ownership. GetDriverInfo2 is
  expected to follow the object, as Misc2 does. Step 2 logs its order with
  the other GUIDs (the ring already records GetDriverInfo enter and exit).

## 3. D3DCAPS8

**Header** (MW `d3d8caps.h:222-290`, `pack(4)`; MW `ddk\d3dhal.h:662-716`
the same): 53 four-byte fields, 212 bytes (inferred from the field count).
In order:

`DeviceType, AdapterOrdinal, Caps, Caps2, Caps3, PresentationIntervals,
CursorCaps, DevCaps, PrimitiveMiscCaps, RasterCaps, ZCmpCaps,
SrcBlendCaps, DestBlendCaps, AlphaCmpCaps, ShadeCaps, TextureCaps,
TextureFilterCaps, CubeTextureFilterCaps, VolumeTextureFilterCaps,
TextureAddressCaps, VolumeTextureAddressCaps, LineCaps, MaxTextureWidth,
MaxTextureHeight, MaxVolumeExtent, MaxTextureRepeat,
MaxTextureAspectRatio, MaxAnisotropy, MaxVertexW (float), GuardBandLeft,
GuardBandTop, GuardBandRight, GuardBandBottom, ExtentsAdjust (floats),
StencilCaps, FVFCaps, TextureOpCaps, MaxTextureBlendStages,
MaxSimultaneousTextures, VertexProcessingCaps, MaxActiveLights,
MaxUserClipPlanes, MaxVertexBlendMatrices, MaxVertexBlendMatrixIndex,
MaxPointSize (float), MaxPrimitiveCount, MaxVertexIndex, MaxStreams,
MaxStreamStride, VertexShaderVersion, MaxVertexShaderConst,
PixelShaderVersion, MaxPixelShaderValue (float)`.

**The mapping from what the tree fills** (`win9x_ddraw_abi.h:863-1011`:
`V9X_D3DDEVICEDESC_V1`, `V9X_D3DPRIMCAPS`, `..._D3DEXTENDEDCAPS7`), as
vmhal9x does it (`d3d.c:290-411`):

- `ZCmpCaps` through `TextureAddressCaps` from `dpcTriCaps`, field by name;
- the extended caps' fields by name, the DX7 WORDs widened to DWORDs;
- `DevCaps`, `PrimitiveMiscCaps`, `RasterCaps` and `TextureFilterCaps`
  rebuilt: DX8 drops some DX7 bits and adds others (below);
- `DeviceType` `D3DDEVTYPE_HAL` (1), `Caps2` `D3DCAPS2_CANRENDERWINDOWED`;
- `CubeTextureFilterCaps`, `VolumeTextureFilterCaps`,
  `VolumeTextureAddressCaps`, `MaxVolumeExtent` 0 while no cube or volume
  format is offered.

**The shader and vertex fields** for the plan's first state:

- `PixelShaderVersion` `D3DPS_VERSION(1,1)` = `FFFF0101h` (header,
  `d3d8types.h:477`), `MaxPixelShaderValue` 1.0. In step 2, 0 and 0.0.
- `VertexShaderVersion` 0, `MaxVertexShaderConst` 0, no
  `D3DDEVCAPS_HWTRANSFORMANDLIGHT`: vertex processing is the runtime's.
- `MaxStreams` 1 (vmhal9x says 16; nothing here needs more until step 3),
  `MaxStreamStride` 256 or more (an FVF vertex's size bounds it),
  `MaxVertexIndex` `FFFFh` (vmhal9x's comment: above `FFFFh` advertises
  32-bit indices, `d3d.c:404`; this driver's indexed path is 16-bit).
- `MaxTextureBlendStages` and `MaxSimultaneousTextures` 4 when the shader
  path is on.

**Cap bits new in DX8** (header, MW `d3d8caps.h` against `d3dcaps.h`):

| Group | New in DX8 | Dropped from DX7 |
|---|---|---|
| DevCaps | PUREDEVICE 100000h, QUINTICRTPATCHES, RTPATCHES, RTPATCHHANDLEZERO, NPATCHES (`:50-54`); driver-only HWVERTEXBUFFER 02000000h, HWINDEXBUFFER 04000000h, SUBVOLUMELOCK 08000000h (DDK8 `:1231-1233`) | |
| PrimitiveMiscCaps | COLORWRITEENABLE 80h, CLIPPLANESCALEDPOINTS 100h, CLIPTLVERTS 200h, TSSARGTEMP 400h, BLENDOP 800h, NULLREFERENCE 1000h (`:94-99`) | MASKPLANES, CONFORMANT |
| RasterCaps | COLORPERSPECTIVE 400000h, STRETCHBLTMULTISAMPLE (`:115-116`) | ROP2, XOR, SUBPIXEL(X), STIPPLE, ANTIALIASSORT*, TRANSLUCENTSORTINDEPENDENT |
| FVFCaps | PSIZE 100000h | |
| VertexProcessingCaps | TWEENING, NO_VSDT_UBYTE4 | VERTEXFOG |
| TextureCaps | VOLUMEMAP, MIPMAP 4000h, MIPVOLUMEMAP, MIPCUBEMAP, CUBEMAP_POW2, VOLUMEMAP_POW2 (`:146-151`) | |
| TextureAddressCaps | MIRRORONCE 20h | |

Whether DX8's TextureFilterCaps take the DX7 bit values unchanged is not
stated locally: DX8 defines the MINF/MIPF/MAGF bits only (`:153-162`),
and so does DX7's `dwTextureFilterCaps`. Step 2 compares T17's reported
caps with what was written.

## 4. The format list

- **Header:** `DDPF_D3DFORMAT` `00200000h` marks "a DX8+ format
  capability entry" (DDK8 `ddrawi_ddk.h:358-362`). The D3DFORMAT value
  goes in `dwFourCC`, `dwOperations` shares the union at offset 10h with
  `dwRBitMask`, and the multisample word sits at 14h (MW `ddraw.h:775-805`;
  vmhal9x's macro `d3d_caps.h:480-482` places them this way; no header
  states the `dwFourCC` placement).
- **Operations** (`ddrawi_ddk.h:368-438`), the ones this driver needs:
  `TEXTURE` 1, `OFFSCREEN_RENDERTARGET` 8, `SAME_FORMAT_RENDERTARGET` 10h,
  `ZSTENCIL` 40h, `ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH` 80h,
  `SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET` 100h, `DISPLAYMODE` 400h ("should
  not be set on alpha formats"), `3DACCELERATION` 800h (needs
  `DISPLAYMODE`). `CUBETEXTURE` 4 and `VOLUMETEXTURE` 2 only when step 3
  needs them.
- **D3DFORMAT values** (MW `d3d8types.h:600-653`): R5G6B5 23, X8R8G8B8
  22, A8R8G8B8 21, X1R5G5B5 24, A1R5G5B5 25, A4R4G4B4 26, D16 80,
  D16_LOCKABLE 70, D24S8 75, D24X8 77; DDI-only S8D24 74, X8D24 76
  (DDK8 `:1574-1585`).
- **What this driver offers** (inferred from what the Haswell path
  samples and renders today): R5G6B5 and X8R8G8B8 as display modes with
  3D acceleration, textures and same-format targets; A8R8G8B8, X1R5G5B5,
  A1R5G5B5, A4R4G4B4 as textures; D16 and the 24-bit depth formats as
  `ZSTENCIL`. The depth formats' `ARBITRARY_COLOR_DEPTH` follows what
  V9XD32's mixed-depth checks show.
- `GUID_ZPixelFormats` stays answered for DDI 6 and 7 (vmhal9x does both,
  `d3d.c:706-768`).

## 5. The DP2 records

**Header** (DDK8 `d3dhal_ddk.h:885-951`, MW `ddk\d3dhal.h:331-420`, the
same values). The tree has every DDI 7 op except `EXT` 37
(`d3d_dp2.h:48-84`). New at DDI 8:

| Op | No. | Payload, then what follows (DDK8 `:1626-1769`) |
|---|---|---|
| CREATEVERTEXSHADER | 45 | `dwHandle, dwDeclSize, dwCodeSize`; declaration then code, bytes |
| DELETEVERTEXSHADER | 46 | `dwHandle` |
| SETVERTEXSHADER | 47 | `dwHandle` |
| SETVERTEXSHADERCONST | 48 | `dwRegister, dwCount`; `dwCount` x 4 floats |
| SETSTREAMSOURCE | 49 | `dwStream, dwVBHandle, dwStride` |
| SETSTREAMSOURCEUM | 50 | `dwStream, dwStride` |
| SETINDICES | 51 | `dwVBHandle, dwStride` |
| DRAWPRIMITIVE | 52 | `primType, VStart, PrimitiveCount` |
| DRAWINDEXEDPRIMITIVE | 53 | `primType, BaseVertexIndex (INT), MinIndex, NumVertices, StartIndex, PrimitiveCount` |
| CREATEPIXELSHADER | 54 | `dwHandle, dwCodeSize`; `dwCodeSize` bytes of tokens |
| DELETEPIXELSHADER | 55 | `dwHandle` |
| SETPIXELSHADER | 56 | `dwHandle` |
| SETPIXELSHADERCONST | 57 | `dwRegister, dwCount`; `dwCount` x 4 floats |
| CLIPPEDTRIANGLEFAN | 58 | `FirstVertexOffset, dwEdgeFlags, PrimitiveCount` |
| DRAWPRIMITIVE2 | 59 | `primType, FirstVertexOffset, PrimitiveCount` |
| DRAWINDEXEDPRIMITIVE2 | 60 | `primType, BaseVertexOffset (INT), MinIndex, NumVertices, StartIndexOffset, PrimitiveCount` |
| DRAWRECTPATCH | 61 | `Handle, Flags`; 4 floats if HASSEGS (1), `D3DRECTPATCH_INFO` if HASINFO (2) |
| DRAWTRIPATCH | 62 | as above, 3 floats, `D3DTRIPATCH_INFO` |
| VOLUMEBLT | 63 | `dwDDDestSurface, dwDDSrcSurface, dwDestX, dwDestY, dwDestZ, D3DBOX srcBox, dwFlags` |
| BUFFERBLT | 64 | `dwDDDestSurface, dwDDSrcSurface, dwOffset, D3DRANGE rSrc, dwFlags` |
| MULTIPLYTRANSFORM | 65 | `xfrmType, D3DMATRIX` |
| ADDDIRTYRECT | 66 | `dwSurface, RECTL` |
| ADDDIRTYBOX | 67 | `dwSurface, D3DBOX` |

Each command's `wStateCount` counts records, as for DDI 7 state records
(VM `mesa3d_draw6.c:1411-1855`).

What the fields mean, where no header says:

- **SETVERTEXSHADER with software vertex processing.** vmhal9x quotes the
  2003 DDK (`mesa3d_draw6.c:1444-1456`): every DX8 driver must accept it,
  because it is sent even without programmable vertex processing, and then
  `dwHandle` "is always an FVF code" giving stream zero's format. A handle
  with bit 0 (`D3DFVF_RESERVED0`) clear is an FVF code (VM `mesa3d.h:763`,
  "from permedia driver"). The same quote: a legacy FVF code selected as
  the vertex shader does not reset the pixel shader; only
  `SETPIXELSHADER 0` does.
- **Streams and indices** name surfaces by their CreateSurfaceEx handle
  (vmhal9x resolves `dwVBHandle` through its handle table,
  `mesa3d_draw6.c:1515-1530`; `surfindex.c:630`). Handle 0 unbinds. This
  driver already keeps those handles per process (`d3d_handles.h`), but
  **stopped entering vertex, index and command buffers** after the
  handle table filled in 3DMark 2001 SE (DDI 7 decision, fault 2). DDI 8
  needs vertex and index buffer handles back. The table is now 4,096 per
  process; whether that holds with them is measured in step 2.
- **SETSTREAMSOURCEUM:** the stream is the DP2 call's user-memory
  vertices, `lpVertices + dwVertexOffset`, re-pointed on each call (VM
  `d3dhal.c:327-331`, `mesa3d_draw6.c:441-450`). vmhal9x reads
  `lpVertices` without checking `D3DHALDP2_USERMEMVERTICES` (inferred
  bug there; this driver checks the flag, as it does today).
- **With software vertex processing**, the 2003 DDK as quoted by vmhal9x
  (`mesa3d_draw6.c:1714-1718`, `:1742-1747`): `DRAWPRIMITIVE2` and
  `DRAWINDEXEDPRIMITIVE2` carry runtime-transformed vertices, and stream
  zero is "the only stream that should be accessed". **Inferred:** with no
  hardware T&L the runtime sends the `2` forms and `CLIPPEDTRIANGLEFAN`;
  vmhal9x treats their offsets as bytes into stream 0 and the index
  buffer (`:1695`, `:1725`, `:1753-1755`). Step 2 counts which draw ops
  3DMark 2001 SE actually sends.
- **Patches, VOLUMEBLT, BUFFERBLT:** with their caps 0 the runtime should
  not send them; if it does, they go to the parse-unknown callback, which
  this runtime requires anyway (`d3d_core.c:83-99`).
- **ADDDIRTYRECT/BOX:** managed-texture hints; accepted and ignored.

**Render and stage states new in DX8** (header, `d3d8types.h:788-807`,
`:938-941`): render states 153-173 (`SOFTWAREVERTEXPROCESSING` 153,
point size and sprite 154-160 and 166, multisample 161-162, patches
163-164, `INDEXEDVERTEXBLENDENABLE` 167, `COLORWRITEENABLE` 168,
`TWEENFACTOR` 170, `BLENDOP` 171); stage states `ADDRESSW` 25,
`COLORARG0` 26, `ALPHAARG0` 27, `RESULTARG` 28. DX8 has no
`D3DTSS_ADDRESS` (12); the tree still maps it, harmlessly.

**The DP2 call itself** (`d3dhal_ddk.h:790-834`): no DX8-specific flag
or field. The vertex base is `lpVertices + dwVertexOffset` with
`USERMEMVERTICES`, else the vertex surface's memory plus the offset (VM
`d3dhal.c:301-311`), as this driver does at DDI 7.

## 6. Pixel shader 1.1

### Tokens (header, MW `d3d8types.h`; `REGNUM` and the type's high bits from `d3d9types.h`)

- **Version** `FFFF0101h`. **End** `0000FFFFh`. **Comment**: opcode
  `FFFEh`, size in DWORDs in bits 30:16; skipped.
- **Instruction:** opcode 15:0; co-issue bit 30 (`D3DSI_COISSUE`). There
  is no length field in 1.x (DX9 adds bits 27:24, used by Wine only from
  version 2): the parser needs its own parameter count per opcode.
- **Destination:** register number 10:0, type 30:28 (bits 12:11 are the
  type's high bits in DX9; always 0 in 1.x), write mask 19:16, result
  modifier 23:20 (`_sat` = 1), shift 27:24 as a signed 4-bit count (1
  `_x2`, 2 `_x4`, 15 `_d2`, per Wine's table; the header names no
  values).
- **Source:** type and number as above; swizzle 23:16, two bits per
  output channel, identity `E4h`; modifier 27:24: 0 none, 1 negate, 2
  bias, 3 bias-negate, 4 sign (`_bx2`), 5 sign-negate, 6 complement
  (`1-x`); 7-10 are 1.4 only.
- **Register types:** 0 temp `r`, 1 input `v`, 2 constant `c`, 3 texture
  `t`.

### Instructions

Parameter counts from the ML syntax pages and Wine's table (inferred where
they agree; no header has them):

| Opcode | No. | dst, src | 1.1 |
|---|---|---|---|
| NOP | 0 | 0, 0 | yes |
| MOV | 1 | 1, 1 | yes |
| ADD, SUB, MUL | 2, 3, 5 | 1, 2 | yes |
| MAD | 4 | 1, 3 | yes |
| DP3 | 8 | 1, 2 | yes |
| LRP | 18 (12h) | 1, 3 | yes |
| CND | 80 (50h) | 1, 3 | yes |
| DEF | 81 (51h) | 1 + four raw floats | yes |
| TEXCOORD, TEXKILL, TEX | 64-66 | 1, 0 | yes |
| TEXBEM, TEXBEML, TEXREG2AR | 67-69 | 1, 1 | yes |
| TEXREG2GB | 70 | 1, 1 | ML disagrees with itself; Wine accepts from 1.0; accept |
| TEXM3x2PAD, TEXM3x2TEX, TEXM3x3PAD, TEXM3x3TEX | 71-74 | 1, 1 | yes |
| TEXM3x3SPEC | 76 | 1, 2 | yes |
| TEXM3x3VSPEC | 77 | 1, 1 | yes |
| DP4, CMP, TEXREG2RGB, TEXDP3(TEX), TEXM3x3, TEXM3x2DEPTH | 9, 88, 82-86 | | 1.2+: refuse |
| TEXDEPTH, BEM, PHASE, TEXM3x3DIFF | 87, 89, FFFDh, 75 | | refuse |

### Registers (MS doc, ps_1_x registers page)

| | Count | Access | Range |
|---|---|---|---|
| `c` | 8 | read | -1 to 1 |
| `r` | 2 | read/write; `r0` is the result | at least -1 to 1 |
| `t` | 4 | read/write | at least -1 to 1 (MaxTextureRepeat) |
| `v` | 2 | read; diffuse, specular | 0 to 1, possibly at low precision |

- Reading a temp no earlier instruction wrote fails validation, so a valid
  shader never reads uninitialised `r` (MS doc).
- Each instruction reads at most two of any one register file per
  instruction (the read-port limit). The runtime validates before the
  driver sees the tokens (inferred: the driver's parser still refuses
  what it cannot translate, rather than trusting validation).
- Limits: 4 texture plus 8 arithmetic instructions; a co-issued pair is
  one arithmetic slot; `texbeml` costs one of each (MS doc, ps
  differences page). Texture instructions before arithmetic: standard,
  but no page found that states it for 1.1 (inferred; enforce).

### Semantics the translator must reproduce

- **Range.** Data within `MaxPixelShaderValue` passes unclamped (MS doc,
  D3DCAPS9). With 1.0 advertised, every arithmetic result is clamped to
  [-1, 1]. Wine and old Nine clamp constants, including `def`, to [-1, 1]
  and do not clamp temps; this driver clamps both, which is what 1.0
  hardware did (inferred). The EU has no [-1, 1] saturate: `sel.ge -1.0`
  then `sel.l 1.0`, two instructions (PRM 2b p141).
- **Source modifiers:** `_bias` x - 0.5, `_bx2` 2(x - 0.5) (both
  undefined outside [0, 1]), `1-x`, negate; bias-negate -(x - 0.5),
  sign-negate -2(x - 0.5) (MS doc; Wine `shader_glsl_gen_modifier`).
  Order: swizzle, then modifier.
- **Result modifiers:** shift (`_x2`, `_x4`, `_d2`), then `_sat` to
  [0, 1] (MS doc; Wine). `_sat` is the EU's `.sat` (PRM 7 p1024).
- **Masks and swizzles in 1.1:** masks `.rgba`, `.rgb`, `.a`; swizzles
  identity or `.a`, and `.b` only into `.a` (MS doc).
- **dp3** replicates to all four channels. **lrp** is
  `s0*s1 + (1-s0)*s2`. **cnd** in 1.1: source 0 must be `r0.a`, result
  `r0.a > 0.5 ? s1 : s2`, strictly greater (MS doc).
- **Co-issue:** a `.rgb` instruction and a `.a` instruction; the second
  carries bit 30. A co-issued `cnd` behaves differently across hardware
  (Wine's `cnd_test`: one driver always takes the second source);
  translate per the MS rule and record what Nature does.
- **texcoord:** stage N's coordinates, missing components 0, the fourth 1,
  clamped to [0, 1]; no sample (MS doc).
- **tex:** samples stage N at coordinate set N (subject to
  `TEXCOORDINDEX`). With `D3DTTFF_PROJECTED` Wine divides by the last
  component for `tex`; which other 1.x instructions honour projection is
  unclear (Nine's note). Only `tex` and `texbem` project here until a
  capture shows otherwise.
- **texkill:** kills the pixel when any of x, y, z of coordinate set N is
  below 0; reads the interpolated set, not the register (MS doc; Wine).
- **texbem tM, tN:** `u' = u + M00 tN.r + M10 tN.g`,
  `v' = v + M01 tN.r + M11 tN.g`, sample stage M; the matrix is stage
  M's `BUMPENVMAT00-11` (stage states 7-10). **texbeml** then multiplies
  by `tN.b * LSCALE + LOFFSET` (22, 23); whether that factor is clamped
  is unsettled (Wine clamps in fixed function, not in shaders).
- **texm3x2pad/tex, texm3x3pad/tex/spec/vspec:** dot products of
  coordinate sets M, M+1, (M+2) with `tN.rgb`, then a sample of the last
  stage; `spec` reflects the eye vector `cK`, `vspec` takes it from the
  three sets' `w` (MS doc). These need cube or volume textures for the
  3x3 forms.
- **texreg2ar / texreg2gb:** sample stage M at `(tN.a, tN.r)` /
  `(tN.g, tN.b)` (MS doc).
- **After the shader:** fog still applies to `r0` ("fog is applied later
  in the pipeline than the pixelshader", MS doc registers page; Wine's
  epilogue blends it for shader models below 3). Specular is **not**
  added: it is only `v1` (Wine's fixed-function path adds it, its shader
  epilogue does not; no MS text found, inferred). Alpha test applies to
  `r0.a` after fog (Wine's order; inferred for D3D).

## 7. Haswell: where each piece lands

All from the PRM; "tree" cites today's code.

- **Constants `c0-c7`** (`3DSTATE_CONSTANT_PS`, PRM 2b p770-771): 7
  dwords, read lengths in 256-bit units, at most 64 in total. Eight vec4s
  are read length 4. A dummy zero-vertex `3DPRIMITIVE` must precede any
  `3DSTATE_CONSTANT_*` (PRM 2b p770, the same note as for VS). The tree
  emits an all-zero `CONSTANT_PS` without one on the non-VS path
  (`hsw_3d.c:1872`) and has drawn correctly; once it carries data the
  dummy goes in, copying `v9x_intel_hsw_constant_vs`
  (`hsw_3d.c:2076-2102`). `3DSTATE_PS` DW4 bit 11 Push Constant Enable
  is set exactly when the lengths are non-zero (2b p815).
  - Placement: constants first at the dispatch GRF (4 today), then the
    setup data (PRM 7 p635). PRM 7 p619 allows the hardware to skip
    registers before each block; whether it does here is settled by a
    probe that writes g4-g19 out (step 5).
  - Allocation: the tree reserves 8 KB of PS push space
    (`hsw_3d.c:1084`); 128 bytes needed.
- **Six attributes** (`3DSTATE_SBE`, PRM 2b p801-809): count 6, read
  offset 1, read length 3 (`ceil(6 / 2)`), DW11 constant interpolation
  3 for flat diffuse and specular, as today with 4. 3DSTATE_PS DW4 bit
  10 stays set. The VUE is 8 x 16 bytes, still two 512-bit rows, so the
  VUE row count (`hsw_3d.c:1775`) holds; the VS must write each
  coordinate set as a full vec4.
  - Setup payload: attribute k, component c at GRF `setup + 2k + c/2`,
    dwords `4 (c % 2)` +0 (bary1 coefficient), +1 (bary2), +3 (constant)
    (PRM 7 p635-636, and the `pln` operand rules 2b p149-150; the tree's
    `pln` already reads it this way, `hsw_eu.c:612`, `:654-657`).
  - With constants in g4-g7, setup is g8-g19, ending where the kernel's
    temporaries start (g20, `hsw_eu.c:600-619`).
- **Four textures** (PRM 2d p21, 2b p811-812, 835-836, 844): binding
  table entries 0 render target, 1-4 `t0-t3`; sampler i for stage i
  (descriptor bits 11:8 sampler, 7:0 entry, PRM 7 p145). `3DSTATE_PS`
  Binding Table Entry Count 5, Sampler Count 1 (one group of four). Both
  are prefetch hints. Today's state layout has room for two surface
  states and two samplers (`intel_hsw_3d.h:564-578`, `:631`) and needs a
  new layout.
- **Sampling** (PRM 7 p149-151): SIMD8 `sample` with message length 2
  (u, v) or 3 (u, v, r), response length 4. No projective sample: divide
  by `math.inv` then `mul`. Cube coordinates must be divided by the
  largest magnitude first (PRM 7 p151); cube surfaces are SURFTYPE_CUBE
  with face enables, and the PRM is inconsistent about the cube control
  mode on GT2 (2d p272 against p278). The SIMD8 parameter order is not
  readable in this PRM copy; today's u, v order is measured, anything
  more is probed.
- **Kill** (texkill):
  - The render-target write without a header kills by execution mask
    (PRM 7 p262, p280): predicate the final `send` on a flag built from
    the coordinates (`cmp.ge`). What a predicated EOT send with all
    channels off does is not stated; probe it before relying on it.
  - The alternative: a header with the pixel mask, as Mesa does
    (recalled). PRM 7 p622: payload R1 bits 15:0 may be cleared for
    killed pixels; 31:16 must not change.
  - Either way `3DSTATE_WM` bit 25 Pixel Shader Kill Pixel must be set:
    the PRM requires it when the kernel can kill, and also "when any
    render target has Alpha Test Enable" (PRM 2b p760), so that early
    depth does not write killed pixels (PRM 7 p606).
- **Floats** (PRM 2b p811-813, 7 p987-992, 7 p1024): IEEE mode, flush
  to zero, round to nearest even: all three are 0, and the tree's DW2
  carries only the sampler and binding counts, so it runs in that mode
  (`hsw_3d.c:1265-1266`). A projective divide by 0 gives INF in IEEE
  mode and the largest float in ALT mode; ALT is a candidate to measure,
  not a requirement.
- **Kernel size** (PRM 2b p810, 7 p965): start 64-byte aligned, 128 bytes
  of prefetch past the end, no stated maximum. Today: 48 slots of 768
  bytes, 40 instructions each after the margin (`intel_hsw.h:166-176`,
  `intel_hsw_pipe.h:122-127`). A full 1.1 kernel is about 70-130
  instructions, 1.1-2.1 KB (inferred: up to 24 `pln`, 4 sends, 8
  arithmetic instructions at up to four EU instructions each plus clamps,
  the write). Shader kernels need their own region (the plan's design).

## 8. Found on the way: alpha test without the kill bit

The tree builds `3DSTATE_WM` with Thread Dispatch and the barycentric mode
only (`hsw_3d.c:1238-1246`); bit 25 is never set, while alpha test is
enabled in BLEND_STATE (`d3d_hsw.c:1042-1051`). PRM 2b p760 requires bit
25 whenever a render target has alpha test, and PRM 7 p606 gives the
reason: without it the windower may write depth early for pixels the test
then discards. Nothing on the 4570 has been seen to show it; alpha-tested
foliage with depth writes is where it would. Not filed as an issue: no
machine has produced evidence. It belongs with step 5, which sets the bit
for texkill, and costs a probe of an alpha-tested quad over a farther one.

## 9. Open questions, and the step that settles each

| Question | Settled by |
|---|---|
| Does `DDHALINFO_GETDRIVERINFO2` make the runtime ask? | step 2, first boot: `F828169C` in the GUID list |
| Does a declined GetDriverInfo2 leave a DDI 7 process working? | step 2, a program listed `=7` |
| The order of GetDriverInfo2 against Callbacks3 and Misc2 | step 2, the GetDriverInfo ring |
| Which draw ops 3DMark 2001 SE sends with software vertex processing | step 2, per-op counters |
| Does the handle table hold vertex and index buffers at 4,096 | step 2, 3DMark 2001 SE |
| Do T17's DX8 caps read back as written (filter caps especially) | step 2, V9XD32 T17 |
| Does Nature start with vertex caps 0; which instructions, formats and streams it uses | step 3 |
| Projection beyond `tex`; texbeml's clamp; co-issued `cnd` | step 3 shows whether Nature uses them |
| Where setup data lands after four constant GRFs | step 5 probe |
| A predicated EOT render-target write with all channels off | step 5 probe |
| Alpha test without WM bit 25 (section 8) | step 5 probe |
