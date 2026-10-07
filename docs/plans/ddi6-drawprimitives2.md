# DDI 6 (DrawPrimitives2): Direct3D 8, per-application opt-in, the runtime's flush rate

Date: 2026-10-06. Status: in progress. Part A steps 1 and 2 done (D3D8
uses the HAL on both machines); step 3 done on the Rage XL (3DMark 2001 SE). Parts B and C done 2026-10-06. Parts B and C recorded
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

## Part A: Direct3D 8 on the HAL (steps 1 and 2 done)

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

Steps 1 and 2, 2026-10-06: the failing rule is `FVFCaps` zero (audit at
`0x40f6d0`, which cuts every format to display-mode only). Fixed by
reporting one texture coordinate set and converting flexible vertex
formats in DrawPrimitives2. D3D8's DxDiag cube draws on the Rage XL
(DirectX 9.0c) and DxDiag 8.0 passes on the netbook; see
`docs/probe/a8u4i5-rage-xl-pci-2026-10-06/README.md`, "Direct3D 8 on the
HAL".

## Part B: DDI 6 for the applications that need it (done)

Built 2026-10-06. `[Velocity9x] Direct3DDdi`: absent (the new default) is
per program, 5 is DDI 5 for every program, 6 is DDI 6 for every program.
Per program, the HAL decides when the runtime negotiates
(`v9x_d3d_dp2_for_process`, `src/display32/d3d/d3d_core.c`): a program
listed in `[Velocity9x.Direct3DDdi]` as `NAME.EXE=5` or `=6` gets that;
otherwise DDI 6 if it has loaded `D3D8.DLL`, DDI 5 if not. The 16-bit side
stamps `CAP_D3D_DP2` (may) and `CAP_D3D_DP2_ALL` (every program); V9XHW.INI
records `Direct3DDdi=5`, `6` or `A`. Settings page: a "DDI 6" selector
(Automatic, Never, Always) sharing the Mode switching row. The default is
per program because it cannot take anything away: a Direct3D 8 program had
no hardware device before, and DX5 to DX7 programs keep DDI 5.

Measured on A8U4I5 with no key set, alternating programs on one boot: the
D3D8 probe gets the HAL (`D3D_OK`, Callbacks3 served), Half-Life then runs
DDI 5 (`Dp2Calls` 0, 2.27M DX5 records, 17.96 and 17.98 fps), the probe
again gets the HAL. 3DMark 2001 SE (build 330) runs on the HAL with
software T&L. Netbook (boot 112, DirectX 8.0, 2026-10-07): the probe gets
the HAL, Half-Life runs DDI 5 (Dp2Calls 0) at 40.19 and 40.16 fps against
42.24 and 42.00 on the previous build; the 5 % is not explained (the
decision is read only while DirectDraw negotiates). It came back from the
restart once it was looked at; the hang was not diagnosed.

The original text:


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

## Part C: why the runtime flushes DrawPrimitives2 so often (done)

Answered 2026-10-06 in
`docs/decisions/2026-10-06-ddi6-runtime-call-rate.md`. The premise below
was wrong: the session totals are dominated by the console screen, a demo
frame is 130 to 210 calls, and the runtime ends a batch only where it does
not own the vertices (an indexed array over 16 vertices, or a vertex buffer
the application locks; Half-Life does the second). No driver cap tried
changes that. The regression was `Clear2` clearing Z on the CPU: with the
Z part on the engine, DDI 6 is within 2.5 % of DDI 5 on the Rage XL and
level on the netbook. The original text, kept for the record:


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

## Follow-ups after Parts A to C (2026-10-07)

- Points and lines in DrawPrimitives2 are drawn (one-pixel quads; see
  `docs/plans/d3d-line-raster-and-backface-cull.md`, "Update 2026-10-07").
- Texture stages past the first are not used: every engine publishes one
  blend stage, so a well-behaved application multipasses. Using a second
  stage needs the Gen3 multitexture path (the OpenGL ICD's) and the FVF
  converter to keep a second coordinate set; a project of its own.
- Hardware transform and lighting does not exist on any of these chips.
  Direct3D 9 stays unavailable because it needs a DDI 7 driver, not
  because of T&L; DDI 7 (the DX7 DrawPrimitives2 records and
  GetDriverState) would be the next step for it.
- The netbook took DirectX 9.0c (December 2005 redistributable); the
  agent restart hung and it came back on a power cycle (boot 113). 3DMark
  2001 SE then ran there too (probe README).

## What Direct3D 8 is offered (2026-10-07)

Measured with `tools/diag/d3d8_formats_win32.c` on both machines
(`docs/probe/a8u4i5-rage-xl-pci-2026-10-06/d3d8-formats-*.txt`):

- Display modes: 16-bit only. D3D8 lists no 32-bit mode at all, because
  only R5G6B5 carries the display-mode operation; a game that insists on
  32-bit has nothing to choose.
- HAL device: R5G6B5 fullscreen and windowed; X8R8G8B8 and A8R8G8B8 back
  buffers refused (D3DERR_NOTAVAILABLE).
- Textures: R5G6B5, A1R5G5B5, A4R4G4B4. Not X1R5G5B5, A8R8G8B8, X8R8G8B8,
  A8, L8, A8L8, P8 or DXT1/3/5.
- Render targets: R5G6B5 only. Depth: D16 (and D16_LOCKABLE on the Rage
  XL); no 24-bit Z, no stencil.

Why: both engines publish `dwDeviceRenderBitDepth` DDBD_16 and three
16-bit texture formats, and the core renders only to 5:6:5 and 1:5:5:5
targets. What 32-bit support would take, per engine, is not measured yet:
a 32-bit target and clear path in the core, the engines' destination
format (Gen3 COLOR_BUF ARGB8888, Mach64 DP_PIX_WIDTH 32 bpp), 32-bit
texture formats in each sampler, and a 32-bit desktop to test on. 24-bit
Z with stencil exists only on the GMA 950.
