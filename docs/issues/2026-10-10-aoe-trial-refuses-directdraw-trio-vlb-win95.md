# Age of Empires refuses DirectDraw on a Trio VLB under Windows 95

Date: 2026-10-10. Status: open (reproduced; cause is Windows 95's
DirectDraw refusing an 8-bpp mode from a 16-bpp desktop, which Microsoft's
s3.drv does too on 486VLB; why the reporter's stock driver runs AoE is
not known).

Source: VOGONS, AncapDude, reply 71 of the Velocity9x thread
(<https://www.vogons.org/viewtopic.php?p=1449658#p1449658>). Reporter's
machine: miro Crystal S3 Trio32 VLB, Windows 95B OSR 2.1, Velocity9x 0.15
S3 package. The desktop works; Age of Empires does not start and says the
card does not support DirectDraw. The reporter's stock driver runs it.

## Reproduced on 486VLB

Machine: 486VLB (10.0.1.217), Diamond Stealth 64 DRAM (Trio64 `5333:8811`,
2 MiB) on VLB, Windows 95 retail 4.00.950, desktop 640x480x16. Driver set
from afb538c (s3 family, 0.15.1), installed by WININIT rename, all four
files size-checked against the package. DirectX 7.0a installed this
session (boot 19); the guest had no working DirectDraw before that.

Age of Empires Trial (1997-10-03 build, `EMPIRES.EXE` 1,607,680 bytes),
files laid out by hand because its `SETUP.EXE` exits without copying when
run from the unpacked cabinet. Result at boot 19:

> Could not initialize graphics system. Make sure that your video card and
> driver are compatible with DirectDraw.

## What the driver saw

`V9XDDP.EXE` run first, same boot: `V9XDDH.INI` `Stage=setinfo-ok`;
`V9XDD.INI` `GblNoHardware=0`, `GblHalCaps=0x04000440`
(GDI|BLT|BLTCOLORFILL), `GblHalVidMemTotal=0x0016A000`, 15 modes listed
by DirectDraw including 640x480x8 (`GblMode0`). `SetExclusiveMode` was
called twice by the probe. So DirectDraw accepts the HAL on Windows 95
with DirectX 7, and the "no DirectDraw" message is not a refused SetInfo.

`V9XTRACE.EXE` after the AoE failure (`V9XSNAP.INI`, `BuildsAgree=1`),
the ring from AoE's start:

    Dd16DestroyDriver / Dd16Get32BitName / DriverInit exit 1
    Dd16NewCallbackFns / Dd16CreateObject exit 1
    CanCreateSurface enter 0x00004200, exit 0
    CreateSurface enter 1, exit 0
    FlipToGDISurface
    Dd16DestroyDriver ...

No `SetExclusiveMode`, no mode change: AoE gives up after the driver
object and one primary surface exist, before it takes exclusive mode or
asks for 640x480x8. Something it checks between `DirectDrawCreate` and
`SetCooperativeLevel` fails. Not yet identified.

## What AoE asks for

`EMPIRES.EXE` (capstone over the trial build). String 2003 is printed
by a switch on a startup error code (jump table at `0x418200`; codes 7,
8, 11, 13 and 17 all select it). The graphics init at `0x43b0f0` runs
`DirectDrawCreate`, `EnumDisplayModes` (callback `0x43aec0` records which
of 640x480, 800x600, 1024x768, 1280x1024 exist at 8 and 16 bpp), then
full screen `SetCooperativeLevel(EXCLUSIVE|FULLSCREEN)` and
`SetDisplayMode(w, h, 8)`; any failure sets the error and returns. The
game only runs at 8 bpp.

## The 8-bpp mode is refused by DirectDraw on Windows 95

`V9XDDP.EXE /pal8`, same boot, desktop 640x480x16:

    CoopExclusiveHr=0x00000000
    EnumModeCount=5: 640x480x16, 800x600x16, 1024x768x16,
                     320x200x8, 320x240x8
    Pal8_640_480_SetModeHr=0x80004005 (and 640x400, 320x240, 320x200)

The HAL hands DirectDraw 15 modes at 8, 16 and 32 bpp (`GblNumModes`,
`GblMode0`=640x480x8), and on Windows 98 the same driver changes depth
live (`ModeSwitching=live-any-depth`). Windows 95's DirectDraw lists and
sets only modes at the desktop's depth, plus its own Mode X modes. So AoE
cannot get 640x480x8 from a 16-bpp desktop, which matches the reporter's
symptom; the driver trace shows no mode switch reaching the driver
(`EnableCount=1`, `DisableCount=0`).

## The stock driver does the same on 486VLB

Boot 21: the display key pointed at Windows 95's own `s3.drv` /
`s3.vxd` (values from `MSDISP.INF` `[S3.AddReg]`; Velocity9x confirmed
not loaded - no new `V9XBOOT.INI`), desktop 640x480x16, same DirectX 7:

    V9XDDP /pal8: CoopExclusiveHr=0x00000000
                  EnumModeCount=2 (320x200x8, 320x240x8 only)
                  Pal8_640_480_SetModeHr=0x80004005 (all four)
    AoE:          the same "Could not initialize graphics system"

So on retail 4.00.950 with DirectX 7, the stock driver refuses the
8-bpp switch and AoE exactly as Velocity9x does; this is Windows 95's
DirectDraw, not something our driver omits. (The `Gbl*` keys in that
report matched the Velocity9x run value for value and are presumably
stale keys left in the INI, not a reading of `s3.drv`.)

The reporter's "stock driver runs it" is therefore unexplained. Untested
candidates: their desktop was already at 8 bpp under the stock driver;
OSR2 (4.00.950B) or their DirectX version differs here; or miro's own
driver rather than Microsoft's `s3.drv`.

Velocity9x was restored for boot 22 (`enable-ok`, 640x480x16). Getting
there cost a blue screen: `V9XMSW /set:640x480x16` was run while the
registry already named `s3.drv`, so `ChangeDisplaySettings` presumably
tried to load a different display driver live; the screen text was not
recorded. Two Windows 95 facts from the swap: a `REGEDIT /S` import was
lost across the agent's forced reboot (`EWX_FORCE`) unless something
flushed the registry afterwards, and Windows 95's REGEDIT cannot delete a
key or value (`[-key]` is ignored) - a stray `minivdd` had to be removed
with `RegDeleteValue` + `RegFlushKey`.

`ExtModeSwitch` is not the gate. Windows 95's `MSDISP.INF` sets it to 0
only for the Chips and XGA drivers and omits it elsewhere, while our INF
writes 0 for every family; with it set to 1 on 486VLB, both live and
after a reboot (boot 23), `V9XDDP /pal8` was unchanged - five modes, the
three 16-bpp ones plus Mode X, and `0x80004005` for every 8-bpp
`SetDisplayMode`. Set back to 0.

Not yet established:

- What the reporter's setup does differently (above).
- Whether the menu is drawn correctly at 8 bpp. With the desktop at
  640x480x8 (boot 20; `V9XMSW /set:640x480x8` got `ChangeResult=1`,
  DISP_CHANGE_RESTART, so Windows 95 itself will not change depth live,
  and the reboot applied it), AoE starts, switches to 800x600x8 and
  reaches its main menu: no DirectDraw error. The agent's capture of that
  screen is almost all black with "Single Player" legible; whether that
  is the capture missing AoE's palette or the screen itself was not
  checked at the monitor. An 8-bpp desktop is a workaround for the
  reporter either way.
- Whether OSR2 (the reporter's 4.00.950B) behaves as retail 950 does here.
- Which DirectX the reporter has.
