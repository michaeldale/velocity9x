# Conservative GDI acceleration (PLAN.md Phase 5)

## Context

Velocity9x's GDI path today is a pure DIB Engine passthrough: every drawing ordinal in `src/display16/dib_thunks.asm` is an unconditional `jmp DIB_*` (e.g. line 74 `V9X_FORWARD BitBlt, DIB_BitBlt`). Meanwhile the 32-bit DirectDraw HAL (`src/display32/ddhal.c`) already drives the ViRGE S3D and Trio64 8514/A 2D engines successfully — screen-to-screen SRCCOPY with overlap handling and solid fills, with bounded waits, engine reset, and measured wins (Ironfield BltFast 3→18 FPS, see `docs/decisions/2026-08-14-virge-blitter.md`). This plan brings that engine to GDI: desktop fills and window scrolls/moves go to hardware, everything else declines to the DIB Engine unchanged. It implements PLAN.md Phase 5 ("Conservative GDI acceleration", one primitive at a time, DIB fallback for every unsupported case, desktop must survive engine timeouts).

Key constraint: the HAL is 32-bit flat code loaded only when DirectDraw asks for it; GDI acceleration must live in the 16-bit `V9XDISP.DRV` and cannot depend on the DLL. The 16-bit side already maps the full 64 MiB ViRGE BAR (framebuffer + MMIO window at BAR+16 MiB, `runtime.asm:631-738`), and the Trio64 engine is plain port I/O — so the 16-bit driver can reach both engines directly.

## Design decisions

1. **Code sharing — constants only.** New `include/velocity9x/s3_engine_regs.h` holds the ViRGE MMIO offsets/command bits and Trio64 port map currently local to `ddhal.c:36-147`; `ddhal.c` includes it (pure preprocessor refactor). The 16-bit primitives (~30 lines each) get their own implementation in a new `src/display16/gdi_accel.c` — the 32-bit code uses flat pointers and can't compile under wcc's compact model. All needed ViRGE 2D registers sit below 0x10000 in the 64 KiB new-MMIO window, so one dedicated LDT selector (base = linear+0x01000000, limit 0xFFFF, same DPMI sequence as `V9xScreenSelector` at `runtime.asm:699-738`) makes them reachable via ordinary `volatile DWORD FAR *` pointers. Trio64 needs only a `#pragma aux` word-out beside the existing byte port I/O (`ddi.c:165-169`).

2. **Dispatch.** Delete the BitBlt forward from `dib_thunks.asm:74`; export a C `WORD __loadds FAR PASCAL BitBlt(...)` with the 11-arg DIB_BitBlt signature (DDK `display_1fn8.htm`). Decline path is a plain `return DIB_BitBlt(<same args>);` — pascal is callee-pop on both sides. Build script gets `export BitBlt.1=BITBLT` (same uppercase-mangling pattern as `Control.3=CONTROL`). Phase 1 touches **ordinal 1 only** — GDI routes PatBlt through driver BitBlt with a NULL source, so fills and copies both arrive here.

3. **Acceptance gates** (checked cheapest-first; any failure → DIB): not poisoned + enabled; dest identity-equals the screen PDEVICE with `DE_VRAM` set and `DE_BUSY` clear; bpp 8 or 16; rects in bounds, coords < 2048; pixel count ≥ threshold (default 1024, INI-tunable). Then per-op: solid fill = BLACKNESS/WHITENESS (no brush parsing) then PATCOPY with a solid DIBENG brush; screen copy = SRCCOPY with src == dest == screen PDEVICE, overlap direction bits per the reference code in `v9x_virge_copy` (`ddhal.c:1134-1144`) / `v9x_trio_copy` (`ddhal.c:1207-1219`).

4. **Synchronization.** GDI DDI calls and Win9x DDHAL callbacks all run under the Win16Lock — the hazard is *pending engine work* crossing a CPU-access boundary, plus interrupt-time software-cursor draws. Authoritative state is DGROUP globals (`v9x_gdi_engine_dirty`, `v9x_gdi_poisoned`, counters) — NOT `V9X_DD_SHARED`, which doesn't exist before the HAL loads. The HAL side is already safe (it drains on hardware status, never flags). GDI side: `V9XDIBBEGINACCESS` (`runtime.asm:76-79`) becomes a two-instruction fast path (dirty check → `jmp DIB_BeginAccess`) with a C slow path that drains the engine, clears the flag, then calls `DIB_BeginAccess` — this covers interrupt-time cursor draws per the DDK BeginAccess contract, so the drain path touches only MMIO/ports and DGROUP flags (no profile/serial calls; poison reporting is deferred to the next BitBlt or Disable). Each accelerated op is bracketed with `DIB_BeginAccess/EndAccess(CURSOREXCLUDE)` over the union rect to lift the software cursor. Engine ops are async: queue command, set dirty, return.

