# Release 1.0.0: a stable release

Status: proposed 2026-11-21, from a desk review of the issue index, the
README's card tables and known issues, `docs/STATUS.md`, every
release-check record from 0.42.0 to 0.60.0 and GitHub issues 16 and 21.
The individual issue records were not re-read, only their index lines.
Nothing here is measured until the Progress section says so.

## Why 1.0 is not mainly a bug count

- **Six of eight packages are unchecked.** From 0.42.0 to 0.60.0 no
  release check booted the `3dfx`, `matrox`, `s3` or `sis` files. `vbe`
  was booted once (0.47.0) and `nvidia` five times. The HAL core, the
  ICD, the Glide wrappers and the 16-bit driver they share changed in
  almost every one of those releases.
- **Cadence.** 18 releases in the 13 days after 0.43.0, most adding a
  chip. Several rows are claimed from a survey or from the Linux tables
  and have never run.
- **The records are behind.** `docs/STATUS.md` describes 0.44.0. The
  issue index was last re-read as a whole on 2026-10-14 and lists about
  130 open records, several saying a fix is in and a re-run is owed.

## Shape

1. **Freeze.** From the 1.0.0 build: no new PCI ids, no new
   interfaces, no new default behaviour. Unmerged branches
   (`rv280-geomscan`, `ati-cayman`, `wip-caicos-6779`, `hsw-panel-fit`,
   the Doom 3 worktree) merge before the freeze or wait for 1.1.
   `rv280-geomscan` and `hsw-panel-fit` wait for 1.1 (Michael,
   2026-11-26); every unmerged branch is parked for 1.0 (Michael,
   2026-11-27).
2. **Three tiers**, in the README's card tables and each package's
   `INSTALL.TXT`:
   - *Supported:* in the 1.0 check fleet, every release probe passing on
     the 1.0 build, a Have Disk install measured.
   - *Experimental:* runs, with failures the README names (RIVA 128ZX,
     GeForce 210, HD 2600 XT and HD 2400 Pro as they stand).
   - *Recognised:* binds by design; not run, or not since an old release
     (GMA 3100, HD 5570, `8086:0122`, the Radeon 9200 ids, the Matrox AGP
     ids, Ironlake desktop and mobile, G45/Q45, HD 5450, Rage Mobility-M,
     HD 4400, Voodoo5).
   The INFs keep binding all three; only the claim changes.
3. **No release candidate and no soak** (decision 4, Michael,
   2026-11-27): when the gate below is met, 1.0.0 is released directly,
   as the ordinary releases are. Problems the field finds go into 1.0.x.
4. **After 1.0:** 1.0.x carries fixes only. New chips and interfaces go
   to 1.1 betas.

## Blockers

Fix, or measure and document, before 1.0.0 (there is no release
candidate; decision 4).

