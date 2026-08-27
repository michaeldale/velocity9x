# GDI acceleration, build 000: a provably free decline path and the harness that can judge it

Status: implemented — shipped with the 2026-08-26 decision record
([2026-08-26-gdi-accel-000.md](../decisions/2026-08-26-gdi-accel-000.md)) and
validated on physical Trio64 silicon in 0.6.0.

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
- **Open item 1, the other half, read 2026-08-26 during this plan's review.**
  The realized-brush layout is `DIBENG.INC:183-253`: six per-depth structs
  (`DIB_Brush1/4/8/16/24/32`) sharing one 14-byte header -
  `BYTE BrushFlags` (offset 0), `BYTE BrushBpp` (1), `WORD BrushStyle` (2),
  `DWORD FgColor` (4, the physical foreground colour), `WORD Hatch` (8),
  `DWORD BgColor` (10) - followed by `Mono[BRUSHSIZE*4]`, `Mask[BRUSHSIZE*4]`
  and a `Bits[]` array sized by depth, with `BRUSHSIZE = 8` (`:25`). Flag bits
  at `:258-265`: `COLORSOLID 0x01`, `MONOSOLID 0x02`, `PATTERNMONO 0x04`,
  `MONOVALID 0x08`, `MASKVALID 0x10`, `PRIVATEDATA 0x20`, `BRUSH40 0x40`,
  `DIBENGBRUSH 0x80`. The solid-fill gate needs exactly two fields:
  `BrushFlags & COLORSOLID` and `FgColor`; the pattern arrays are never
  touched. So Block 1 mirrors the common header once with offset asserts, plus
  whole-struct `sizeof` asserts for the two accelerated depths:
  `DIB_Brush8` = 142 bytes (14+32+32+64) and `DIB_Brush16` = 206 bytes
  (14+32+32+128). The structs are byte-packed; the asserts exist to catch a
  compiler that pads.
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

Copy the rule exactly, not approximately. `BB_JumpToDibEngineX`
(`BITBLT.ASM:44-50`) re-checks `lpSrcDev == lpDestDev` and still accelerates a
screen-to-screen blit under `PALETTE_XLAT`, because a copy that never leaves
VRAM moves pixel values untranslated. Fills arrive with `lpSrcDev` not equal to
the screen PDEVICE, so they decline. The distinction is load-bearing from build
002 onward: an 8-bpp desktop with an active palette translate is precisely the
desktop whose window scrolls and moves builds 002/003 exist to accelerate, and a
blanket "decline whenever `PALETTE_XLAT`" gate would silently turn the feature
off exactly there.

## Scope

Four blocks. Blocks 1 and 2 are the stage; Block 3 is what makes the stage's
claim believable; Block 4 is a small safety net this stage's blast radius earns.

### Block 1 - the decline path, and nothing else that runs

Build `gdi-accel-000` per the parent plan: all infrastructure and primitives
compiled, every primitive default-off, the dispatcher declining unconditionally.

- Transcribe the realized-brush layout recorded above into
  `src\display16\win9x_display_abi.h` - the common 14-byte header with offset
  asserts, and the `DIB_Brush8` / `DIB_Brush16` `sizeof` asserts - beside the
  two constants already resolved.
- New `src\display16\gdi_accel.c` and `.h`: the dispatcher, the acceptance
  gates, the ViRGE and Trio primitives, bounded waits, the CR66 reset, the
  poison latch, counters, and the config read. Compiled everywhere; the
  primitives must be unreachable on an engine-less family.
