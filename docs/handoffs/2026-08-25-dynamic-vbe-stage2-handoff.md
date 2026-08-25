# Dynamic VBE Stage 2 handoff — runtime table live, Stage 1 gate closed

Date: 2026-08-25
Branch: `dynamic-vbe-stage0`

This continues the Stage 1 handoffs
([QEMU/guest](2026-08-23-qemu-win98-stage1-guest-handoff.md),
[fresh install](2026-08-25-qemu-win98-fresh-install-current-state.md) — read
that one's resolution addendum for how the root VGA devnode conflict was
fixed). Both Stage 1 and Stage 2 of
[`docs/plans/dynamic-vbe-pipeline.md`](../plans/dynamic-vbe-pipeline.md) are
now implemented and guest-verified; the plan doc and CHANGELOG carry the
details in this commit.

## Test guests

Two working Windows 98 SE guests, both with V9x Remote Agent 0.6.1:

- **Local QEMU 4.2 fresh VM** — `127.0.0.1:9872`, launcher
  `build\vm-clean\launch-fresh-install.ps1 -Phase Transfer`, disk
  `win98se-stage1-fresh.qcow2` (backups listed in the fresh-install handoff).
  This VM ran the Stage 1 gate and all Stage 2 verification. It currently has
  the **stage2 build installed** (staged via a WININIT.INI rename of
  `V9XDISP.DRV`, `V9XMINI.VXD`, `V9XHAL.DLL`; package pushed to
  `C:\V9XREMOTE\JOBS\STAGE2`), and was left running at 800x600x16 with test
  windows open.
- **UTM (Mac) VM** — `10.0.1.250:9869`, Michael starts/stops it from the Mac.
  Runs the stage1 build; its guest copy of `VELOCITY9X.INF` was hand-patched
  with the SUBSYS-qualified id before the generator fix landed. Re-push a
  freshly generated package before using it for driver work again.

Agent driving notes (also in auto-memory `utm-win98-vm`): GUI test apps block
`exec` until their dialog is dismissed — run them `-Detach -ShowWindow`, then
`screenshot` (BMP output, convert before viewing) and dismiss via
`input -Sequence 'key ENTER'` or absolute mouse `move X,Y; click left`.

## What Stage 2 added (this commit)

- `include/velocity9x/vbe_modes.h`, `src/common/vbe_modes.c`: admission
  reason codes (`v9x_vbe_scan_admit`), the refuse-only family distrust
  predicate and per-reason tallies (`v9x_vbe_build_mode_table_ex`), and the
  publication/hiding rule (`v9x_vbe_publish_rows`). The `V9X_VBE_ATTR_*` /
  `V9X_VBE_MODEL_*` / `V9X_VBE_MIN_PHYS_BASE` constants moved from
  `vbe_parse.c` into `vbe_parse.h`.
- `src/display16/modes16.c` (new): DGROUP runtime table, transactional init
  (baseline first, all-or-nothing commit), `C:\V9XMODES.INI` inventory with
  `Complete=0/1` sentinel discipline.
- `src/display16/ddi.c`: `v9x_modes`/`V9X_MODE_COUNT` repointed to the
  runtime table; `v9x_find_mode` honours publication; fallbacks follow the
  first published row; inventory written after enable-ok.
- All four family manifests link `vbe_modes` + `modes16`; non-scanning
  families commit a byte-identical baseline table. `scripts/check-tree.ps1`
  allowlists `modes16.c` as an OS boundary (it writes the inventory INI).
- INF generator (`scripts/lib/inf.ps1` + vbe manifest + spec doc): a chip may
  declare `SubsystemId`; the model line then leads with the SUBSYS-qualified
  hardware id and keeps the bare id as the compatible id.
- Host tests: `test_admit_reasons`, `test_build_ex_reasons_and_distrust`,
  `test_publish_hides_contradicted_baseline`, `test_publish_fallback_rules`.