| # | Item | State on 2026-11-21 | Done when |
|---|---|---|---|
| B1 | A write Lock of the back buffer does not wait for a pending flip ([issue](../issues/2026-11-21-haswell-lock-does-not-wait-for-pending-flip.md)) | Closed 2026-11-23: fixed, FlipSafety `held` on the HD 4600 and the Oland 6610 | dispbench FlipSafety judges Lock as it does draws, on the HD 4600 and one non-Intel card |
| B2 | The `ati` gamma hook writes Mach64 DAC offsets into other chips' register windows ([issue](../issues/2026-11-09-ati-gamma-hook-writes-mach64-dac-on-evergreen.md)) | Closed 2026-11-23: only the Mach64-class rows claim gamma; refused on the Rage 128 (host test) and the Oland (measured) | The hook is per engine; a ramp measured on the Rage 128 and one Evergreen-or-later part, or refused there |
| B3 | Engine-placed render targets are never freed ([issue](../issues/2026-11-15-placed-render-targets-never-freed.md)) | Closed 2026-11-23: freed; V9XRTLK leaks 0 on the Oland and the HD 7660D | A create/destroy loop in a probe leaves the heap where it started on two engines |
| B4 | DirectX 8 MIPFILTER read with DirectX 7's numbering ([issue](../issues/2026-11-19-ddi8-mip-filter-read-as-dx7.md)) | Closed 2026-11-23: fixed in 0.57.0 (`4b0aa6ba`); 3DMark 2001 SE on the HD 4600 reaches MIPNEAREST and MIPLINEAR | The record says which |
| B5 | Full-screen DOS box and return ([issue](../issues/2026-09-12-netbook-dos-box-return-hardlock.md)) | Run on the GMA 950, the HD 4600 and the 86Box ViRGE/DX; issue 16's reporter fails on `vbe` too; 2026-11-24: passes on 86Box with `vbe` (Millennium), `matrox` and `ati` (Mach64 VT2, not a Radeon); 2026-11-25: passes on the Oland R7 250, the `ati` Radeon ([record](../decisions/2026-11-25-1.0-b5-oland-dos-box.md)); the S3 card and the reporter's case remain; 2026-11-27: the ViRGE/DX round skipped it under the standing "no DOS box on A8U4I5" rule (2026-10-02) without asking; Michael wants it run there, on the Trio64 and the Trio3D, the S3 cards left in the round | A round trip on one physical S3 card, one `ati` Radeon and `vbe`; the reporter's case explained |
| B6 | A square follows the cursor over a full-screen game with the desktop copy ([issue](../issues/2026-11-13-evergreen-cursor-square-over-fullscreen-game.md)) | The reporter confirms `NOSHADOW.TXT` removes it; 2026-11-25: reproduced on the Oland R7 250 with a new probe (V9XCURSQ), gone with `NOSHADOW.TXT`; cause: the shadow copies the cursor rectangle out during exclusive mode ([record](../decisions/2026-11-25-1.0-b6-oland-cursor-square.md)); fixed the same day (`shadow_hold`), V9XCURSQ passes on the Oland and on the R3E (fix only; the square not reproduced there); the reporter's confirmation still owed; 2026-11-26: on an HD 5450 the square is still there at 320x240, the reporter's mode, while 640x480 is fixed ([record](../decisions/2026-11-26-1.0-hd5450-evergreen-check.md)) | **Closed for 1.0 (Michael, 2026-11-26):** 640x480 fixed; the square at 320x240 is a README known issue, its fix left for 1.1 |
| B7 | HD 5770 offers 16 MB where 128 is expected (GitHub issue 21) | Waiting on the reporter's `V9XBOOT.INI` and `V9XHW.INI`; 2026-11-26: on our HD 5770 (M-BXMFA8) the `ati` driver gives DirectDraw 128 MiB and the VBE BIOS reports 16 MiB, so the reporter's 16 is the VBE figure; which path their Windows Me machine took is unknown ([record](../decisions/2026-11-26-1.0-trio3d2x-hd5770.md)) | **Closed (Michael, 2026-11-26)** on that measurement; the HD 5770 stays Supported |
| B8 | Two Direct3D processes at once on the HAL's shared statics ([issue](../issues/2026-10-06-hal-statics-shared-across-processes.md)) | Closed 2026-11-23: DDHELP faulted on another process's draw ([issue](../issues/2026-11-23-ddhelp-destroysurface-runs-another-process-draw.md)); fixed, and UT2004 with the probes beside it passes on the Oland and the HD 4600 | Two probes run together on one Intel and one `ati` machine, recorded |
| B9 | Suspend and resume ([issue](../issues/2026-11-06-no-mode-restore-after-resume.md)) | 2026-11-24 on 86Box (Stand By pauses the emulator, no power loss): `vbe` and `matrox` come back; `s3` and `ati` (Mach64 VT2) now come back too ([record](../decisions/2026-11-24-standby-monitor-power-s3-ati.md), [issue](../issues/2026-11-24-s3-ati-screen-off-after-standby-86box.md)); 2026-11-25 on the Oland R7 250 (a desktop, APM): resumes to the desktop with the mode kept and the probes passing after it, slowly, as Windows' own driver does ([record](../decisions/2026-11-25-1.0-b9-standby-oland.md)); no laptop yet | **Closed for 1.0 (Michael, 2026-11-27):** a README known issue says what was measured (86Box, one desktop); the laptop measurement moves to the next release |
| B10 | The install path: Have Disk from the released zip, the 16-colour first install ([issue](../issues/2026-11-07-first-install-stays-at-16-colours.md)), the V9XDISP.DRV prompt ([issue](../issues/2026-08-29-inf-asks-for-v9xdisp-drv.md)) | `s3` done 2026-11-24 (released 0.60.0 zip on a fresh 86Box ViRGE/DX: enable-ok; the missing Display Properties tab found and fixed); INSTALL.TXT rewritten for users 2026-11-24; `ati` and `matrox` done the same day on 86Box Mach64 VT2 and Millennium, the tab's cause corrected (DESK.CPL's OEMCPL.INF seed); `vbe` stops at `fail-hardware-vbe-mode` on a ViRGE/DX and passes on a Millennium (VBE LFB, enable-ok 1024x768x16); `intel-gma` 2026-11-25 on the GMA 950 netbook from Windows' VGA driver, not a never-had-it machine (one model, enable-ok 640x480x8; [record](../decisions/2026-11-25-1.0-b10-have-disk-intel-gma-netbook.md)); `nvidia` 2026-11-25 on the GeForce 210 in W9QI0DAD, which had run it before (main's package, not a released zip: one model, no file prompt, enable-ok 640x480x8; [record](../decisions/2026-11-25-t19-specular-saturate-before-fog.md)); then the signed 0.59.0 `nvidia` zip by Have Disk the same day (one model, no file prompt, enable-ok 1024x768x16; the two Glide DLLs kept their newer copies, as on the netbook; [record](../decisions/2026-11-25-1.0-b13-update-rollback-gt218.md)); `sis`, `3dfx` owed (physical cards); 2026-11-27: the Voodoo4 went back onto `3dfx` by Have Disk from the package folder after Windows put 3dfx's INF over it, on a machine that had the driver ([record](../decisions/2026-11-27-1.0-voodoo4-a8u4i5.md)) | **Closed (Michael, 2026-11-27):** six packages done; a never-had-it Have Disk for `sis` and `3dfx` is more testing than 1.0 needs |
| B11 | Every package README reads "HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED" ([issue](../issues/2026-08-30-package-status-string-is-hardcoded.md)) | Closed 2026-11-26: each family's `TestedStatus` names its cards' tiers from decision 3 and the evidence, and `CheckRecord` its records (check record done 2026-11-23); `run-checks.ps1` builds all eight with them. The GeForce 210, back in W9QI0DAD, is written Supported on its 2026-11-25 probes | The line states the package's tier and its check record |
| B12 | `C:\V9XDIAG\V9XGLD3.LOG` grows without limit; the other diagnostic logs unaudited | Closed 2026-11-23: six logs roll over at 2 MiB, all listed in `diagpaths.h` | Every log the driver writes by default is bounded, listed in one place |
| B13 | The updater's rollback, and an update taken on physical hardware | 2026-11-25 on the GeForce 210 (W9QI0DAD): Check for updates took released 0.59.0 to 0.60.0 (ten files hash-matched, `State=committed`, enable-ok, Display Properties on 0.60.0); the backup's files put back by WININIT rename and its REGISTRY.REG imported, back at 0.59.0 and enable-ok ([record](../decisions/2026-11-25-1.0-b13-update-rollback-gt218.md)). Not exercised: RECOVER.TXT's Safe Mode COPY by hand, any registry or INF change (the two INFs are identical); RECOVER.TXT's `COPY V9X*.*` missed the three Glide/3DCIF DLLs (fixed the same day: it copies by type). Owed: the step to the release candidate | 0.60.0 to the release candidate through Check for updates on one machine; one rollback from the backup |
| B14 | Every Windows 9x supported (decision 2): Windows 95, 98 and Me have no measured configuration beyond a Trio64 on VLB under 95 installed by hand without the mini-VDD, the RIVA 128ZX machine on 98, and one Me reporter | Decided 2026-11-24; nothing measured. **Closed for 1.0 (Michael, 2026-11-27): 1.0 supports Windows 98 SE only**; Windows 95, 98 and Me move to a later release, and the README's answer says so | On each of 95, 98 and Me: a Have Disk install, the boot, the release probes and the DOS box round trip on at least one card, recorded; what fails is fixed or named in the README; the README's "Will it run on Windows 95 or Windows Me?" answer rewritten from those records |

Not blockers, but re-run for the record before 1.0: the RV630/RV610
constant-Z after the first CP start, the R3E's first-clear timeout, the
Rage 128's 3DMark 2000 hang (0.32.0, one run in two), the three
unreproduced hard locks on A8U4I5 and the Gateway. Each either gets a
measurement on the 1.0.0 build or stays a named known issue.

From the issue re-read of 2026-11-26 (gate item 4), fixes in source or
on one machine whose run is owed, by machine:

- GM45 and GM965: Forsaken gameplay, and a colour-keyed title on the
  GM965 ([issue](../issues/2026-11-08-gen4-keyed-depth-write-draws-dropped-forsaken.md)).
- GMA 950 netbook: Final Reality's Robots watched on the panel for the
  flicker ([issue](../issues/2026-09-19-the-flicker-state-of-play.md)).
