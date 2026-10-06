# A8U4I5: Rage XL PCI back on the ati build, 2026-10-06

- Machine: A8U4I5 (`10.0.1.172:9869`), card `PCI\VEN_1002&DEV_4752&SUBSYS_80081002&REV_27`,
  8 MB, in PCI slot DEV_09. Bound to `Display\0007` ("Velocity9x ATI Rage XL PCI"), the
  key from [2026-10-03](../a8u4i5-rage-xl-pci-2026-10-03/README.md).
- Purpose: a Mach64 stand-in for GitHub issue 2 (Rage Mobility-P in a Compaq Armada E500:
  DX8 "3D not available", 3DMark 99 black-screen hang, UT99 polygon dropouts and an OpenGL
  hang, a brief screen darkening on the caps query).
- SiS 6326 work is parked. Its last installed binaries are kept in `C:\V9XSIS` on the guest.

## What was wrong after the card swap (boot 297)

The card was swapped back while `C:\WINDOWS\SYSTEM` still held the SiS build of 5 Oct
(`V9XHAL.DLL` 325,120 bytes). Two separate faults:

1. Wrong family binaries. `V9XBOOT.INI` said `Stage=fail-hardware-present` and `V9XHW.INI`
   still said `Adapter=SiS 6326`. Fixed by installing the ati package.
2. A 4 bpp hardware profile. After installing the ati files, boot 298 came up on `vga.drv`
   with no `V9XBOOT.INI` written at all. `HKLM\Config\0001\Display\Settings` held
   `BitsPerPixel=4`, and every Velocity9x class key maps `MODES\4\640,480` to `vga.drv`, so
   our DRV was never loaded. The device node had no problem code and the DRV loaded fine
   under `V9XSTAGE.EXE` (`Stage=query-ok`). The 4 bpp value was most likely written by
   Windows when the SiS build failed on the Rage XL. Setting `BitsPerPixel=16`,
   `Resolution=800,600` fixed it.

Lesson: after a card swap that left a failed boot, check the hardware profile's
`BitsPerPixel` before suspecting the driver.

## Install

Package `build\win98se-ati`, build `526c508`, built with
`build-active-package.ps1 -Family ati`. Uploaded to `C:\V9XATI`, then one plain WININIT
rename of five files (no `NUL=` lines, per the A8U4I5 rules in
[the Rage IIC plan](../../plans/ati-rage-iic-hardware-3d.md)) and a warm restart. All five
dated 16:29 after boot 298:

| File | Bytes | SHA256 |
|------|------:|--------|
| V9XDISP.DRV | 51,022 | `F50CEA26483B0A7457BD1DBBC34253B3408DB4BBBBA5DC9B93C1A7D9BE85A362` |
| V9XMINI.VXD | 12,332 | `373699B3AB0A9DBD28D63C140F5BF5BD006766B55CC7E8F211F5C4E6A9F7BE89` |
| V9XHAL.DLL | 337,408 | `15FB52A2B8EDE2293A8733849DFA74C147FBD1092710E483CD7754E4347A418C` |
| V9XSETP.DLL | 50,688 | `3204D7AF5ECFAAE2932F77318060F31D2046148CB0FDEDE57E90306C0C8DC8F6` |
| V9XGL.DLL | 413,696 | `B3868F98C08DC4BF04D7DD2D16BBA970D50FD4FB3EE7245A10D3058929495F50` |

Hashes are the package's `SHA256.TXT`; the guest copies were matched by size and date,
not re-hashed.

`update-associated-driver.ps1` was not used: it writes `NUL=` lines and does not install
`V9XGL.DLL`.

## Boot 299 result

- `V9XBOOT.INI`: `Stage=enable-ok`, 800x600x16, `vram=8323072`, aperture `dc000000`.
- `V9XHW.INI`: `Adapter=ATI 3D Rage XL PCI`, `Direct3D=hardware-mach64`,
  `Acceleration=directdraw-fill`.
- `V9XMODES.INI`: build `526c508`, family `ati`, 19 rows published.
- Desktop screenshot at 16 bpp.

`SYSTEM.INI` `[Velocity9x]` carries `Direct3D=0`. That is `V9X_D3D_REQUEST_HARDWARE`
(`include\velocity9x\d3dmode.h`: 0 hardware, 1 disabled, 2 software, 3 hybrid, 4
offload), the same as an absent key, and `V9XHW.INI` reports `Direct3DMode=hardware`. It
does not gate anything.

