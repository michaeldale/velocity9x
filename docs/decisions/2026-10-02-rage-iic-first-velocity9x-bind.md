# Rage IIC: first Velocity9x bind on A8U4I5, tier-0, enable-ok at 1024x768x16

Date: 2026-10-02
Machine: A8U4I5, 10.0.1.172, ATI 3D Rage IIC AGP `1002:4757` rev `7A`,
4 MiB, Windows 98 SE. Boot 126.
Evidence: [`../probe/a8u4i5-rage-iic-registers-2026-10-02/`](../probe/a8u4i5-rage-iic-registers-2026-10-02/)
(`BOOT126-*.INI`, `display-class-*.reg`).

This is the first boot with the ati family's `rage-iic` entry bound (commit
`397da71`): tier-0, no engine claimed, Direct3D not advertised. The
reasoning is in [the register survey](2026-10-02-rage-iic-register-survey.md).

## Install

The package was downloaded into `C:\V9XDIAG` and installed through Display
Properties > Settings > Advanced > Adapter > Change > Have Disk, driven by
agent input. The stock class key was exported first. Under "Show compatible
devices" Have Disk listed "Velocity9x ATI 3D Rage IIC" alone. Windows warned
that the current driver might be a closer match, which was expected: the
community 2611 INF is dated the same day and lists revision-qualified ids.

The install left `WININIT.INI` with one rename, for `v9xsetp.dll`, which
Display Properties had loaded. There was no `NUL=` line. The agent then
issued a warm restart.

The generator does not write `PCIRebalance`, and the new class entry has
none, so the BARs stayed where the BIOS put them. ATI values from the
previous entry survive, because AddReg does not delete:
`EnumPropPages=atipdnad.dll`, three `ATIPagesX*` property-sheet handlers,
and the `ATI WDM Configurations` subkeys. They affect Display Properties
only. `DEFAULT` holds Velocity9x's `drv`, `minivdd` and `Mode`.

## Boot 126

| | Value |
|---|---|
| Stage | `enable-ok`, `DriverInitResult=ok` |
| Aperture | `m=2 bar=0 pci=1 b=dc000000`: VBE and PCI BAR0 agree |
| Surface | 1024x768x16, pitch 2048, 565 (`is555=0`) |
| VRAM | 4 MiB reported by VBE 4F00h and usable |
| VBE | 2.0; 40 modes listed, 40 queried, 28 cached; every mode's `PhysBasePtr` is `DC000000` |
| Modes published | 19: the seven baseline rows plus 12 the BIOS added (320x200 to 640x350, 1280x1024 at 8 and 16 bpp) |
| Hardware report | `Acceleration=none`, `Direct3D=not-advertised`, `Direct3DMode=none` |
| MTRR | slot 3 free, a write-combining range proposed for `DC000000` + 4 MiB (`r=0`) |
| Desktop | drawn correctly in the agent screenshot at 17:23 guest time |

The desktop came up at 1024x768x16, the mode the previous driver left in
the registry, not the INF's `8,640,480` default.

The seven VESA mode numbers the manifest declared are all present in this
BIOS (3.096) with linear framebuffers. That settles the open item from the
survey.

`V9XMODES.INI` names its build `b404e0c-dirty`. The package was built by the
gate run of the uncommitted bind change, whose content is commit `397da71`.

## GDI and mode switching, same boot

All runs used the package's own tools from `C:\V9XDIAG`, in this order.
The results are the `BOOT126-V9XGDI.INI` and `BOOT126-V9XMSW-*.INI` files.

| Run | Result |
|---|---|
| `V9XGDI /auto` at 1024x768x16 | PASS in 161 ms: black, white, red, blit and SetPixel read back as written |
| `V9XMSW /cycle:10` (640x480 and 800x600 at 16 bpp) | PASS, 10 of 10 |
| `V9XMSW /depth:10` (8 and 16 bpp at 1024x768) | PASS, 10 of 10 |
| `V9XMSW /set:1024x768x8` | PASS, `ChangeResult=0` |
| `V9XMSW /set:1024x768x16` | PASS, `ChangeResult=0` |

An agent screenshot afterwards (17:38) shows the desktop intact at
1024x768x16. Screenshots on this machine need `-TimeoutSeconds` above the
default: a 1024x768 capture is a 2.3 MB BMP over a link measured at
0.1-0.2 MB/s. Three default-timeout attempts failed at the transport,
while the agent kept answering `info`.

## Every published mode, and DirectDraw, boots 126-127

`V9XMSW /set:` to each of the 12 BIOS-added modes and to the four
baseline rows not yet exercised, each followed by `/set:1024x768x16`:
all 32 runs PASS with `ChangeResult=0` (`BOOT126-V9XMSW-SETS.TXT`). That
covers 1280x1024 at 8 and 16 bpp, and 640x350, 512x384, 400x300, 320x240
and 320x200 at both depths. All 19 published modes have now passed a live
GDI switch.

DirectDraw (`V9XDDP`):

| Run | Result |
|---|---|
| default probe | `COMPLETE`. 640x480x16 mode set, flip chain, fills, 20 flips, overlapping blits, pixels correct. **But no HAL**: see below |
| `/modestress` | 32 of 32 SetDisplayMode/RestoreDisplayMode round trips, 97 s |
| `/pal8` | 640x480, 640x400, 320x240 and 320x200 at 8 bpp: primary, palette, lock and readback all succeed |

The HAL is refused. `V9XDDH.INI` reads `setinfo-fail`, and DirectDraw runs
emulation only, with no heap. A video-memory surface fails with
`DDERR_OUTOFVIDEOMEMORY`. With `Direct3D=2` set for one boot (127) it
attaches, with a 2.5 MiB heap and the Direct3D HAL found. This is shared
code, not this chip; filed as
[2026-10-02 DirectDraw refuses the HAL without the D3D capability](../issues/2026-10-02-directdraw-hal-refused-without-d3d-capability.md).
`SYSTEM.INI` is restored to `Direct3D=0`, effective from the next boot.

## Not run

- DOS boxes: every agent route into one is a DOS program, which Michael
  ruled out on 2026-09-26 because it stops Windows rebooting itself.
- A cold boot: a remote power-off leaves the machine off until someone is
  at it.
- Whether the earlier screen overwrite during `V9XSTAGE`, under ATI's
  driver, has a counterpart under this one.
- Whether the leftover ATI property-page handlers misbehave when Display
  Properties opens.
