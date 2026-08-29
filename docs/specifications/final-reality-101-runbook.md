# Final Reality 1.01 runbook (Win86SE guest)

Status: current, 2026-08-30
Applies to: the `s3` family on `Win86SE` (86Box, ViRGE/DX 86C375, 4 MiB,
agent port 9869). Nothing here is emulator-specific except the timings.

## Why this document exists

[`final-reality-101-hardware.md`](../plans/final-reality-101-hardware.md)
recorded four rounds of FR *results* and never the *procedure*, which blocked
its own step 4 for two weeks. The numbers were reconstructible only by reading
coordinates off the screenshots in `build\driver-results\fr101-*-vm1`. This is
that procedure, written down after using it.

Two rules that apply throughout:

- **The agent's screenshot is black for the whole benchmark.** It reads the
  GDI primary and FR runs fullscreen DirectDraw. Black means running, not
  hung. The first non-black frame is the UI coming back.
- **Do not run `V9XTRACE.EXE` before FR.** It faults in KRNL386 once
  DirectDraw has run on the boot
  ([issue](../issues/2026-08-30-trace-dump-krnl386-flush-gpf.md)), and a
  16-bit GPF dialog holds the Win16Mutex and wedges the agent - which would
  cost the whole FR run. Take the trace *after* the results are captured,
  when losing the guest costs nothing.

## Preconditions

- FR 1.01 installed at `C:\Program Files\Final Reality\FR.exe`. It stores its
  settings in the registry, not an INI, so there is no file to preset - the
  test selection has to be clicked.
- The driver package deployed and the guest rebooted.
- Desktop at **1024x768x16**. Every coordinate below is for that mode, and
  the Benchmark results dialog does not fit in 640x480. Set it with the
  package's own tool rather than Display Properties:

```bash
v9xctl exec -Port 9869 -Json -Application "C:\V9XREMOTE\JOBS\<job>\V9XMSW.EXE" -Arguments "/set:1024x768x16" -WorkingDirectory "C:\V9XREMOTE\JOBS\<job>"
```

## Driving it

`v9xctl`'s input verbs take a **button name, not coordinates**: a click is two
segments, `move X Y; click left`. `click 500 400` fails with "Unknown mouse
button". Launch GUI apps with `exec -Detach -ShowWindow`; never
`shell -Command "start ..."`, which wedges the agent.

| Step | Control | Click at |
|---|---|---|
| 1 | Launch `FR.exe` detached | - |
| 2 | **Licence agreement** -> `Accept` | 458, 512 |
| 3 | Main window -> `Advanced options` | 614, 414 |
| 4 | 2D tests: `Radial blur`, `Chaos zoomer` | 238, 227 / 238, 253 |
| 5 | 3D tests: `25 pixel`, `Robots`, `Fill rate`, `City scene` | 238, 327 / 353 / 380 / 406 |
| 6 | Bus tests: `2D transfer rate`, `3D transfer rate` | 238, 472 / 238, 498 |
| 7 | `Run all tests 5 times` | 442, 472 |
| 8 | `Run advanced benchmark` | 716, 429 |
| 9 | (after the run) `Display results` | 716, 478 |
| 10 | `3D tests` tab | 384, 183 |

The licence dialog appears on every launch. Accepting terms on someone's
behalf is a decision for whoever is running this, not for an agent driving it
unattended.

All four 3D tests and both 2D and both bus tests are checked by default. The
2D and bus tests do not touch Direct3D; clearing them saves roughly a third of
the wall clock and changes no 3D number.

FR returns to **Advanced Options**, not to a results dialog, when the
benchmark finishes. `Display results` is a separate click.

## Waiting for it

Four 3D tests at five repeats took **14.5 minutes** (03:24:00 to 03:38:48) on
this guest. The 25-pixel test alone is about two minutes.

Poll for the first non-black frame rather than sleeping blind. A grid sample
of a screenshot is enough - the mean of a 24-pixel grid reads 0 during the
run and >100 when the desktop is back. `build\driver-results\fr101-zfifo-vm1\
RUN-TIMING.LOG` is one such log.

## Reading the results

**Take the screenshot of the `3D tests` tab. It is the evidence.**

`Save results to file` does **not** save your run. It writes the database
entry currently selected, which with `Compare to: <none>` is FR's built-in
`Baseline system (Pentium 150 MHz + S3 Virge/VX)` - a fixed reference whose
numbers look plausible enough to be mistaken for a measurement. The file kept
at `FR-DATABASE-BASELINE-ENTRY.TXT` is that reference, kept so the next person
recognises it rather than re-derives it. To export your own run you must
`Add your system to database` first, which writes to FR's installed database.

**`Visual appearance` is not an image-quality measurement.** It read exactly
`74.07 %` before hardware depth testing worked and exactly `74.07 %` after,
and FR's built-in ViRGE reference entry also reads `74.07`. The most
consistent reading is that it is derived from the advertised capability set -
the `3D graphics options` list in Advanced Options, which is identical across
both runs - rather than from rendered pixels. Not proven: proving it needs a
run with a deliberately different capability set. Until then, do not read a
change in it, or the absence of one, as a correctness result.

## Afterwards

Take the trace, and read the **DWORD** diagnostics, not the ring counters:

```
D3dRenderPrimitiveCalls=2697602      <- authoritative
CountD3dRenderPrimitive=10626        <- WORD, wrapped 41 times
```

`trace.counters[]` is `WORD[]`. Anything FR drives wraps it within seconds, so
`CountD3d*` is only useful for callbacks that fire tens of times.
`CountAddAttachedSurface` cross-checks `D3dDepthOffered`.

Expect zero in `EngineFifoTimeouts`, `EngineIdleTimeouts`, `EngineResets` and
`D3dContextRejects`, and `D3dContextCreates == D3dContextDestroys`.
`D3dTextureDestroys` may trail `D3dTextureCreates` by a few while FR is still
open.
