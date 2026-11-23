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

1. **Freeze.** From the first release candidate: no new PCI ids, no new
   interfaces, no new default behaviour. Unmerged branches
   (`rv280-geomscan`, `ati-cayman`, `wip-caicos-6779`, `hsw-panel-fit`,
   the Doom 3 worktree) merge before the freeze or wait for 1.1.
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
3. **Release candidates** as GitHub prereleases, which the update server
   puts on the beta channel. A soak with the remote-help owners and the
   GitHub reporters, then 1.0.0.
4. **After 1.0:** 1.0.x carries fixes only. New chips and interfaces go
   to 1.1 betas.

## Blockers

Fix, or measure and document, before the first release candidate.

| # | Item | State on 2026-11-21 | Done when |
|---|---|---|---|
| B1 | A write Lock of the back buffer does not wait for a pending flip ([issue](../issues/2026-11-21-haswell-lock-does-not-wait-for-pending-flip.md)) | Closed 2026-11-23: fixed, FlipSafety `held` on the HD 4600 and the Oland 6610 | dispbench FlipSafety judges Lock as it does draws, on the HD 4600 and one non-Intel card |
| B2 | The `ati` gamma hook writes Mach64 DAC offsets into other chips' register windows ([issue](../issues/2026-11-09-ati-gamma-hook-writes-mach64-dac-on-evergreen.md)) | Closed 2026-11-23: only the Mach64-class rows claim gamma; refused on the Rage 128 (host test) and the Oland (measured) | The hook is per engine; a ramp measured on the Rage 128 and one Evergreen-or-later part, or refused there |
| B3 | Engine-placed render targets are never freed ([issue](../issues/2026-11-15-placed-render-targets-never-freed.md)) | Found by reading; seven engines | A create/destroy loop in a probe leaves the heap where it started on two engines |
| B4 | DirectX 8 MIPFILTER read with DirectX 7's numbering ([issue](../issues/2026-11-19-ddi8-mip-filter-read-as-dx7.md)) | The index says host-tested, not run; the 0.57.0 notes name a DDI 8 mip fix, so this may be a stale line | The record says which |
| B5 | Full-screen DOS box and return ([issue](../issues/2026-09-12-netbook-dos-box-return-hardlock.md)) | Run on the GMA 950, the HD 4600 and the 86Box ViRGE/DX; issue 16's reporter fails on `vbe` too | A round trip on one physical S3 card, one `ati` Radeon and `vbe`; the reporter's case explained |
| B6 | A square follows the cursor over a full-screen game with the desktop copy ([issue](../issues/2026-11-13-evergreen-cursor-square-over-fullscreen-game.md)) | The reporter confirms `NOSHADOW.TXT` removes it; not fixed | Fixed, and confirmed by the reporter or reproduced on the R3E or R7E |
| B7 | HD 5770 offers 16 MB where 128 is expected (GitHub issue 21) | Waiting on the reporter's `V9XBOOT.INI` and `V9XHW.INI` | Explained from the files, or the row marked Experimental |
| B8 | Two Direct3D processes at once on the HAL's shared statics ([issue](../issues/2026-10-06-hal-statics-shared-across-processes.md)) | Closed 2026-11-23: DDHELP faulted on another process's draw ([issue](../issues/2026-11-23-ddhelp-destroysurface-runs-another-process-draw.md)); fixed, and UT2004 with the probes beside it passes on the Oland and the HD 4600 | Two probes run together on one Intel and one `ati` machine, recorded |
| B9 | Suspend and resume ([issue](../issues/2026-11-06-no-mode-restore-after-resume.md)) | Untested on any machine | Measured on one laptop; fixed, or a README line saying what happens |
| B10 | The install path: Have Disk from the released zip, the 16-colour first install ([issue](../issues/2026-11-07-first-install-stays-at-16-colours.md)), the V9XDISP.DRV prompt ([issue](../issues/2026-08-29-inf-asks-for-v9xdisp-drv.md)) | Release checks install by file rename, never from the archive | One Have Disk install per package from the signed zip, on a guest or machine that has never had the driver |
| B11 | Every package README reads "HOST-AUDITED; GUEST ACTIVATION NOT YET TESTED" ([issue](../issues/2026-08-30-package-status-string-is-hardcoded.md)) | Check record done 2026-11-23 (each family's manifest); the tier waits on decision 3 | The line states the package's tier and its check record |
| B12 | `C:\V9XDIAG\V9XGLD3.LOG` grows without limit; the other diagnostic logs unaudited | Known issue in 0.60.0 | Every log the driver writes by default is bounded, listed in one place |
| B13 | The updater's rollback, and an update taken on physical hardware | STATUS.md says not exercised, as of 0.23.0; unchecked since | 0.60.0 to the release candidate through Check for updates on one machine; one rollback from the backup |

Not blockers, but re-run for the record before 1.0: the RV630/RV610
constant-Z after the first CP start, the R3E's first-clear timeout, the
Rage 128's 3DMark 2000 hang (0.32.0, one run in two), the three
unreproduced hard locks on A8U4I5 and the Gateway. Each either gets a
measurement on the release candidate or stays a named known issue.

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
5. `docs/STATUS.md` rewritten against the release candidate.
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
- **To test for 1.1:** UT2004's graphical issues on the Oland
  ([issue](../issues/2026-11-23-oland-ut2004-graphical-issues.md)),
  reported, not yet described or measured.
- **Parked work:** the Rage 128 AGP ring, Gen4 ring flips, Gen3 batch
  cost, the Doom 3 ARB path, PAL8 on the Rage Pro.
- **Performance:** the GCN drain, copy and flip-wait records of
  2026-11-18, OpenGL throughput, Direct3D core throughput.
- **New chips:** GitHub issues 20, 27, 28 and 30; Cayman; Caicos 6779.

## Decisions owed (Michael)

1. **Refresh rate choice.** Left out above. 60 Hz only matters on a CRT,
   and the VBE 3.0 CRTC builder is host-tested and unused
   ([issue](../issues/2026-11-06-no-refresh-rate-choice.md)).
2. **Windows versions.** Proposed: Windows 98 SE supported; Windows 98
   first edition (the RIVA 128ZX machine), Windows 95 (S3 VLB only) and
   Windows Me (one reporter) stated as run, not supported.
3. **Tier of each card.** The list under Shape is a first reading.
4. **Length of the soak**, and whether 0.61.0 is the first candidate or
   one more ordinary release.

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
