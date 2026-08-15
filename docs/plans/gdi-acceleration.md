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

| Build | Content | Default | Exit gate |
|---|---|---|---|
| gdi-accel-000 | All infrastructure + primitives compiled, default-off; ddhal.c header refactor; HAL Lock/Flip drain audit | off | Byte-identical behavior: V9XGDI PASS, full mode matrix, Ironfield numbers unchanged, both targets; one manual INI-on run proving fill fires |
| gdi-accel-001 | Solid fill (BLACKNESS/WHITENESS, then PATCOPY+solid brush) | fill on | Randomized fill comparison PASS, engine counters nonzero, timeout injection recovers, DD probes unchanged |
| gdi-accel-002 | Screen SRCCOPY, non-overlapping (overlap declines) | +copy on | Randomized non-overlap copy PASS; window-drag/scroll soak |
| gdi-accel-003 | Overlap in all 8 directions | +overlap on | Randomized overlap PASS both chips; **full PLAN.md Phase 5 exit gate** |
| gdi-accel-004 | CPU-to-screen upload (design after 003) | off | Same harness with memory-source ops |
| gdi-accel-005 | Extra ROPs (DSTINVERT, PATINVERT, DPx/DPa) | per-ROP INI | Per-ROP conformance |

## Verification

1. **Build asserts**: `build-win16-ddi-skeleton.ps1` NE checks extended for the BITBLT export and new symbols; both ViRGE and Trio64 targets built.
2. **V9XGDI `/accel` phase** (new, existing smoke untouched so `Result=PASS` semantics stay stable): seeded LCG generates ~500 ops — random solid-color PatBlts (including below-threshold), screen-to-screen SRCCOPY BitBlts covering all 8 overlap directions, plus decline-noise ops — each mirrored into a DIBSection reference DC; compare via GetDIBits every 25 ops. Then query `V9X_GDIGETSTATS` via ExtEscape and **fail if engine counters are zero** when the build advertises the primitive (no vacuous pass). Finally `V9X_GDIFAULTINJECT` → one more PatBlt → assert desktop still renders, `Poisoned=1`, pixels still correct.
3. **VM matrix**: `scripts/run-vm-mode-matrix.ps1` across all modes on both the ViRGE (9869) and Trio64/native-S3 VM profiles, with A/B runs via SYSTEM.INI key injection.
4. **DirectDraw regression**: rerun the Ironfield BltFast benchmark and V9XDDP probe after each build (shared engine, new drain points must not regress the HAL paths in `docs/decisions/2026-08-14-virge-blitter.md`).

## Open items (verify during implementation, read-only against C:\98DDK)
1. Exact CURSOREXCLUDE/FB_ACCESS values and realized-brush layout from `dibeng.h` (size-asserted).
2. Whether `V9xLock`/`V9xFlip` in ddhal.c drain the engine (build-000 audit).
3. 16-bit spin-limit calibration on 86Box via serial log.