- A8U4I5 round (the Rage XL's GLQuake runs done 2026-11-26; the
  ViRGE/DX's Final Reality and 1024x768x32 done 2026-11-27, both issues
  closed): a live mode switch repaint and the black level on the Trio64
  ([issue](../issues/2026-08-20-live-mode-switch-no-repaint-barry.md),
  [issue](../issues/2026-08-23-s3-pedestal-black-level.md)).
- Guests: Doom95 on the 86Box ViRGE
  ([issue](../issues/2026-08-15-doom95-low-resolution-modes.md)), the
  QEMU `vbe` guest's 1024x768x16 scanout
  ([issue](../issues/2026-08-27-vbe-1024x768x16-scanout-stripes.md)).

## The gate for the 1.0 build

1. `run-checks.ps1` green from a clean tree.
2. Every package booted and probed on at least one machine or guest:
   - `intel-gma` and `ati`: the release-check fleet, as now;
   - `s3`, `matrox`, `sis`, `3dfx` and the older `ati` engines: a
     card-swap round on A8U4I5 (Trio64, ViRGE/DX, Millennium II or
     Mystique, SiS 6326, Voodoo4, Rage XL, Rage IIC, Rage 128 Pro);
   - `s3`, `vbe` and the Mach64 VT2: the 86Box and QEMU guests;
   - `nvidia`: the RIVA 128ZX, and a GeForce 210 if one is fitted.
3. B10's Have Disk installs and B13's update.
4. The issue index re-read record by record: closed where a re-run
   exists, moved to the README's known issues where it stays, moved to
   the post-1.0 list below where it is a gap.
5. `docs/STATUS.md` rewritten against the 1.0.0 build.
6. The README's tables carry the tiers; every "from 0.x" claim older
   than the last run on that card says when it was last run.

## Not in 1.0

- **Feature gaps:** YUV surfaces and overlays, DPMS beyond S3, a
  hardware cursor, banked-only VBE modes, the Bochs DISPI interface,
  VMware SVGA II, stencil and more blend factors in the CPU rasterizer,
  OpenGL display lists and texgen (first for 1.1: Tux Racer and
  Minecraft need them in the field, reports V9X-VK1M80 and
  V9X-JVGQM2), the 3DCIF gaps, 8 bpp gamma, a GPU clock readout on
  Intel (`ClockStatus=unavailable` on every Intel chip; the RP0 request
  itself is made and reported, `RenderClock=rp0-granted`).
- **GitHub PR 32** (Gen3 GDI screen-to-screen copies on the blitter and
  a hardware cursor, intel-gma): left for 1.1, decided 2026-11-25
  (Michael). Not run on hardware as submitted, touches the shared 16-bit
  GDI and DOS-box paths, and leaves intel-gma's `_TEXT` 135 bytes short
  of its budget.
- **Unmerged branches** (`ati-cayman`, `wip-caicos-6779`, the Doom 3
  worktree): parked for 1.0, decided 2026-11-27 (Michael).
- **Windows 95, 98 (first edition) and Me:** 1.0 supports Windows 98 SE
  only; B14's measurements move to a later release, decided 2026-11-27
  (Michael).
- **Stand By on a laptop:** B9's laptop measurement, decided 2026-11-27
  (Michael).
- **Branches `rv280-geomscan` and `hsw-panel-fit`:** left for 1.1,
  decided 2026-11-26 (Michael). The Radeon 9250's depth band and the
  HD 4600's VGA 1080p wrap stay README known issues in 1.0; the
  branches are not merged before the freeze.
- **ViRGE/DX:** 3DMark 2000's engine idle timeouts at 640x480 (none at
  800x600); left for a later release, decided 2026-11-27 (Michael)
  ([issue](../issues/2026-11-27-virgedx-3dmark2000-640x480-engine-timeouts.md)).
- **Rage 128 Pro:** V9XD32's X8R8G8B8 texture drawing black, as on the
  SiS 6326; left for a later release, decided 2026-11-27 (Michael)
  ([issue](../issues/2026-11-27-rage128pro-v9xd32-x8r8g8b8-texture-draws-black.md)).
- **SiS 6326:** V9XD32's X8R8G8B8 texture drawing black; left for a
  later release, decided 2026-11-27 (Michael)
  ([issue](../issues/2026-11-27-sis6326-v9xd32-x8r8g8b8-texture-draws-black.md)).
- **Voodoo4:** V9XD32's 32-bit textures drawing black (as in 0.17.0);
  left for a later release, decided 2026-11-27 (Michael)
  ([issue](../issues/2026-11-27-voodoo4-v9xd32-32bit-textures-draw-black.md)).
- **Rage IIC:** the engine wedge in 3DMark 99 and 2000, the depth fill
  answering "still drawing" forever on a quarantined core, and the
  empty off-screen render targets; left for a later release, decided
  2026-11-26 (Michael)
  ([issue](../issues/2026-11-26-rage-iic-3dmark2000-depth-fill-loop-and-offscreen-targets.md)).
- **Rage XL improvements:** V9XD32 T20's additive blend and T22's
  32-bit textures, with the rest of the Mach64 work (the GLQuake
  `-nomtex` lightmaps among it); left for a later release, decided
  2026-11-26 (Michael)
  ([issue](../issues/2026-11-26-rage-xl-v9xd32-add-blend-and-32bit-textures.md)).
- **B6 at 320x240:** the cursor square over a full-screen game remains
  in 320x240 modes (V9XCURSQ on the HD 5450; 640x480 is fixed); left
  for 1.1, decided 2026-11-26 (Michael)
  ([issue](../issues/2026-11-13-evergreen-cursor-square-over-fullscreen-game.md)).
- **GeForce 210 with vsync forced off:** frames flash black because an
  unsynced NV50 flip is not held while the old buffer is scanned; left
  for 1.1, decided 2026-11-26 (Michael)
  ([issue](../issues/2026-11-26-gt218-vsync-off-clears-scanned-buffer.md)).
- **To test for 1.1:** UT2004's graphical issues on the Oland
  ([issue](../issues/2026-11-23-oland-ut2004-graphical-issues.md)),
  reported, not yet described or measured. GLQuake `-nomtex
  +gl_texsort 0` on the GMA 950: walls show only their lightmaps
  ([issue](../issues/2026-10-08-mach64-replace-fragment-alpha-refused.md));
  GLQuake's default `gl_texsort 1` draws correctly, so 1.0 names it as a
  known issue and the cause waits for 1.1 (decided 2026-11-26).
- **Parked work:** the Rage 128 AGP ring, Gen4 ring flips, Gen3 batch
  cost, the Doom 3 ARB path, PAL8 on the Rage Pro.
- **Performance:** the GCN drain, copy and flip-wait records of
  2026-11-18, OpenGL throughput, Direct3D core throughput.
