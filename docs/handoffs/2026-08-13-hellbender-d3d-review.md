# Hellbender Direct3D review and test handoff

> Status: Historical. The later compatibility plan records the outcome.

Date: 2026-08-13

Scope: review the current uncommitted DirectDraw/Direct3D changes, run the
regression tests before launching Hellbender, and investigate a hard wedge
that occurs after the game enters 640x480x16 but before the first Direct3D
context or texture callback.

Related plan: [Hellbender hardware Direct3D compatibility plan](../plans/hellbender-hardware-d3d.md)

## 1. Current machine and repository state

The development VM is the Windows 98SE Velocity9x VM reached through the
remote agent on host port 9869. At handoff:

- agent version: `0.5.1`, build `v0.5.1-grandchild-fix`;
- last observed boot counter: `129`;
- desktop: 1024x768x16 and ready;
- installed driver build: `hellbender-texture-handles-control`;
- guest update job: `C:\V9XREMOTE\JOBS\hellbender-texture-handles-control-vm1`;
- Hellbender configuration: `useDirect3D=1`; and
- the repository changes are uncommitted and unstaged.

Do not assume the boot counter alone proves a reboot. Compare it with the
previous value and require a disconnect/reconnect plus desktop readiness.

The current working tree is expected to contain changes in:

- `CHANGELOG.md`;
- `include/velocity9x/win9x_ddraw_abi.h`;
- `src/display32/ddhal.c`; and
- `tools/diag/d3d_trace_dump_win32.c`.

Preserve those changes. Do not reset or replace the worktree before reviewing
the diff.

## 2. Run these tests before Hellbender

Use PowerShell from the repository root, `C:\everything\velocity9x`.

### 2.1 Review and host checks

```powershell
git status --short
git diff --check
git diff --stat
.\scripts\check-tree.ps1
.\scripts\build-active-package.ps1 -BuildId handoff-review
```

Expected results:

- `git diff --check` reports no whitespace errors;
- the tree check passes;
- both the Win16 and Win32 ABI size guards compile; and
- the active package is produced under `build\vm-probe\ACTIVE`.

Review the capability table and callback table together. In particular,
verify that the advertised flags match implemented behavior. The current
perspective and RGB565 texture advertisement was added to diagnose
Hellbender startup; texture sampling is not implemented yet, so this is not a
release-ready capability claim.

### 2.2 Confirm VM health

```powershell
$ctl = $env:V9X_AGENT_CTL   # path to the remote agent's v9xctl.ps1
& $ctl info -Json
```

Require all of the following before continuing:

- `DesktopReady=true`;
- 1024x768x16;
- agent version 0.5.1; and
- a responsive request on port 9869.

The known-good control build is already installed. Do not reinstall merely to
repeat the probe. If another build must be installed, use only the verified
already-associated updater:

```powershell
.\scripts\update-associated-driver.ps1 `
    -JobId handoff-review-vm1 -BootTimeoutSeconds 240
```

The updater must report a real boot-counter transition, desktop readiness,
and byte-for-byte verification of the installed driver files.

### 2.3 Mandatory DirectDraw/Direct3D regression gate

For the currently installed control build:

```powershell
$job = 'C:\V9XREMOTE\JOBS\hellbender-texture-handles-control-vm1'
$result = 'C:\everything\velocity9x\build\driver-results\handoff-review'
New-Item -ItemType Directory -Force -Path $result | Out-Null
& $ctl exec -Json -Application "$job\V9XDDP.EXE" `
    -WorkingDirectory $job -TimeoutSeconds 120
& $ctl get -Json -Source 'C:\V9XDD.INI' `
    -Destination "$result\V9XDD.INI"
```

The gate passes only when the result contains:

```text
Result=COMPLETE
D3DHalFound=1
D3DCreateDeviceHr=0x00000000
D3DTrianglePixelOk=1
D3DContextCycleOk=1
BltFillPixelOk=1
```

`FlipPixelOk=0` is an existing, separately tracked result and was present in
the accepted control run. Do not reinterpret it as a new regression without
comparing earlier evidence.

After the probe, capture the trace without launching the game:

```powershell
& $ctl exec -Json -Application "$job\V9XTRACE.EXE" `
    -WorkingDirectory $job -TimeoutSeconds 60
& $ctl get -Json -Source 'C:\V9XSNAP.INI' `
    -Destination "$result\V9XSNAP.INI"
