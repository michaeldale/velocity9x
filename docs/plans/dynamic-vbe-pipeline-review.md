# Review of docs/plans/dynamic-vbe-pipeline.md

Status: closed — this is a point-in-time review, not a plan; the reviewed
pipeline has since shipped in 0.5.0. Renamed on 2026-08-28 from
`for-tier-0-issue-we-concurrent-mist.md`, a garbled dictation-artifact
filename.

## Context

Michael asked for a review of the dynamic VBE pipeline plan in the velocity9x repo
(`C:\everything\velocity9x\docs\plans\dynamic-vbe-pipeline.md`). The plan addresses the tier-0
blocker from the VOGONS research: the generic VBE family queries a hardcoded seven-mode VESA
list, which on panel-filtered Intel iGPU BIOSes (GMA950 survey,
`personal/v9x-intel950/V9XINTL.TXT`) leaves most offered modes nonexistent and the good
OEM-numbered modes (1024x576 at `0x160`-`0x162`) unreachable.

Every "current state" claim in the plan was verified against the source tree and is accurate:
the seven-mode list at `src/minivdd32/loader.asm:55-56` with no VideoModePtr walk, API v1 with
four ops, `v9x_vbe_scan_entry` / `v9x_vbe_build_mode_table` / `v9x_vbe_dd_subset` already in
`src/common/vbe_modes.c` with boolean-only admission, `ddi.c`/`dd16.c` consuming
`v9x_hw16.modes` with depth-synthesized masks (`dd16.c:117-152`), `V9X_MODE_TABLE_MAX=64` /
`V9X_DD_MODE_COUNT=32`, ABI already carrying `mode_count` + 32 slots, both host test files
present with strong coverage, `MiniVddVbeCollect` default-on for ati+vbe and `$false` for
s3+matrox-m2, and `v9xsetp.dll` already exporting a rundll32-pattern entry (`V9xRegisterPage`,
`tools/diag/settings_propsheet.c:447`, wired at `scripts/lib/inf.ps1:293,400`). The plan is
well grounded. DGROUP cost is a non-issue: 64 rows x 14 bytes + 64 masks x 12 bytes = 1664
bytes, already documented in `vbe_modes.h:26-28`.

## Findings (ranked)

### 1. Stale baseline rows survive publication on the motivating hardware

The runtime-table transaction copies baseline rows verbatim and lets scanned rows only update
or append. Nothing demotes a baseline row the scan proves dead. Invariant 7 additionally bans
pruning INF baseline registry keys, and line 286 defers quarantine of failed sets.

Consequence on the GMA950 (the hardware class that motivated this work): 5 of 7 baseline rows
are dead - `0x100` is not even enumerated (Doom95 row), and `0x103`/`0x105`/`0x114`/`0x117`
report `Attributes=0000`. GDI, DirectDraw and native Display Properties will offer
800x600/1024x768 forever; each user selection fails at `4F02h` into staged failure + restore.

**Michael's decision: hide, keep for fallback.** Baseline rows stay in the runtime table for
transaction fallback and recovery, but when the list walk is valid and complete,
scan-contradicted rows are excluded from publication: `ValidateMode`, requested-mode
selection, the DirectDraw subset and the mode inventory all skip them. This preserves the
immutable-baseline invariant literally (invariant 1 unchanged), at the cost of a per-row
publication flag. Amendment must specify: (a) exclusion applies only after the full list
validity gate passes - invalid/absent scan publishes every baseline row as today; (b) if
baseline row zero itself is scan-contradicted, boot/fallback selection uses the first
published row instead (row zero remains in storage as last-resort recovery); (c) the
inventory records each hidden row with reason `scan-contradicted` so the registry
synchronizer can decline to create its dynamic key while never touching the INF baseline key
(invariant 7 unchanged).

### 2. 24 bpp is new depth bring-up smuggled in as admission policy

`display16` has never run 24 bpp: no family baseline table has a 24-bpp row, `ddi.c:785-789`
is a three-way 8/16/else split, and the DIBENG comment at `ddi.c:776-783` notes there is no
flag for 24/32 (relies on `biBitCount`). The shared judgement code tolerates 24 bpp, but the
plan's policy line "24 bpp must be packed RGB 8:8:8" admits a depth the driver has never laid
out, blitted or flipped. QEMU std-vga *does* publish 24-bpp modes, so this lands in the Stage
2 exit gate immediately - it is a blocking dependency, not a corner.

**Michael's decision (confirmed): reject 24 bpp initially**, exactly like 5:5:5 (plan line 259 already
has the right template). Amendment: move 24 bpp from the accepted list to the
rejected-until-implemented list, keep the 24/24 host-test derivation cases, and drop 24 from
the Stage 2/3 QEMU exit-gate matrices (8/16/32 only). 24-bpp bring-up becomes a separate
follow-up.