- **New chips:** GitHub issues 20, 27, 28 and 30; Cayman; Caicos 6779.
- **From the issue re-read of 2026-11-26** (each record says why):
  - *Feature gaps:* the Intel BIOSes' missing panel modes, the `vbe`
    8-bit DAC; the 3DCIF records (blended sprites, the Phase 3 probe,
    the unimplemented primitives, Gen3 flicker and speed, a tester's
    Rage Pro).
  - *Probe coverage:* the faults the 0.57.0 probes missed
    ([issue](../issues/2026-11-19-release-probes-missed.md)), the
    Oland's unjudged texture shapes, V9XDDP `/mixed` and `/zprivate`;
    untested leftovers: Quake 3's wall emblem and the GMA 900 and HD 4000
    menus (GitHub issue 11), Re-Volt's tyres and Tux Racer at 32 bpp on
    the HD 4600, Gen3's doubled single stage drawn as MODULATE.
  - *Investigations with no user-visible fault:* the Oland's ring
    pointer read past wptr (avoided since 1101edcb), Gen3 local-heap
    fragmentation (the texture pool holds textures now), the Pineview's
    load failure after the survey, the Trio64's unisolated drag lock
    with text acceleration (off by default), A8U4I5 dropping off the
    network, one Rage XL AGP OpenGL session on the CPU.
  - *Code health and performance:* NaN checks in fifteen copies, Gen3
    culling in software, heap restriction masks for engineless
    families.
  - *Tools and the settings page:* V9XTRACE's exit flush, FlipPixelOk,
    agent screenshots at 640x480, V9XSTAGE under Microsoft's Matrox
    driver, the settings page's data gaps and its stale-revision read.

## Decisions owed (Michael)

1. **Refresh rate choice. Decided 2026-11-23 (Michael): parked until a
   release after 1.1.** Not in 1.0 or 1.1; modes stay at the video BIOS's
   timing. 60 Hz only matters on a CRT, and the VBE 3.0 CRTC builder is
   host-tested and unused
   ([issue](../issues/2026-11-06-no-refresh-rate-choice.md)).
2. **Windows versions. Decided 2026-11-24 (Michael): support all of
   Windows 9x** - Windows 95, Windows 98, Windows 98 SE and Windows Me.
   (Proposed before: 98 SE supported, the others stated as run.) What
   that commits 1.0 to is blocker B14. **Revised 2026-11-27 (Michael):
   1.0 supports Windows 98 SE only**; the other versions move to a later
   release (B14 closed for 1.0).
3. **Tier of each card. Decided 2026-11-25 (Michael), card by card:**
   - *Supported, by the Shape rule* (in the check fleet, probes passing
     recently): GMA 950, HD 4600, HD 3000 `0112`, GMA X3100, Oland XT
     `6610`, R3E, R7E, HD 7660D, X1300. (The GMA 4500MHD, whose
     two-unit REPLACE and DECAL failures go to the README's known
     issues, and the X1200 (RS690) were here until 2026-11-26.)
   - *Supported, on a 1.0 run still owed:* the A8U4I5 card-swap round
     (Rage 128 Pro, Rage XL, Rage IIC, Trio64, ViRGE/DX, Trio3D,
     Millennium, Millennium II, Mystique, SiS 6326, Voodoo4), which
     Michael will do on the release candidate (done on the 0.61.0 tree
     from 2026-11-26, as there is no candidate); and the remote-help cards
     outside the fleet (GMA 900, GMA 3150, HD 4000 mobile, Radeon 9250,
     HD 7470, Rage Mobility-M), Supported if they pass on the 1.0.0 build,
     Experimental if not, so they join the release-check list.
   - *Supported on their last run, the card no longer fitted:* Ironlake
     `0042`, HD 4000 `0162`, HD 2600 XT, HD 2400 Pro, HD 5450, GeForce
     210. These do not meet the Shape rule (no 1.0 build run); the
     README says when each last ran. The X1200 (RS690) and GMA 4500MHD
     joined this group on 2026-11-26 (Michael): their machines went
     offline after 0.51.0 and 0.57.0.
   - *Supported, other evidence:* the `vbe` package; HD 5770 (drops to
     Experimental only if B7 is a real fault); the field reports G45,
     Q45 and HD 2500 (no diagnostic files of ours); Oland 6611 (our
     probes on one build, no game); Mach64 VT2 (emulator only). The
     README states the evidence beside each.
   - *Experimental:* RIVA 128ZX (3DMark 2000's draws mostly refused,
     one Glide probe failure).
   - *Recognised:* GMA 3100, Ironlake mobile `0046`, HD 3000 `0122`,
     HD 5570, Radeon 9200 `5961`/`5962`, Mystique AGP and Millennium II
     AGP; nothing has run on any of them.

   So the Shape section's definition of Supported is wider in practice:
   for 1.0 it means "Michael stands behind it", with the evidence named
   beside each card. B11 and gate item 6 can now be written from this
   list.
4. **Length of the soak**, and whether 0.61.0 is the first candidate or
   one more ordinary release. **Decided 2026-11-27 (Michael): no release
   candidate and no soak; 1.0.0 is released directly** once the gate is
   met.

## Progress

Dated entries, appended as work is measured.

### 2026-11-21: baseline on the 0.61.0 tree

`ac9aa76b-dirty`, the driver code of 0.60.0
([record](../decisions/2026-11-21-1.0-baseline-checks.md)).

- Gate step 1: `run-checks.ps1` passes (dirty tree: untracked evidence,
  this plan, the quarantined 0.52.0 `s3` zip).
- Gate step 2, in part: six of eight packages booted and probed on
  thirteen machines and guests. `intel-gma` on the HD 4600, GMA 950,
  GM965 and HD 3000, and `ati` on the R7E and X1300: every probe passes.
  `vbe` on the HD 4600: every probe passes. `s3` on the 86Box Trio64:
  every probe passes; on the 86Box ViRGE/DX: Glide FogTable fails (known)
  and V9XD32 needs `/bpp16`. `ati` on the Oland 6611 and `nvidia` on the
  RIVA 128ZX: their known failures only. `matrox` on the 86Box
  Millennium: boots; Glide and the render interface do not open (open
  question in the record).
- Still unbooted: `sis` and `3dfx`, and every physical S3, Matrox and
  older ATI card. A8U4I5's address answers as nothing today.
- The runner takes a `Port` per machine, so the 86Box guests are checked
  like any other machine. The four guests' agents are on 0.19.0.
- V9XFBCMP assumed a 5:6:5 desktop and failed on correct 5:5:5 screens;
  fixed in the probe and measured on both S3 guests.
- B4 is a stale line: the fix is `4b0aa6ba`, shipped in 0.57.0. Its
  record still says not run on hardware; one 3DMark 2001 SE run on the
  HD 4600, reading `FilterMinSeen`, closes it.
