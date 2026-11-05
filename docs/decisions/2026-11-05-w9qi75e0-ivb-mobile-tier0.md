# Tier-0 `vbe` on W9QI75E0: enable-ok on the HD 4000 mobile, 1366x768 sets live

Date: 2026-11-05
Status: **tier-0 passed** on the first boot after the install, at
640x480x8, and at the panel's 1366x768x16 by a live switch in the same
boot. `V9XMSW` and `V9XGDI` pass there.

Step 1 of `docs\plans\intel-ivybridge-mobile-0166.md`. The machine and its
pre-install state are in `2026-11-05-w9qi75e0-ivb-mobile-audit.md`.

Captures: `docs\probe\w9qi75e0-ivb-mobile-tier0-2026-11-05\`. Files named
`boot4-*` were read from `C:\V9XDIAG` after that boot.

## Install

- **Package.** `build\win98se-vbe` at `8121b4c3` (0.36.0), built in the
  `intel-ivb-mobile` worktree after `run-checks.ps1` passed there.
  `PACKAGE-MANIFEST.TXT` and `PACKAGE-SHA256.TXT` are that build's.
- **Route.** Pushed to `C:\V9XPKG`, then Device Manager on the IGD's
  "Standard PCI Graphics Adapter (VGA)" (hardware version 009; the
  GeForce's is the other entry, with the warning), Update Driver, "Display
  a list", Have Disk. The only model offered was "Velocity9x VBE-generic
  display (any VESA VBE 2.0+ adapter)". Windows' restart prompt was
  declined. The GeForce was left on its failing VGA binding.
- **Driven by sight.** Unlike Ironlake, the agent's screenshots of the
  4 bpp desktop are clean here, so each step was clicked from a screenshot.
  The agent's chat window comes to the front when a message arrives and
  takes the next click.
- **WININIT.** `WININIT-after-install.INI` holds the two plain Glide
  renames and no `NUL=` line.
- **Agent.** Updated from 0.12.0 to 0.14.0 (`dev-0140`) by `update` over
  the relay before the restart, because 0.12.0 has no `cat`. The update
  restarts only the agent: its boot counter went 2 to 3 with Windows still
  on `vga.drv`. The update's VNC and SETUP steps went to `127.0.0.1`
  instead of the relay and did nothing.
- **Restart.** The agent's `reset` (job `m4-20261105-221243-ffce`),
  boot counter 3 to 4.

## Boot 4

- **Logon.** The boot stopped at Windows' "Enter Network Password" dialog,
  so the agent's `wait-desktop` timed out. It was cancelled, not answered.
  IE6's `LOADWC.EXE` ("Personalized Settings") then faulted with
  `C06D007F`, a delay-load "procedure not found". That is not the display
  driver; whether it happens on this machine without the cancelled logon
  is not known.
- **Lifecycle.** `Stage=enable-ok` at 640x480x8, pitch 640, the LFB at
  `D0000000` (BAR2). The agent inventory reports "Velocity9x VBE-generic
  display", provider "Velocity9x Project", on `PCI\VEN_8086&DEV_0166`.
- **Identity.** `V9XHW.INI`: "Generic VESA adapter (no chip-specific
  support)", PCI `8086:0166`, acceleration none, Direct3D software.
- **VBE 3.0, `mem=1023`: 67,043,328 bytes**, the 64 MiB stolen less
  64 KiB, as the audit's GGC decode said. The driver uses 16 MiB.
- **EDID works.** `V9XMODES.INI`: `Edid=v=0103 preferred=1366x768`,
  recommendation 1366x768. Ironlake's EDID read failed at every boot; the
  desktop HD 4000's returned block 0 for block 1.
- **MTRRs.** 8 variable ranges. `9C000000` (64 MiB) and `9B000000`
  (16 MiB) are uncached, covering stolen memory and the GTT. None covers
  `D0000000`, and the driver added none.
- **DirectDraw.** `V9XDDH.INI` `Stage=setinfo-callback-missing`, the same
  line every Intel tier-0 capture carries.

### Modes

39 listed, 39 queried, 12 described, 12 published (one baseline row,
`0100` 640x400x8, hidden as `scan-contradicted`):

- 640x480, 800x600, 1024x768 and 1366x768, each at 8, 16 (565) and 32 bpp;
- 1366x768 is `017D`/`017E`/`017F`, with pitches 1408, 2752 and 5504 (the
  width rounded up to 64 pixels at 8 bpp, 32 at 16 and 32 bpp); every
  other pitch is width times bytes per pixel, except `0103` (800x600x8) at
  832, as on every Intel BIOS measured so far.

`V9XSYNC.INI` kept the 6 baseline modes and added 6.

### 1366x768x16, live

`V9XMSW /set:1366x768x16`: `Result=PASS`, `ChangeResult=0`. `V9XGDI /auto`
at that mode: `Result=PASS`. The agent's screenshot is a clean
1366x768 desktop (`boot4-1366x768x16.png`).

The screenshot reads our framebuffer. Nobody has looked at the panel, so
whether the LVDS shows the mode is not recorded.

The machine was left at 1366x768x16.

## Not measured

- What the panel shows, at any mode.
- Which pipe the BIOS put the LVDS on.
- Warm restart through Windows' own path: only the agent's `reset` was
  used.
