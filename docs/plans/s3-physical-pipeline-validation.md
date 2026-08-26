# Validating the dynamic VBE pipeline is inert on physical S3 silicon

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
  `docs/decisions/2026-08-18-minivdd-vbe-collect-gating.md` §2: both S3 chips
  have a `read_aperture` hook, so the 4F9Ch cache is never consulted. The
  collection is "all risk and no benefit" for `s3`; the boot hang made that
  concrete rather than creating the argument.
- **The corpus's headline defects are VBE 2.0-specific.** The 360-wide FIFO
  fault, the lost 8x14 font and the Pinball Illusions crash appear on ViRGE/DX
  reference 2.01.07 / 2.01.16, Genoa 3.0 and Trio64V2/DX reference 2.04.07 - and
  on none of the VBE 1.2 BIOSes of the same silicon
  (`docs/specifications/dos-vbe-conformance.md:94-112`). BARRY reports
  **VBE 1.2**, so it is the benign half of that table.
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
cache, hence a refused scan, hence all seven baseline rows published and nothing
hidden. This is the real-silicon instance of Stage 2's exit gate, that "a
scan-disabled or invalid-scan build is byte-for-behavior equivalent to the static
baseline path", which is currently proven only in host tests and on QEMU.

## Scope

Four blocks, all read-only or already-shipped-binary, ordered so the cheap
evidence lands first.

### Block 1 - baseline capture before touching anything

Record what BARRY is running now, so any later difference is attributable.

- `v9xctl info` for driver and build id; pull `C:\V9XHW.INI`, the driver's
  serial log, and `C:\V9XMODES.INI` if one already exists.
- Note whether BARRY holds 0.4.3 or 0.5.0. If 0.4.3, block 2 is the first 0.5.0
  boot on this card and its serial capture is the load-bearing artefact.

### Block 2 - deploy 0.5.0 `s3` and take the regression evidence

Build and deploy the shipping S3 package, then assert the pipeline is present
but contributes nothing.

Build with `scripts/build-active-package.ps1 -Family s3 -ChipId trio64`, deploy
with `scripts/update-associated-driver.ps1 -GuestHost 10.0.1.47 -Port 9869`.

Assertions, ordered by what they would catch:

1. **No new BIOS calls.** The mini-VDD's serial output carries
   `V9X-MINI vbe-collect disabled` and no `vbe-collect start`, `vbe-call fn=` or
   `vbe-status ptr=` line. `scripts/build-minivdd-skeleton.ps1:259` asserts the
   marker at build time; this asserts the runtime consequence.
2. **`C:\V9XMODES.INI` describes a refused scan**, not a partial one: scan state
   unavailable or invalid, 7 runtime rows, 7 published, 0 hidden, 0 dynamic,
   EDID unavailable, `Complete=1`. Any hidden row here is a defect - hiding
   requires a trustworthy scan and there is none.
3. **No synchronizer artefacts in the installed registry.** A scan-disabled
   family's INF emits neither the `V9xFamily` marker nor the `Run,V9xSyncModes`
   line, and `scripts/lib/inf.ps1:431,439` asserts both absent at build time.
   Confirm the installed side: no
   `HKLM\Software\Microsoft\Windows\CurrentVersion\Run\V9xSyncModes`, no
   `V9xFamily` on the display instance, no `V9xDynamic=1` on any `MODES` key and
   no `V9xSyncGeneration`.
4. **Mode matrix unchanged.** `scripts/run-vm-mode-matrix.ps1` cannot address
   BARRY, because the family's VM target is an emulator port, so drive the
   family's `Vm.Modes` set by hand through the agent: 640x480, 800x600 and
   1024x768 at 8/16/32, 1280x1024x8, and the Doom95 640x400x8 row. Reboot
   between modes, per the caveat below. Allow each capture to settle: the
   screenshot race on this machine already produced one withdrawn regression
   report (`docs/issues/2026-08-20-barry-tiling-was-a-screenshot-race.md`).
5. **VRAM refusal still holds.** `ValidateMode` refuses 1024x768x32 (3 MiB) and
   1280x1024x16 (2.5 MiB) on the 2 MiB card, and a forced attempt fails staged
   with a trace and the VGA fallback rather than a black screen.
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
display instance, which is the case the pipeline plan calls "ambiguous devnodes"
and requires to be a recorded no-op.

`V9xSyncModes` has a read-only mode: any command line containing `rep`
(`tools/diag/settings_syncmodes.c:648-659`) opens the class key `KEY_READ` only
and writes nothing but its report.

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
- `C:\V9XMODES.INI` shows 7 runtime, 7 published, 0 hidden, 0 dynamic, EDID
  unavailable and `Complete=1`;
- the installed registry has no `Run\V9xSyncModes`, no `V9xFamily`, no
  `V9xDynamic` key and no `V9xSyncGeneration`;
- every family mode boots and captures correctly, Doom95's 640x400x8 included,
  with the two oversized rows refused by `ValidateMode`;
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
  recording which S3 items are now physically verified.
- `docs/issues/2026-08-20-live-mode-switch-no-repaint-barry.md` updated only to
  confirm the fault still reproduces on 0.5.0. No diagnosis attempted.

## Explicitly out of scope

- Enabling `MiniVddVbeCollect` for `s3` in any form.
- Diagnosing the live-repaint bug.
- The S3 pedestal black-level bit
  (`docs/issues/2026-08-23-s3-pedestal-black-level.md`), untouched and
  unrelated.
- Any registry mutation on BARRY beyond what the driver install itself performs.
