# HD 4600: DDI 8 without shaders; 3DMark 2001 SE completes once the driver keeps its own copy of the runtime's surfaces

Date: 2026-10-29. Machine: the i5-4570 (W9QI5A83, HD 4600), boots 111 to
116. DirectX 9.0c. Step 2 of
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md);
the spec it relies on is
[`../specifications/ddi8-on-win9x.md`](../specifications/ddi8-on-win9x.md).
Evidence: [`../probe/hsw-4570-ddi8-step2-2026-10-29/`](../probe/hsw-4570-ddi8-step2-2026-10-29/).
The builds tested were made from the working tree before each commit.

## Decision

A program that loads D3D8.DLL or D3D9.DLL gets DDI 8 on the HD 4600, with
no shaders claimed. `[Velocity9x.Direct3DDdi] <program>=7` keeps a program
at DDI 7. No other chip changes: the flag the runtime needs is set only for
an engine stamped `V9X_DD_ENGINE_CAP_D3D_DDI8`, which only the Haswell open
stamps.

## What was measured

| Boot | Build | What it showed |
|---|---|---|
| 110 | 0.33.0 | GUID_GetDriverInfo2 (`F828169C`) is never asked: not in the 14 GUIDs of any snapshot |
| 111 | DDI 8 first build | Asked once `DDHALINFO_GETDRIVERINFO2` is set. V9XD32 PASS default and `/ddi8`. 3DMark 2001 SE: first texture `D3DERR_INVALIDCALL` |
| 112 | caps matched to the runtime | 3DMark completes, but 569,165 DP2 calls draw 0 triangles; its 2818 is not a result |
| 113 | buffer refusals logged | Stream handle 5's kept local half reads caps `00010000`, memory 0 |
| 114 | buffer memory copied at CreateSurfaceEx | 21,752 triangles, then 3DMark faults at `B0444C36`, in `v9x_d3d_hsw_texture_unit` |
| 115 | held-surface measurement | Runtime-made surfaces' halves are stale by their first use; the flip chain's are not |
| 116 | held surfaces | 3DMark 2001 SE completes: **1655**, 127,487 DP2 calls, 239,664,378 triangles, 0 refused, malformed or unparsed, 0 engine resets, no fault. V9XD32 PASS default and `/ddi8` |

### The handshake (boot 111, `V9XD7-b111-gdi2.LOG`)

- DirectX 8 asks DXVERSION (`802`), then D3DCAPS8 (212 bytes), the format
  count (7) and each format.
- DirectX 9 asks DXVERSION (`902`) and D3DCAPS9 (type `10h`), is declined,
  and asks the DX8 sequence instead.
- Every call came from a process the driver had made DDI 8 (the log's last
  two fields), so the latch rule in `v9x_d3d_get_driver_info2` was not
  exercised by a DDI 7 process asking.

### D3DCAPS8 against the runtime's own (boot 111)

T17 now dumps all 53 fields. With `/ddi7` the runtime builds D3DCAPS8
itself from the DDI 7 answers; with `/ddi8` it reports the driver's. They
differed in six fields:

| Field | Runtime at DDI 7 | First DDI 8 build | Now |
|---|---|---|---|
| TextureCaps | `00004005` | `00000005` | `00004005` (DX8's MIPMAP) |
| TextureFilterCaps | `0303033F` | `03030300` | `0303033F` |
| MaxPrimitiveCount | `FFFF` | `FFFFF` | `FFFF` |
| MaxStreams | 0 | 1 | 1, kept: stream 0 is sent to a DDI 8 driver |
| MaxStreamStride | 255 | 256 | 255 |
| MaxPixelShaderValue | 1.0 | 0 | 1.0 |

Without MIPMAP, 3DMark's first mipped texture was refused.

### The runtime's surfaces are announced with throwaway halves

DDI 7 kept the local half (LCL) CreateSurfaceEx was given and read it at
each use. At DDI 8 that is wrong for the surfaces the DirectX 8 runtime
makes:

- **Buffers, boot 113.** Every stream was refused: the kept LCL of handle 5
  read caps `00010000` (WRITEONLY alone), memory 0, size 0; handle `40`
  read caps `80`. Copying memory and size at CreateSurfaceEx, as vmhal9x
  does (`surfindex.c`), made the draws work on boot 114.
- **Textures, boot 114.** Then a fault: `C0000005` at `B0444C36`, which the
  HAL map and `wdis` of `d3d_hsw.obj` put at offset `A90` in
  `v9x_d3d_hsw_texture_unit`, reading `lpGbl+18h` through the texture's
  kept LCL.
- **Which halves outlive the call, boot 115** (`V9XD7-b115-held.LOG`:
  handle, global half at CreateSurfaceEx, the LCL's `lpGbl` now, that
  global half's memory now):
  - handles 1 to 3, the flip chain and its Z buffer: LCL still names its
    global half, memory live (`E5BF4000`);
  - 3DMark's textures and buffers: the LCL's `lpGbl` reads `00004025` or
    `00000052`, and the old global half holds no memory; handle `98`'s LCL
    names another surface's global half.

So both halves are temporary for the runtime's surfaces, and DirectDraw's
own (the flip chain) are not.

**What the driver does now** (`d3d_core.c`, "DDI 8's held surfaces"): for
a DDI 8 surface that is not a flip chain's or a Z buffer, CreateSurfaceEx
keeps a copy of every mip level's local and global halves, linked by
attachment nodes of its own, in the creating process's heap. It is
refreshed in place when the handle is announced again, and freed when the
handle is retired. Everything downstream reads the copy unchanged. The
flip chain is not copied because a flip swaps its memory between global
halves, which a copy would not follow.

## Not checked

- **The pictures.** Agent screenshots are black during full-screen flips
  and OBS is not connected to this machine. 3DMark completing with
  239 million triangles drawn says the draws reached the engine, not that
  they look right.
- **The score against DDI 7.** 1655 is recorded, not compared: the DDI 7
  figure (1120, boot 97) predates PAT write-combining, and the settings are
  this machine's saved ones.
- **A DDI 7 process asking GetDriverInfo2** after a DDI 8 one, and DDHELP
  resets during a DDI 8 program.
- **Copies dropped with their DirectDraw object** (DestroyDDLocal) are not
  freed until the process ends.
- **Driver writes into a copied global half** are lost when the handle is
  announced again. Nothing found in the tree writes there for textures.
- Half-Life Direct3D is DirectX 7 and stays at DDI 7; not rerun.
- The S3 floppy no longer fits the HAL; floppies are now built only on
  request (`f457dafb`).
