# GDI acceleration, build 000: a provably free decline path and the harness that can judge it

Parent plan: [gdi-acceleration.md](gdi-acceleration.md), which owns the design
and the `gdi-accel-000`..`005` rollout table. This is the implementation plan for
the next stage only: **build 000 plus the `/accel` harness**. It exists because
the parent plan was written against a tree that has since been refactored, and
because the two pieces below are the ones that make every later build checkable.

## What is already done

Do not redo these; they were completed and verified on 2026-08-26 and the parent
plan's "Corrections" section records them.

- **Design decision 1.** The S3 2D register map lives in
  `include\velocity9x\s3_engine_regs.h`, moved out of the HAL-private
  `src\display32\ddhal_internal.h`, which now includes it. Proven a pure
  preprocessor move: `v9xhal.dll` is byte-identical across the change with a
  pinned build id. The ViRGE S3D `0xb4xx` windows and `V9X_VBLANK_SPIN_LIMIT`
  deliberately stayed behind.
- **Open item 1, half of it.** `FB_ACCESS = 0x0001` and
  `CURSOREXCLUDE = 0x0008`, from `C:\98DDK\inc\win98\inc16\DIBENG.INC`, which
  carries both an assembly `equ` (126-127) and a C `#define` (131-132). There is
  no `dibeng.h` in this DDK; the parent plan named a file that does not exist.
  The realized-brush layout is still unread - see Block 1.
- **Open item 2.** `Lock` and `Flip` already drain the engine
  (`src\display32\ddhal_core.c:503` and `:340`), each under
  `v9x_engine_status_validated()`, honouring `DDLOCK_DONOTWAIT` /
  `DDFLIP_DONOTWAIT` and returning `DDERR_WASSTILLDRAWING` otherwise. Two
  further CPU-access boundaries at `:448` and `:481` do the same. So this stage
  adds no HAL drain points, and the parent plan's Verification §4 risk ("new
  drain points must not regress the HAL paths") does not apply to Lock or Flip.

## Two premise corrections that shape this stage

### The decline path is the only path on three of the four families

`s3` is the sole family with a 2D engine. `ati`, `vbe` and `matrox-m2` all
declare `EngineType = NONE` on every chip, and all four families link the same
`ddi.c` and `dd16.c`. The `BitBlt` export lives in the shared 16-bit layer, so
the moment ordinal 1 stops being `V9X_FORWARD BitBlt, DIB_BitBlt`
(`src\display16\dib_thunks.asm:74`) and becomes a C function, **every family
gets the new dispatcher** - and three of them will take its decline branch on
every single blit, for ever.

That reframes build 000. It is not scaffolding on the way to the interesting
part; for three quarters of the fleet the decline path *is* the shipping code,
permanently. Its exit gate has to be run on a family with no engine, not only on
the ViRGE.

### The signature and the gates have a first-party source, and it is not the one cited

The parent plan says to take the 11-argument `DIB_BitBlt` signature from the DDK
doc `display_1fn8.htm`. Those docs are inside `C:\98DDK\help\*.CHM` and awkward
to read. There is a better source: `C:\98DDK\src\display\mini\xga\BITBLT.ASM:57`
declares the entry point in first-party code, with the arguments named, in push
order:

```
cProc   BitBlt,<FAR,PUBLIC,WIN,PASCAL>
        parmD   lpDestDev       ; destination bitmap descriptor
        parmW   DestxOrg
        parmW   DestyOrg
        parmD   lpSrcDev        ; source bitmap descriptor
        parmW   SrcxOrg
        parmW   SrcyOrg
        parmW   xExt
        parmW   yExt
        parmD   Rop
        parmD   lpPBrush        ; physical brush (pattern)
        parmD   lpDrawMode
```

Read that file before writing the C prototype. A far-pascal frame that disagrees
with the caller by one word corrupts the stack on the first blit GDI issues,
which on this path is during boot.

The same file also supplies a gate the parent plan's list omits. Its sequence is
`test VRAM` → `test BUSY` → **`test PALETTE_XLAT`** → give it a try, declining to
the DIB Engine at each step. `PALETTE_XLAT` means the blit needs background
palette translation, which the engine cannot do. **Add it to the acceptance
gates.** Without it, the first accelerated 8-bpp fill under a translated palette
would produce right-shaped, wrong-coloured pixels - the class of bug the harness
exists to catch, avoidable for free by copying the reference driver's gate.

