# The 32-bit engine dispatch vtable and the ddhal.c split

Date: 2026-08-16
Status: accepted

Phase 7 of `docs/plans/multi-chip-restructure.md` collapses the runtime
`v9x_trio_engine_ready() ? trio : virge` pairs in `src/display32/ddhal.c` onto
one `v9x_engine32_ops` table selected from `engine.engine_type`, then splits
the file along the seams that table exposes. The two landed as separate
commits, in that order, so the dispatch change and the physical motion fail
independently — a behavioural regression in the first would have shown up
before 3500 lines moved on top of it.

## What the four call sites actually asked

The pairs looked interchangeable and were not. Written out, the four sites ask
three different questions of the engine, plus a fourth that only DirectDraw
asks:

| site | ViRGE gate | Trio64 gate |
|---|---|---|
| `v9x_blt_drain` | `v9x_engine_status_validated()` (passive) | `v9x_trio_engine_ready()` |
| `v9x_srccopy_body` | `v9x_engine_validate_status()` (latching) | `v9x_trio_engine_ready()` |
| `v9x_colorfill_body` | `v9x_engine_validate_status()` (latching) | `v9x_trio_engine_ready()` |
| `V9xHalGetBltStatus` DDGBS_CANBLT | `v9x_engine_validate_status()` | `v9x_trio_wait_idle(0)` |

So the table needs `ready`, `validate_status` (may a command be issued now —
latching on the ViRGE), `status_validated` (has that already happened — the
passive form the drain wants), and `can_blt`, alongside `wait_idle`, `fill` and
`copy`. On the Trio64 the first three are all its single flag test, because its
8514/A engine has no status to latch; on the ViRGE they are three distinct
things. Collapsing `validate_status` and `status_validated` into one member
would have made the drain latch the engine as a side effect of asking whether it
was busy, and collapsing `validate_status` with `can_blt` would have made a
Trio64 CANBLT answer "yes" while the engine was still drawing.

`V9X_DD_ENGINE_STATUS_VALIDATED` once aliased `V9X_DD_ENGINE_S3_TRIO64`, which
made validating a ViRGE's status route its next blit down the Trio64 port-I/O
command sequence. That is the same confusion in flag-bit form, and it is why
these members are kept apart by name rather than by convention.

## No `recover` member

The plan listed `recover` in the vtable. It is not there. Recovery is never
dispatched across engines: `v9x_engine_recover` is called only from inside the
ViRGE bounded wait that expired, and the Trio64 has no recovery at all. The
fault-injection baseline in `docs/decisions/2026-08-16-engine-fault-injection.md`
measures exactly that — four forced timeouts give ViRGE `fifo_timeouts=4` and
`reset_count=4`, and Trio64 `idle_timeouts=4` with `reset_count=0`. A member no
caller reads would only invite the assumption that every engine has one. When
the waits move into `engines/eng_s3_virge.c` at the file split, recovery goes
with them as a file-local call.

`set_display_start`, `in_vblank` and `build_caps` are likewise absent for now.
None of them is dual-dispatched today: the first two are chip-agnostic S3 CRTC
and 3DA access destined for `vga_scanout.c`, and the caps builder has one
implementation. They become table members when the files move, not before.

## Resolution is per call, not cached

`DriverInit` runs before the 16-bit side has filled the engine descriptor, so
there is nothing to select at load time. The resolver returns null unless
`V9X_DD_ENGINE_VALID` is set and `engine_type` is one it knows, and the result
is deliberately not cached: `v9x_dd_refresh_framebuffer` rewrites the descriptor
on every DirectDraw session setup, and a two-way switch costs less than the risk
of holding a pointer chosen from a block since invalidated. A resolved pointer
still means nothing on its own — each table's own `ready`/`validate_status`
re-reads the descriptor.

## Retiring the identity bits

The dispatch collapse left `engine_type` selecting the table while each table's
`ready()` still tested a `V9X_DD_ENGINE_S3_*` bit, with `dd16.c` deriving one
from the other. Two statements of the same fact, kept in step by hand. Both
predicates now test `engine_type` directly and `dd16.c` writes
`flags = V9X_DD_ENGINE_VALID` alone, so `flags` is runtime state — VALID plus
the `STATUS_VALIDATED` latch — and identity is data.