5. **Failure containment.** Bounded spins (start at the 32-bit orders of magnitude, calibrate on 86Box); on timeout run the CR66 bit-1 reset exactly as `v9x_engine_recover` (`ddhal.c:603-614`), then set a session-long **poison latch**: every future op declines at gate #1, the failed op is completed by `DIB_BitBlt` so pixels stay correct, and `Acceleration=gdi-poisoned` is written lazily to `C:\V9XHW.INI` + serial trace. Latch survives mode switches; clears only at reboot.

6. **Config.** SYSTEM.INI `[Velocity9x]` read once per Enable via `GetPrivateProfileInt`: `GdiAccel`, `GdiAccelFill`, `GdiAccelCopy`, `GdiAccelOverlap`, `GdiAccelThreshold`. Compile-time defaults advance per rollout build. Effective state surfaces through the existing `Acceleration=` key (`ddi.c:317`, read by `settings_status.c:240`), e.g. `directdraw-fill-blt+gdi-fill-copy`.

## File-level changes

**New**
- `include/velocity9x/s3_engine_regs.h` — shared register/command constants (no code, no pointers).
- `src/display16/gdi_accel.c` — BitBlt dispatcher, gates, ViRGE/Trio primitives, waits, reset, poison latch, counters, config read, `v9x_gdi_begin_access_slow`.
- `src/display16/gdi_accel.h` — externs for ddi.c/dd16.c.

**Modified**
- `src/display16/dib_thunks.asm` — remove BitBlt forward (line 74).
- `src/display16/runtime.asm` — BeginAccess fast-path dirty check; allocate/free the ViRGE MMIO selector in `V9XHARDWAREENABLE`/`V9XHARDWAREDISABLE`; `V9XENGINESELECTOR` accessor (Trio build returns 0).
- `src/display16/ddi.c` — call `v9x_gdi_accel_configure()` from `v9x_build_pdevice`; extend `Acceleration=` string at :317; poison-report flush from Disable.
- `src/display16/dd16.c` — `V9X_GDIGETSTATS` / `V9X_GDIFAULTINJECT` escapes (pattern: `V9X_DDGETTRACE`); mirror timeout counters into `shared->engine.*` when the block exists.
- `src/display16/win9x_display_abi.h` — minimal DIBENG realized-brush struct + size assert, CURSOREXCLUDE/FB_ACCESS constants (exact values from `C:\98DDK\inc\win98\dibeng.h` — do not guess).
- `src/display32/ddhal.c` — include shared regs header, delete local defines; add Lock/Flip engine drain if the build-000 audit finds it missing.
- `scripts/build-win16-ddi-skeleton.ps1` — add gdi_accel source; `export BitBlt.1=BITBLT`; extend map/NE asserts.
- `tools/diag/gdi_smoke_win32.c` — `/accel` phase (below).
- `scripts/run-vm-mode-matrix.ps1` — new pass keys; optional SYSTEM.INI key injection for A/B runs.
- `include/velocity9x/win9x_ddraw_abi.h` — escape IDs + stats struct.
- `README.md` capability table, `PLAN.md` checkboxes, new `docs/decisions/` record.

## Watcom 16-bit specifics
- Build flags are `-bt=windows -mc -zu -zc -zls -s -zq -wx` (compact model, SS!=DS): exports must be `__loadds FAR PASCAL` like the existing `Enable`/`Disable`/`Control`.
- MMIO: `(volatile DWORD FAR *)MAKELP(v9x_engine_selector, off)`.
- Trio64 word I/O: `#pragma aux v9x_port_out_word = "out dx,ax" parm [dx] [ax]`.

## Rollout builds (each independently testable, `build/driver-results/gdi-accel-NNN`)

> **Build 000 has its own implementation plan:**
> [gdi-accel-000-and-harness.md](gdi-accel-000-and-harness.md). It carries the
> corrected file references, the first-party `BitBlt` signature and the extra
> `PALETTE_XLAT` gate, and the reasoning for why 000's exit gate must also run
> on an engine-less family.

