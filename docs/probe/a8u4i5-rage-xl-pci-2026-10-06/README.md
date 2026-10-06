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
- Why D3D8 has no 3D-accelerated format for this HAL.
- One hard hang (Windows alive, Ctrl+Alt+Del dead) during a DxDiag run at
  DDI 6 before the two requirements were met, with `Clear2` served. Not
  reproduced with the breadcrumb log; cause unknown. The drain loop in
  `Clear2` has since been bounded.
- Points and lines in DrawPrimitives2 are parsed and not drawn.