Bits 0x2 and 0x4 are left unassigned rather than reused, so a stale diagnostic
reading this field reports nothing rather than confidently reporting the wrong
chip. No ABI bump: the layout is untouched, and this is the same release train
as the bump that added `engine_type`.

That derivation also had a latent bug. It read as ViRGE for *any* `engine_type`
it did not recognise, `NONE` included, so a family with a descriptor hook but no
engine claimed a ViRGE. It was inert only because `v9x_engine_ready` separately
requires a non-zero control base and a mapped aperture. A family arriving with
a hook, no engine, and a valid aperture would have found the bug.

`V9XTRACE` had to learn the new vocabulary first, or retiring the bits would
have blinded it: `EngineFlags` alone can no longer say which chip ran. It now
also dumps `EngineType` and `EngineCaps`, and those are the values to read.

| | ViRGE | Trio64 |
|---|---|---|
| `EngineFlags` (was 3 / 5) | 1 | 1 |
| `EngineType` | 1 (`S3_VIRGE_DX`) | 2 (`S3_TRIO64`) |
| `EngineCaps` | `0x1F` | `0x0F` |

`EngineCaps` is the first direct reading of the phase 6 caps data on live
hardware, and it says what the D3D gate says by another route: the ViRGE claims
`D3D`, the Trio64 claims the other four capabilities and not that one.



## Header layering

`V9X_DD_ENGINE_TYPE_*` and `V9X_DD_ENGINE_CAP_*` were in `include/velocity9x/hw16.h`,
the 16-bit hardware layer, and `win9x_ddraw_abi.h` reached into that header to
get them — so the flat 32-bit HAL was including the 16-bit hardware layer to
learn its own engine's identity. They now live in a new
`include/velocity9x/engine_abi.h` with no includes at all, which both headers
pull in. That keeps `src/chipsets` free of `<windows.h>` as before and gives the
32-bit side the vocabulary without the layer it belongs to.

## The split

`ddhal.c` became six translation units and one private header:

| file | holds |
|---|---|
| `ddhal_core.c` | DirectDraw callbacks, trace ring, surface bookkeeping, Lock/Unlock/Flip, blit dispatch, ops resolution, `DriverInit` |
| `blt_cpu.c` | the CPU fill and copy every engine falls back to |
| `engines/vga_scanout.c` | CRTC display start and the vblank bit |
| `engines/eng_s3_virge.c` | MMIO engine, bounded waits, CR66 recovery, ops table |
| `engines/eng_s3_trio.c` | 8514/A port-I/O engine, ops table |
| `d3d/d3d_virge.c` | the whole S3D path and the caps it publishes |

`ddhal_internal.h` is the seam, and it is deliberately narrow: a symbol appears
in it only where a second unit needs it. That is what makes the split worth
more than the file count — the ViRGE's MMIO accessors, its engine recovery, and
the Trio's readiness test are now unreachable from outside their own engine,
which no amount of care could enforce while they were file-scope statics in one
3500-line file. It is also the module's only `<windows.h>`, which `check-tree`
now enforces in place of the old per-file allowance, so a new HAL module cannot
quietly acquire its own OS boundary.

Two pieces are not verbatim motion. The CPU copy loop that was inline in
`v9x_srccopy_body` became `v9x_cpu_copy` beside `v9x_cpu_fill`, so `blt_cpu.c`
holds both fallbacks rather than one and a half. And the D3D capability block
`DriverInit` wrote inline became `v9x_d3d_publish`, called at the same point
with the same fields written in the same order, so the D3D module owns what it
advertises rather than having the core advertise on its behalf.

`build-ddraw-hal-dll.ps1` compiles and links a source list, and refuses two
sources sharing a base name because the objects share one output directory.

### What was not split, and why

`engines/eng_none.c` is not there. A null ops pointer already *is* tier-0: the
resolver returns null for an unknown `engine_type`, and every call site treats
that as "no engine", falling to `blt_cpu.c`. A module of stubs would add a file
without adding behaviour. It becomes worth writing when `build_caps` joins the
table and tier-0 needs to publish Lock/Unlock-only caps of its own.

The S3 register vocabulary went into `ddhal_internal.h` rather than the
`include/velocity9x/regs/s3.h` the plan sketches. Nothing outside the 32-bit
HAL uses those constants today; the public header is worth creating when the
GDI acceleration work needs the same registers from the 16-bit driver, which is
the sharing that motivated it.