- Later the same day on 86Box: the mode matrix passes every mode for
  `s3` on the Trio64 and ViRGE/DX guests (eleven each) and `ati` on the
  Mach64 VT2 (six). Each S3 guest stuck once at shutdown on the way
  ([issue](../issues/2026-11-21-86box-s3-guests-stick-at-shutdown-in-mode-matrix.md));
  that needs a restart loop before `s3` can be called Supported. The
  ViRGE/DX guest's V9XD32 fails T1, T4 and T14 at 16 bpp, and released
  0.36.0 fails the same, so it is not a regression; cause open.
- **New blocker, B14 (2026-11-22):** the HD 7470 hangs the machine in
  its first Direct3D program on released 0.56.1, and runs on 0.56.0
  ([issue](../issues/2026-11-22-caicos-hd7470-hangs-in-direct3d-from-0.56.1.md)).
  Every release from 0.56.1 to 0.60.0 carries it. The HD 5450, HD 5570
  and HD 5770 use the same engine code and have not been run since
  0.34.0. Done when the commit is isolated, fixed, and V9XD32 passes on
  the HD 7470 and one other Evergreen card. The same day: isolated to
  the per-group flush of c0c9381b and fixed in source (the group closes
  after every batch on the non-Trinity parts); V9XD32 passes on the
  HD 7470 at both depths. One other Evergreen card still owed. `run-release-checks.ps1`
  takes a `PackageDir` per machine for such A/Bs.
- New for the list: M-D7TFW4 with an HD 7470 fitted beside its HD 7660D
  fails and hangs on released 0.56.2 (2026-11-19); not run today. The
  86Box Trio64's 134 ms windowed presents. The Mach64 VT2 guest's 8 bpp
  desktop hides three probes.

### 2026-11-23: B4 closed, B8 in part

No new machine online; the same nine as on 2026-11-21. Evidence in
`docs/probe/1.0-b4-b8-2026-11-23/`.

- **B4, closed by measurement.** 3DMark 2001 SE (all tests) on the
  HD 4600 from a fresh `reset`: `FilterMinSeen` 6h to 1Eh, so DirectX 8
  programs now reach MIPNEAREST and MIPLINEAR; no trilinear was asked
  for; no engine timeout
  ([issue](../issues/2026-11-19-ddi8-mip-filter-read-as-dx7.md)). The
  picture was not looked at.
- **B8, in part.** The release probes cannot be the two programs: since
  980724ab (2026-10-27) each takes a named mutex and answers
  `Result=BUSY` while another holds it, because two probes once read each
  other's pixels as failures. Measured on the 86Box ViRGE/DX guest: of
  three started together, three rounds, only V9XGLP ran. So each machine
  ran 3DMark 2001 SE's benchmark (Direct3D 8, exclusive full screen) and,
  during it, V9XGLP, V9XWPRS and V9XR3DP one after another:
  - **R7E (`ati`, GCN 3):** 3DMark completed its run (852, recorded, not
    compared) with the probes run 3-5 minutes into it; each probe's
    verdict identical to its solo run on 2026-11-21; `EngineIdleTimeouts`,
    `EngineResets` and every flip-wait timeout 0; `FlipHandled=7191`.
    `DrawsIntoPresented=18` is cumulative since boot 82 and not
    attributed to this run. The `key ALT+B` that started it returned
    "ambiguous input target", yet the benchmark ran; how is not known.
  - **HD 4600:** the probes' verdicts identical to their solo runs and
    the counters 0, but 3DMark stopped its own run with "display window
    lost focus" once the first probe window opened, so the two drew
    together only briefly.
  - Still owed for B8: a Direct3D program that keeps drawing in a window
    (Half-Life `-window` on the HD 4600) beside a probe, and two Direct3D
    programs in one run.
- **RIVA 128ZX:** the fixed V9XFBCMP passes (second run `Differ=0`; the
  first read 458 with the screen still changing).
- **86Box guests** stop with the session that started them; restart them
  before a run (the ViRGE/DX guest was restarted and set to 16 bpp).

### 2026-11-23, later: B1, B3 and B12 fixed

One build, `713bc38a-dirty`, installed on the HD 4600, the Oland 6611
(M-BXMFA8) and the HD 7660D (M-D7TFW4); release probes unchanged where a
comparison exists
([record](../decisions/2026-11-23-1.0-b1-b3-b12-checks.md)).

- **B1, fixed; half done.** A Lock of the retiring buffer waits for the
  flip. FlipSafety `held` on the HD 4600 at 16 and 32 bpp. Owed: the same
  check on one non-Intel card (dispbench needs a LAN address).
- **B3, closed.** Placed render targets are freed. V9XRTLK: the Oland lost
  24.6 MB in 20 rounds before, 0 after; the HD 7660D 0. Two engines, as
  the row asks.
- **B12, closed.** The six appended logs roll over to `.OLD` at 2 MiB,
  listed in `include/velocity9x/diagpaths.h` with every other file the
  driver writes and why it does not grow. Seen rolling on all three
  machines.
- B1's issue said "every family", but only the HD 4600 was measured on
  either side of the fix.

### 2026-11-23, evening: B1 closed on the Oland

W9QI0DAD's Oland 6610 is on the LAN, so dispbench reached it
([record](../decisions/2026-11-23-1.0-b1-oland-flipsafety.md)). FlipSafety
Lock on released 0.59.0: 114 of 114 and 117 of 117 judged writes early at
16 and 32 bpp, so the gap was the shared Lock path, not Intel's. On the
fix (`713bc38a-dirty`, the code of `6a1c80d0`): 0 of 119 at both depths,
`LockFlipWaited=240`, no timeout; release probes as on boot 123. **B1
closed.** The machine's `VSync=2` was set to 0 for the runs and restored.

### 2026-11-23, night: B2 and B11 done, B8 fails on the Oland

On W9QI0DAD's Oland 6610, `a92e01f3-dirty`, release probes as on
boot 123 ([record](../decisions/2026-11-23-1.0-b2-gamma-b11-status.md)).

- **B2, closed.** Only the Mach64-class rows claim gamma; the Oland
  accepted and wrote a ramp before and refuses it after. The Rage 128
  refuses by host test; the Rage XL keeps gamma, not re-run.
- **B11, done but the tier.** Each package's Status line comes from its
  family manifest and names its check record; the tier waits on
  decision 3.
- **B8, a failure.** UT2004 windowed with the release probes beside it:
  V9XWPRS's DirectDraw half failed, then DDHELP faulted in the HAL
  inside DestroySurface, running a GCN draw with another process's
  texture pointer
  ([issue](../issues/2026-11-23-ddhelp-destroysurface-runs-another-process-draw.md)).
  Restarted clean. Owed: the fix, the same run again, the HD 4600 run.

