# Engine fault injection

Date: 2026-08-16
Status: accepted

## Why

Phase 7 of `docs/plans/multi-chip-restructure.md` gates on "timeout-injection
recovery (`fifo_timeouts`/`reset_count`) matches baseline". No mechanism to
induce a timeout existed. `V9X_GDIFAULTINJECT` is only a proposal in
`docs/plans/gdi-acceleration.md`, and `ddhal.c` had no INI knob, escape or
build-time switch that could force one.

On healthy emulated hardware the bounded waits never expire, so every counter
the gate names is zero and stays zero. The gate would have passed vacuously —
immediately after a split that rewrites `v9x_wait_fifo`, `v9x_wait_idle`,
`v9x_trio_wait_idle` and `v9x_engine_recover` into a vtable. Those functions
are precisely what it exists to protect, so it was worth making real rather
than rewording.

## What

- `V9X_DD_ENGINE.reserved0` becomes `fault_inject`. Layout is unchanged and
  the ABI value is not bumped again: spending a reserved DWORD is what it was
  reserved for, and phases 6 and 7 land in one release train.
- New project-private escape `V9X_DDFAULTINJECT` (`'V9FI'`), armed with a
  count through the existing `V9X_DCICMD.dwParam1`, so no new struct. Handled
  in `dd16.c` beside `V9X_DDGETTRACE`; the 16-bit side only writes the count.
- All three bounded waits in `ddhal.c` consume it. An armed wait skips its
  fast check and spin loop and falls into the **existing** timeout tail:
  count the timeout, flush the fault trace, run the engine's recovery. The
  tails themselves are untouched, which is the point — the injector drives
  production code rather than a parallel test path.
- Consumption is gated on `wait &&`, so a non-blocking probe (`ddhal.c:1570`)
  cannot spend an injection on a "not ready" answer it was always entitled to
  give.
- `V9XTRACE.EXE -inject=N` arms; `EngineFaultInjectRemaining` reports what is
  left. Arming is separate from dumping because the tool issues no blits: a
  real workload has to run in between.

It ships enabled rather than behind a build-time define, so the binary under
test is the binary that ships. It is inert until something issues a private
DCICOMMAND escape, and its only effect is to force an already-existing,
already-survivable recovery path.

## Procedure

```
V9XTRACE.EXE            # baseline counters
V9XTRACE.EXE -inject=4  # arm
V9XDDP.EXE              # workload consumes them
V9XTRACE.EXE            # compare
```

## Baseline, 2026-08-16 (pre-phase-7)

Both guests freshly booted, counters starting at zero, four injections armed:

| | ViRGE | Trio64 |
|---|---|---|
| `EngineFifoTimeouts` | **4** | 0 |
| `EngineIdleTimeouts` | 0 | **4** |
| `EngineResets` | **4** | **0** |
| `EngineFaultInjectRemaining` | 0 | 0 |
| `CountBlt` / `CountBltEngine` | 11 / 7 | 10 / 3 |
| Desktop after the run | alive | alive |

All four injections were consumed on both targets, and the declined blits show
up exactly as CPU fallbacks: ViRGE's four forced timeouts are the whole of its
11-vs-7 gap; Trio64's gap is its four forced timeouts plus the three genuine
declines its 8514/A engine cannot address. Both desktops survived.

The counters land in different places per target, and that is the finding
worth carrying into the split:

- ViRGE forces through `v9x_wait_fifo`, which calls `v9x_engine_recover()`, so
  resets track timeouts one for one.
- Trio64 forces through `v9x_trio_wait_idle`, which has **no recovery call at
  all**. Its `reset_count` stays flat by design.

So `recover` is genuinely absent for the Trio64 engine rather than a shared
no-op, and the `v9x_engine32_ops` vtable must allow a null `recover` instead of
assuming every engine has one. A gate asserting "resets match baseline"
uniformly across both targets would be wrong.

## What the first run caught

The injector was armed successfully but silently cleared before any wait could
spend it: `InjectArmed=1`, `EngineFaultInjectRemaining=0`, every blit still on
the engine. The cause was a disarm-on-refresh in `v9x_dd_refresh_framebuffer`,
added on the reasoning that an injection armed against one mode should not
survive into the next.

Both halves of that reasoning were wrong. The refresh runs on DirectDraw
session setup, which is between arming and every possible workload, so the knob
could never fire. And it provided no initialisation either — `v9x_dd_block`
zeroes the whole shared block on allocation. A forced timeout acts on whichever
bounded wait runs next, which is mode-independent, so there was nothing to
protect against. The clear was removed; the escape is now the only writer and
HAL consumption the only decrementer.
