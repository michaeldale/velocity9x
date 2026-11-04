# Tier-0 `vbe` on a Radeon Xpress IGP (RS690, 1002:791F): enable-ok, every mode and GDI test passes

Date: 2026-11-03

Status: **tier-0 passed** on the first boot with the driver, restarted by
`reset`. One finding: `V9XDDP /pal8` stops at 320x200x8, a mode this BIOS
does not list. The 3D probes, run later the same day, pass except for
V9XD32's seven cull checks, which the software engine declines by design.
Whether the monitor shows what GDI reads back is not known.
Captures: [`../probe/m-gb8kre-rs690-vbe-2026-11-03/`](../probe/m-gb8kre-rs690-vbe-2026-11-03/).
`boot6-*` files are from the first boot with the driver.

## The machine

- Remote-help machine M-GB8KRE (computer `W9QI534A`), registered
  2026-11-03, driven only over the relay. The owner offered it for any use.
- Windows 98 SE (4.10.2222 A), DirectX 4.06.03.0518, agent 0.13.1.
- AMD Sempron 210U, 1.5 GHz, single core; 2 GiB reported to Windows.
  C: FAT32, 62 GB. Award BIOS "Ver 1.0H_SPCPARTTBL", dated 01/29/10.
- Southbridge functions `1002:4380`/`4386`-`438C` (SB600 family, from
  memory of the PCI id database, not checked against a copy) on
  SweetLow's ATA driver. RTL8169/8110 network (`10EC:8167`).
- Display: `PCI\VEN_1002&DEV_791F&SUBSYS_053A1002&REV_00`, class 0300.
  `791F` is an RS690 IGP id (Radeon Xpress 1200/1250 family); the exact
  marketing name is from memory, not checked.
- Before the install it ran Bearwindows' VBEMP, "VBE Miniport(ATI/AMD)"
  (JW Soft, `JWSOFT~1.INF` section `PCIVID`, matched by
  `PCI\VEN_1002&CC_0300`), at 640x480x32, driver key `Display\0001`.
  `Display\0000` is MSDISP's `[VGA]` entry from an earlier detection.
- Windows assigned (LogConfig, decoded by hand): memory
  `F0000000`-`F7FFFFFF` (128 MiB), `F8000000`-`F80FFFFF` (1 MiB),
  `F8100000`-`F810FFFF` (64 KiB), I/O `9000`-`90FF`, the legacy VGA
  ranges, and a ROM shadow at `C0000`-`CD9FF`. Which BAR each range
  belongs to is inferred; the driver later confirmed the LFB at `F0000000`.

## Screenshots under VBEMP

Full-colour, `-Gray` and `-Window` captures of the VBEMP desktop were all
noise, the same symptom as M-XP7053's 4 bpp VGA desktop. The install was
driven blind, reading each step's dialog from `V9XWND.EXE`
(`C:\V9XDIAG\V9XWND.INI`) instead. Under the Velocity9x driver every
capture was clean. Not investigated.

## Install

- **Package.** The signed 0.34.0 release, `velocity9x-0.34.0-vbe.zip`,
  build `d9d7aeb9`, unmodified; SHA-256 `4068AD82...5C5F` matches
  `releases/0.34.0/SHA256SUMS.txt` (`PACKAGE-SHA256.TXT`). Pushed to
  `C:\V9XPKG` (29 files); INF, DRV, VXD, HAL and GL DLL hashed back and
  matched.
- **Route.** `RUNDLL32.EXE shell32.dll,Control_RunDLL sysdm.cpl,,1`
  (`CONTROL.EXE` is refused by the agent's `allow_dos_vm=0`), then
  Alt+T, Tab, Home, Down x3, Right, Down, Alt+R, Ctrl+Tab, Alt+U; Enter,
  Down, Enter ("Display a list"); Alt+H, `C:\V9XPKG`. The one model
  offered was "Velocity9x VBE-generic display (any VESA VBE 2.0+
  adapter)". Windows warned that the current driver may be a closer match
  (VBEMP's `VEN_1002&CC_0300` against our `CC_0300`); answered Yes.
- **Restart.** The restart prompt was declined. `WININIT.INI` held the two
  Glide renames and no `NUL=` line (`WININIT-after-install.INI`). Then
  `reset`: boot counter 5 to 6, desktop ready, no hang.

## Boot 6: enable-ok

- **Lifecycle.** `Stage=enable-ok` at 640x480x8, pitch 640, LFB
  `F0000000` (`bar=0 pci=1`), `DpmiMap=01000000:ok`.
- **Identity.** `V9XHW.INI`: "Generic VESA adapter (no chip-specific
  support)", PCI `1002:791F`, acceleration none, Direct3D software.
- **VBE 3.0** (OEM revision `0a37`). 4F00h reports `mem=256`, 16 MiB,
  which the driver uses. The 128 MiB range at `F0000000` suggests a
  larger carve-out; not measured.
- **Modes.** 51 listed, 48 queried, 35 published: 8, 16 (565) and 32 bpp
  from 320x240 to 1400x1050, plus 320x200 at 16 and 32 bpp. 1280x1024x8
  appears twice (`0107` and `0163`). The nine modes at 1600x1200,
  1856x1392 and 1920x1440 carry attributes `00BA` (bit 0 clear) and are
  not published. **No 320x200x8 VBE mode.** Pitches are width times bytes
  per pixel except 800x600x8 (832), the 720x400 modes (768, 1472, 2944)
  and 1400x1050 (1408, 2816, 5632); the driver took each from 4F01h.
- **EDID.** v1.3 with an extension block, preferred 1920x1080, which no
  BIOS mode offers (`Recommendation=1920x1080 reason=edid-unpublished`).
