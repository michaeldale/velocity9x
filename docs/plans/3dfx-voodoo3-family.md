# 3dfx Voodoo3 family: tier-0 bring-up

Date: 2026-08-28

Status: proposed — Phase 0 not yet run

Roadmap Track C (`family-structure-and-next-d3d-roadmap.md`). The card on hand
is confirmed a Voodoo3, which selects 3dfx as the next family and, later, the
next Direct3D engine: it is the one candidate with official register
documentation, open Glide source as a second reference, and an emulated
iteration loop — the local 86Box 6.0 build carries both Banshee and Voodoo3
device models, so this is the first family since s3 whose bring-up can run
emulator-first with physical verification behind it, the methodology every
hard ViRGE problem was solved with.

This plan covers tier-0 only. The 2D engine, the Track B D3D core/engine
split, and the D3D implementation are later plans, sketched at the end so
tier-0 decisions do not paint over them.

## What is already true

- After roadmap Track A2/A3, a tier-0 family is manifest + evidence: no build
  script, check-tree list, or registry C edit. The sequence is
  `docs\specifications\family-manifest.md` "Adding a family".
- The dynamic VBE pipeline is live on Win98: the family's static mode rows are
  the fallback, and the runtime table comes from the card's own BIOS. The
  audited-evidence rule still applies to the static rows.
- The standard DOS survey (`scripts\build-vga-survey.ps1`) covers a PCI card's
  whole Phase 0: PCI config space, video BIOS image, VBE controller and
  per-mode data, EDID. Nothing Voodoo3-specific is needed at tier-0 — the
  intel survey's extra collectors existed for UMA facts (GGC/BSM) that a
  discrete card does not have.
- Expected identity: 3Dfx vendor `121A`, Voodoo3 device `0005`. Phase 0
  measures the real ids (board model 2000/3000/3500 and PCI vs AGP are
  unknown until the card is read), and the manifest repeats what was measured,
  not this expectation.

## Phase 0 — evidence from the physical card

1. Decide and record the host machine (bus slot available, OS on it, how
   files move on/off — the netbook's USB-stick loop is the fallback shape).
2. Run the VGA survey from real DOS on that machine. It answers: exact PCI
   ids and BARs, VBE version and OEM string, the full mode list with per-mode
   attributes and linear-framebuffer claims, BIOS image and hash, EDID if the
   monitor answers DDC.
3. Windows half only if the host runs Windows: registry/WMI capture per the
   intel Phase 0 pattern.
4. Write the dated decision doc (`2026-XX-XX-voodoo3-phase0-dos-evidence.md`)
   in the shape of the intel one: measured tables, findings that change the
   plan called out at the top.

Exit gate: the decision doc, plus the survey INI archived. The VBE 2.0-era S3
lesson applies — defects track the BIOS, not the chip
(`docs\specifications\dos-vbe-conformance.md`), so no mode row is trusted
until this run says so.

## Phase 1 — 86Box guest

1. Create a Win98SE 86Box 6.0 profile with the Voodoo3 model matching the
   physical board as closely as 86Box offers, agent on a fresh port (the
   established two-VM topology keeps 9869/9870 for the existing guests).
2. Run the same DOS survey inside the guest and diff against Phase 0: the
   deltas are the standing list of what this emulator proves nothing about,
   the same discipline the ati manifest records for 86Box's Mach64.

Exit gate: profile boots to a Win98 desktop on the emulated Voodoo3;
survey-diff note written (it can live in the Phase 0 decision doc).

## Phase 2 — the tier-0 family

1. `packaging\families\3dfx\family.psd1` — family id `3dfx`, chip id
   `voodoo3`, ids and mode rows from Phase 0 evidence only. C symbols use the
   `tdfx` prefix (`v9x_tdfx_backend`, `src\chipsets\tdfx\`), since an
   identifier cannot start with a digit; the Linux driver established the
   same spelling for the same reason.
2. Backend section per the spec; `scripts\update-backend-registry.ps1`;
   `run-checks.ps1` green.
3. Enable gate and mode matrix on the 86Box guest, then the same on the
   physical card. EngineType stays `NONE`; a Voodoo3 running tier-0 draws
   with the CPU by design.

Exit gate: `Stage=enable-ok` plus a clean mode matrix on both targets, and
the scanout check (added 0.6.0) passing — the Voodoo3's desktop is served
from its 16 MiB linear framebuffer, so the vbe-family 1024x768x16 stripe
issue's check applies verbatim.

## Later phases (sketch only)

- **2D engine.** The Banshee/Avenger 2D core is a full GUI accelerator behind
  MMIO BAR0: screen-to-screen blt, solid fill, command FIFO with a status
  register. It becomes `V9X_DD_ENGINE_TYPE_TDFX`, an `engines\eng_tdfx.c` ops
  table, and a `gdi-accel` arm, developed against 86Box first. New engine
  vocabulary means new `engine_abi.h` values and manifest EngineType names.
- **Track B lands here**: before the D3D phase, `d3d_virge.c` splits into the
  chip-neutral core and `V9X_D3D_ENGINE_OPS`, per the roadmap's two fixed
  rules (batch granularity, behavior-change-free against the ViRGE matrix).
- **D3D.** The Voodoo3 3D engine is FIFO command-stream, renders to the
  linear framebuffer (no V1/V2-style separate 3D-only device), DX6-native
  era. Milestone shape: a named game, the Hellbender-plan pattern — chosen
  then, with the lesson applied that the game must be verified to actually
  use hardware D3D before it becomes the gate.
