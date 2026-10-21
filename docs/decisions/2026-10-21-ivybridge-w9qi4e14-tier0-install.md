# Tier-0 `vbe` on W9QI4E14: enable-ok, 1920x1080 sets; the agent restart locked

Date: 2026-10-21
Status: **tier-0 passed** on a cold boot. The warm restart after the
install locked the machine, and where it stopped is unknown.

Step 2 of `docs\plans\intel-ivybridge-gen7.md`. The machine and its
pre-install state are in `2026-10-21-ivybridge-w9qi4e14-audit.md`.

Captures: `docs\probe\w9qi4e14-ivb-hd4000-tier0-2026-10-21\`. Files named
`boot5-*` are from the first boot that reached the desktop.

## Install

- **Package.** `build\win98se-vbe` at `1cc25342`, built in a clean
  worktree (`C:\everything\velocity9x-ivb-tier0`) after `run-checks.ps1`
  passed there. The main checkout had another session's uncommitted
  intel-gma edits. `PACKAGE-MANIFEST.TXT` and `PACKAGE-SHA256.TXT` are
  that build's.
- **Route.** Pushed to `C:\V9XPKG`, then Device Manager, Update Driver,
  "Display a list", Have Disk. The only model offered was "Velocity9x
  VBE-generic display (any VESA VBE 2.0+ adapter)", the INF's
  `PCI\CC_0300` model.
- **WININIT.** `WININIT-after-install.INI` holds two plain Glide renames
  and no `NUL=` line.

## The restart locked

Windows' own restart prompt was declined. At 22:09Z the agent's `reboot`
(job `ivb-tier0-boot1`) went out and the machine stopped checking in. At
05:34Z the owner wrote in the remote-help chat that it "seemed like it
locked", and power-cycled it.

The agent's boot counter went from 4 (before the install) to 5 (now). So
the agent never started between the `reboot` and the power cycle, and the
lock came before that point. Nothing recorded tells apart Windows' restart
path from the first warm boot with the new driver. That boot's
`V9XBOOT.INI` was overwritten by boot 5.

From now on, restart remote machines with the agent's `reset` verb, not
`reboot`. Check `WININIT.INI` first, as here.

## Boot 5: enable-ok

`boot5-V9XBOOT.INI`, `boot5-V9XHW.INI`, `boot5-V9XMODES.INI`,
`boot5-V9XSYNC.INI`, `boot5-V9XESC.INI` and `boot5-V9XDDH.INI` were read
from `C:\V9XDIAG` after the cold boot.

- **Lifecycle.** `Stage=enable-ok` at 640x480x8, pitch 640, the LFB at
  `E0000000` (BAR2). The desktop draws (`boot5-desktop.png`). The agent
  inventory reports "Velocity9x VBE-generic display", provider
  "Velocity9x Project", `PCI\VEN_8086&DEV_0162`.
- **Identity.** `V9XHW.INI` reports "Generic VESA adapter (no
  chip-specific support)", PCI `8086:0162`, acceleration none, Direct3D
  software. This is the expected tier-0 result.
- **VBE 3.0.** `mem=1023`: 67,043,328 bytes, the 64 MiB stolen less
  64 KiB. The driver uses 16 MiB of it (`Vram usable=16777216`).
- **Modes.** 39 listed, 39 queried, 21 described, 21 published:
  - 640x480, 800x600, 1024x768, 1280x1024, 1600x1200, 1920x1080 and
    1920x1440;
  - each of the 7 sizes at 8, 16 (565) and 32 bpp;
  - every pitch is width times bytes per pixel, except `0103`
    (800x600x8), which is 832, as on every Intel BIOS measured so far.

  `V9XSYNC.INI` kept the 6 baseline modes and added 15.
- **EDID.** v1.3, preferred 1920x1080; the driver recommends it
  (`reason=edid-preferred`).
- **MTRRs.** 8 variable ranges; `Mtrr2` covers `E0000000` with a 512 MiB
  mask, type 0 (uncached).
- **DirectDraw.** `V9XDDH.INI` `Stage=setinfo-callback-missing`. This is
  the same line the GM965, Pineview and Haswell tier-0 captures carry.

### Six modes that DOS did not describe

The DOS survey (2026-10-19, `16503bf2`) returned attributes 0 for `013A`,
`013C`, `014B`, `014D`, `015A` and `015C`. Here the driver's 4F01h
describes all six, with attributes `009B`: 1600x1200 and 1920x1440 at 8,
16 and 32 bpp. `0160`-`0171` stay undescribed in both. The same BIOS on
the same machine answered differently, and nothing here says why. The
driver's query runs under Windows from the mini-VDD, after the BIOS has
set a mode. Do not rely on the DOS list for this BIOS.

## 1920x1080x8 sets

Display Properties at 256 colours, slider to 1920 by 1080, Apply. The
desktop came up at 1920x1080 with the "keep this setting?" prompt
(`boot5-1920x1080x8-keep-prompt.png`).

The screenshot took minutes through the relay, so the 15-second timer
expired. Windows reverted to 640x480x8 by itself
(`boot5-reverted-640x480x8.png`, `info` 640x480). Both mode changes, up
and back, completed without a restart. The desktop was left at
640x480x8; nothing was kept.

## Not measured

- 16 and 32 bpp. A depth change needs a restart, and that waits for
  `reset`.
- Where the warm restart locked.
- Anything beyond tier 0.