- **MTRRs.** One variable range, `Mtrr0` = 0, 2 GiB, write-back; default
  type uncached, so the LFB at `F0000000` is uncached.
- **DirectDraw.** `V9XDDH.INI` `Stage=setinfo-callback-missing`, as on
  every tier-0 capture.

## Tests run through the agent

| Test | Result |
|---|---|
| `V9XGDI /auto` at 640x480x8 | PASS |
| `V9XMSW /set:1024x768x16`, then `V9XGDI /auto` | PASS, PASS |
| `V9XMSW /set:1280x1024x32`, then `V9XGDI /auto` | PASS, PASS |
| `V9XDDP` at 1024x768x16 and at 1280x1024x32 | `Result=COMPLETE`; 60 checks `Ok=1`, 47 `Ok=0`, the same keys at both depths and the same `Ok=0` set as the Cedar's 2026-10-26 run |
| `V9XMSW /cycle:10` | PASS |
| `V9XMSW /depth:10` | PASS |
| `V9XDDP /pal8` | `Result=INCOMPLETE`, exit -1 after 4.5 s; see below |

Every Z-buffer creation returns `0x88760231`, the software path's
HRESULT (as on the Cedar). `VBlank10Ms` read 163 ms for ten waits against
167 ms expected at 60 Hz. Desktop left at 1024x768x16.

### `/pal8` stops at 320x200x8

640x480, 640x400 and 320x240 at 8 bpp all set, create a primary and a
palette, lock and read back. At 320x200x8 `SetDisplayMode`, the primary
(pitch 320), `CreatePalette` and `SetPalette` all return S_OK, and the
report ends there: no `LockHr`. `stat` gives the same 12,473 bytes and
`cat` the same last line, so it is not a short `get`. The trace
(`V9XDDT.TXT`) ends at the same key. No error dialog was left; the
desktop came back at 1024x768 and the next `V9XMSW /set` passed.

The Cedar's BIOS lists 320x200x8 and its run went on to `LockHr`. This
BIOS does not, and the driver publishes no 320x200x8 row, so the mode
DirectDraw set is not one of ours: most likely DirectDraw's own VGA mode
13h / Mode X path. That is a hypothesis; nothing here shows which code
the probe died in.

The Cedar's `/pal8` was `INCOMPLETE` too, but only for want of Direct3D
in 8 bpp: it locked and read back at all four sizes. No other `vbe` first
look has a BIOS without 320x200x8, so whether the stop follows the missing
mode or this machine is not known.

## 3D probes, 2026-11-03 21:50Z

The machine went offline at 20:32Z and came back at 21:42Z, restarted by
the owner. The driver reported `enable-ok` again, desktop 1024x768x32 (the
owner's choice). The boot counter was not read.

The release package carries no 3D probes, so the four the 0.34.0 release
checks use were built from `main` at `4d53ab07` (build id
`gb8kre-4d53ab07`; the four probes' sources are unchanged since the
package's `d9d7aeb9`), pushed to `C:\V9XPKG` and hash-checked (4 of 4).
The driver under test stayed the installed 0.34.0 package. Each ran after `V9XMSW /set:1024x768x16` and again
after `/set:1280x1024x32`; captures are the `p3d-*` files.

| Probe | 1024x768x16 desktop | 1280x1024x32 desktop |
|---|---|---|
| `V9XD32` (default) | `Result=FAIL`, `Failures=7`; see below | the same, key for key |
| `V9XGLP` | `Result=PASS`, `LitResult=differ` | the same |
| `V9XGLIDP` | `Result=PASS failures=0` | the same |
| `V9XR3DP` | `Result=COMPLETE`, every call 0 | the same |

- **V9XD32 ran at 32 bpp both times.** By default it sets its own
  fullscreen mode (`RequestBpp=0x20`, `ModeBpp=0x20`), so the two rows
  differ only in the desktop it came back to. `/bpp16` was not run.
- **The seven V9XD32 failures are culling, by design.** `T9Ok=0xD` (the
  triangle that should be culled was drawn) and all six `T11*Ok=0x1B`
  (cells 2 and 5, the quads each cull mode should remove, drawn) make
  seven. Everything else the probe judges passed: T1-T8, T10, T13, T14,
  T15, T4. The software engine publishes `D3DPMISCCAPS_CULLNONE` alone
  (`d3d_soft.c`), and the core applies a cull mode only when the engine
  publishes it (`r3d_cull.h`, `v9x_r3d_cull_honoured`), so the requests
  were ignored as intended; the core path's counters agree
  (`T9CoreCulled=0`). The probe judges T9 and T11 without reading the
  caps. Its release-check passes were all on engines that claim CW/CCW.
- **DirectX 6.1 runtime.** `RunDdi=5` (no DrawPrimitives2 calls).
  T16 and T18 skip (`T16DDraw7Hr=0x80004002`, no IDirectDraw7), T17 finds
  no Direct3D 8 or 9 and T19 skips; T12 skips with one texture stage.
- **V9XGLP `LitResult=differ`** is the probe's own label for an engine
  without T&L: every lighting sample reads unlit white (`FFFFFF`), as the
  probe's comment says it will. The 0.32.0 release checks show the same
  on four machines.
- **Screenshots.** Both `screenshot` calls after the probes timed out at
  the relay; the agent answered the next verb. Not retried.

## Not measured

- What the monitor shows. Screenshots read back what GDI drew.
- The `/pal8` stop beyond the above.
- The carve-out size, PCI config space and the video BIOS image.
- `V9XD32 /bpp16`, and games.

## Where Velocity9x stands on it

- No family in the tree claims `791F` (searched 2026-11-03); the `ati`
  family has no RS690 or other R4xx/R5xx-class code.
- `vbe` works on it as far as the agent can see, without the 1920x1080
  panel mode the EDID asks for.
