# Velocity9x and VelocityNT cross-review

Date: 2026-10-22. A read-only comparison of this repository at `6aaa252f`
(0.25.0 released, 0.26.0 unreleased, uncommitted Rage 128 work in the tree)
against the sibling VelocityNT checkout at `9a1bcb74`, the Windows 2000/XP
port. Nothing was changed in either repository. These are findings only; no
fix has been made. A matching record is in VelocityNT at
`docs/reference/2026-10-22-velocity9x-cross-review.md`.

## Where the two stand

| | Velocity9x | VelocityNT |
|---|---|---|
| OS | Windows 98 (95 on one Trio64 VLB machine) | Windows 2000 SP4, XP SP3 |
| Hardware | Seven families: Intel Gen3 to HD 4600, ATI Rage, S3, Matrox, SiS, Voodoo4, VESA | Trio64 (86Box only), 3dfx VSA-100 Mini-Me (physical) |
| 3D | D3D with DX8 (DDI 6), OpenGL 1.1 ICD, Glide 2/3, 3DCIF | D3D7 HAL, Glide 3, Glide 2 adapter; no OpenGL |
| Acceptance | Games and benchmarks with frame rates | Scene pixel oracle (about 2M pixels, zero mismatches), lifecycle and loss matrices; no application run yet |
| Size | About 120k lines of C89 and assembly, 48.6k lines of host tests | About 18k dense lines of source, 18.8k lines of host tests |
| Shipping | 32 signed releases, update server | No releases |

Velocity9x covers far more hardware and has application-level proof.
VelocityNT's claims are narrower but bound more tightly to evidence.

## What Velocity9x can take from VelocityNT

1. **Generate counts and state from the gates.** VelocityNT's
   `scripts/check-all.ps1` writes `build/results.json` with the source
   revision, a clean-tree flag and per-gate summaries, and its docs must quote
   counts from that file. Here the docs drift:
   - `docs/STATUS.md:12` names 0.23.0 as the latest release and line 22 calls
     the checkout 0.24.0; the README says 0.25.0 is out.
   - `docs/issues/README.md` gives its open/closed split "as of 2026-10-14" but
     lists issues up to 2026-10-22.
   - Comments in `scripts/run-checks.ps1` say GLIDE2X and 3DCIF are not
     packaged yet; `docs/plans/glide-2x-wrapper.md` records every package
     installing them.
   A results file, plus a `check-tree.ps1` rule that STATUS names the README's
   version, would catch all three.
2. **Bind each run to a hashed candidate.** VelocityNT records a guest run only
   from `build/runs/` after an identity match with a deployed, hashed candidate
   (`scripts/new-candidate.ps1`, `scripts/deploy-guest.ps1`,
   `docs/probe/README.md`), in a new directory that is never rewritten. The 238
   entries in `docs/probe/` here have no hash link from capture to binary. With
   remote testers (`M-KYHT4Q`, `M-C71087` and others) running builds outside
   this machine, that link matters more here than there.
3. **Smoke-test the shipped archive.** VelocityNT restores the normal package
   and re-checks drawing after every physical run. `docs/STATUS.md` here says no
   release archive has had an activation smoke test from a Have Disk install of
   the archive itself. That could be a release gate in `docs/RELEASING.md`.
4. **Lifecycle matrices.** VelocityNT's Glide 3 and D3D gates cover thread
   exit, window destroy, process kill, surface loss and console switch as
   first-class cases. The open issue
   `docs/issues/2026-10-06-hal-statics-shared-across-processes.md` is the kind
   of defect such a matrix finds; Glide and the ICD here are multi-process
   clients of `V9XHAL.DLL`.
5. **An exact pixel oracle.** VelocityNT compares rendered scenes pixel by
   pixel against a host oracle. Screenshots here are compared by eye; no
   automated pixel comparison was found.
6. **A uniform issue status.** 78 of the issues here are open. A
   machine-readable status line per issue, checked by `check-tree.ps1`, would
   keep `docs/issues/README.md` from going stale. (VelocityNT lacks this too.)

## What VelocityNT can take from Velocity9x

Recorded here for completeness; the VelocityNT copy has the detail.

- Run real applications early: Carmageddon II and NFS II SE shaped the Glide
  texture design here; VelocityNT has not yet run UT.
- The per-page generation model in `src/glide/glide_texmem.c`; VelocityNT's
  new Glide 2 byte store uses one revision for the whole store.
- The refusal cascade (`v9x_glide_refusal_next`), `scripts/compare-probe.ps1`,
  `scripts/golden-baseline.ps1`, the MSVC host-test warning pass, and the
  signed release pipeline.

## Shared weaknesses

- Very large files in both trees.
- The guest agent (`v9xctl.ps1` via `V9X_AGENT_CTL`) lives outside both repos,
  so neither can reproduce its guest runs on its own.
- One physical machine per card.
- Stale plans: `PLAN.md` here is marked out of date; VelocityNT's porting map
  still says no source has been imported.

## Problems found in this repository

1. **Stale status documents**, listed under item 1 above.
2. **Large files:** `src/display32/d3d/d3d_core.c` (6,195 lines),
   `src/minivdd32/loader.asm` (5,123), `src/opengl/gl_icd.c` (4,339),
   `src/display16/runtime.asm` (3,858), `src/display32/d3d/d3d_i9xx.c` (3,070).
3. **Thin hardware coverage:** ViRGE 3D is emulator-only; Glide has not been
   measured on S3, Matrox or SiS; HD 4600, HD 4000, GMA 3150 and X3100 ran only
   probes or sessions, with no game.
4. **Open design issues:** HAL statics shared across processes; NaN checks
   duplicated in four places, three of them untested; Intel culling done in
   software.
5. **No Intel DVMT:** `docs/plans/intel-gen3-system-memory-textures.md` is
   proposed with nothing coded, so 8 MiB of stolen memory limits textures
   (Serious Sam at 3.46 fps).
6. **Binary releases in git:** `releases/` holds 32 versions (305 files) and
   `.git` is about 253 MB.
7. **No video overlay or hardware cursor** on any card; GDI is drawn by the CPU
   except on S3, Matrox and the Voodoo4. These are scope facts, not defects.

## Method

Two read-only surveys, one per repository, covering README, PLAN, STATUS,
CHANGELOG, docs folders, source layout (`wc -l`), scripts and tests. The stale
STATUS release line was checked directly with `grep`. Line counts are
approximate.