### 2026-11-23, late: B8 closed

The pending DrawPrimitives2 run is drawn only by the process that queued
it ([record](../decisions/2026-11-23-1.0-b8-dp2-run-owner.md)). UT2004
windowed with V9XWPRS, V9XGLP and V9XR3DP beside it: no DDHELP fault,
every probe passes, on the Oland 6610 (boot 131) and the HD 4600 (boot
299, every release probe passing there too); the runs other processes
flushed are dropped and counted (98 and 111). **B8 closed.** Closing
UT2004 mid-benchmark faults in UT2004 itself, with or without the probes;
recorded on its 1.1 issue.

### 2026-11-24: B10 started, the s3 package

Released 0.60.0's `s3` zip by Have Disk on clones of the 86Box S3
reference VM, which never had the driver
([record](../decisions/2026-11-24-1.0-b10-have-disk-s3.md)): one model
offered, no `V9XDISP.DRV` prompt, first boot `enable-ok` at 1024x768x16
(no 16-colour boot). But no Display Properties tab: the installer's Tag
is rejected by the shell and the RunOnce repair never runs. Fixed in
source (`V9xSyncModes` registers the page at every logon) and verified
on a fresh clone. Owed for B10: the other seven packages (no clean
guest with an agent exists for `vbe`, `ati` or `matrox` yet; the
physical ones need a clean machine) and the release candidate's zips.
INSTALL.TXT was rewritten for users the same day (prerequisites, steps,
the two tabs, reporting; the serial checkpoints moved to a testers'
section). Still bring-up text: FIRSTBOOT.TXT (an 86Box checklist) and
the first section of RECOVER.TXT (restore the cold VM backup); the
family descriptions at the top of each INSTALL.TXT come from the
manifests and are engineering text; the support tier (decision 2) is
not in it yet.

### 2026-11-24, later: B10 for ati, matrox and vbe; the tab's real cause

Fresh 86Box clones with the card changed
([record](../decisions/2026-11-24-1.0-b10-have-disk-ati-matrox.md)):
`ati` on a Mach64 VT2 and `matrox` on a Millennium install by Have Disk
with one model each, no `V9XDISP.DRV` prompt, and `enable-ok` at
1024x768x16. `vbe` installs on a ViRGE/DX but stops at
`fail-hardware-vbe-mode` (desktop 640x480), consistent with the open
linear-frame-buffer gap; `vbe` on a card with an LFB BIOS is still owed.
The logon repair above did not help on these clones: DESK.CPL seeds the
Tag from `OEMCPL.INF`, not from the neighbours, and on a failed Tag moves
a display driver's page under Settings, Advanced. Fixed (seed from
OEMCPL.INF, moved copy removed) and verified on a fresh Millennium
install and the cleanup path. One Millennium boot hung at the logo after
a WININIT staging, cause unknown. Owed for B10: `intel-gma`, `nvidia`,
`sis` and `3dfx` (no 86Box card for them), `vbe` on an LFB BIOS, and the
release candidate's zips.

### 2026-11-24, evening: B5, B9 and B10 `vbe` on 86Box

([record](../decisions/2026-11-24-1.0-b5-b9-86box.md)) B10: `vbe` by Have
Disk on a fresh Millennium clone reaches `enable-ok` at 1024x768x16 on
the BIOS's linear frame buffer. B5: the full-screen DOS box round trip
passes on `vbe`, `matrox` and `ati` (Mach64 VT2); the physical S3 card,
a Radeon and the reporter's case are still owed. B9: on 86Box Stand By
pauses the emulator, so no power is lost; `vbe` and `matrox` come back,
`s3` and `ati` leave the scanout off while S3's and ATI's own drivers
bring it back. Windows calls none of our monitor-power callbacks (D0
only since 2026-08-11) and sends no 4F10h; cause not found, filed. A
laptop is still owed. The `-VesaTrace` lines now carry the VESA function
and BX (trace builds only).

B9 later the same evening
([record](../decisions/2026-11-24-standby-monitor-power-s3-ati.md)): with
monitor D0-D3 advertised, Windows calls low before Stand By and D0 after
Resume. `s3` builds now advertise them, and the ViRGE/DX desktop comes
back after Stand By and after a monitor off/on cycle; the 2026-08-11 lost
display did not reproduce. `ati` now has a Mach64 D0 (CRTC_GEN_CNTL bits
2-3), run on the callbacks and after every VBE mode set, because Windows
called the callbacks on only one Mach64 boot in four; the VT2 desktop
comes back after Stand By (twice) and a monitor cycle. Physical cards and
Radeons are not run.

Gate item 2 measured on main on four 86Box guests (Trio64, Trio32,
Millennium, Mach64 VT2; the ViRGE/DX guest was in use):
no probe verdict moved since the baseline, and the Trio32's owed 16 bpp
run passes V9XFBCMP and V9XWPRS
([record](../decisions/2026-11-24-1.0-86box-gate-checks.md)). It is
repeated on the release candidate.

**Decision 1 settled (Michael, 2026-11-23):** a refresh-rate choice is
parked until a release after 1.1.

**Decision 2 settled (Michael, 2026-11-24):** every Windows 9x is
supported: 95, 98, 98 SE and Me. That adds blocker B14 (an install,
boot, probes and DOS box run on each). Machines that exist: the 486 VLB
Trio64 under Windows 95 and the RIVA 128ZX remote-help machine under
Windows 98. There is no 86Box guest of 95, 98 or Me and no Windows Me
machine, and Claude sessions have no Windows media, so the guests or
machines for B14 come from Michael.

### 2026-11-25: B10 `intel-gma` on the netbook

No 86Box card takes the `intel-gma`, `nvidia`, `sis` or `3dfx` INFs (the
`3dfx` INF claims only the VSA-100, `121A:0009`). The GMA 950 netbook was
put back on Windows' VGA driver and given released 0.60.0 by Have Disk
([record](../decisions/2026-11-25-1.0-b10-have-disk-intel-gma-netbook.md)):
one model, no `V9XDISP.DRV` prompt, stored depth 8 bpp before the
restart, first boot `enable-ok` at 640x480x8, tabs present, 1024x576x16
by V9XMSW. The machine has had the driver since September, so its old
INF, files and tab key were present; the tab result says nothing about a
fresh Intel machine. `GLIDE2X.DLL` and `GLIDE3X.DLL` were not replaced
over a newer dev build while the other six files were; cause not found.
Owed for B10: `nvidia`, `sis` and `3dfx` on physical cards that never had
the driver, `intel-gma` on such a machine if one appears, and the release
candidate's zips.

### 2026-11-26: B11 closed, the README carries the tiers, the GMA 900 run

