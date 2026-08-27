# Roadmap: family-structure fixes and the next Direct3D engine

Date: 2026-08-28

Status: Track A complete 2026-08-28 (A1 pre-existing, A2/A3/A4 done).
Track C resolved to 3dfx: the card is a confirmed Voodoo3, planned in
[3dfx-voodoo3-family.md](3dfx-voodoo3-family.md). Track B queues before that
plan's D3D phase. Track D (tier-0 quality: MTRR write-combining, hardware
cursor, synthetic vblank, DOS-box guard) added 2026-08-28 from the
vmdisp9x/vmhal9x survey, planned in [tier0-quality.md](tier0-quality.md);
its D1/D4 land before the 3dfx packages ship.

This roadmap came out of three reviews on 2026-08-28: the stale
`intel-gma-tier0` branch (what to salvage), the cost of adding a new VGA
chipset family on main today, and the shape of the Direct3D code ahead of a
second 3D engine. It orders three tracks. Track A is planned in detail here;
Tracks B and C are scoped and sequenced but each gets its own plan document
when scheduled, per house convention.

Existing plans this defers to rather than repeats:

- `docs/plans/dynamic-vbe-pipeline.md` — **complete**: stages 1–5 shipped in
  0.5.0 and the physical-S3 inertness validation ran on BARRY (0.6.0). An
  earlier draft of this roadmap treated stages 2–5 as open; corrected
  2026-08-28.
- `docs/decisions/2026-08-24-chipset-hal-free-split.md` — the noted, not
  scheduled, chipset-library refactor. Nothing here schedules it, and Track A
  deliberately avoids touching the chipset/driver boundary it discusses.

---

## Track A — family-structure fixes ("the 2D fixes")

Goal: adding a VGA chipset at tier-0 collapses to *manifest + survey evidence*,
with no hand-edited C or build-script lists. Four items, ordered; each is
independently shippable and none blocks the others' rollback.

### A1. Dynamic VBE pipeline — already done

The prerequisite that kills the per-family audited mode table shipped before
this roadmap was written: stages 1–5 of `docs/plans/dynamic-vbe-pipeline.md`
landed in 0.5.0, and the netbook findings work in 0.6.0 built on it. The
static `V9X_HW16_MODE` rows already hold the plan's fallback role (and remain
primary on Win95 by design, per the mini-VDD Code-24 constraint). Nothing to
do; A2 can start immediately against the settled consumption path in
`ddi.c`/`dd16.c`.

One follow-up worth confirming when convenient: a Have-Disk install on the
netbook after 0.6.0 should now publish the panel's native OEM-numbered mode
with zero family C code — the 2026-08-27 run predates parts of this.

### A2. Generate the backend registry from the family manifests — done 2026-08-28

`src/common/backend_registry.c` is a hand-written if-chain over PCI ids that
duplicates ids already declared in `packaging/families/*/family.psd1`. The
intel-gma branch had to edit it to exist; no future family should.

Scope:

- A new emitter in `scripts/lib/` (pattern: the intel branch's
  `vbe-cache.ps1`, which generated mini-VDD data from manifests) walks the
  family manifests and writes a checked-in, generated
  `src/common/backend_registry_table.inc`: rows of
  `{vendor_id, device_id, backend-getter}`.
- `backend_registry.c` becomes a loop over that table. The allowlist policy is
  unchanged and now single-sourced: a PCI id appears in the table only because
  a manifest lists it, and manifests only list hardware someone has run. The
  tier-0 non-fallback comment block moves with the code; it is the load-bearing
  part of the file.
- `check-tree.ps1` gains a rule: the generated table must match a fresh run of
  the emitter (regenerate-and-diff), so a manifest edit cannot silently
  diverge from the compiled dispatch.
- Host tests: the existing family-matrix tests keep passing unchanged; add one
  test asserting an id present in a manifest resolves and an absent one
  refuses.

Risks: manifests currently spell ids as strings for INF emission; the emitter
must parse, not re-declare, so a hex-case or subsystem-id convention mismatch
is caught by the check-tree rule rather than at runtime. Deliberately out of
scope: `pci_match_optional` and the 16-bit family table — the registry is the
host-testable policy layer only, exactly as the current comment says.

### A3. Glob-driven family discovery in the build — done 2026-08-28

As implemented, the derivation runs on the two host build scripts' chipset
source lists (from `Backend.Sources`) and check-tree's manifest list (glob +
schema validation, no per-family lines); no skip list proved necessary.

`build-host.ps1`, `build-host-msvc.ps1`, and `check-tree.ps1` each carry a
hand-maintained family list (the intel branch touched all three). Replace the
lists with discovery over `packaging/families/*/family.psd1`, keeping
`Assert-V9xFamilyKeys` as the gate that rejects a malformed manifest at build
time rather than a missing list entry hiding it. A family is then *added* by
creating its directory and *excluded* only by an explicit skip list, which is
where a deliberate exclusion becomes visible and reviewable.

Exit gate: `git grep` for family names in the three scripts returns only the
skip list and comments.

### A4. Salvage the intel-gma branch — done 2026-08-28

As executed: items 1–2 landed on main with provenance notes (the branch's
scaffolding is archived at tag `archive/intel-gma-tier0` and the branch
deleted). Item 3's verdict: the branch's `vbe-cache.ps1` build-time mode cache
is **superseded** by the dynamic-VBE stage-1 runtime `VideoModePtr` walk in
`src\minivdd32\loader.asm`, which solves the same OEM-mode-number problem
per-BIOS at boot with no manifest duplication; nothing ported.

