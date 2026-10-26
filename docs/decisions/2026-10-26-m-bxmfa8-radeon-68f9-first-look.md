# Tier-0 `vbe` on a Radeon HD 5450 (Cedar, 1002:68F9): enable-ok, every mode and GDI test passes

Date: 2026-10-26
Status: **tier-0 passed** on the first boot with the driver, restarted by
`reset`. Whether the monitor shows what GDI reads back is not yet known.
Machine: `M-BXMFA8` (computer `W9QI3632`), a remote-help machine
registered 2026-10-26 by the submitter of
[issue #12](https://github.com/michaeldale/velocity9x/issues/12)
(TeraScale 2 support).
Captures: [`docs/probe/m-bxmfa8-cedar-vbe-2026-10-26/`](../probe/m-bxmfa8-cedar-vbe-2026-10-26/).
`boot3-*` files are from the first boot with the driver.

## Which card it is

The owner offered it as the HD 7470 (Caicos) from issue #12. The registry
says otherwise:

| | Measured |
|---|---|
| Function 0 | `PCI\VEN_1002&DEV_68F9&SUBSYS_010E1028&REV_00`, class 0300 |
| Function 1 | `PCI\VEN_1002&DEV_AA68&SUBSYS_AA681028&REV_00` (HDMI audio, no driver) |

`68F9` is a Cedar id and `AA68` the Cedar/Redwood HDMI audio function.
Caicos uses `6760`-`677B` with audio `AA98`. Those ranges are from memory
of the PCI id database, not checked against a copy here. Asked in the
remote-help chat, the owner confirmed the card is the Radeon HD 5450
Cedar PRO.

## The machine before the install

- Windows 98 SE (4.10.2222 A), DirectX 4.08.01.0901, agent 0.13.1.
- Core 2 Duo E7500 2.93 GHz; 2 GiB reported to Windows; C: FAT32, 10 GiB.
- Host bridge `8086:2E20` rev 03 (Q45/Q43 family), ICH10, R. Loew's AHCI
  driver. Subsystem `1028:0439`, a Dell. BIOS "American Megatrends, BBS
  V3.21 0203", dated 02/26/10.
- Display: MSDISP.INF `[VGA]` at 640x480x4. The devnode's driver key is
  `Display\0001`; `Display\0000` is a second VGA entry from an earlier
  detection.
- Windows assigned (LogConfig, decoded by hand): memory
  `D0000000`-`DFFFFFFF` (256 MiB), memory `FEAE0000`-`FEAFFFFF` (128 KiB),
  I/O `D000`-`D0FF`, the legacy VGA ranges, and a ROM shadow at
  `C0000`-`CE7FF`. Which BAR each range belongs to is inferred, not read
  from config space. The driver later confirmed the LFB at `D0000000`.
- Device Manager problems unrelated to display: AHCI ports 1-5 code 10,
  UHCI `3A34` and EHCI `3A3A` code 12.

## Install

- **Package.** The signed 0.30.1 release, `velocity9x-0.30.1-vbe.zip`,
  build `66ab7fcb`, unmodified (`PACKAGE-SHA256.TXT`). Nothing was built;
  the main checkout was in use by another session.
- **Route.** Pushed to `C:\V9XPKG` over the relay. Device Manager, Update
  Driver, "Display a list", Have Disk, `C:\V9XPKG`. The one model offered
  was "Velocity9x VBE-generic display (any VESA VBE 2.0+ adapter)".
- **Restart.** Windows' restart prompt was declined. `WININIT.INI` held
  the two Glide renames and no `NUL=` line (`WININIT-after-install.INI`).
  Then `reset`: boot counter 2 to 3, desktop ready, no hang.

## Boot 3: enable-ok

- **Lifecycle.** `Stage=enable-ok` at 640x480x8, pitch 640, LFB
  `D0000000` (BAR0, `bar=0 pci=1`), `DpmiMap=01000000:ok`.
- **Identity.** `V9XHW.INI`: "Generic VESA adapter (no chip-specific
  support)", PCI `1002:68F9`, acceleration none, Direct3D software.
- **VBE 3.0** (`v=0300`, OEM revision `0c0f`). 4F00h reports
  `mem=256`, 16 MiB. The card carries more than that; the BIOS reports
  16 MiB and the driver uses what it reports
  (`Vram reported=16777216 usable=16777216`).