## Scope

Four blocks. Blocks 1 and 2 are the stage; Block 3 is what makes the stage's
claim believable; Block 4 is a small safety net this stage's blast radius earns.

### Block 1 - the decline path, and nothing else that runs

Build `gdi-accel-000` per the parent plan: all infrastructure and primitives
compiled, every primitive default-off, the dispatcher declining unconditionally.

- Read the realized-brush layout out of `DIBENG.INC` and add it to
  `src\display16\win9x_display_abi.h` with a `sizeof` assert, beside the two
  constants already resolved. Do not guess a field order.
- New `src\display16\gdi_accel.c` and `.h`: the dispatcher, the acceptance
  gates, the ViRGE and Trio primitives, bounded waits, the CR66 reset, the
  poison latch, counters, and the config read. Compiled everywhere; the
  primitives must be unreachable on an engine-less family.
- Remove the BitBlt forward at `dib_thunks.asm:74`; export
  `BitBlt.1=BITBLT` in `scripts\build-win16-ddi-skeleton.ps1` alongside the
  existing pattern (`Control.3=CONTROL`, and `UserRepaintDisable.500` added
  2026-08-26 for the shape of it). Extend that script's map assertions.
- `V9XDIBBEGINACCESS` (`src\display16\runtime.asm:76-79`) gains the
  two-instruction dirty-check fast path and a C slow path. The slow path runs at
  interrupt time for software-cursor draws, so it may touch MMIO, ports and
  DGROUP only - **no `WritePrivateProfileString`, no serial write**. Poison
  reporting defers to the next BitBlt or to Disable.

The poison latch and the dirty flag live in DGROUP, not in the PDEVICE and not in
`V9X_DD_SHARED`. Two reasons, both concrete: `V9X_DD_SHARED` does not exist
before the HAL DLL loads, and `ReEnable` rebuilds the PDEVICE in place on a live
mode switch (`src\display16\ddi.c`), so anything latched inside it would be
silently cleared by a resolution change. The parent plan requires the latch to
survive mode switches; DGROUP is what makes that true.

### Block 2 - `/accel`, the harness that can fail

`tools\diag\gdi_smoke_win32.c` gains an `/accel` phase, leaving the existing
smoke path untouched so `Result=PASS` keeps its current meaning.

- A seeded LCG drives ~500 operations: solid-colour `PatBlt`s including
  deliberately below-threshold ones, screen-to-screen `SRCCOPY` `BitBlt`s
  covering all eight overlap directions, and decline-noise operations that must
  take the DIB path. Every operation is mirrored into a DIBSection reference DC
  and compared with `GetDIBits` every 25 operations.
- Then query `V9X_GDIGETSTATS` through `ExtEscape` and **fail if the engine
  counters are zero while the build advertises the primitive.** This is the
  anti-vacuous-pass check and it is the single most important line in the
  harness. A comparison harness that silently exercised the decline path on
  every operation would pass perfectly and prove nothing - which is exactly how
  the `ati` package shipped unable to enable for a release
  (`docs\issues\2026-08-26-ati-package-cannot-enable.md`): every check it had to
  pass was a check it could pass without working.
- Finally `V9X_GDIFAULTINJECT`, one more `PatBlt`, then assert the desktop still
  renders, `Poisoned=1`, and the pixels are still correct.

**Land this with or before build 001, never after.** Build 000 turns nothing on,
so the harness cannot yet catch a wrong fill - but writing it against 000, where
the correct answer is "the reference DC and the screen agree because the DIB
Engine did all of it", is how the harness itself gets debugged while the stakes
are zero. Retrofitting it onto code already declared working inverts that.

### Block 3 - exit gate for 000: behaviour, measured, on two families

The parent plan words this gate "byte-identical behavior". Read it as
**behaviour**, not bytes: build 000 adds code, so the DRV cannot hash the same,
and there is no point pretending otherwise. What must be indistinguishable is
what the driver does.

- `run-vm-mode-matrix.ps1` on the ViRGE/DX guest (`:9869`) and the Trio64 guest
  (`:9871`) - the engine family, both chips out of one binary.
