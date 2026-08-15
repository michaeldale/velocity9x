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

## Serial boot log

The Trio64 profile was already configured with `serial1_device = pipe` on
`trio64-com1`, so its boot log needs no configuration change. Captured from
the pre-restructure package and again from the phase 5a build, the log is
identical:

```
V9X-DRV disable
V9X-MINI init build=golden-compare
V9X-MINI power-callbacks-ok callbacks=4 build=golden-compare
V9X-DRV load build=golden-compare
V9X-DRV lfb=0xE7000000 bytes=00400000
V9X-DRV enable-ok mode=1024x768x16 lfb-mapped
```

The ViRGE profile still uses `serial1_device = file`, which 86Box buffers
until exit. Switching it to the `velocity9x-com1` pipe already declared in its
config needs that VM stopped, so it has not been changed.

## Induced failure

Pointing the Trio64 family's device list at a non-matching PCI ID
(`5333:88FF`) produced `Stage=fail-hardware-present`, and Windows fell back to
640x480 VGA rather than failing to boot. The guest recovered by redeploying
the good package; no manual Safe Mode was needed.

Two things came out of that:

- **`fail-hardware-pci` (stage code 1) is not reachable by an absent card.**
  `V9xHardwarePresent` is queried before `V9XHARDWAREENABLE` and short-circuits
  first, so an absent device always reports `fail-hardware-present`. Reaching
  stage 1 would need the present check to succeed and the enable-time find to
  fail, and both call the same `V9xFindPciDevice`. Treat stage 1 as
  effectively dead unless that changes.
- The remote agent stops answering for several minutes across a failed
  display bring-up and then recovers, reporting `DesktopReady = False` while
  shell commands work. Budget for that before concluding a guest is lost.

The remaining stage codes were verified structurally rather than by induction:
the `mov V9xHardwareStageCode, N` sequence in `runtime.asm` is unchanged in
value and order (1, 2, 9, 3, 4, 8, 4, 0, 5, 6, 7, 0), and the switch in
`v9x_trace_hardware_failure` is untouched. Inducing each of the remaining
stages costs a build, an install and a failed boot per stage on a guest that
has to be recovered afterwards, which is out of proportion to what it proves
while those two facts hold.

## Ironfield BltFast FPS (captured 2026-08-16, pre-phase-7)

Captured against the guests as installed for phase 6 (`PendingJob`
`phase6-virge` / `phase6-trio64`), so these belong to the post-ABI-bump HAL
rather than to the `golden-compare` snapshot above. That is the point: phase 7
splits `ddhal.c`, and the comparison it needs is against the tree it starts
from, not against the pre-descriptor-generalisation numbers in
`docs/decisions/2026-08-14-virge-blitter.md`.

Run: `C:\IRONFIELD\IRONFIELD.EXE -benchmark -fullscreen -renderer-video`,
640x480x16, MMX on, music off, one 15 s run per guest, appended to
`C:\IRONFIELD\ironfield-benchmarks.log`.

| | 2026-08-14 reference | 2026-08-16 pre-phase-7 |
|---|---|---|
| ViRGE | 18 FPS, 304 frames / 16613 ms | **18 FPS, 309 frames / 16642 ms** |
| Trio64 | 16 FPS, 275 frames / 16581 ms | **16 FPS, 274 frames / 16569 ms** |

Engine counters from `V9XTRACE.EXE` (`C:\V9XSNAP.INI`), cumulative for the
boot that contains the run:

| | ViRGE | Trio64 |
|---|---|---|
| `CountBlt` / `CountBltEngine` | 316 / 316 | 280 / 277 |
| `EngineFifoTimeouts` / `EngineIdleTimeouts` | 0 / 0 | 0 / 0 |
| `EngineResets` | 0 | 0 |
| `EngineFlags` | 3 | 5 |
| D3D callback counts | non-zero | **absent** |

`EngineFlags` is chip identity, not capability: `VALID|S3_VIRGE_DX` and
`VALID|S3_TRIO64`. These are the old flag bits the plan keeps until phase 7
retires them in favour of `engine_type` / `engine_caps`, so they are the last
reading of them before the vtable lands. `STATUS_VALIDATED` is clear on both.

ViRGE executes every blit on the engine. Trio64's three CPU fallbacks are the
documented decline of copies that are not display-pitch on a scan-line
boundary, which its 8514/A engine cannot address. Trio64 recording no D3D
callbacks at all is an incidental confirmation that the phase 6 data-driven
caps clamp still keeps D3D off that target.

### The modal-GPF wedge does not apply to this workload

The run was previously deferred on the grounds that a modal GPF holds the
Win16Mutex and wedges the remote agent. That risk is real but belongs to
Hellbender (`docs/issues/2026-08-14-hellbender-dibeng-gpf.md`), not here:

- Passing `-benchmark` on the command line makes Ironfield skip its launcher
  dialog *and* suppress the completion `MessageBox` (`rts/src/main.cpp:606`
  gates that box on `!strstr(cmd,"-benchmark")`), so the run raises no modal
  window of its own.
- `-benchmark` also forces windowed mode; `-fullscreen` must follow it on the
  command line to get the fullscreen measurement.
- The result is written to disk by the process itself, so it survives without
  the agent having to observe the run.
- Both runs completed with the agent responsive throughout, matching the six
  clean fullscreen Ironfield runs recorded on 2026-08-14.

The Hellbender D3D run on ViRGE, the other phase 7 gate item, still carries the
wedge risk and is not captured here.

### The deployed diagnostics were stale

The first dump attempt returned `Ok=0`, `Error=abi-mismatch`,
`SnapshotAbi=2026081601`: the guests' `C:\V9XTRACE.EXE` predated the shared-ABI
bump while the installed driver did not. The tool refusing rather than
misreading the block is the ABI guard working as designed, and it is the first
live exercise of it. `update-associated-driver.ps1` refreshes the driver but
not the root-level diagnostic copies, so `V9XTRACE.EXE` and `V9XDDP.EXE` must
be pushed alongside any ABI bump.

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