The branch is 64 commits behind and its scaffolding predates current
manifest/INF conventions; it is a reference, not a merge. Take, as
cherry-picks or ports onto main:

1. The evidence documents — the Gen3 hardware audit, the Phase 0 DOS and
   Windows evidence (OEM modes 0160/0161, panel-height refusals), and the
   bring-up runbook. These are measured ground truth for any future intel
   family and should not live only on a stale branch.
2. `tools/diag/intel_survey_dos.c` and its capture script — the instrument
   that produced the evidence, standalone and family-independent.
3. Evaluate, do not assume: the branch's `scripts/lib/vbe-cache.ps1` +
   `loader.asm` mode-cache generation. Dynamic-VBE stage 1 landed a
   `VideoModePtr` walk in the mini-VDD after the branch diverged, and may have
   superseded it. One sitting: diff the two approaches, keep the merged one,
   record the verdict in the CHANGELOG entry.

The intel-gma *family* itself is re-added only if Track C ever reaches Intel,
following whatever conventions exist then, with A2/A3 having removed most of
the boilerplate it originally needed.

---

## Track B — Direct3D core/engine split

Goal: `src/display32/d3d/d3d_virge.c` splits into a chip-neutral core (context
pool, texture handle table, render-state bookkeeping, software clipper,
size-guarded caps/callback publication) and a per-engine ops table —
`V9X_D3D_ENGINE_OPS`, selected on `engine.engine_type` exactly as
`v9x_engine32()` selects 2D ops today.

Two rules fixed now, so phase-2/3 knowledge does not have to be re-derived
from the monolith later:

1. **Batch granularity.** The vtable draws at
   `draw_triangles(context, tris, count)` level, never at the S3D
   per-register level. The ViRGE is an immediate-mode register engine; every
   candidate next engine (3dfx FIFO, ATI Rage setup engine, Intel Gen3
   ring/batch) is a command-stream engine, and `d3d_virge.c` already batches
   at `V9X_D3D_MAX_BATCH_TRIANGLES`, so the seam exists.
2. **Behavior-change-free.** The split ships against the existing ViRGE D3D
   validation matrix (Hellbender gates included) with identical results; it is
   mechanical extraction inside a file nothing else reaches into.

Timing: written as its own plan and executed when Track C's engine is
scheduled — not before, per the house rule against speculative refactors — but
the decision doc recording the ops boundary and the two rules above can be
written any time.

---

## Track C — the next Direct3D engine

Hardware on hand: 3dfx, ATI, NVIDIA, Intel, and more. Ranked by expected
cost, best first:

| Candidate | Docs | Emulated 3D iteration loop | In-tree scaffolding | Notes |
|---|---|---|---|---|
| 3dfx Banshee / Voodoo3 | Official register specs released by 3dfx; open Glide source | **Yes — 86Box emulates the Voodoo line including the 3D engine** | none (new family) | 2D+3D single chip; DX6-native era; the roadmap in the 2026-08-24 decision already names 3dfx next |
| 3dfx Voodoo1/2 | Same quality | Yes (86Box) | none | 3D-only passthrough card: presents as a *second* DirectDraw device with its own HAL, not this driver's display chip — architecturally a different (interesting, larger) project |
| ATI Rage Pro / Mobility-M | ATI register specs + Mesa mach64 reference | No (86Box's Mach64 is 2D-only) | **ati family exists at tier-0, Rage Mobility-M already dispatched** | Setup engine famously quirky; 2D engine bring-up needed first |
| NVIDIA Riva 128/TNT | No official docs; envytools/nouveau reverse-engineered | No | none | Weakest documentation of the era; hardest to defend register writes against |
| Intel GMA 950 | Excellent (PRMs, Mesa i915, kernel i915) | No, and the only hardware is the netbook with USB-stick-only evidence | none on main (branch is reference) | Requires GTT/ring-buffer memory management before any drawing; largest lift, worst loop |

**Recommendation: 3dfx, provided the card on hand is a Banshee or Voodoo3.**
It is the only candidate where the whole ViRGE methodology transfers intact:
develop and regress in an emulator (86Box, which a session can drive per the
established VM workflow), then verify on the physical card — the loop that
made the ViRGE D3D work tractable. Documentation is the best of any candidate,
and it is already the stated next step in the 2026-08-24 ordering. If the
3dfx card is a Voodoo1/2, fall back to ATI Rage: the passthrough-device
architecture is a genuinely different driver shape and should not be the
second data point for the Track B abstraction.

Phasing mirrors the proven ViRGE ladder: family at tier-0 → 2D engine +
`V9X_ENGINE32_OPS` table → Track B split lands → `V9X_D3D_ENGINE_OPS`
implementation → a named game milestone (the Hellbender-plan pattern) as the
exit gate.

---

## Sequencing

1. **A1** — already shipped (0.5.0/0.6.0); nothing queues behind it.
2. **A2 + A3** — one small branch each, order between them free; can start
   now.
3. **A4** — any time.
4. **Track B decision doc** — any time; **Track B execution** when 5 is
   scheduled.
5. **Track C: 3dfx family** — tier-0 first, which after A1–A3 is
   manifest-plus-evidence; then the engine ladder above.