```

Expected engine timeout and reset counters are zero. A normal probe produces
one context create/destroy cycle and no texture callbacks because the current
probe does not create a legacy Direct3D texture handle.

## 3. What has been implemented

The current diff contains these related changes:

- a bounded callback trace ring and exception-path attempt to write
  `C:\V9XTRACE.INI`;
- corrected primary/flip-chain pitch and RGB565 target validation;
- Win16 DIBENGINE re-enable ordering and cursor teardown guards;
- ViRGE Gouraud RGB gradients using 8.7 fixed-point register values;
- one Direct3D RGB565 texture-format descriptor;
- bounded, context-owned texture create/destroy/swap/get-surface handles;
- texture lifecycle counters and trace event names;
- dormant legacy Direct3D Execute parsing; and
- dormant DirectDraw execute-buffer pseudo-surface callbacks.

The cross-bitness ABI for this diff is `2026081303`. The trace ring has 50
counter slots and `sizeof(V9X_DD_TRACE) == 380`; the compile-time size guards
must remain green in both driver builds.

The published driver intentionally leaves these fields disabled because the
Win98 runtime rejects every tested table that exposes Execute:

```text
DDHALINFO.lpDDExeBufCallbacks = NULL
D3DHAL_CALLBACKS.Execute = NULL
D3DHAL_CALLBACKS.ExecuteClipped = NULL
```

Do not enable only one of the four texture callbacks. The DDK requires the
texture callback group to be complete.

## 4. Confirmed findings

### 4.1 Display corruption and initial DIBENG faults

The earlier striped/corrupted output was caused by primary-surface
pitch/format handling. Normalizing display-sized primary and backbuffer
targets to the authoritative scanout descriptor removed that corruption.

Earlier Hellbender and Winoldap dialogs faulted inside `DIBENG.DLL`. Cursor
teardown guards and re-enable ordering improved recovery, and Hellbender can
run in software mode after ignoring the Winoldap dialog.

### 4.2 Hellbender capability filter

Hellbender initially rejected the HAL because it required Gouraud shading and
perspective correction. With those bits advertised, it proceeds with only a
nonfatal warning that fog is unavailable.

The Gouraud color-gradient path is implemented. Textured perspective-correct
rasterization is not implemented; the perspective bit is currently useful
only for reaching the next diagnostic checkpoint and must be reviewed before
acceptance.

### 4.3 Current hard wedge

After selecting New Game, Hellbender switches to 640x480x16 and presents a
black client/menu frame. It then stops making observable progress. The trace
does not advance into a new Direct3D context:

```text
D3dContextCreates=1       # prior synthetic probe baseline
D3dContextDestroys=1      # prior synthetic probe baseline
D3dTextureCreates=0
D3dTextureDestroys=0
D3dTextureSwaps=0
D3dTextureGetSurfs=0
```

The last new events are DirectDraw/driver initialization and object creation.
There is no new `ContextCreate`, render, Execute, or texture callback from
Hellbender. This places the failure before the HAL device context is created.

The most recent failure stopped the remote agent and the VM subsequently
rebooted (`BootCounter 125` to `126`). No `C:\V9XTRACE.INI` existed after
recovery, so the exception filter did not catch this path. Treat it as a hard
wedge/reset, not a captured user-mode GPF.

### 4.4 Legacy Execute experiments

The following candidates were built and VM-tested after the RGB565 texture
contract was added:

| Candidate | D3D table | Result |
| --- | --- | --- |
| `hellbender-legacy-execute-texture` | Execute + ExecuteClipped + RenderState/RenderPrimitive + execute-buffer callbacks | `D3DHalFound=0` |
| `hellbender-execute-only-texture` | Execute only for v1, execute-buffer callbacks enabled | `D3DHalFound=0` |
| `hellbender-texture-handles-control` | RenderState/RenderPrimitive, Execute disabled | `D3DHalFound=1`, triangle/context tests pass |

The DDK documents `SceneCapture` as optional, so its null pointer does not
explain the rejection. Do not repeat these combinations without first adding
a probe that explains the runtime's validation failure.

## 5. Evidence to review

The most useful result files are:

- accepted control probe:
  `build/driver-results/hellbender-texture-handles-control-vm1/V9XDD.INI`;
- trace immediately after Hellbender entered the black frame:
  `build/driver-results/hellbender-texture-handles-vm1/HELLBENDER-NEWGAME.INI`;
- earlier final black-frame trace:
  `build/driver-results/hellbender-gouraud-perspective-vm1/HELLBENDER-BLACK-FINAL.INI`;
- rejected paired-Execute probe:
  `build/driver-results/hellbender-legacy-execute-texture-vm1/V9XDD.INI`;
- rejected Execute-only probe:
  `build/driver-results/hellbender-execute-only-texture-vm1/V9XDD.INI`; and
- stable control desktop screenshot:
  `build/driver-results/hellbender-texture-handles-control-vm1/DESKTOP.BMP`.

The Windows 98 DDK reference used for the legacy table review is installed at
`C:\98DDK`. The S3 ViRGE sample table is in
`C:\98DDK\src\display\mini\s3v\D3DDRV.C`, and the callback contract is in
`C:\98DDK\inc\win98\D3DHAL.H`.

## 6. Safe Hellbender reproduction, only after the gates pass

Before launching, confirm:

```powershell
& $ctl shell -Json -ShellCommand `
    'FIND "useDirect3D" "C:\Program Files\Microsoft Games\Hellbender\system\hellbend.ini"'
```