- **GMA 900 (remote-help card outside the fleet):** a second 915GM,
  M-8Z30ST, passes every release probe on the 0.61.0 tree, pixel shader
  1.1 included. 3DMark 2000 ran out of video memory until the texture
  pool was opened to the 915GM, which needed the mini-VDD to stop
  reading the 915's BAR3 as a Gen4 GTT; then it completes
  ([record](../decisions/2026-11-26-gma900-m8z30st-release-probes.md)).
  It joins the release-check machine list; its 1.0 run is still the
  candidate's.
- **B11 closed:** every family's Status line names its cards' tiers.
- **Gate item 6, in part:** the README's six card tables have a 1.0
  tier column from decision 3, with the last run of each card no longer
  fitted. Owed: the "from 0.x" claims inside the rows, checked against
  each card's last run.
- **Waiting on Michael:** B9 (a laptop someone can wake), B10 `sis` and
  `3dfx` (86Box emulates neither the SiS 6326 nor the Voodoo4), B14
  (no Windows 95 or Me guest or install media here), B5's physical S3
  card, and the A8U4I5 card-swap round.

### 2026-11-26, later: gate item 4, the issue re-read

Every open record in `docs/issues/` re-read (134 in the index, and one
the index had missed): each has a "1.0 re-read, 2026-11-26" section and
the index's Open list is split by outcome. 35 closed, each on a cited
run, measurement or explanation (among them the Caicos hang, fixed in
the 0.61.0 tree; the T19 specular fix; the GT218 flips; the survey's
DS:0 writes; four found not to be ours). 43 stay as known issues, all
linked from the README's known issues, which gained entries for the
Rage XL and IIC, the S3D cards, the Millennium II, the GMA 900 and a
"seen once, not reproduced" list. 13 need a run on the release
candidate, listed by machine under the blockers. 39 moved after 1.0
into "Not in 1.0". Five stay with blockers B5, B6, B9 and B10.

Desk work, from the records and a search of `docs/` for later evidence;
nothing re-run. Two README lines were stale and are corrected: the
HD 5570 cursor square (fixed in the 0.61.0 tree, reporter owed) and the
Oland's T19 mismatch (fixed 2026-11-25).

### 2026-11-26, evening: three of the owed runs

([record](../decisions/2026-11-26-1.0-reruns-gt218-netbook-gm965.md))

- **GeForce 210, 3DMark 2000 Helicopter trees:** steady in 48
  consecutive frames where the pop was seen; closed.
- **GMA 950, GLQuake `-nomtex`:** nothing refused now, but the walls
  show only their lightmaps (the multitexture control draws correctly);
  a README known issue. The Rage XL run stays with the A8U4I5 round.
- **GM965, the 915 GTT fix:** all release probes pass and the GTT record
  matches boot 59's; the change leaves the GM965 as it was.
- Not run: Doom95 on the 86Box ViRGE, and the QEMU `vbe` guest, which
  boots but carries a September driver and cannot restart reliably;
  both left for gate item 2.

### 2026-11-26, night: gate item 6 written

Every README row whose tier is not "1.0 run owed" and does not already
say "last run on" now carries its last run's date (17 rows), taken from
the release-check folders, this plan and the machine records; the table
introduction says a "from 0.x" dates a feature, not a check. Two of
decision 3's "in the check fleet, probes passing recently" cards have
not run since: the X1200 (RS690) last ran 0.51.0 on 2026-11-14 and its
machine has been offline since; the GMA 4500MHD last ran 0.57.0 on
2026-11-19, offline since. **Decided (Michael, 2026-11-26): both stay
Supported on their last run**, stated so in the README, as the cards
no longer fitted are; a run on the candidate is welcome if either
machine returns. The "1.0 run owed" rows get their date from that run.
Desk work; nothing run.

### 2026-11-26, night: the HD 5450 in W9QI0DAD

Michael's HD 5450 (Cedar) in the FX-6300, the 0.61.0 tree's `ati` set
([record](../decisions/2026-11-26-1.0-hd5450-evergreen-check.md)): every
release probe passes, 3DMark 2000 runs to its score and Quake 3 runs.
The Caicos hang fix's "one other Evergreen card" is done. The README's
HD 5450 row moves from "last run on 0.33.0, card gone" to Supported,
run 2026-11-26. **B6 is not done:** this card has 320x240, and there
V9XCURSQ still finds the stale-desktop square; 640x480 is fixed.
Michael closed B6 for 1.0 on this: the 320x240 square is a README known
issue and its fix goes to 1.1.

### 2026-11-26, night: the A8U4I5 round starts, Rage XL

The Rage XL first ([record](../decisions/2026-11-26-1.0-ragexl-a8u4i5.md)).
Its devnode had been left on ATI's driver and Windows on a 16-colour
mode; both set back through the agent. On the 0.61.0 tree every
release probe passes except V9XD32's T20 (additive blend draws
nothing) and T22 (32-bit textures draw black), neither run on a
Mach64 before ([issue](../issues/2026-11-26-rage-xl-v9xd32-add-blend-and-32bit-textures.md));
3DMark 2000 runs to its score. The owed GLQuake `-nomtex` run: no
batch refused (October 111,915) and no invalid draw, but lightmaps
only, the GMA 950's 1.1 fault. Michael: the Rage XL's fixes go to a
later release; the two failures are README known issues in 1.0.

### 2026-11-26, night: Trio3D/2X and HD 5770

([record](../decisions/2026-11-26-1.0-trio3d2x-hd5770.md))

- **HD 5770** (Daniel refitted it in M-BXMFA8): every release probe
  passes and 3DMark 2000 completes, the third Evergreen card on the
  Caicos fix. **B7:** DirectDraw has 128 MiB here; the card's VBE
  BIOS reports 16 MiB, which is the reporter's figure. Their own
  files are still not in. Michael closed B7 on this measurement.
- **Trio3D/2X** (A8U4I5 round, second card): fails only V9XD32 T21
  and the Glide half blend and fog table, all README known issues;
  its first 3DMark 2000 run (640x480, the default needs 16 MB)
  completes, with 51 engine idle timeouts and resets
  ([issue](../issues/2026-11-26-trio3d2x-3dmark2000-engine-timeouts.md)).
  `BitsPerPixel` drifted to 4 and then 8 across the swaps; each card
  needs it checked.

### 2026-11-26, night: Rage IIC

The third card of the A8U4I5 round
([record](../decisions/2026-11-26-1.0-rageiic-a8u4i5.md)). Every
release probe passes but V9XD32's T4 and T16: off-screen render
targets get neither the clear (`E_NOTIMPL`) nor the draws. 3DMark
2000 at 640x480 stops after 1,340 flips in a loop of depth fills
([issue](../issues/2026-11-26-rage-iic-3dmark2000-depth-fill-loop-and-offscreen-targets.md)).
Both first runs on the card. Then two programs that ran in October:
Half-Life's Direct3D timedemo locks the machine (a regression;
power-cycled), and 3DMark 99 runs 24 of 26 tests and stops at Image
Quality in the same fill loop. Whether these hold the Rage IIC's 1.0
tier is Michael's call.