## Issue 2: DxDiag (boot 299, DirectX 9.0c, `dxdiag/`)

- Display tab: "Velocity9x ATI Rage XL PCI", 8.0 MB, driver 0.11, **DDI Version 5**,
  DirectDraw and Direct3D acceleration Enabled. Notes: "Hardware accelerated Direct3D 8+
  is not available because the display driver does not support it."
  (`dxdiag-display-tab.png`, `DXI2.TXT`).
- D3D7 test: the textured spinning cube renders correctly, fullscreen 640x480x16
  (`d3d7-cube.png`). One of four agent screenshots during the run was entirely black
  (`d3d7-black-frame.png`). The trace shows `FlipHandled=418`, `FlipDeclined=0`, so the
  test page-flipped; a GDI screenshot of a flipping primary can catch the buffer just
  flipped away, so this frame is **not** proof of the flicker the reporter saw. Unresolved
  without eyes on the monitor.
- D3D8 and D3D9 tests: skipped by DxDiag 9.0c, "the display driver does not support it".
  The reporter's DX8-era DxDiag ran it and failed at GetDeviceCaps with `0x8876086A`
  (`D3DERR_NOTAVAILABLE`). Same cause: the D3D8 runtime does not drive a DDI 5 (DX5-level)
  HAL. Reproduced; it is a missing feature, not a Mobility-P fault.
- The "screen goes dark on a caps query" symptom was not seen on DxDiag start or on the
  Display tab (CRT on a VGA-to-HDMI adapter, so a panel-only effect would not show here).
- `V9XSNA7.INI` after DxDiag: one DXDIAG.EXE context, 421 `RenderPrimitive` calls, 1 texture, engine
  type 4 (Mach64), zero FIFO/idle timeouts and resets, `D3dDepthOffered=0`.

## Issue 2: 3DMark 99 Max (boot 299, defaults: 800x600x16, Z16, triple buffer, all tests)

- Ran through 9 of 26 tests without a hang (Game 1 race at about 9 fps, then the texture
  rendering tests). Michael stopped it there to look at the monitor, so there is no score
  and no full-session trace.
- The monitor (CRT output through a VGA-to-HDMI adapter into a USB capture card, OBS at
  1024x768) showed a stretched corner of the picture: about the left 220 pixels and 400
  lines of the 800x600 frame, scaled up. The agent's framebuffer screenshots at the same
  time were correct full 800x600 frames. The desktop at 800x600x16 was already wrong on
  the monitor before 3DMark started.
- Cause: the VGA-to-HDMI adapter's sync, not the driver. After 3DMark was closed the
  picture came back correct with no driver change. Supporting evidence: mode switching on
  this card is `vbe-lfb` (the ATI BIOS sets the timings), and `m64_scanout.c` rewrites
  only the start-address field of `CRTC_OFF_PITCH`, keeping the BIOS pitch, so nothing of
  ours can scale the picture.
- Consequence for testing: a "broken screen" seen through this capture chain needs a
  framebuffer screenshot beside it before it counts as a driver fault.

## Not established

- Whether 3DMark 99 completes at 800x600 on this build (it did at 640x480 on 3 Oct).
- Whether the reporter's 3DMark hang reproduces on any Mach64 here; it did not in the
  first 9 tests.
- The D3D7 cube flicker: needs eyes on the screen, not an agent screenshot.

## DDI 6 (DrawPrimitives2) bring-up, same evening (`ddi6/`)

Built behind `[Velocity9x] Direct3DDdi=6` (off by default): the DrawPrimitives2
walker (`src\display32\d3d\d3d_dp2.c`, host-tested), the core glue, stage-0
texture states re-expressed as the DX5 fields, the DX7-length extended caps,
`GUID_ZPixelFormats`, `Clear2` and `ValidateTextureStageState`. The probe
bits (`Direct3DDdiProbe`, `include\velocity9x\engine_abi.h`) and the
breadcrumb log `C:\V9XDIAG\V9XDP2.LOG` are temporary instruments.

What the runtime requires, read out of Windows 98's DDRAW.DLL
(4.09.0000.0904) because four builds measured "DDraw Status: Not Available"
and the documentation did not explain it. After `GUID_D3DCallbacks3`, when
the global data carries `D3DDEVCAPS_DRAWPRIMITIVES2`:

- the table must pass a flag/pointer check (every non-null slot needs its
  `dwFlags` bit and must pass `IsBadCodePtr`);
- `DrawPrimitives2` **and `ValidateTextureStageState`** must be non-null;
- the driver must then accept `GUID_D3DParseUnknownCommandCallback`
  (`2e04ffa0-...`) with DDHAL_DRIVER_HANDLED and DD_OK.

Any failure skips the rest of driver setup, so DirectDraw runs without its
HAL. Hypotheses killed on the way, each by a build on the card: the
execute-buffer callback table, nulling RenderState/RenderPrimitive, and
`Clear2` on its own.

With both requirements met: **DDI Version 6, DDraw and D3D Enabled**. The
DxDiag D3D7 cube draws through DrawPrimitives2 (29 DP2 and 27 Clear2 calls,
all DD_OK, `d3d7-cube-dp2.png`). The D3D8 test now runs instead of being
skipped and fails exactly as the issue 2 reporter saw: step 5
`GetDeviceCaps` `0x8876086A` (`d3d8-getdevicecaps.png`).

`d3d8probe.c` (`D3D8PRB.TXT`): display mode and all nine modes are R5G6B5;
`CheckDeviceType` and `GetDeviceCaps` for HAL return `0x8876086A`; REF works.
From d3d8.dll: `GetDeviceCaps` needs a format whose op flags carry
`D3DFORMAT_OP_3DACCELERATION`; the legacy builder grants it from
`dwDeviceRenderBitDepth` (`DDBD_16`, which the Mach64 publishes), and an
audit throws the whole list away on a rule violation. Which step drops it
for this driver is not established; `SoftwareOnly=0` in the registry was
checked and is not it.

Open:
- Why D3D8 has no 3D-accelerated format for this HAL. Answered below: the
  audit refuses `FVFCaps` zero.
- One hard hang (Windows alive, Ctrl+Alt+Del dead) during a DxDiag run at
  DDI 6 before the two requirements were met, with `Clear2` served. Not
  reproduced with the breadcrumb log; cause unknown. The drain loop in
  `Clear2` has since been bounded.
- Points and lines in DrawPrimitives2 are parsed and not drawn.

## DDI 6 regression check: Half-Life on the Rage XL and the netbook (`ddi6-halflife/`)

Build `a24aaba` on both. Half-Life 1.1.1.0 `timedemo mwd5`, `-d3d -w 640 -h
480 -full`, three runs each, DDI 5 and DDI 6 on the same build and boot
(the setting is read per DirectDraw program), Half-Life restarted between.

| Machine | DDI 5 (runs) | DDI 6 (runs) | Change |
|---|---|---|---|
| A8U4I5, Rage XL PCI | 17.16, 17.97, 17.96 | 10.60, 12.27, 12.27 | -32 % |
| Netbook, GMA 950 (boot 103) | 35.31, 42.09, 42.02 | 21.46, 23.71, 23.59 | -44 % |

- Pictures are correct at DDI 6 on both (`*-ddi6-frame.png`); no refusals,
  unparsed or malformed records, FVF or buffer refusals.
- The runtime sends Half-Life as about 1,000 DrawPrimitives2 calls a frame,
  each with 2 to 3 records (inline fans and INDEXEDTRIANGLELIST2) and about
  5 triangles; opcodes seen `0x36800100` (render state, fan_imm, stage
  state, indexed list2, viewport, W info). Each call ends in its own engine
  batch, so nothing is merged across calls.
- The cost is per call, not per triangle. On the netbook an extra ~7 s a
  run over ~400,000 calls is ~18 us a call; what share is the runtime's
  translation and what is the HAL's (three pointer probes, FPU save, the
  per-call flush and submission) is not measured: the timing counters were
  off.

Conclusion: DDI 6 stays off by default. It is correct but a large
regression for DX5-era games until the per-call cost comes down.

## DDI 6 performance work, same evening (`ddi6-perf/`)

Half-Life `timedemo mwd5`, 640x480 D3D, best of runs 2 and 3. Netbook cycle
figures are the Gen3 timing buckets over the whole Half-Life session.

