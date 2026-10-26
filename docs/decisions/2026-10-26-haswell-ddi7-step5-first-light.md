# HD 4600: DDI 7 without T&L - what the DX7 runtime asks of the driver

Date: 2026-10-26. Machine: the i5-4570 (W9QI5A83, HD 4600), boots 89 to
92. The runtime is DirectX 9.0c (4.09.00.0904). Step 5 of
[`../plans/hardware-tnl.md`](../plans/hardware-tnl.md), first part.
Evidence:
[`../probe/hsw-4570-ddi7-step5-2026-10-26/`](../probe/hsw-4570-ddi7-step5-2026-10-26/).

## What was built

- `[Velocity9x.Direct3DDdi] <program>=7` gives a named program DDI 7, on
  an engine that runs T&L (Haswell); elsewhere it gives DDI 6. DDI 7 is:
  - `D3DDEVCAPS_DRAWPRIMITIVES2EX`;
  - `GUID_Miscellaneous2Callbacks` (CreateSurfaceEx, GetDriverState,
    DestroyDDLocal);
  - the DX7 records (33-44) walked instead of handed to the runtime's
    parser.
- A CreateSurfaceEx handle table (`d3d_handles.c`, host-tested), keyed by
  DirectDraw local object and handle.
- TEXTUREMAP, TEXTUREHANDLE, SETRENDERTARGET and TEXBLT resolve surfaces
  through the table. A handle used as a texture becomes a DX6-style
  texture record, so everything after it is the DDI 6 code unchanged.
- CLEAR goes through Clear2. TEXBLT is a CPU copy of the mip chain.
- Material, light, transform and clip-plane records are consumed and
  unused. STATESET goes to the runtime's parser.
- The HAL logs what the runtime does to `C:\V9XDIAG\V9XD7.LOG`.
- V9XD32 gains `/ddi7` and T16, a DirectX 7 device. T16 has its own
  `DirectDrawCreateEx` object and an off-screen target, and checks:
  - the device caps;
  - a device Clear;
  - a managed texture;
  - an untransformed XYZ quad the runtime transforms;
  - a state block.

## What the guest showed

- **`V9X_DD_SURFACE_MORE_HANDLE` (64h) is right.**
  - The handles read there are 1, 2, 3 and upward per local object.
  - Execute and vertex buffers (caps 00800800h) get handles too.
- **ContextCreate passes the local object and the surfaces' LCLs at
  DDI 7.**
  - Its first argument equals CreateSurfaceEx's `lpDDLcl`.
  - The DX5 to DX6 tests T1-T11 pass with the target and Z read as LCLs.
- **DirectDraw asks for the Miscellaneous2 callbacks before
  GUID_D3DCallbacks3** while it makes a driver object (boot 89).
  - The level used to latch at Callbacks3 alone, so the first object was
    refused Misc2 under the previous process's level.
  - That object never got CreateSurfaceEx, and its textures resolved to
    nothing: T6, T12 and T13's two-unit form failed.
  - The Misc2 answer now latches the level too.
- **A flip chain announces only its primary.** The back buffer (handle 2)
  and the Z buffer are reached through the primary's attachments, so the
  table is filled by walking them.
- **`V9X_DD_ATTACH_NODE` had its first two fields wrong.**
  - The header named them next and prev. In `ddrawi.h`'s ATTACHLIST they
    are dwFlags and lpLink.
  - The engines' mip walks stop at the first node, so they never followed
    the bad field.
  - The flip-chain walk did, and faulted on DDAL_IMPLICIT (1) on boot 90.
  - The struct now has the SDK's layout, keeping the name `next` on the
    link, so the engine code is unchanged.
- **A DX5 program's TEXTUREHANDLE also arrives as a CreateSurfaceEx
  handle** at DDI 7: T6 drew white from handle 8 on boot 91. It is now
  translated as TEXTUREMAP is.
- **Records sent by this runtime:** in every run only SETRENDERTARGET and
  CLEAR. Never TEXBLT, even for T16's managed texture; never STATESET,
  though T16 makes and applies a state block; never a transform, light or
  material.
  - With no `HWTRANSFORMANDLIGHT`, the runtime keeps all of these itself.
  - TEXBLT and the STATESET path are therefore unexercised.
- **`IDirect3D7` needs `DirectDrawCreateEx`.** Queried from a DirectDraw 1
  object it answers E_NOINTERFACE (boot 89).

## Results, V9XD32 boot 92

| Run | Result |
|---|---|
| `/ddi6` (boot 89, control) | PASS, T16 included: DevCaps 00082651, no EX bit |
| `/ddi7` | PASS: T1-T16 all pass, DevCaps 0008A651, DrawPrimitives2Ex 1 |

The first `/ddi7` run after each reset on boots 91 and 92 came back empty
or cut short after a transport error from the agent. The second run on
each boot completed. Read as the agent's network, not the driver; not
proven.

## Not done, not checked

- **Not done:** the step 5 gate's other parts.
  - The DX7 SDK samples on the plain HAL device.
  - Whether DirectX 9's own HAL device appears at DDI 7.
  - One DX7 title.
- **Not done:** DDI 7 by default on Haswell (decision 1) waits for those.
- **Not checked:** TEXBLT, GetDriverState and STATESET on any title.
- **Not checked:** a second DDI 7 process alongside a first, and DDHELP's
  resets.
