# Multi-chip restructure baseline

Date: 2026-08-16
Status: accepted

Phase 1 of `docs/plans/multi-chip-restructure.md` requires a snapshot of what
the driver does *before* the restructure, so every later phase has something to
diff against. This records what was captured, where it lives, and the reference
values a later phase must reproduce.

## Build under test

Every artefact below was produced by, or captured against, a build pinned with
`-BuildId golden-compare` from the tree at the time of writing. Both S3 guests
were updated to that exact build with `update-associated-driver.ps1` and
byte-verified after reboot, so the guest-side numbers belong to the same image
as the host-side hashes.

## Host-side archive

`..\velocity9x-golden\golden-compare\` — deliberately outside `build\`, because
`build\` is what the restructure rewrites.

- `golden.txt` — 119 entries: normalised SHA-256 of every file in
  `build\win98se-active`, `build\win98se-trio64`, `build\floppy` and
  `build\matrox-candidate`, plus the segment and DGROUP sizes from all three
  `v9xdisp.map` files.
- A copy of each tracked tree.

Recapture or verify with `scripts/golden-baseline.ps1`. Through phase 3 the
comparison is exact; from phase 4 on the images legitimately change and the map
sizes become the budget check (2 KiB per step, 1 KiB for phase 4).

## Guest-side archive

`..\velocity9x-golden\golden-compare\vm\{virge,trio64}\` — `V9XHW.INI`,
`V9XBOOT.INI`, `V9XDD.INI`, `V9XSNAP.INI`, `V9XGDI.INI`, `V9XWND.INI`,
`info.json`, `installed.json` and a desktop screenshot per guest.

### Hardware identity

Both guests report `Stage=enable-ok`. `V9XHW.INI` matches the family manifests
field for field, which is the cross-check that phase 2's manifest data is
faithful:

| | ViRGE | Trio64 |
|---|---|---|
| Adapter | `S3 ViRGE/DX 86C375` | `S3 Trio32/64 86C764` |
| Vendor / device | `5333` / `8A01` | `5333` / `8811` |
| ClockDetector | `s3-virge-pll-v1` | `s3-virge-pll-v1` |
| ModeSwitching | `live-any-depth` | `live-any-depth` |
| Acceleration | `directdraw-fill-blt` | `directdraw-fill-blt` |
| Direct3D | `hardware-s3d` | `not-advertised` |
| VideoMemoryBytes | 4194304 | 4194304 |
| CoreClockKHz | 56079 | 69800 |

### DirectDraw and Direct3D caps (`V9XDD.INI`, from `V9XDDP.EXE`)

This is the invariant the plan's D3D-leak risk turns on. It must survive the
`dd16.c` caps clamp becoming data-driven at phase 6 and the D3D block moving to
`eng_s3_virge` at phase 7.

| | ViRGE | Trio64 |
|---|---|---|
| `GblD3DGlobal` | non-null | `0x00000000` |
| `D3DHalFound` | 1 | 0 |
| `D3DHalFlags` | 451 | 0 |
| `D3DHalRenderDepth` | 1024 | 0 |
| `GblHalCaps` | `0x04000441` | `0x04000440` |
| `GblHalDdsCaps` | `0x40623258` | `0x40200258` |

`GblD3DGlobal` is the DirectDraw runtime's pointer to its D3D global, so its
value changes with every boot. Compare it as null versus non-null; the caps
either side of it are the ones that must match exactly.

### HAL callback counters (`V9XSNAP.INI`, after one `V9XDDP` run)

Rerunning `V9XDDP.EXE` and then `V9XTRACE.EXE` reproduces these deterministically,
which makes them a better regression gate than a frame-rate measurement.

| | ViRGE | Trio64 |
|---|---|---|
| `EngineFlags` | 3 | 5 |
| `CountBlt` / `CountBltEngine` | 7 / 7 | 6 / 3 |
| `CountFlip` | 23 | 23 |
| `CountLock` / `CountUnlock` | 61 / 61 | 35 / 35 |
| `CountCreateSurface` / `CountDestroySurface` | 9 / 10 | 4 / 4 |
| `EngineFifoTimeouts` | 0 | 0 |
| `EngineIdleTimeouts` | 0 | 0 |
| `EngineResets` | 0 | 0 |
| `CountD3dContextCreate` | 2 | - |

The Trio64 split of 6 blits with 3 engine-executed is expected, not a fault:
its 8514/A engine only serves display-pitch surfaces on scan-line boundaries,
and the probe's small-pitch overlap check is declined to the CPU copy by
design.

### Mode matrix

`run-vm-mode-matrix.ps1` passes all six modes on both guests: `enable-ok` after
every reboot, GDI test PASS, palette PASS in all three 8-bpp modes. Results and
per-mode screenshots under `build/driver-results/baseline-matrix-{virge,trio64}`.

## Not captured here

- **Serial boot log.** Needs 86Box COM1 reconfigured as a named-pipe server
  and `capture-serial-pipe.ps1` running across a cold boot. Run it before
  phase 5, which is where the enable sequence moves and the serial log becomes
  the primary evidence.
- **Induced per-stage failure strings.** `v9x_trace_hardware_failure` maps
  stage codes 1-10 to `fail-hardware-*` markers. Phase 5 must preserve the
  numbering verbatim; the induced-failure sweep belongs with that phase rather
  than here.
- **Ironfield BltFast FPS and the Hellbender D3D run.** Both are fullscreen
  DirectDraw workloads driven by input injection, and a modal GPF in the guest
  holds the Win16Mutex and wedges the remote agent. The reference numbers for
  this same source are recorded in
  `docs/decisions/2026-08-14-virge-blitter.md`: BltFast at 640x480x16 gives
  18 FPS on ViRGE and 16 FPS on Trio64, every frame engine-executed. Re-measure
  before phase 7 signs off.

## Incidental findings

- `run-vm-mode-matrix.ps1` never passed `-Port` to the controller, so it could
  only ever drive the guest on 9869; the Trio64 guest was unreachable. It now
  takes `-Family` and reads the port, package path and mode list from the
  manifest.
- Remote agent 0.5.2 reports `BitsPerPixel` as 0 against the Velocity9x driver
  while reporting it correctly against the stock S3 driver. The in-guest GDI
  test reads the true depth through `GetDeviceCaps`, so the matrix now verifies
  depth from `V9XGDI.INI` and only cross-checks the agent value when it is
  non-zero. This is agent-side; the driver is not implicated.
