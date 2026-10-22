# Tier-0 `vbe` on W9QI2F4F: enable-ok on Ironlake, 128 MiB reported

Date: 2026-10-22
Status: **tier-0 passed** on the first boot after the install, at
640x480x8. Live mode changes up to 1920x1440, and from 8 to 32 bpp, all
in the same boot. `V9XGDI` passes and `V9XDDP` completes.

Step 2 of the Order in `docs\plans\intel-gen4-gen5-bringup.md`, and the
first Velocity9x driver to run on Gen5. The machine and its pre-install
state are in `2026-10-22-ironlake-w9qi2f4f-audit.md`.

Captures: `docs\probe\w9qi2f4f-ironlake-tier0-2026-10-22\`. Files named
`boot43-*` were read from `C:\V9XDIAG` after that boot.

## Install

- **Package.** `build\win98se-vbe` at `1cc25342` (0.23.0), from the clean
  worktree `C:\everything\velocity9x-ivb-tier0`. It is the same build that
  passed tier-0 on the Ivy Bridge. The main checkout's package was
  `9d4c7012-dirty`, another session's work, and was not used.
  `PACKAGE-MANIFEST.TXT` and `PACKAGE-SHA256.TXT` are that build's.
- **Route.** Pushed to `C:\V9XPKG`, then Device Manager, the `(VGA)` entry
  on `PCI\VEN_8086&DEV_0042`, Update Driver, "Display a list", Have Disk.
  The model offered and installed was "Velocity9x VBE-generic display (any
  VESA VBE 2.0+ adapter)". Windows' restart prompt was declined.
- **Driven blind.** The agent's screenshots of the 4 bpp `vga.drv` desktop
  were noise (see the audit), so every step was keyed and checked with
  `V9XWND.EXE`'s window list. The Device Manager tree took focus only after
  a click into it; type-ahead and key presses before that went elsewhere.
- **WININIT.** `WININIT-after-install.INI` holds two plain Glide renames
  and no `NUL=` line.
- **Restart.** The agent's `reset` (job `ilk-tier0-boot1`): `BootCounter`
  42 to 43, reconnected, desktop ready. `reboot` was not used
  (`2026-10-21-ivybridge-w9qi4e14-tier0-install.md`).

## Boot 43: enable-ok

- **Lifecycle.** `Stage=enable-ok` at 640x480x8, pitch 640, the LFB at
  `D0000000` (BAR2). The agent's inventory reports "Velocity9x VBE-generic
  display", provider "Velocity9x Project", on `PCI\VEN_8086&DEV_0042`.
- **The desktop draws** (`boot43-desktop.png`). At 8 bpp the agent's
  screenshot is clean. So the noise before the install came from capturing
  the 4 bpp `vga.drv` desktop. Whether the monitor showed noise too was
  never checked, and now cannot be.
- **Identity.** `V9XHW.INI`: "Generic VESA adapter (no chip-specific
  support)", PCI `8086:0042`, acceleration none, Direct3D software. This is
  the expected tier-0 result.
- **VBE 3.0, `mem=2047`: 134,152,192 bytes**, 128 MiB less 64 KiB. This
  agrees with the GGC decode in the audit (GMS 8, 128 MiB), as the DOS
  survey's `511` agreed with its 32 MiB. The driver uses 16 MiB of it
  (`vram=16777216`).
- **MTRRs.** 8 variable ranges, 5 used. `Mtrr4` covers stolen memory at
  `C8000000` (128 MiB, type 0) and `Mtrr3` covers `C4000000` (64 MiB,
  type 0). None covers `D0000000`, and the driver added none (`r=0`).
- **DirectDraw.** `V9XDDH.INI` `Stage=setinfo-callback-missing`. This is
  the same line the GM965, Pineview, Haswell and Ivy Bridge tier-0 captures
  carry.

### Modes

27 listed, 27 queried, 18 described, 18 published (`V9XMODES.INI`; one
baseline row, `0100` 640x400x8, is hidden as `scan-contradicted`):

- 640x480, 800x600, 1024x768, 1280x1024, 1600x1200 and 1920x1440, each at
  8, 16 (565) and 32 bpp;
- every pitch is width times bytes per pixel, except `0103` (800x600x8),
  which is 832, as on every Intel BIOS measured so far.

`V9XSYNC.INI` kept the 6 baseline modes and added 12.

The DOS survey of 2026-09-28 described 12 of the same 27 modes. Six it
returned with attributes 0 are described here with attributes `009B`:
`013A`, `013C`, `014B`, `014D`, `015A` and `015C`, which are 1600x1200 and
1920x1440. The Ivy Bridge showed the same six, the same way. On this
machine the stolen size also changed between the two (32 MiB to 128 MiB),
so this capture cannot say whether the query context or the memory made
the difference. `0160`-`0168` stay undescribed in both.

### No EDID

`Edid=none`, `Recommendation=none reason=no-edid`. On 2026-09-28 the DOS
survey read a valid EDID (Samsung Odyssey G5, 2560x1440). Whether the
monitor, the cable or the connector has changed since, or DDC fails from
Windows on this BIOS, is not known.

## Mode changes, same boot

`V9XMSW /set:` drives `ChangeDisplaySettings` with `CDS_UPDATEREGISTRY`,
as Display Properties does. Each switch below returned `ChangeResult=0`.
After each, `V9XBOOT.INI` `Surface=` named the new mode, and the agent's
screenshot showed a complete desktop at that size
(`boot43-<mode>.png`, `boot43-<mode>-V9XMSW.INI`):

| From | To | Surface pitch |
|---|---|---|
| 640x480x8 | 1024x768x8 | 1024 |
| 1024x768x8 | 1280x1024x8 | 1280 |
| 1280x1024x8 | 1600x1200x8 | 1600 |
| 1600x1200x8 | 1920x1440x8 | 1920 |
| 1920x1440x8 | 1024x768x32 | 4096 |

**The depth change, from 8 to 32 bpp, took effect live with no
restart.** The package's `MANIFEST.TXT` says "depth change needs
restart". For this build, on this machine, that line is wrong. It was
not checked on other machines here.

The screenshots read the framebuffer, not the monitor. With no EDID,
whether the monitor showed 1600x1200 and 1920x1440 is not known. The
owner was not asked.

16 bpp was not set as a desktop. V9XDDP ran its full-screen half at
640x480x16, below.

## Tests at 1024x768x32

- **`V9XGDI /auto`: `Result=PASS`** (`boot43-1024x768x32-V9XGDI.INI`).
  Without `/auto` it waits on a message box, and the first run timed out
  at 180 s for that reason.
- **`V9XDDP`: `Result=COMPLETE`**, full screen at 640x480x16
  (`boot43-ddp-*`). `V9XDDH.INI` moved from `setinfo-callback-missing`
  to `Stage=setinfo-ok` once DirectDraw was used. Direct3D ran on the CPU
  engine (`D3DMainHwColorModel=2`, `Direct3DMode=software`):
  - 50 of 96 `*Ok` keys read 1;
  - 29 keys that the Haswell's engine passes in 0.22.0 read 0 here: fog,
    specular, the alpha curves, colour key, mip and trilinear selection,
    chains, sprites, perspective;
  - **every key shared with the 86Box Trio64 software-Direct3D run of
    0.19.0 (`docs\probe\86box-trio64-0.19.0-2026-10-18\`) has the same
    value** (94 shared keys). So the zeros are the CPU engine's own
    results, and the same on emulated S3 hardware. Nothing in them is
    specific to Ironlake.

## Not measured

- 16 bpp as a desktop.
- Where the monitor is connected (CRT or the VBT's HDMI port C), and what
  it is.
- Anything beyond tier 0. Phase 1's MMIO questions are still open (the
  audit's *Not measured*).
