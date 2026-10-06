# DDI 6 (DrawPrimitives2): Direct3D 8, per-application opt-in, the runtime's flush rate

Date: 2026-10-06. Status: open. Part A in progress; Parts B and C recorded
at Michael's request to be done after it. Machines: A8U4I5 with the Rage XL
PCI (`10.0.1.172`), the netbook (GMA 950, `10.0.1.254`).

Where it stands (`docs/probe/a8u4i5-rage-xl-pci-2026-10-06/README.md`,
commits `a24aaba`, `a6cf5b9`, `7053c43`): DrawPrimitives2 works behind
`[Velocity9x] Direct3DDdi=6`, off by default. DxDiag reports DDI 6 and
draws its D3D7 cube through it. The D3D8 test runs and fails at
`GetDeviceCaps` with `0x8876086A` (GitHub issue 2's error). Half-Life is
correct but slower than at DDI 5 (netbook 27.2 vs 42.1 fps, Rage XL 12.6 vs
17.9), and the driver now does less work than at DDI 5: the gap is the
runtime making about three DrawPrimitives2 calls for each of the game's
DX5-style DrawPrimitive calls.

## Part A: Direct3D 8 on the HAL (in progress)

`d3d8.dll` refuses the HAL device in `GetDeviceCaps` unless the adapter's
format-operation list holds a format with `D3DFORMAT_OP_3DACCELERATION`.
For a pre-DX8 driver it builds that list itself (around `0x414520` in
4.09.0000.0904) from the legacy caps that DDRAW's `DdEntry2` hands back,
and grants 3D acceleration to the display format from
`dwDeviceRenderBitDepth` (`DDBD_16` here). The list is then audited and
discarded whole on any rule violation. Not established: which step drops
it for this driver. Steps:

1. Read what `DdEntry2` returns for this driver (the 0xC0-byte global data
   copy, the texture formats, the Z formats) and replay the builder's
   rules against them, rather than guessing.
2. Find the failing rule or early exit, fix it in the driver's caps, and
   confirm with `docs/probe/.../ddi6/d3d8probe.c` (`GetDeviceCaps` HAL
   returns `D3D_OK`), then the DxDiag D3D8 cube.
3. A real DX8 title on both machines.

## Part B: DDI 6 for the applications that need it

DDI 6 replaces the DX5 interface for every Direct3D program, which costs
DX5- and DX6-era games their speed. Make it selectable per application:
DDI 6 for DX8 programs (and any listed by name), DDI 5 for everything
else.

- Decide the mechanism. The 16-bit side reads `Direct3DDdi` per driver
  object, so a per-process answer needs the process: the HAL already
  records the client process (`v9x_d3d_note_client`, the diag identity
  process table). Options: a `[Velocity9x.Direct3DDdi]` section keyed by
  executable name; or detect a D3D8 client (d3d8.dll loaded) at driver
  object creation.
- Whatever is chosen, the DDI level must be decided before the runtime
  asks for `GUID_D3DCallbacks3`, and stay the same for that driver object.
- Settings page: expose it beside the Direct3D mode selector.

## Part C: why the runtime flushes DrawPrimitives2 so often

Half-Life arrives as ~1,000 DrawPrimitives2 calls a frame of 2-3 records
(netbook: 3.09M calls against the DX5 path's 0.94M for the same demo).
The cost is in the runtime, which turns the legacy `DrawPrimitive` calls
into DrawPrimitives2. Find from the runtime binary (the DX6/7 immediate-mode
runtime the legacy interfaces go through, `d3dim700.dll` on this install,
to be confirmed) what makes it flush per primitive: candidates are buffer
sizes the driver advertises, the swap flags (`dwFlags` 0xC seen: swap
vertex and command buffers offered, never taken), texture-handle changes,
or a legacy-interface path that flushes by design. Then change what the
driver can change and re-measure Half-Life on both machines.

Measurement for all three parts: Half-Life `timedemo mwd5`, 640x480 D3D,
DDI 5 and DDI 6 on the same build and boot; the Gen3 timing buckets on
the netbook.