`scripts\run-checks.ps1` is fully green (tree check, safety gate, host tests
on both compilers, all four family packages). Remember
`$PSNativeCommandArgumentPassing = 'Legacy'` before package builds, and never
pipe/redirect the build scripts' output in PowerShell 5.1 — wrapping ML.EXE's
stderr banner kills the build spuriously.

## Guest evidence (local VM, boot 10)

- `C:\V9XBOOT.INI`: `Stage=enable-ok`, `VbeCache=s=1835 l=92 q=74 c=64 p=0
  f=0047`.
- `C:\V9XMODES.INI` (fetched to `build\vm-clean\stage2-v9xmodes.ini`):
  `Complete=1`, 46 rows = 7 baseline + 39 dynamic, all published, `first=0`,
  `Reasons=39,0,0,0,0,18,0,0,0,0,7,0,0` (OK, depth, duplicate — sums to the
  64 cached records), scan state 2 (merged, hiding disabled because the cache
  is full/truncated — the correct trustworthiness verdict on this BIOS).
- Live switching: `V9XMSW /set:1152x864x16` (dynamic, mode 014a) rendered a
  perfect desktop and passed `V9XGDI` there; then `/set:1280x800x16` and back
  to baseline `/set:800x600x16`, all live, windows intact. Screenshots:
  `build\vm-clean\stage2-1152.png`, `stage2-gdi.png`, `stage2-800.png`.

## Stage 1 gate closure (same day, for the record)

The guest dump vs DOS inventory comparison passed on both guests: 64/64
records exact (mode, attributes, geometry, depth, memory model, pitch, base
`FD000000`, RGB masks), list order preserved, absences explained
(legacy/planar modes, 15-bpp policy plus mode 0013, and the 64-cap tail with
the overflow flagged). Comparison script:
`scratchpad compare-vbe.ps1` pattern — parse `VbeModeNN` against
`personal/v9x-qemu-stdvga/QSTDVGA.INI` `Mode.NN` records. One footnote: the
mini-VDD counts `l=92` listed on QEMU 4.2 where the DOS tool counted 93 (the
UTM BIOS lists 93 and matched too); every record still matches, the counter
difference is unexplained and worth five minutes someday.

## Stage 3 addendum (2026-08-26)

Stage 3 is done in the following commit: `dd16.c` publishes the runtime
table's published rows with their real masks, subsetting 46 down to 32 via
the now publication-aware `v9x_vbe_dd_subset`, guaranteeing the active
desktop row, and refilling on every driver-object refresh. Guest evidence in
`build\vm-clean\stage3-v9xdd*.ini`: the DirectDraw probe `Result=COMPLETE`
with 8/16/32-bpp desktops (32 bpp on a dynamic row), SetDisplayMode across
depths, primary/blits/flips/restore clean, exact pitch and 5:6:5 masks.
`FlipPixelOk=0` stays the known tracked result; DDRAW's merged GBL reports
33 modes against the driver's 32 (DDRAW-side merge, recorded in the plan).
The guest runs the stage3 build and was left at 800x600x16.

## Known items and next steps

1. ~~Stage 3 — DirectDraw publication~~ — done, see addendum above. Next is
   Stage 4 (native Display Properties synchronization via `v9xsetp.dll`'s
   `V9xSyncModes`) per the plan.
2. **Hiding on real hardware**: the GMA950 netbook (MICHAEL-NETBOOK) is the
   machine whose scan will actually contradict baseline rows; the host tests
   pin the rule until then.
3. The **UTM guest's INF** is the hand-patched copy; re-push a regenerated
   package before more driver work there.
4. `build\vm-clean\setup-source.raw` (8 GiB) remains deletable; the abandoned
   `win98-clean-agent061b.qcow2` overlay can be deleted or kept for
   forensics.
5. 24-bpp support, refresh-rate truthfulness and EDID remain deliberately out
   of scope per the plan.
