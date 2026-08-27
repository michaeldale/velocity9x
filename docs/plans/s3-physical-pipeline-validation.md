# Validating the dynamic VBE pipeline is inert on physical S3 silicon

Status: executed (2026-08-27) — run on BARRY; results are in the 0.6.0
CHANGELOG entry and the
[CrystalMark baseline decision](../decisions/2026-08-27-crystalmark-barry-baseline.md).

## Context

Velocity9x 0.5.0 shipped the dynamic VBE pipeline (Stages 1-5 of
`docs/plans/dynamic-vbe-pipeline.md`): BIOS mode discovery, a transactional
runtime mode table, scan-contradicted baseline hiding, DirectDraw publication
from that table, the native Display Properties synchronizer, and EDID as a
fallback hint. Every guest gate passed on QEMU std-vga.

None of it has been exercised on physical S3 silicon. BARRY - S3 Trio64 86C764
(`5333:8811`), 2 MiB, Win98 SE, 32 MB RAM, reached at `10.0.1.47:9869` - is
available, and this plan uses it to prove the pipeline is **inert** on a
scan-disabled family, plus two zero-risk measurements only real hardware can
supply.

### Premise correction that shapes the whole plan

The question that prompted this was framed as: the plan declines boot-time VBE
calls on S3 until they are validated on S3 silicon. That is not what the plan
says, and the difference decides what BARRY is worth.

- **The reason is zero benefit, not missing validation.**
  `docs/decisions/2026-08-18-minivdd-vbe-collect-gating.md` §2: the `s3` family
  has a `read_aperture` hook, so the 4F9Ch cache is never consulted. The
  collection is "all risk and no benefit" for `s3`; the boot hang made that
  concrete rather than creating the argument.
- **The corpus's headline defects are VBE 2.0-weighted.** The lost 8x14 font
  hits all four 2.0 BIOSes and none of the 1.2 ones. The 360-wide FIFO fault
  hits only the two newest 2.0 BIOSes, Trio64V2/DX reference 2.04.07 and
  ViRGE/DX reference 2.01.16. Pinball Illusions hangs on those plus ViRGE/DX
  2.01.07 - Genoa 3.0 survives it - while on the 1.2 BIOSes the same defect
  degrades from a hang to a drawing fault
  (`docs/specifications/dos-vbe-conformance.md:98-110`). BARRY reports
  **VBE 1.2**, so it sits on the milder half of that table - milder, not
  spotless.
- **A clean BARRY run would unlock nothing.** All four measured S3 targets are
  VBE 1.2 with every mode at attribute `001B` (bit 7 clear), `PhysBasePtr=0` and
  `LinBytesPerScanLine=0` (`docs/decisions/2026-08-20-vbe-mode-inventory.md`,
  finding 2). Two independent filters refuse that by construction: the
  controller gate requires at least `0x0200` (`src/common/vbe_parse.c:151`), and
  `v9x_vbe_scan_accept` requires the linear attribute plus a physical base at or
  above 1 MiB. Enabling `MiniVddVbeCollect` for `s3` buys eight `Exec_Int 10h`
  boot calls and zero discovered modes, on the exact card that hung from that
  code path.

**Decisions taken.** No collection-enabled experiment build. The honest
prerequisite for ever enabling S3 collection is a **VBE 2.0 S3 board**, which
nothing in the fleet has; that is recorded here so a clean BARRY run is never
mistaken for clearance.

### What makes this more than a formality

The `s3` family manifest links `vbe_modes.c`, `edid.c` and **`modes16.c`**
(`packaging/families/s3/family.psd1`, `Build.Sources`). So the S3 driver does
build the runtime mode table at load and does write `C:\V9XMODES.INI` after a
successful enable - with the mini-VDD's collection assembled out, hence a zeroed
cache, hence a refused scan, hence all 12 baseline rows
(`src/chipsets/s3/s3_hw16.c:56-75`) published and nothing hidden. This is the
real-silicon instance of Stage 2's exit gate, that "a
scan-disabled or invalid-scan build is byte-for-behavior equivalent to the static
baseline path", which is currently proven only in host tests and on QEMU.

## Scope

Four blocks, all read-only or already-shipped-binary, ordered so the cheap
evidence lands first.

### Block 1 - baseline capture before touching anything

Record what BARRY is running now, so any later difference is attributable.

- `v9xctl info` for driver and build id; pull `C:\V9XHW.INI`, the driver's
  serial log, and `C:\V9XMODES.INI` if one already exists.
- Capture the pre-deploy registry state that block 2 will assert absences
  against: `HKLM\...\CurrentVersion\Run\V9xSyncModes`, any `V9xFamily` value on
  the display instances, any `V9xDynamic` or `V9xSyncGeneration`, and the
  `MODES` tree itself. An absence asserted without this baseline is an
  assumption, not evidence.
- Note whether BARRY holds 0.4.3 or 0.5.0. If 0.4.3, block 2 is the first 0.5.0
  boot on this card and its serial capture is the load-bearing artefact.

### Block 2 - deploy 0.5.0 `s3` and take the regression evidence

