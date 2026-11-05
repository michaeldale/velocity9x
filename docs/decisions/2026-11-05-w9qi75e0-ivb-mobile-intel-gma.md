# intel-gma on the HD 4000 mobile: the LVDS is on pipe A, flips and Direct3D pass

Date: 2026-11-05
Status: **measured once, one boot.** The `0166` alias of `hd4000-ivb`
(`aa70a3a9`) ran on W9QI75E0 with no code beyond the claim: the engine
opened, flips through plane A were all taken, and `V9XD32` passed at 16
and 32 bpp, with 16 and 24-bit Z. What the panel shows has not been seen.

Steps 2 to 4 of `docs\plans\intel-ivybridge-mobile-0166.md`. Tier 0 is
in `2026-11-05-w9qi75e0-ivb-mobile-tier0.md`.

Captures: `docs\probe\w9qi75e0-ivb-mobile-intel-gma-2026-11-05\`, all
from boot 5.

## Install

- **Package.** `build\win98se-intel-gma` at `aa70a3a9`, clean, built after
  `run-checks.ps1` passed on the branch. Pushed to `C:\V9XGMA`.
- **Route.** Device Manager on the Velocity9x VBE entry, Update Driver,
  "Display a list", Have Disk. The only model offered was the new
  exact-id "Velocity9x Intel HD Graphics 4000 (Ivy Bridge mobile)".
  Windows' restart prompt was declined.
- **WININIT.** Three plain renames (`glide2x`, `glide3x`, `v9xdisp.drv`),
  no `NUL=` line.
- **Restart.** The agent's `reset`. The boot stopped at the network logon
  again, which was cancelled; IE6's `LOADWC.EXE` faulted again with
  `C06D007F`, as at boot 4 under tier 0. It follows the cancelled logon,
  not the driver.

## Boot 5: the driver

- `Stage=enable-ok` at 640x480x8. `V9XHW.INI`: "Intel HD Graphics 4000
  (Ivy Bridge mobile)", `8086:0166`, `Direct3D=hardware-gen7`,
  `EngineStatusEnable=open-reused`, `EngineCaps=00080017`.
- Heap: `StolenBytes=04000000`, `Reg1082C0=9F900007`,
  `ProtectedBytes=00100000`, `ReserveOffset=03EA0000`.
- Write-combining: the BIOS MTRRs leave no pair for it
  (`MtrrWrite=refused`), and PAT covers the aperture instead
  (`PatWrite=ok pages=00004000`, `PatReserve=ok`).

## Where the panel is (V9XIVMM, read-only, no forcewake)

At 640x480x8, before any of our code touched the display:

| Register | Value | Meaning |
|---|---|---|
| `LVDS_CTL` E1180h | `80300302` | port on, pipe select 0: transcoder A |
| `PIPE_CONF_A` 70008h | `D8000050` | pipe A on |
| `PIPE_CONF_B`, `_C` | `0` | off |
| `PRI_CTL_A` 70180h | `C8000000` | plane A on |
| `PF_CTL_0` 68080h | `80800000` | panel fitter 0 on |
| `PF_WIN_SZ_0` 68074h | `05560300` | 1366x768 window |
| `PIPE_SRCSZ_A` 6001Ch | `027F01DF` | 640x480 source |
| `PIPE_HTOTAL_A`, `_VTOTAL_A` | `05D30555`, `030D02FF` | 1366/1492 x 768/782: the panel's timing |
| `FDI_TX_CTL_A`, `TRANS_CONF_A` | on | CPU pipe A, FDI A, PCH transcoder A |

So the BIOS drives the LVDS from pipe A at the panel's own timing and
scales every VBE mode up with the panel fitter. At 1366x768x16 the source
becomes `055502FF` (1366x768), `PRI_STRIDE_A` 2752 (`AC0h`), and the
fitter stays on at 1:1.

Pipe A is what `hsw_scanout.c` requires before it writes plane A, so the
flip path needed nothing new. The CRT and HDMI ports are off.

## DirectDraw (`V9XDDP`, 1366x768x16)

- `V9XDD.INI` `Result=COMPLETE`; the flip block is in `V9XDD2.INI`:
  `FlipHr=0`, `Flip20Ms=322`, `FlipMaxMs=17`. Twenty `DDFLIP_WAIT` flips
  at one vblank each on a 60 Hz panel. W9QI4E14 read 327.
- `FlipPixelOk=0`, as on W9QI4E14 and the 4570: the GDI readback this
  probe fails under real flips.
- The Z-buffer rows (`D3DZSurfaceHr=0x88760231`) are the same as
  W9QI4E14's capture.

`V9XTRACE` after `V9XDDP` and `V9XD32` (`boot5-after-ddp-d32-V9XSNAP.INI`):
`FlipHandled` 27, `FlipTakenAtDone` 27, `FlipNotTakenAtDone` 0,
`FlipDeclined` 0, every wait and ring timeout 0, `DrawsIntoPresented` 0.

## Direct3D (`V9XD32`)

| Run | Mode | Z | Result |
|---|---|---|---|
| default | 32 bpp | 16 | `Failures=0`, `Result=PASS` |
| `/bpp16` | 16 bpp | 16 | `Failures=0`, `Result=PASS` |
| `/z24` | 32 bpp | 24 (`RequestZBits=0x20`) | `Failures=0`, `Result=PASS` |

`T18` reports `NO-TNL-DEVICE` and `T19` `SKIP-NO-PS`: Ivy Bridge has
neither the T&L device nor DDI 8, which are Haswell's alone.

## Not measured

- **What the panel shows.** Every check here reads the framebuffer or the
  registers. The owner has been asked in the remote-help chat.
- Mode changes away from the BIOS's: the driver sets modes through VBE,
  so the fitter, the panel power sequencer and the backlight stay the
  BIOS's. `PP_STATUS`/`PP_CONTROL` and the backlight PWM were not read;
  `V9XIVMM` does not list them.
- OpenGL, Glide, 3DMark and games.
- A DOS box and the return from it.