- Remove the BitBlt forward at `dib_thunks.asm:74`; change the existing
  `export BitBlt.1` line (`scripts\build-win16-ddi-skeleton.ps1:164`) to
  `export BitBlt.1=BITBLT`, the `Control.3=CONTROL` pattern - `Control` is the
  existing proof that a Watcom `FAR PASCAL` C function can own an ordinal here.
  The post-link assertions live in `scripts\audit-family-binary.ps1`, not the
  build script (which delegates at `:195-201`): it already requires the
  `BitBlt` export (`:90`), and its thunk disassembly audits cover `CheckCursor`,
  `SetCursor`, `MoveCursor` and `DibBlt` only, so deleting the forward disturbs
  none of them (the `:233` error text says "BitBlt thunk" but the pattern audits
  `DibBlt`, ordinal 19 - a trap for the unwary). New `gdi_accel` symbols go in
  the audit's script-level required-symbol list, since the file links into
  every family; and add one new disassembly audit worth its lines: the
  dispatcher's decline branch must still reach `DIB_BitBlt`.
- `V9XDIBBEGINACCESS` (`src\display16\runtime.asm:133-136`; both this plan and
  the parent said `:76-79`, which is now DGROUP data - the tree moved) gains the
  two-instruction dirty-check fast path and a C slow path. The PDEVICE really
  does route through it - `ddi.c:917` sets `deBeginAccess = V9xDibBeginAccess` -
  so interrupt-time cursor draws hit the patched entry. But it is not the only
  door: `V9XDIBBEGINACCESSRECT` (`runtime.asm:231-234`) jumps to the same
  `DIB_BeginAccess`, and ReEnable's live-switch cursor exclusion calls it
  (`ddi.c:957`). Give both entries the same dirty check - it is a shared
  two-instruction stub either way - rather than reasoning per caller about who
  can never race pending engine work. The slow path runs at
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

`V9X_GDIFAULTINJECT` should mirror `V9X_DDFAULTINJECT`
(`docs\decisions\2026-08-16-engine-fault-injection.md`): an armed count that the
production bounded waits consume by falling into their existing timeout tail, so
the injector drives the shipping recovery path rather than a parallel test one.
That choice has a consequence this plan must own: on a default 000 build every
primitive is off, no GDI bounded wait ever runs, and an armed injection is never
consumed - `Poisoned` stays 0 honestly. So the harness gates its fault-injection
step on the same condition as the zero-counter check (a primitive advertised and
enabled), and at 000 the step runs only in Block 3's deliberate `GdiAccelFill=1`
session. From 001 onward it runs unconditionally.

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
  prove the primitive can fire at all before build 001 claims it works. This is
  also the session in which the `/accel` fault-injection step and the
  poison-latch mode-switch test run at this stage: an armed injection is only
  consumed by an op that actually reaches a bounded wait (see Block 2).

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

Coverage honesty: the gate reaches three families, not four. `matrox-m2`
declares `Emulator = 'none'` (physical only), as does the `ati` family's Rage
Mobility target, so the gate covers `s3` (both chips, 86Box), `vbe` (QEMU) and
`ati`/mach64-vt2 (86Box). The script should print the skipped families by name
rather than silently equating "each family with an emulator" with "every
family". `run-vm-mode-matrix.ps1` already has the pieces to reuse: the
no-emulator refusal wording (`:36-50`) and the `Stage=enable-ok` assert
(`:227`).

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
- `PALETTE_XLAT` is among the acceptance gates, with the screen-to-screen
  exemption copied from `BB_JumpToDibEngineX`;
- both `DIB_BeginAccess` entries (`V9XDIBBEGINACCESS` and
  `V9XDIBBEGINACCESSRECT`) carry the dirty check;
- the mode matrix passes on `:9869`, `:9871` **and** `:9873`, with `V9XGDI`
  `PASS` on each;
- the `/accel` phase passes and its zero-counter check has been proven to fail
  when it should - inject a build with the primitive advertised but stubbed out
  and confirm the harness rejects it, because an assertion never observed
  failing is not known to work;
- fault injection leaves the desktop rendering, `Poisoned=1`, pixels correct -
  run with a primitive enabled, per Block 2's consumption note;
- Ironfield and `V9XDDP` numbers unchanged;
- the poison latch survives a live mode switch, tested by switching mode after
  injecting a fault and confirming the latch still reads set (same session:
  needs a primitive enabled for the fault to be consumed).

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