| Step | Netbook DDI 6 | Rage XL DDI 6 | What the counters said |
|---|---|---|---|
| DDI 5 reference | 42.1 | 17.9 | netbook HAL 66.1 G cycles, 921k engine batches |
| DDI 6 as first built | 23.7 | 12.3 | a batch per DrawPrimitives2 call |
| + run kept across calls | (not run) | 12.3 | still a flush per call: per-state settle |
| + guard band for every engine | 19.6 | 12.2 | Gen3 refused ~298k batches (vertex range), replayed |
| + pointer-probe cache | 21.9 | - | HAL overhead 81k -> 15k cycles a call |
| + settle once per group | 22.2 | - | settle flushes 2.1M -> 114k |
| + guard band only where the core clips | **27.2** | **12.6** | netbook 238k batches, HAL 52.5 G cycles; XL 172k batches |

Where it ends: on both machines the driver now does less work than at
DDI 5 (fewer engine batches, and on the netbook fewer HAL cycles: 52.5 G
against 66.1 G), and Half-Life is still slower, because the runtime makes
about three times as many calls to translate the game's DX5-style
DrawPrimitive calls into DrawPrimitives2: 3.09M DrawPrimitives2 calls on
the netbook against 0.94M DX5 calls. That cost is in the runtime, not
measured here, and not reachable from the driver as built.

Kept from this work: the run held across calls with flushes at every entry
point that touches the engine, a surface or a texture; the state snapshot
compared once per primitive; the probe cache, dropped on every surface or
context destruction; the guard band only for engines with `clip_in_core`.
DDI 6 stays off by default.

## Direct3D 8 on the HAL: the FVFCaps rule (`ddi6-d3d8/`)

Why d3d8.dll 4.09.0000.0904 had no 3D-accelerated format, read out of the
DLL and then measured. The probe (`ddi6/d3d8probe.c`, extended to dump the
runtime's adapter block) showed the format list built (7 entries) with every
operation mask zero except the display format's `0x400` (display mode
only). The legacy-caps audit at `0x40f6d0` requires, among others, a
non-zero texture caps, a non-zero `FVFCaps`, and for a driver with no
streams (`MaxStreams` 0) the DX7 defaults for point size, shader versions
and vertex index; on any failure `0x40f910` cuts every format down to
`0x400`. Each rule was checked against the dump: the only one this driver
broke was `FVFCaps` (`+0x8c`) zero, which the DX6 extended caps reported
because DrawPrimitives2 only took D3DTLVERTEX. `MaxPointSize` 0 passes (the
constant it is compared with at `0x407bac` is 0.0).

Fix (one build, `V9X_DD_SHARED_ABI` 2026100605): `dwFVFCaps` 1 (one texture
coordinate set), and DrawPrimitives2 accepts any XYZRHW flexible vertex
format, converting each vertex into D3DTLVERTEX (`v9x_dp2_fvf_layout` and
`v9x_dp2_fvf_convert` in `d3d_dp2.c`, host-tested). Missing diffuse is
white, missing specular black with no fog, missing coordinates zero.
D3DTLVERTEX itself is not copied. Counter `Dp2ConvertedCalls`.

Measured at DDI 6:

| Machine | Runtime | Result |
|---|---|---|
| A8U4I5, Rage XL (boot 311) | DirectX 9.0c | `CheckDeviceType` and `GetDeviceCaps` HAL `D3D_OK`, R5G6B5 ops `0xc00` (3D acceleration). DxDiag's D3D8 test draws the textured cube correctly (`rage-xl-dxdiag-d3d8-cube.png`): 62 DrawPrimitives2 calls, 360 triangles, FVF `0x144` (XYZRHW, diffuse, one set), all converted; nothing refused, unparsed or malformed. D3D9 is skipped (`rage-xl-dxdiag-d3d9-skipped.png`), as expected below DDI 7 |
| Netbook, GMA 950 (boot 110) | DirectX 8.0 | `Direct3DCreate8` takes only SDK version 120 there; HAL `D3D_OK`. DxDiag 4.08's tests: "All tests were successful" (`netbook-dxdiag80-all-passed.png`), the hardware cube correct (`netbook-dxdiag80-hal-cube.png`): 200 calls, 2,400 triangles, FVF `0xc4` (no coordinates), all converted |

The D3D7 cube in the same DxDiag run now also arrives as FVF `0x144` and is
converted: with FVFCaps non-zero the runtime stops expanding to D3DTLVERTEX
itself.

Regression: Half-Life at DDI 6 on the Rage XL, 12.57 and 12.58 fps against
12.60 and 12.61 for the previous build (`rage-xl-halflife-ddi6.png`). It
sends D3DTLVERTEX, so none of its 1.06M calls was converted. The netbook's
Half-Life was not rerun.