- **Modes.** 53 listed, 53 queried, 39 published. Sizes 320x200 to
  1600x1200, at 8, 16 (565) and 32 bpp, including 1400x1050 and 1152x864.
  The nine modes at 1792x1344, 1856x1392 and 1920x1440 carry attributes
  `00BA` (bit 0, "supported", clear) and are not published.
- **Pitches.** Every mode is width times bytes per pixel except `0103`
  800x600x8 (832), `0133` 720x400x8 (768), `0135` 720x400x16 (1472),
  `0136` 720x400x32 (2944) and the three 1400x1050 modes (1408, 2816,
  5632). The driver took each pitch from 4F01h.
- **EDID.** v1.3, preferred 1920x1200, which no BIOS mode offers. The
  driver says so (`Recommendation=1920x1200 reason=edid-unpublished`) and
  stays at the configured mode.
- **MTRRs.** One variable range, `Mtrr0` = 0, 2 GiB, write-back. The
  default type is uncached, so the LFB at `D0000000` is uncached.
- **DirectDraw.** `V9XDDH.INI` `Stage=setinfo-callback-missing`, the line
  every tier-0 capture carries.

## Tests run through the agent

| Test | Result |
|---|---|
| `V9XGDI /auto` at 640x480x8 | PASS |
| `V9XMSW /set:1024x768x16`, then `V9XGDI /auto` | PASS, PASS |
| `V9XMSW /set:1280x1024x32`, then `V9XGDI /auto` | PASS, PASS |
| `V9XMSW /cycle:10` (resolution at 32 bpp) | PASS, 10 of 10 |
| `V9XMSW /depth:10` | PASS, 10 of 10 |
| `V9XDDP` at 1280x1024x32 and at 1024x768x16 | `Result=COMPLETE`; 59 checks `Ok=1`, 47 `Ok=0`, the same set at both depths |
| `V9XDDP /pal8` | `Result=INCOMPLETE` (no Direct3D in this mode); fullscreen 8 bpp set, primary, palette, lock and readback succeed at 640x480, 640x400, 320x240 and 320x200 |

Every Z-buffer creation returns `0x88760231`. The software engine reference
from 2026-09-26 (`2026-09-26-phase1d-soft-V9XDD-after.ini`) shows the same
HRESULT, so this is the software path, not this card. The probe's colour
expectations are the ViRGE's
([2026-09-01](2026-09-01-software-rasterizer-depth.md)), so the `Ok=0` count
alone does not mean 47 wrong draws. The `Ok=0` set was not compared rung by
rung with the reference: the reference predates the newer rungs.

`VBlank10Ms` read 151 ms at 1280x1024x32 and 166 ms at 1024x768x16 for ten
waits, against 167 ms expected at the reported 60 Hz. Not followed up.

## Not measured

- **What the monitor shows.** Screenshots read back what GDI drew, not what
  the card scans out. The owner was asked in the chat, at 10:17Z, whether
  the picture is clean; no reply by the time of writing.
- **Issue #16.** Its fullscreen flicker on an HD 5570 (Redwood) needs
  someone watching the monitor during a fullscreen game. Nothing here
  tests it; the probe's pal8 results only show that the mode sets and the
  surfaces work.
- PCI config space, the video BIOS image, and the true VRAM size. A
  V9XSURV `/rom` report from real DOS would give them. It was not run
  through the agent: DOS programs launched that way have left guests unable
  to restart on their own, and with pipe capture V9XSURV stops at a
  critical error
  ([2026-09-05](2026-09-05-a-register-capture-of-both-trio3d-blend-states.md)).
- Games: 3DMark 2000 and the Quake 3 demo are installed; neither was run.

## Where Velocity9x stands on it

- No family in the tree claims an Evergreen or Northern Islands id. The
  `ati` family stops at Rage 128.
- `vbe` works on this card as far as the agent can see. What it cannot
  offer is the monitor's 1920x1200, because the BIOS has no such mode.
- Anything beyond `vbe` needs the chip's documentation: AMD published
  Evergreen register and ISA references, and Linux `radeon` covers Cedar.
  None of it has been read for this project.

## Next

1. The owner's answer on the monitor picture, then a fullscreen game with
   the owner watching, for issue #16.
2. A V9XSURV `/rom` report from real DOS.
3. Only then decide whether an Evergreen family is worth a plan in
   `docs/plans/`.