### 3. No Intel physical target in the verification matrix

The matrix covers QEMU std-vga, ATI (Mach64 VT2 guest + Rage Mobility physical), and S3/Matrox
regression. QEMU's Bochs VBE publishes standard numbers, unfiltered - it cannot exercise:
panel-filtered mode lists, OEM-only mode numbers, `VideoModePtr` pointing into the controller
buffer (a hazard the plan itself warns about at line 125-127), or a real panel EDID for Stage
5. The 945GM netbook (agent 1036, Win98 USB image, survey already in the repo) is the
motivating machine.

**Michael's decision (confirmed): add the 945GM netbook as an exit gate for Stages 2 and 5.** Wave 1
(after Stage 2): mini-VDD dump matches the DOS survey record-for-record; 1024x576 x8/16/32
admitted, dead baselines dropped per finding 1. Wave 2 (after Stage 5): EDID hint selects
1024x576 on fallback. Netbook-specific gotchas from memory: drive it via the v9x agent, USB
drivers must keep their renamed state, MaxPhysPage constraint - see
`v9x-netbook-usb-image` memory note.

### 4. OS scope is silent: dynamic discovery is Win98-only by construction

The pipeline depends entirely on the mini-VDD for collection, and the VLB handover
(`docs/handoffs/2026-08-22-vlb-manual-select-handover.md`) established that the mini-VDD does
not load on Win95 - the manual-select model deliberately names none. The plan never says
"Win95 keeps the static table". That is the right outcome but should be one stated sentence in
the plan (and a diagnostic distinction: "no mini-VDD" on Win95 is expected, not a defect).

### 5. Minor points

- **Whole-list invalidation on one malformed entry** (line 122-123): a single listed value
  with high flag bits nukes all dynamic discovery. Conservative-fail is the project
  philosophy, but skip-and-count (with a diagnostic) would be more robust against sloppy
  BIOSes without weakening any bound. Suggest raising it as a considered alternative in the
  plan text; either answer is defensible.
- **Registry sync freshness**: inventory is written at driver enable; the synchronizer runs
  as a persistent logon command. A same-session mode change leaves native Display Properties
  stale until next logon. Worth one sentence of stated expectation. Also confirm the INF
  writes a persistent `Run`-style entry, since the existing `V9xRegisterPage` precedent at
  `inf.ps1:400` is `RunOnce`.
- **Positive**: the mask-source flags, staged-copy-before-4F01h rule, baseline-probe
  separation and transaction boundary are all well designed; test infrastructure
  (`test_vbe_modes.c` already covers overflow bounding, DD subset, scan-corrects-baseline) is
  close to what Stages 1-3 need.

## Recommended plan amendments (the actionable output)

Edit `docs/plans/dynamic-vbe-pipeline.md`:

1. **Baseline hiding (finding 1)**: keep the update-or-append merge and the immutable
   baseline storage, but add a per-row publication flag set by a verified complete scan.
   Scan-contradicted baseline rows are hidden from ValidateMode, mode selection, the
   DirectDraw subset and the inventory; row-zero fallback selection moves to the first
   published row when row zero is hidden; the inventory reports hidden rows with reason
   `scan-contradicted`. Touches the "Runtime table ownership" section, the GDI and
   DirectDraw publication sections, the inventory schema, the host-test list (new cases:
   contradicted baseline hidden, hidden row zero fallback, invalid scan publishes all
   baselines) and the Stage 2 exit gate. Invariants 1 and 7 stay as written.
2. **24 bpp (finding 2)**: move to the rejected list beside 5:5:5; strip 24 from Stage 2/3
   and QEMU matrices; note it as a named follow-up.
3. **Intel physical gate (finding 3)**: add a "945GM netbook" subsection to the verification
   matrix with the two waves above; reference the existing survey as the fixture.
4. **OS scope (finding 4)**: one sentence in Purpose or Current state - dynamic discovery
   requires the mini-VDD and is therefore Win98-family only; Win95 retains the static table
   by design; "no mini-VDD" diagnostic is expected there.
5. **Minor (finding 5)**: note the skip-vs-invalidate alternative for flagged list entries;
   state the sync freshness expectation; confirm `Run` vs `RunOnce`.

No source code changes in this task - the deliverable is the amended plan document.

## Verification

- Re-read the amended plan end-to-end for internal consistency (stages, exit gates, host-test
  list, invariants all reflect the three decisions).
- Check no stage still lists 24 bpp as an exit-gate depth.
- Confirm the GMA950 walkthrough: with the amended rules, the *published* mode set on that
  machine is exactly {640x480x8, 640x480x16, 640x480x32, 1024x576x8, 1024x576x16,
  1024x576x32}; the five contradicted baseline rows remain in storage but hidden; row zero
  (640x480x8) is alive there so fallback selection is unchanged; EDID hint = 1024x576.