**Stopped 2026-08-27, before build 005.** The `Default` column below records
what each build earned *in emulation*. All of it is now unreachable: the master
`GdiAccel` switch compiles to **0** because the fill path corrupts the display on
physical S3 Trio64 silicon
([issue](../issues/2026-08-27-gdi-accel-corrupts-display-on-physical-trio64.md)),
while 86Box passes 11/11 modes on both S3 chips at the very mode that fails.
Build 005 does not start until that is understood - adding ROPs to a fill path
that mis-addresses on hardware would only widen the blast radius.

| Build | Content | Default | Exit gate |
|---|---|---|---|
| gdi-accel-000 **(done)** | All infrastructure + primitives compiled, default-off; shared 2D register header; HAL Lock/Flip drain audit; `/accel` harness; per-family enable gate | off | **Behaviour** unchanged (not bytes - see below): V9XGDI PASS and the new `/accel` phase on the full mode matrix, on the engine family **and on an engine-less one**; Ironfield numbers unchanged; one manual INI-on run proving fill fires |
| gdi-accel-001 **(done)** | Solid fill (BLACKNESS/WHITENESS as ROPs, PATCOPY from the realized brush) | fill on | Randomized fill comparison PASS, engine counters nonzero, timeout injection recovers, DD probes unchanged |
| gdi-accel-002 **(done)** | Screen SRCCOPY, non-overlapping (overlap declines) | +copy on | Randomized non-overlap copy PASS; window-drag/scroll soak |
| gdi-accel-003 **(done)** | Overlap in all 8 directions | +overlap on | Randomized overlap PASS both chips; **full PLAN.md Phase 5 exit gate** - met except clipping regions, see the 003 record |
| gdi-accel-004 **(done)** | CPU-to-screen upload, narrowed to **monochrome expansion**; colour upload deliberately not implemented (see the design record). ViRGE only - the Trio64 declines | off | Same harness with memory-source ops: mono accelerates when on, declines when off, colour declines always, and the reject mask carries no unintended reason |
| gdi-accel-005 | Extra ROPs (DSTINVERT, PATINVERT, DPx/DPa) | per-ROP INI | Per-ROP conformance |

## Verification

1. **Build asserts**: `build-win16-ddi-skeleton.ps1` NE checks extended for the BITBLT export and new symbols; both ViRGE and Trio64 targets built.
2. **V9XGDI `/accel` phase** (new, existing smoke untouched so `Result=PASS` semantics stay stable): seeded LCG generates ~500 ops — random solid-color PatBlts (including below-threshold), screen-to-screen SRCCOPY BitBlts covering all 8 overlap directions, plus decline-noise ops — each mirrored into a DIBSection reference DC; compare via GetDIBits every 25 ops. Then query `V9X_GDIGETSTATS` via ExtEscape and **fail if engine counters are zero** when the build advertises the primitive (no vacuous pass). Finally `V9X_GDIFAULTINJECT` → one more PatBlt → assert desktop still renders, `Poisoned=1`, pixels still correct.
3. **VM matrix**: `scripts/run-vm-mode-matrix.ps1` across all modes on both the ViRGE (9869) and Trio64/native-S3 VM profiles, with A/B runs via SYSTEM.INI key injection.
4. **DirectDraw regression**: rerun the Ironfield BltFast benchmark and V9XDDP probe after each build (shared engine, new drain points must not regress the HAL paths in `docs/decisions/2026-08-14-virge-blitter.md`).