Expected: `useDirect3D=1`.

Launch detached and visible:

```powershell
& $ctl exec -Json `
    -Application 'C:\Program Files\Microsoft Games\Hellbender\HELLBEND.EXE' `
    -WorkingDirectory 'C:\Program Files\Microsoft Games\Hellbender' `
    -TimeoutSeconds 30 -Detach -ShowWindow
```

The expected sequence is:

1. dismiss the no-fog warning;
2. reach Quick Configuration;
3. select New Game;
4. observe the switch to 640x480x16; and
5. wait at least 15 seconds before collecting a trace.

Do not request a screenshot during the fullscreen transition or while the
display is wedged. The agent screenshot path uses GDI and has itself faulted
or become unresponsive in this state. Prefer `info` followed by a direct
`V9XTRACE.EXE` execution and file download. Take screenshots only on a known
stable desktop or after a stable application window is confirmed.

If the game wedges but the agent remains responsive, capture in this order:

1. `C:\V9XSNAP.INI` through `V9XTRACE.EXE`;
2. `C:\V9XTRACE.INI`, if it exists;
3. agent `info` including mode and boot counter; and
4. serial/agent logs if available.

Then use the agent's controlled reboot. If the agent is unavailable, recover
the VM externally and immediately check for `C:\V9XTRACE.INI` before running
another graphics program.

To restore a responsive 640x480 desktop without rebooting:

```powershell
& $ctl exec -Json -Application "$job\V9XMSW.EXE" `
    -Arguments '/set:1024x768x16' -WorkingDirectory $job `
    -TimeoutSeconds 60
```

## 7. Recommended next work

Do not add more raster capabilities first. The immediate problem occurs before
`ContextCreate`.

Recommended order:

1. Extend the synthetic probe to enumerate the advertised texture format and
   exercise texture create/get-surface/swap/destroy, proving the new ABI and
   counters independently of Hellbender.
2. Add bounded tracing around the DirectDraw surface-creation/setup phase
   that occurs after the second `DriverInit` and before D3D context creation.
3. Determine whether a hidden Winoldap/DIBENG error dialog is blocking the
   fullscreen game, without using the GDI screenshot path during the wedge.
4. Compare the same launch sequence and callback trace with a stock ViRGE
   reference VM.
5. Only after the pre-context stop is explained, implement and advertise the
   texture sampling, render-state, and perspective behavior actually observed.

The key review question is whether Hellbender is waiting in application or
DirectDraw setup, or whether the diagnostic capability advertisement is
causing the runtime to enter an unsupported path before it creates the HAL
context. The existing trace proves that adding more code inside render or
texture callbacks cannot fix the current stop until one of those callbacks is
actually reached.