Not established:
- Whether the netbook's DirectX 8.0 refused the HAL before this build; it
  was not probed at DDI 6 on the old one.
- A real DX8 title on either machine (plan Part A step 3).
- Both machines are back at DDI 5 (`Direct3DDdi` unset).

## DDI 6 call rate and the Clear2 regression (`ddi6-runtime-flush/`)

The full account, with the hypotheses the evidence killed, is
`docs/decisions/2026-10-06-ddi6-runtime-call-rate.md`. In short:

- A per-call capture of DrawPrimitives2 (probe bit 32,
  `tools/diag/dp2ring.py`) showed the "three calls per draw" was the console
  screen between demos; a demo frame is 130 to 210 calls. On the Rage XL a
  demo frame was 81 M cycles, ~25.5 M of it one Z-only `Clear2` done by the
  CPU across PCI (`xl-demo-capture-before-clear2-fix.txt`).
- `Clear2` now clears Z through the `DDBLT_DEPTHFILL` path on the engine.
  Same build and boot, `timedemo mwd5` runs 2 and 3: Rage XL DDI 5 17.99 and
  17.98, DDI 6 17.05 and 17.53 (was 12.57 and 12.58); netbook DDI 5 42.24
  and 42.00, DDI 6 40.82 and 42.25 (was 27.2). Pictures correct
  (`xl-halflife-ddi6-frame.png`, `netbook-halflife-ddi6-frame.png`).
- The reproducer (`tools/diag/dp2_repro_win32.c`, `repro-*`) shows the
  runtime batches across texture and state changes and ends a batch only
  for an indexed array over 16 vertices (user memory, `dwFlags 0x9`) or a
  vertex buffer locked between draws (`dwFlags 0x8`, Half-Life's case).
  Withholding `D3DDEVCAPS_TLVERTEXSYSTEMMEMORY` (probe bit 64) changed
  nothing.
- Both machines left at DDI 5.

## Part B and the first Direct3D 8 title (3DMark 2001 SE)

Installed from the retro-web share (10.0.0.7:8081, files 2, 7 and 3):
3DMark2000 1.1 on both machines, 3DMark 2001 SE plus its build 330 patch on
A8U4I5. The InstallShield self-extractors refused to unpack when started by
the agent ("The contents of this file cannot be unpacked", the archive tests
clean with 7-Zip), so each was extracted on the host and its `Setup.exe` run
from `C:\BENCH\`. The netbook cannot take 3DMark 2001 SE: it has
DirectX 8.0 and the installer requires 8.1.

Per-program DDI 6 (plan Part B), build after `6ff23ee`, no `Direct3DDdi`
key: see the plan for the alternation test. 3DMark 2001 SE, 640x480x16,
16-bit textures and Z, double buffered, D3D software T&L, default tests:

| Test | Result |
|---|---|
| Game 1 Car Chase, low / high detail | 1.5 / 0.8 fps |
| Game 2 Dragothic, low / high | 1.9 / 0.9 fps |
| Game 3 Lobby, low / high | 2.3 / 1.0 fps |
| Game 4 Nature, EMBM, DOT3, Pixel Shader | not supported by hardware |
| Fill rate single / multi texturing | 17.0 / 15.6 MTexels/s |
| High Polygon Count 1 and 8 lights | N/A |
| Vertex Shader | 1.3 fps |
| Advanced Pixel Shader, Point Sprites | N/A |

Car Chase renders correctly (`3dmark2001-car-chase-1024.png`, from the
first attempt). The run ended with "Could not create texture -
D3DERR_INVALIDCALL" at the Point Sprites test, so there is no total score.
At 1024x768 it stopped earlier: `CreateTexture (for a rendertarget)`
`D3DERR_OUTOFVIDEOMEMORY` on the 8 MB card.

Open from this:
- Why the game tests run at 1 to 2 fps when Half-Life runs at 18: not
  measured. Candidates are the runtime's batch ending (vertex buffers
  locked per draw, decision `2026-10-06-ddi6-runtime-call-rate.md`), the
  flexible-vertex conversion, and the Mach64 engine itself.
- The texture `CreateTexture` refused with INVALIDCALL (Point Sprites), and
  why High Polygon Count reports N/A.
- The netbook did not come back from the restart onto this build (boot 111
  was the last seen; no ping after 13 minutes). Not known whether it hung
  shutting down or booting.