Build and deploy the shipping S3 package, then assert the pipeline is present
but contributes nothing.

Build with `scripts/build-active-package.ps1 -Family s3` (the script has no
`-ChipId` parameter; the package covers the family and BARRY's `5333:8811`
selects trio64 at install), deploy with
`scripts/update-associated-driver.ps1 -GuestHost 10.0.1.47 -Port 9869`, and
boot twice - the verification section counts two clean boots, so take both
serial captures here. If the deploy leaves BARRY unbootable, Safe Mode retains
the stock VGA fallback and reinstalling the baseline package recovers
(`docs/plans/dynamic-vbe-pipeline.md`, rollback strategy).

Assertions, ordered by what they would catch:

1. **No new BIOS calls.** The mini-VDD's serial output carries
   `V9X-MINI vbe-collect disabled` and no `vbe-collect start`, `vbe-call fn=` or
   `vbe-status ptr=` line. `scripts/build-minivdd-skeleton.ps1:259` asserts the
   marker at build time; this asserts the runtime consequence.
2. **`C:\V9XMODES.INI` describes a refused scan**, not a partial one, in the
   inventory's own field names (`src/display16/modes16.c:416-592`):
   `Scan state=` unavailable or invalid, `Table rows=12 published=12`, every
   row `src=baseline` and none `src=dynamic`, no `HiddenNN` key, `Edid=none`,
   `Complete=1`. Any `HiddenNN` key here is a defect - hiding requires a
   trustworthy scan and there is none.
3. **No synchronizer artefacts in the installed registry.** A scan-disabled
   family's INF emits neither the `V9xFamily` marker nor the `Run,V9xSyncModes`
   line, and `scripts/lib/inf.ps1:431,439` asserts both absent at build time.
   Confirm the installed side: no
   `HKLM\Software\Microsoft\Windows\CurrentVersion\Run\V9xSyncModes`, no
   `V9xFamily` on the display instance, no `V9xDynamic=1` on any `MODES` key and
   no `V9xSyncGeneration`.
4. **Mode matrix unchanged.** `scripts/run-vm-mode-matrix.ps1` cannot address
   BARRY, because the family's VM target is an emulator port, so drive the
   trio64 chip's `Modes` list by hand through the agent - the 12-row baseline
   minus the two rows that do not fit 2 MiB, which is 10 bootable modes:
   640x400x8 (the Doom95 row), 640x480 and 800x600 at 8/16/32, 1024x768 at
   8/16, and 1280x1024x8. (Do not use `Vm.Modes` as the checklist: it carries
   1280x1024x16 and omits 640x400x8, because it is sized for the 4 MiB
   emulated targets.) Reboot between modes, per the caveat below. Allow each
   capture to settle: the screenshot race on this machine already produced one
   withdrawn regression report
   (`docs/issues/2026-08-20-barry-tiling-was-a-screenshot-race.md`).
5. **VRAM refusal still holds - and this run settles a contradiction in the
   tree.** `packaging/families/s3/family.psd1:285-292` claims
   `v9x_vbe_vram_reported` is permanently zero on `s3` so `ValidateMode`'s
   memory test is inert; `src/display16/enable16.c:719-731` assigns it from
   the family's CR36 hook (`src/chipsets/s3/s3_hw16.c:96-99`) after a
   successful enable, and `s3_hw16.c:46-49` says the refusal is live. The code
   reads as live; BARRY is the experiment that proves it on silicon. Assert:
   the inventory's `Vram reported=` line shows the CR36-decoded 2 MiB (proof
   the hook ran), and 1024x768x32 (3 MiB) and 1280x1024x16 (2.5 MiB) are
   refused. The refusal is a plain `V9X_VALMODE_NO_NOMEM` return
   (`src/display16/ddi.c:1086-1089`), deliberately indistinguishable from
   "not in the table" - do not expect a staged trace; that path belongs to the
   separate hardware check at `ddi.c:1055-1063`. Whichever way it lands, the
   losing comment gets corrected (deliverables).
6. **Text mode returns intact**, 8x14 font included, after a mode set and
   release. BARRY is VBE 1.2, so per the corpus this should need no fixer -
   worth asserting once on the silicon rather than inferring it from the table.

**Caveat that bounds this block.** Live mode switching is already broken on this
card and has been since 0.4.3:
`docs/issues/2026-08-20-live-mode-switch-no-repaint-barry.md` is open and
undiagnosed, reproduces at 800x600x32 and on a live 8-to-16 switch at 1024x768,
and does not reproduce under emulation. Reboot-into-mode is clean. Block 2
therefore tests reboot transitions only, and a live-switch failure here is that
known bug rather than a 0.5.0 regression. This is also why Stage 2's
"survives live same-depth switching" gate remains QEMU-only evidence; closing
that bug is separate work and deliberately not in scope.

### Block 3 - a real monitor EDID

Every EDID this project has parsed is QEMU's synthetic block, and even that was
unstable: preferred 1280x800 in the 2026-08-23 DOS survey against 1024x768 at
the Stage 5 guest boot. BARRY drives a real CRT.