### 2026-11-26, night: the Rage IIC investigated

([issue](../issues/2026-11-26-rage-iic-3dmark2000-depth-fill-loop-and-offscreen-targets.md))
The 3DMark stall is the engine wedging (a wait-for-idle timeout whose
reset fails and quarantines the core), after which every depth fill
answers "still drawing" forever. Released 0.10.0 does the same, so it
is not a regression. The Half-Life lock goes with the agent's VNC
server: with it stopped Half-Life runs on both 0.10.0 and the 0.61.0
tree (5.2 fps); with it running both lock. **Decided (Michael,
2026-11-26): the Rage IIC's faults go to a later release**, as the
Rage XL's did; they stay README known issues in 1.0.

### 2026-11-27: Voodoo4, the first `3dfx` boot of the 1.0 work

The fourth card of the A8U4I5 round
([record](../decisions/2026-11-27-1.0-voodoo4-a8u4i5.md)). The card
came back in another PCI slot; Windows installed 3dfx's own INF over
Velocity9x's key, and a Have Disk install put Velocity9x back (on a
machine that had the driver before, so it is not B10's fresh-card
case). Every release probe passes but V9XD32's T22, 32-bit textures
drawing black, which 0.17.0 does too
([issue](../issues/2026-11-27-voodoo4-v9xd32-32bit-textures-draw-black.md)).
3DMark 2000 runs to its score and draws correctly on screen, on this
build and on 0.17.0. Its grabbed frames are corrupt because the
VSA-100 accepts no flips, so V9XTRACE's grabs and its "drew into the
page on screen" count do not show the screen on this family. Two
runner faults fixed on the way: Glide files staged from a package that
ships none, and a 3DMark freeze watch that counted only accepted flips.
The `3dfx` package is now booted (gate item 2). **Decided (Michael,
2026-11-27): the Voodoo4's T22 goes to a later release**; it stays a
README known issue in 1.0.

### 2026-11-27: SiS 6326, the first `sis` boot of the 1.0 work

The fifth card of the A8U4I5 round
([record](../decisions/2026-11-27-1.0-sis6326-a8u4i5.md)), card 2
(rev 0B, 4 MiB). It came up in VGA on the Voodoo4's files until the
runner installed the `sis` set. Every release probe passes but
V9XD32's T22 X8R8G8B8 texture, which draws black while A8R8G8B8 passes
([issue](../issues/2026-11-27-sis6326-v9xd32-x8r8g8b8-texture-draws-black.md));
the first T22 run on the card. Its first 3DMark 2000 run, by hand at
640x480, completes (1,633 flips, no engine reset) and draws correctly.
The `sis` package is now booted (gate item 2). **Decided (Michael,
2026-11-27): the SiS 6326's T22X goes to a later release**; it stays a
README known issue in 1.0.

### 2026-11-27: Rage 128 Pro

The sixth card of the A8U4I5 round
([record](../decisions/2026-11-27-1.0-rage128pro-a8u4i5.md)). Bound
through the Rage XL's class key (`Display\0007`; the `ati` files serve
both), it came up in VGA on the SiS files until the runner installed
the `ati` set. Every release probe passes but V9XD32's T22 X8R8G8B8
texture, black at every DDI level while A8R8G8B8 passes, the same
fault as the SiS 6326's
([issue](../issues/2026-11-27-rage128pro-v9xd32-x8r8g8b8-texture-draws-black.md)).
3DMark 2000 at its defaults runs to its score with no refusal, stall
or engine reset, its grabbed frames whole. **Decided (Michael,
2026-11-27): the Rage 128 Pro's T22X goes to a later release**, as the
SiS 6326's did; it stays a README known issue in 1.0.

### 2026-11-27: ViRGE/DX, and two owed re-runs closed

The seventh card of the A8U4I5 round
([record](../decisions/2026-11-27-1.0-virgedx-a8u4i5.md)).
`BitsPerPixel=4` cleared again. V9XD32 fails T20, T21 and T22 exactly
as the 86Box guest does; all three tests are newer than November's
PASS on this card, so none is a regression. 3DMark 2000 by hand runs
to its score at 640x480 with 45 engine idle timeouts and at 800x600
with none
([issue](../issues/2026-11-27-virgedx-3dmark2000-640x480-engine-timeouts.md));
the Trio3D/2X's 51 were also at 640x480. The two owed re-runs: Final
Reality runs clean on the silicon (the September speckle was 86Box's),
and 1024x768x32 displays correctly on a monitor through OBS, against
XFree86's ViRGE limit; both issues closed. **Decided (Michael,
2026-11-27): the 640x480 timeouts go to a later release**; they stay a
README known issue in 1.0.

### 2026-11-27: blockers B9, B10 and B14 closed for 1.0; branches parked

Michael's decisions: **B9** closed for 1.0, Stand By stated in the
README as measured (86Box, the Oland desktop) and the laptop moved to
the next release. **B10** closed: six packages done by Have Disk, and a
never-had-it install of `sis` and `3dfx` is more testing than 1.0
needs. **B14** closed for 1.0 by narrowing decision 2: **1.0 supports
Windows 98 SE only**. **Every unmerged branch** is parked for 1.0.
**B5** stays open: it was not run on the ViRGE/DX because of the
standing "no DOS box on A8U4I5" rule, which was not put to Michael; he
wants the round trip run on the Trio64 and the Trio3D when they are
fitted.

**Decision 4 (Michael, 2026-11-27): no release candidate and no soak.**
1.0.0 is released directly once the gate is met; field problems go
into 1.0.x. The plan's "on the release candidate" re-runs now mean the
1.0.0 build.

### 2026-11-27: Mystique

The eighth card of the A8U4I5 round
([record](../decisions/2026-11-27-1.0-mystique-a8u4i5.md)), bound
through the Millennium II key and in VGA on the S3 files until the
`matrox` set went in. Every release probe passes but V9XD32's T22
(32-bit textures black, as on the Voodoo4) and the Glide BlendHalf the
card has failed since October (no blender). Its first 3DMark 2000 run,
by hand at 640x480, scores; smoke draws as white boxes (the known
Matrox limit), and large areas of Adventure are black, with 911
textures refused for placement in 4 MiB
([issue](../issues/2026-11-27-mystique-3dmark2000-adventure-areas-missing.md)).
Whether the black areas hold the Mystique's 1.0 tier is Michael's call.