## Open items (verify during implementation, read-only against C:\98DDK)
1. ~~Exact CURSOREXCLUDE/FB_ACCESS values and realized-brush layout from `dibeng.h` (size-asserted).~~
   **Values resolved 2026-08-26: `FB_ACCESS = 0x0001`, `CURSOREXCLUDE = 0x0008`.**
   There is no `dibeng.h` in this DDK - the plan named a file that does not
   exist. They come from `C:\98DDK\inc\win98\inc16\DIBENG.INC`, which carries
   both an assembly `equ` (lines 126-127) and a C `#define` (lines 131-132) of
   each, so the C consumer has a first-party source. ~~The realized-brush layout
   still needs reading out of the same file and size-asserting.~~ **Read
   2026-08-26: recorded in
   [gdi-accel-000-and-harness.md](gdi-accel-000-and-harness.md) ("What is
   already done") - six per-depth structs at `DIBENG.INC:183-253` sharing a
   14-byte header; the fill gate needs only `BrushFlags & COLORSOLID` and
   `FgColor`.**
2. ~~Whether `V9xLock`/`V9xFlip` in ddhal.c drain the engine (build-000 audit).~~
   **Audited 2026-08-26: both already drain, so build 000 has nothing to add
   here and the "new drain points must not regress the HAL paths" risk in
   Verification §4 does not apply to Lock or Flip.** `Flip`
   (`src\display32\ddhal_core.c:340`) and `Lock` (`:503`) each call
   `v9x_wait_idle` under `v9x_engine_status_validated()` and return
   `DDERR_WASSTILLDRAWING` when the engine will not go idle, honouring
   `DDFLIP_DONOTWAIT` / `DDLOCK_DONOTWAIT` respectively. Two further CPU-access
   boundaries at `:448` and `:481` do the same.
3. ~~16-bit spin-limit calibration on 86Box via serial log.~~ **Closed
   2026-08-26 in `gdi-accel-001`, by a failure rather than by a measurement
   campaign, and the answer is that the 16-bit limits are the 32-bit limits.**

   They were first set 64 times shorter, scaled down on the grounds that this
   driver is built without a `-3` so an iteration costs 60-100 clocks against
   the flat HAL's ten. That scaled the wrong quantity. An iteration's cost is
   its bus access, not its instructions: on the Trio64 every spin is an
   `in ax,dx` on a 9AE8h port, ISA-timed at roughly a microsecond and identical
   in both bitnesses, and on the ViRGE it is an uncached MMIO dword read. The
   loop overhead is noise beside either.

   Found because it broke: at 0x00010000 the Trio64's idle wait expired on real
   uninjected work in exactly its three largest modes - 1024x768x16,
   1280x1024x8, 1280x1024x16 - latching the poison and silently turning
   acceleration off for the rest of the boot, while every mode of 960 KB or
   less was fine. The ViRGE passed all eleven, which is the asymmetry that
   identifies the cause: its MMIO read is cheaper than a port cycle.

   The `/accel` phase did not catch it on the first pass, and that is worth
   recording too: its zero-counter check compared `fills` against zero rather
   than against its own starting snapshot, so fills from the smoke phase
   earlier in the same boot satisfied it. It now compares deltas and fails on
   an unrequested poison (`poisoned-before-run`, `poisoned-during-run`).

## Corrections to this plan, 2026-08-26

The plan was written against a HAL layout that no longer exists, so every file
reference in the Context and Design sections above needs reading with this in
mind. Nothing about the *design* changed - only where the code lives.

- **`src/display32/ddhal.c` is gone.** The HAL was split into
  `src\display32\ddhal_core.c` plus per-chip `src\display32\engines\eng_s3_virge.c`
  and `eng_s3_trio.c`, with `ddhal_internal.h` shared between them. So the
  "ddhal.c:36-147" register block, "v9x_virge_copy (ddhal.c:1134-1144)" and
  "v9x_trio_copy (ddhal.c:1207-1219)" citations are all stale. The per-chip
  split is helpful rather than not: the engine ops are already behind a
  `wait_idle`/`fill`/`copy` ops table (`eng_s3_trio.c:193`), which is the shape
  the 16-bit side wants to mirror.
- **Design decision 1 is done.** The 2D register map now lives in
  `include\velocity9x\s3_engine_regs.h`, moved out of `ddhal_internal.h`, which
  includes it. Verified a pure preprocessor move: `v9xhal.dll` built with a
  pinned build id is **byte-identical** across the change
  (`1BEDF7BD692792F39F23ED7B1E182DF597EA65BAAC85CF5E8B90F71C847A3685`). The
  ViRGE S3D windows at 0xb4xx deliberately stayed behind in `ddhal_internal.h`:
  they are Direct3D's, the 16-bit side has no use for them, and copying them
  into a shared header would invite a 16-bit caller to poke the 3D pipeline.
  `V9X_VBLANK_SPIN_LIMIT` also stayed, being a DirectDraw service.
- **`runtime.asm:76-79` is stale.** `V9XDIBBEGINACCESS` (design decision 4) now
  sits at `runtime.asm:133-136`, and it has a sibling the fast path must also
  cover: `V9XDIBBEGINACCESSRECT` (`:231-234`) jumps to the same
  `DIB_BeginAccess` and is what ReEnable's live-switch cursor exclusion calls.
  The child plan carries the detail.
- **The 16-bit selector constraint is now load-bearing on that header.** Every
  ViRGE offset in it is below 0x10000 and must stay so, because the 16-bit side
  reaches them through one LDT selector based at linear + 0x01000000. The header
  says this where someone adding a register will read it.

## Gate wording this stage found to be wrong

Recorded here because the wording, not the work, is what needed changing.

- **"Byte-identical behavior" for build 000 was not achievable and should not
  have been asked for.** Build 000 adds a translation unit and an ordinal-1
  export, so the DRV cannot hash the same and there is no point pretending
  otherwise. The gate is behaviour: what the driver *does* must be
  indistinguishable. See
  [gdi-accel-000-and-harness.md](gdi-accel-000-and-harness.md) Block 3.
- **"Both targets" was too narrow.** `s3` is the only family with a 2D engine;
  `ati`, `vbe` and `matrox-m2` declare `EngineType = NONE` on every chip, and
  all four link the same `ddi.c`/`dd16.c`. Ordinal 1 is now a C function in the
  shared 16-bit layer, so *every* family gets the dispatcher and three of them
  take its decline branch on every blit, permanently. The exit gate therefore
  has to be run on a family with no engine as well.
- **The "byte-identical `v9xhal.dll`" claim in the Corrections below is not
  reproducible today, and the check that replaces it is better.** The HAL is a
  Win32 PE and its COFF header carries the link timestamp, so two builds of
  identical source from the same pinned `-BuildId` hash differently. What was
  verified for the second half of the register-header move (2026-08-26) is that
  all seven HAL translation units **disassemble identically** across it -
  `wdis -a` on every `.obj`, byte-for-byte equal. That is the property the
  claim was reaching for, and unlike an image hash it cannot be satisfied by
  accident or defeated by a clock.
- **Design decision 1 was only half done.** The register move left the 2D
  status, command, ROP, source-base, stride-mask and coordinate-limit constants
  behind in `ddhal_internal.h`, which the 16-bit primitives need. They are in
  `include\velocity9x\s3_engine_regs.h` now. `V9X_VIRGE_FIFO_SPIN_LIMIT` and
  `V9X_VIRGE_IDLE_SPIN_LIMIT` deliberately stayed: they are the flat HAL's own
  calibration, and the 16-bit side's loop does not cost the same, so sharing
  one number would silently make one of the two wrong.
- **Design decision 2's "plain `return DIB_BitBlt(...)`" needs one more hop.**
  This driver compiles PASCAL exports with their names uppercased - the reason
  the build script says `export Control.3=CONTROL` - so a C `extern WORD FAR
  PASCAL DIB_BitBlt(...)` asks the linker for `DIB_BITBLT` while `DIBENG.LIB`
  supplies `DIB_BitBlt`. The decline branch calls a typed
  `V9XDIBBITBLTCALL` forward in `runtime.asm`, exactly as every other DIBENG
  routine C code calls in this driver already does.
- **Design decision 4's "two-instruction fast path" is four.** The DIB Engine
  calls `deBeginAccess` with DS holding whatever its caller had, so the dirty
  flag is reached through an explicit `mov ax,DGROUP / mov es,ax` - the idiom
  every thunk in `dib_thunks.asm` uses for the same reason.
- **The 16-bit MMIO plan needed a correction the design did not anticipate.**
  Design decision 1 said the 16-bit side reaches the ViRGE registers as
  `volatile DWORD FAR *` through one selector. It cannot: this driver is built
  without a `-3`, so wcc emits 8086 code and a 32-bit store compiles to two
  16-bit halves. On `CMD_SET` that is wrong rather than slow, because the low
  half starts the blit. Every 32-bit engine access goes through
  `V9XENGINEREAD`/`V9XENGINEWRITE` in `runtime.asm`, where `.386p` guarantees
  one bus cycle, and `audit-family-binary.ps1` asserts the single-instruction
  forms.
- **Design decision 6's combined `Acceleration=` string was dropped.** That key
  is per-chip manifest data written by the chip module
  (`s3_regs16.c:315` writes `device->acceleration`), and GDI acceleration is
  decided by the shared 16-bit layer from SYSTEM.INI. Appending to it would put
  this layer's policy inside the chip's key, and `settings_status.c` reads that
  key expecting the manifest's own words. GDI state is published as its own
  `GdiAcceleration` key instead.

### What build 000 still needs

With the above done and items 1-2 closed, `gdi-accel-000` reduces to: the
realized-brush struct and its size assert, `src\display16\gdi_accel.c` with the
primitives compiled but default-off, the `BeginAccess` fast path, the BitBlt
export and dispatcher with an unconditional decline, and the build-script
asserts. The V9XGDI `/accel` harness in Verification §2 should land with or
before the first build that turns a primitive **on** (001), not after - it is
the only thing that makes a fill or copy claim checkable, and a randomized
comparison against a reference DC is not something to retrofit onto code that
has already been declared working.