`tools/diag/vga_survey_dos.c:1448` already does the whole job: the 4F15h BL=00h
capability probe, then BL=01h for block 0, plus block 1 when the extension count
is non-zero. Query-only: no mode set, no driver involvement, no boot risk.

Build with `scripts/build-vga-survey.ps1`, run from DOS on BARRY, and keep the
report beside the existing `docs/decisions/2026-08-20-vbe-inventory-barry.txt`.

Both outcomes are useful:

- **DDC present** gives the first real EDID block `src/common/edid.c` has been
  offered; add it to `tests/host/test_edid.c` as a fixture. Do not commit the raw
  block with its serial number - the survey tool's privacy warning and the
  pipeline plan's diagnostics rule both apply.
- **DDC absent**, which is likely on a VBE 1.2 S3 ROM, records the
  "unavailable DDC is a no-op" case on real silicon instead of assuming it.

Re-dump the mode list on the same run, to confirm the 18-mode, VBE 1.2,
`0x0211`-only-on-BARRY fixture still holds.

### Block 4 - the synchronizer's ambiguous-devnode path

Stage 4's unique-instance matching was tested on a clean QEMU guest. BARRY
carries a stale Cirrus `Display\0000`
(`docs/decisions/2026-08-18-minivdd-vbe-collect-gating.md:51`), a genuine second
display instance, which is the "ambiguous devnodes" case in the pipeline plan's
Stage 4 test list, with the no-op reason among its required diagnostics.

`V9xSyncModes` has a read-only mode: any command line containing `rep`
(a case-insensitive substring scan, `tools/diag/settings_syncmodes.c:648-660`)
sets the dry-run flag, which opens the class key `KEY_READ` only
(`settings_syncmodes.c:677-678`) and writes nothing but its report.

```text
rundll32.exe v9xsetp.dll,V9xSyncModes report
```

Drive it with the agent's `shell` action, not `exec`; `exec` returns before
rundll32 finishes, as the pipeline plan's Stage 4 operational notes record.
Expect `DryRun=1` and a no-op. Because `s3` emits no `V9xFamily` marker the run
should stop at instance matching rather than at inventory loading, so the reason
code distinguishes "no marked instance" from
`inventory-missing-or-incomplete` - and which one appears is the finding. Re-read
the report after a beat: it goes through the Win9x profile cache and a too-quick
read catches a partial flush.

## Verification

This plan is satisfied when:

- BARRY boots 0.5.0 clean twice, with no `vbe-collect start` or `vbe-call` line
  anywhere in the serial capture;
- `C:\V9XMODES.INI` shows `Table rows=12 published=12`, every row
  `src=baseline`, no `HiddenNN` key, `Edid=none`, a `Vram reported=` figure of
  2 MiB, and `Complete=1`;
- the installed registry has no `Run\V9xSyncModes`, no `V9xFamily`, no
  `V9xDynamic` key and no `V9xSyncGeneration` - measured against the block 1
  baseline, not assumed;
- all 10 fitting baseline modes boot and capture correctly, Doom95's 640x400x8
  included, and the two oversized rows are refused by `ValidateMode` with
  `V9X_VALMODE_NO_NOMEM`;
- text mode and its 8x14 font survive a mode set and release;
- the DOS survey's mode list still matches the 2026-08-20 BARRY fixture, and its
  EDID section is recorded either way;
- the `V9xSyncModes` dry-run mutates nothing and names its reason.

## Deliverables

- A `docs/decisions` note carrying the regression evidence and the explicit
  statement that a **VBE 2.0 S3 board, not BARRY, is the prerequisite** for
  enabling `MiniVddVbeCollect` on `s3` - with the 1.2-versus-2.0 asymmetry and
  the zero-benefit `read_aperture` argument recorded together, so the question
  cannot be reopened from the wrong premise.
- The BARRY survey report beside the existing inventory dump, and a new EDID
  host fixture if DDC answered, serial number excluded.
- A note in `docs/plans/dynamic-vbe-pipeline.md`'s regression-targets section
  recording which S3 items are now physically verified - and, in the same
  edit, correcting that section's claim that the VLB 486's Trio64 is "the only
  VBE 1.2 target this project can reach" (`dynamic-vbe-pipeline.md:1136`);
  BARRY is a reachable VBE 1.2 Trio64 too.
- The stale VRAM-test comment at `packaging/families/s3/family.psd1:285-292`
  corrected to match `enable16.c:719-731` and the BARRY evidence, whichever
  way block 2's item 5 lands.
- `docs/issues/2026-08-20-live-mode-switch-no-repaint-barry.md` updated only to
  confirm the fault still reproduces on 0.5.0. No diagnosis attempted.

## Explicitly out of scope

- Enabling `MiniVddVbeCollect` for `s3` in any form.
- Diagnosing the live-repaint bug.
- The S3 pedestal black-level bit
  (`docs/issues/2026-08-23-s3-pedestal-black-level.md`), untouched and
  unrelated.
- Any registry mutation on BARRY beyond what the driver install itself performs.