- The same matrix on an **engine-less** family, `ati` on `:9873`, for the reason
  in the premise correction above. This is the run that proves the decline path
  is free for the three families that will only ever decline.
- `V9XGDI` existing phase `PASS` on all three, plus the new `/accel` phase.
- Ironfield `BltFast` numbers unchanged against
  `docs\decisions\2026-08-14-virge-blitter.md`, and `V9XDDP` unchanged: the
  engine is shared, and this stage must not have disturbed the HAL.
- One deliberate manual run with `GdiAccelFill=1` in `SYSTEM.INI`, purely to
  prove the primitive can fire at all before build 001 claims it works.

Capture discipline, because this project has already lost a day to it: on a slow
guest a screenshot taken straight after a mode change samples an animation, not a
display. Capture twice and read the second
(`docs\issues\2026-08-20-barry-tiling-was-a-screenshot-race.md`).

### Block 4 - a per-family enable gate in `run-checks`

Small, and this stage earns it: Block 1 modifies the shared 16-bit layer that
all four families link, so a mistake there is a four-family mistake.

`run-checks` builds every family package and passes, because **a package that
builds is not a package that enables**. That is precisely how `ati` shipped
broken. `run-vm-mode-matrix.ps1` would have caught it on the first mode - it
asserts `Stage=enable-ok` - but it is not part of `run-checks` and needs a guest.

Add an opt-in smoke gate that, for each family whose manifest declares an
emulator, starts the guest, deploys, and asserts `Stage=enable-ok` and nothing
else. Not in the default `run-checks` path - it needs VMs and minutes - but one
command, so it is runnable before any merge that touches the shared layer.

This is now practical in a way it was not believed to be: 86Box starts and drives
from an automated session. Two mechanics worth writing into the script rather
than rediscovering - the forwarded host port opens **before** the guest agent
listens, so poll `v9xctl ping` rather than trusting a port check; and a guest
that wedges recovers with a force-kill and restart, with committed WININIT driver
installs surviving.

## Verification

This stage is satisfied when:

- the `BitBlt` C prototype matches `xga\BITBLT.ASM:57` argument for argument,
  and the realized-brush struct carries a `sizeof` assert against `DIBENG.INC`;
- `PALETTE_XLAT` is among the acceptance gates;
- the mode matrix passes on `:9869`, `:9871` **and** `:9873`, with `V9XGDI`
  `PASS` on each;
- the `/accel` phase passes and its zero-counter check has been proven to fail
  when it should - inject a build with the primitive advertised but stubbed out
  and confirm the harness rejects it, because an assertion never observed
  failing is not known to work;
- fault injection leaves the desktop rendering, `Poisoned=1`, pixels correct;
- Ironfield and `V9XDDP` numbers unchanged;
- the poison latch survives a live mode switch, tested by switching mode after
  injecting a fault and confirming the latch still reads set.

## Deliverables

- `src\display16\gdi_accel.c` / `.h`, the `dib_thunks.asm` and `runtime.asm`
  changes, the build-script export and asserts, the brush struct with its
  assert.
- The `/accel` harness phase, and a recorded run of its self-test from the
  Verification list above.
- A `docs\decisions` note for build 000 carrying the three-family matrix
  results, the Ironfield comparison, and the manual fill-on run.
- The parent plan's rollout table updated with 000 marked done and any gate
  wording this stage found to be wrong.
- Block 4's smoke-gate script, and a line in `docs\BUILDING.md` saying when to
  run it.

## Explicitly out of scope

- Builds 001-005. Fill, copy, overlap, CPU-to-screen upload and extra ROPs all
  follow the parent plan's table and are gated by Block 2's harness.
- Hardware cursor. Still `No` on every target in the README matrix, and a
  separate piece of work.
- The live-repaint fault on the physical Trio64
  (`docs\issues\2026-08-20-live-mode-switch-no-repaint-barry.md`). Open,
  narrowed, and unrelated - but note its next step is cheap and does not need
  this stage: establish whether the stock S3 driver live-switches cleanly on
  BARRY, which either reframes the issue as a platform property or confirms it
  is ours.
- The `s3` table's 640x400x8 row, absent from BARRY's BIOS
  (`docs\decisions\2026-08-26-s3-physical-pipeline-inert.md` §8). Wants
  per-device row admission, which is a table-structure change with its own plan.