## Verification

Behaviour is unchanged, which is the whole claim, so every gate is a comparison
against the pre-split reading rather than a pass/fail. The full set was run
twice — once after the dispatch collapse and again after the files moved — and
both rounds reproduced the baseline.

`scripts/run-checks.ps1 -BuildId golden-compare` green at both points. Both
guests updated and rebooted each time, with `V9XTRACE.EXE` and `V9XDDP.EXE`
pushed to the guest root alongside the driver, because
`update-associated-driver.ps1` refreshes the driver and not those.

| | ViRGE (9869) | Trio64 (9871) |
|---|---|---|
| `V9XDDP` D3D gate set | all 11 = 1 | no D3D HAL, as before |
| `GblHalCaps` / `GblHalDdsCaps` | `0x04000441` / `0x40623258` | `0x04000440` / `0x40200258` |
| `CountBlt` / `CountBltEngine` after `V9XDDP` | 7 / 7 | 6 / 3 |
| `CountFlip` / `CountLock` / `CountUnlock` | 23 / 61 / 61 | 23 / 35 / 35 |
| `CountCreateSurface` / `CountDestroySurface` | 9 / 10 | 4 / 4 |
| Ironfield, vtable commit | 18 FPS, 307 frames | 16 FPS, 279 frames |
| Ironfield, after the split | 18 FPS, 310 frames | 16 FPS, 276 frames |
| `CountBlt` / `CountBltEngine` after Ironfield | 317 / 317 | 282 / 279 |
| `-inject=4`: fifo / idle / resets | 4 / 0 / 4 | 0 / 4 / 0 |
| `CountBlt` / `CountBltEngine` under injection | 11 / 7 | 10 / 3 |
| desktop after injection | alive | alive |

The Ironfield frame counts move by a few frames between runs, as they did
across the pre-phase-7 captures; the reported FPS and the engine-versus-CPU
split are what the gate reads, and neither moved. The set was run a third time
after the identity bits retired, with the same result.

### Warnings as errors

The split surfaced six dead locals in `v9x_srccopy_body` — and the build
reported them and exited 0, because `-wx` is Watcom's warning *level*, not
warnings-as-errors. `-we` is now set on both the 32-bit HAL and the 16-bit
driver compiles, and both were already clean once those six were removed, so
nothing was suppressed to get there.

### Hellbender, as an enable regression

Run on the ViRGE after the split and the retirement: launcher rendered, New Game
switched 1024x768 to 640x480x16, gameplay rendered with correct colour, and the
orderly exit through the game's own menu restored the desktop to 1024x768. No
GPF, and the agent answered throughout — including with the game's "End without
saving?" prompt up.

It confirms the phase-6 finding rather than adding coverage. Every 32-bit HAL
counter was identical before and after: `CountBlt` 11, `CountBltEngine` 7,
`CountFlip` 23, `CountLock`/`CountUnlock` 61, and every D3D counter. The only
movement was `EnableCount` 3 to 9, `DisableCount` 0 to 4, `CountDriverInit` 1 to
2 and the `Dd16*` object-creation counters — the 16-bit enable path and
DirectDraw session setup, none of which phase 7 touched. It is the right
regression test for phase 8, and it remains the wrong instrument for this one.

Two notes for whoever runs it next. The agent's `screenshot` reads the GDI
primary and returns black while the game is flipping, so gameplay has to be
captured host-side by `PrintWindow` on the 86Box window. And `C:\V9XTRACE.INI`
existing afterwards is not evidence of a fault: `FaultCode=0x56394646` is the
ViRGE FIFO-timeout marker left by the `-inject=4` run earlier in the same boot,
not an exception.

Every figure matches `docs/decisions/2026-08-16-restructure-baseline.md` and
`docs/decisions/2026-08-16-engine-fault-injection.md`, including the asymmetries
those documents predicted: Trio64's three-blit gap is the copies its engine
cannot address, and its flat `reset_count` under injection is the absent
recovery path this design declines to invent a member for.

Hellbender was not re-run here. It exercises the 16-bit enable and mode-switch
path and moves no `ddhal.c` counter at all, so it certifies nothing this change
touches; it belongs at the end of phase 7, after the files move. The enable path
was still exercised — three clean boots per guest, plus the 1024x768 to
640x480x16 mode switch and back that every `V9XDDP` run performs.
