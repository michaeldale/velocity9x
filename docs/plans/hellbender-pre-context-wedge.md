# Hellbender pre-ContextCreate wedge: diagnosis plan

Date: 2026-08-13

Status: closed — the wedge was diagnosed and fixed (mode-teardown PDEVICE
guard; see the 0.2-era CHANGELOG entries), and the parent milestone was then
closed on 2026-08-15 when Hellbender was shown never to use hardware Direct3D
on the ViRGE (see [hellbender-hardware-d3d.md](hellbender-hardware-d3d.md)).

Related: [2026-08-13 handoff](../handoffs/2026-08-13-hellbender-d3d-review.md),
[Hellbender hardware Direct3D plan](hellbender-hardware-d3d.md)

## Progress: `hellbender-surface-trace`

Implemented and VM-tested on 2026-08-13:

- A1-A5: surface callback tracing, 56-entry ring, corrected Win16 exit
  bookkeeping, handled `GetDriverInfo` returns, ABI `2026081401`;
- B: RGB565 texture enumeration and two-texture handle/load/swap lifecycle in
  V9XDDP; and
- C1/C2: instrumented Hellbender rerun plus the GDI-free V9XWND window-list
  diagnostic.

The VM 9869 install passed with boot counter 129 -> 130 and byte verification.
V9XDDP completed with every mandatory gate result, `TexFormatCount=1`,
`TexFormat565=1`, and successful surface/handle/load/swap HRESULTs. V9XTRACE
reported two texture creates, two destroys, one swap, five clean
`CanCreateSurface`/`CreateSurface` pairs, and zero engine timeouts/resets.

The Hellbender New Game repro switched to 640x480x16, then stopped after two
additional successful `Dd16CreateObject` calls following the second
`DriverInit`. No surface callback entered after that `DriverInit`, no HAL
callback was left unmatched, and no engine timeout/reset occurred. V9XWND
showed the Quick Configuration `#32770` had closed and found no replacement
dialog or Winoldap window; only the visible `Hellbender` application window
belonged to the game. This rules out C2's hidden-dialog hypothesis for this
run and places the stop above the callbacks published by the HAL. Proceed with
C3 next.

Evidence: `build/driver-results/hellbender-surface-trace-vm1/V9XDD.INI`,
`V9XSNAP.INI`, `HELLBENDER-WEDGE.INI`, `V9XWND-HEALTHY.INI`, and
`V9XWND-WEDGE.INI`. Recovery required a controlled reboot after V9XMSW could
not restore the live wedged mode; boot counter 130 -> 131 and desktop readiness
were confirmed.

### C3 result and C4a checkpoint

C3 was completed with two separately installed and gated builds:

- `hellbender-getdriverinfo-decline`: `GETDRIVERINFOSET` was accepted by the
  runtime and V9XDDP passed, with 18 handled-and-declined queries. Hellbender
  queried the full GUID inventory, then displayed a visible `#32770` dialog
  titled `Hellbend`. Pressing Enter produced an orderly
  `ContextDestroyAll`/`Dd16DestroyDriver` exit.
- `hellbender-getdriverinfo-callbacks2`: serving `GUID_D3DCallbacks2` also
  passed the mandatory V9XDDP gate. Hellbender returned to the original silent
  pre-context wedge: callbacks2 was served successfully, the remaining GUIDs
  were declined, and no new surface or context callback followed the final
  `DriverInit`. The probe additionally recorded `TexSwapHr=0x80070057` with
  zero HAL texture-swap calls, while texture enumeration/create/handle/load and
  every mandatory gate result still passed.

The repeated Hellbender queries include `GUID_D3DCallbacks2` (`0x0BA584E1`),
`GUID_D3DExtendedCaps` (`0x7DE41F80`), `GUID_KernelCaps` (`0xFFAA7540`), and
`0x3B8A0466`. C3 therefore does not fix the wedge; callbacks2 prevents the
explicit initialization-error dialog but is not sufficient for progress.

C4a (`hellbender-caps-no-texture`) is implemented and host-built with the
texture format and perspective bit removed. Its first install attempt did not
reach reboot: the guest agent became unreachable while the previous
Hellbender wedge was active. Resume by resetting the VM, confirming a new boot
counter and 1024x768x16 desktop, then rerunning
`update-associated-driver.ps1 -JobId hellbender-caps-no-texture-vm1`.

After guest recovery, C4a installed and passed the mandatory non-texture
probe results. V9XDDP reported zero formats and `TexEnumHr=0x887602D0` while
the HAL, context, triangle, and blit gates remained good. Hellbender rejected
the HAL immediately with a `3D Adapter Error` naming missing perspective
correction, before any fullscreen transition.

C4b (`hellbender-caps-consistent`) advertised perspective plus POW2 and
square-only textures, nearest/linear filtering, decal/modulate/copy blending,
and wrap/clamp addressing. It passed the mandatory gate but reproduced the
same silent pre-context wedge, with the same final GUID queries and no surface
or context callback after the final `DriverInit`. The caps inconsistency is
therefore not the wedge trigger.

The stock ViRGE reference on VM 9870 was configured temporarily with
`useDirect3D=1`. Hellbender remained in 640x480 gameplay for 30 seconds and
responded to `Alt+F4` by cleanly restoring 1024x768. Its GDI-free window list
contained the visible game window and no blocking dialog. The original
reference INI was preserved as
`build/driver-results/stock-virge-reference-20260813/hellbend-original.ini`.

With C3 and C4 complete, the source default returns to the instrumented
control: `GETDRIVERINFOSET` off and the original texture caps. The experiments
show the wedge is specific to the Velocity9x/runtime interaction above the
published HAL callbacks, not a general Hellbender/ViRGE behavior, hidden
dialog, callbacks2 omission, or inconsistent texture-cap advertisement.

The final `hellbender-post-experiments-control` package built and was uploaded
and staged successfully, but its reboot request lost the agent connection
while C4b Hellbender was wedged. The source and staged package are already at
the control configuration. Reset VM 9869 once, require a boot counter greater
than 136 and 1024x768x16 desktop readiness, then rerun the updater or verify
the pending boot-time replacement before the final V9XDDP gate.

## Context

Per the 2026-08-13 handoff, Hellbender in hardware D3D mode wedges after
switching to 640x480x16, before any `ContextCreate`. The handoff's
"uncommitted changes" are now committed as `a2f891b`; the working tree is
clean.

Review findings that shape this plan:

1. **The wedge window is a total tracing blind spot.** The driver publishes
   no `CreateSurface`/`CanCreateSurface`/`DestroySurface`/
   `AddAttachedSurface` callbacks (all NULL at
   `src/display32/ddhal.c:1849-1872`), so nothing the runtime does with
   surfaces after the second `DriverInit` is visible. Both wedge traces end
   identically: three `Dd16CreateObject` successes, then silence, zero
   engine timeouts — nothing is calling the driver, or it is calling paths
   we do not hook.
2. **Confirmed bug — `V9xHalGetDriverInfo` return code.**
   `ddhal.c:1726-1763` returns `V9X_DD_OK` (0 = `DDHAL_DRIVER_NOTHANDLED`)
   on *every* path, including after successfully copying the callbacks2
   payload. The DDK contract (and the S3V sample,
   `C:\98DDK\src\display\mini\s3v\S3_DD32.C:5641`) requires
   `DDHAL_DRIVER_HANDLED` (1) always, with declines expressed via
   `ddRVal=DDERR_CURRENTLYNOTAVAIL`. This plausibly explains why enabling
   `DDHALINFO_GETDRIVERINFOSET` in phase 3 got the HAL rejected
   (`docs/decisions/2026-08-11-direct3d-phase3.md`) — every query,
   including the correctly filled one, read back as not-handled. Today the
   flag is never set (`ddhal.c:1821`), so the runtime can never query
   `GUID_D3DExtendedCaps`/`GUID_ZPixelFormats`, which a DX5 game's device
   setup may need before `ContextCreate`.
3. **Confirmed bug — 16-bit trace writer corrupts `LastEnter`.**
   `src/display16/dd16.c:228` passes an id with `V9X_DD_TRACE_EXIT_FLAG`
   into `v9x_dd_trace_event`, which unconditionally stores it as
   `last_enter_id`; the 16-bit side has no `last_exit` path. Both wedge
   traces' `LastEnter=Dd16CreateObject` is therefore unreliable.
4. **Self-inconsistent caps.** `D3DPTEXTURECAPS_PERSPECTIVE` plus one
   RGB565 texture format are advertised (`ddhal.c:1916-1939`) while
   `dwTextureFilterCaps`/`dwTextureBlendCaps`/`dwTextureAddressCaps` are
   all zero. The S3V sample never ships a format list without
   filter/blend/address caps (`D3DDRV.C:256-269`).
5. **Ring too small.** 32 entries versus `TraceEvents=125` — all
   pre-fullscreen history scrolls out. 224 bytes of headroom remain in the
   2048-byte shared block.

DDK verification (S3V sample): `CreateSurface32`/`DestroySurface32` set
`ddRVal=DD_OK` and return `NOTHANDLED` (the runtime performs the
allocation); `CanCreateSurface32` returns `HANDLED` + `DD_OK`; S3V never
implements a `SetMode` HAL callback (mode switches flow through the 16-bit
re-enable, already traced). 32-bit callbacks are published via
`DDHAL_CB32_*` flag bits — the pattern `ddhal.c` already uses.

## A. Trace infrastructure (one build: `hellbender-surface-trace`, one ABI bump)

**A1. Surface-phase trace stubs** — `src/display32/ddhal.c` (~110 lines),
`include/velocity9x/win9x_ddraw_abi.h` (~10),
`tools/diag/d3d_trace_dump_win32.c` (name table ~8).

- New trace ids in the free 20-29 block (`counters[50]` already covers
  them): 20 `CanCreateSurface` (detail: requested `ddsCaps.dwCaps`),
  21 `CreateSurface` (detail: surface count), 22 `DestroySurface`,
  23 `AddAttachedSurface`; 24-29 reserved.
- Semantics per S3V: `CanCreateSurface` → trace, `ddRVal=DD_OK`, return
  `V9X_DDHAL_DRIVER_HANDLED`; `CreateSurface`/`DestroySurface`/
  `AddAttachedSurface` → trace, `ddRVal=DD_OK`, return
  `V9X_DDHAL_DRIVER_NOTHANDLED`.
- Publish via `dd_callbacks.dwFlags |=
  CB32_CREATESURFACE|CB32_CANCREATESURFACE` and
  `surface_callbacks.dwFlags |=
  SURFCB32_DESTROYSURFACE|SURFCB32_ADDATTACHEDSURFACE` in `DriverInit`.
  **Do not add a SetMode callback** (no S3V precedent).

**A2. Enlarge the ring** — `V9X_DD_TRACE_RING_COUNT` 32 → 56 (+192 bytes;
shared ~2016 ≤ 2048). Update the `sizeof(V9X_DD_TRACE)==380` guard
(`win9x_ddraw_abi.h:1042`) to 572, and the flush/dump loops that key off
the constant.

**A3. Fix the dd16 LastEnter bug** — `src/display16/dd16.c:42-62`: branch
on `id & V9X_DD_TRACE_EXIT_FLAG`; exits write
`last_exit_id`/`last_exit_result` and skip the counter, mirroring
`ddhal.c:160-168`.

**A4. Fix the GetDriverInfo return code** — `ddhal.c:1726-1763`: return
`V9X_DDHAL_DRIVER_HANDLED` on all paths (keep the
`DDERR_CURRENTLYNOTAVAIL` preset for declined GUIDs). Correct regardless
of whether the flag experiment (C3) proceeds. Do **not** set
`GETDRIVERINFOSET` in this build.

**A5. ABI discipline** — single bump of `V9X_DD_SHARED_ABI`
(`win9x_ddraw_abi.h:833`, e.g. `2026081401`); rebuild display16 +
display32 + V9XTRACE + V9XDDP together in one guest job; CHANGELOG entry;
both bitness size guards must stay green.

**Gate (VM 9869):** `update-associated-driver.ps1` with a real
boot-counter transition, then V9XDDP: `Result=COMPLETE, D3DHalFound=1,
D3DCreateDeviceHr=0x00000000, D3DTrianglePixelOk=1, D3DContextCycleOk=1,
BltFillPixelOk=1` (`FlipPixelOk=0` accepted). V9XTRACE must now show
`CanCreateSurface`/`CreateSurface` pairs from the probe's own surface
work.

## B. Probe extension: texture lifecycle (handoff §7 item 1)

`tools/diag/ddraw_probe_win32.c` (~200 lines): `EnumTextureFormats`
(→ `TexFormatCount`, `TexFormat565`), create a 64x64 `DDSCAPS_TEXTURE`
RGB565 surface (→ `TexSurfaceHr`), `QueryInterface(IID_IDirect3DTexture)`
+ `GetHandle` (drives HAL `TextureCreate`; → `TexHandleHr`), second
texture + `Load`/`SwapTextureHandles` (→ `TexSwapHr`), teardown. Failures
are recorded keys, not aborts — `Result=COMPLETE` must still emerge.
Cross-check `D3dTextureCreates/Destroys >= 1` via V9XTRACE. If the runtime
refuses texture creation here, that HRESULT is itself signal — Hellbender
would hit the same wall.

## C. Discriminating experiments (in order; each: build → gate → New Game repro → V9XSNAP.INI)

Repro procedure per handoff §6: confirm `useDirect3D=1`, launch
detached and visible, dismiss the no-fog warning, New Game, wait at least
15 seconds, collect the trace via `V9XTRACE.EXE` — **no GDI screenshots
during the wedge**. Recover with `V9XMSW.EXE /set:1024x768x16` or a
controlled reboot.

- **C1. Instrumented control rerun** (build = A+B, zero caps change). If
  surface events appear after the second `DriverInit`, the missing-exit
  event names the wedging callback (`LastEnter` now trustworthy). If clean
  pairs then silence, the stop is above the HAL — weight shifts to
  C2/C3/C4.
- **C2. Hidden-dialog detector** (parallel, zero driver risk) — new
  `tools/diag/window_list_win32.c` (~150 lines): `EnumWindows` +
  `GetWindowText`/`GetClassName`/`IsWindowVisible` → `C:\V9XWND.INI` via
  `WritePrivateProfileString` (avoids the faulting GDI screenshot path).
  Run during the C1 wedge, plus healthy-desktop and stock-ViRGE (VM 9870)
  baselines. A `#32770`/Winoldap window present only during the wedge
  confirms handoff §7 item 3.
- **C3. `GETDRIVERINFOSET` trace-and-decline** — set the flag
  (`ddhal.c:1821`); stage 1 declines every GUID (compile out the
  callbacks2 payload branch). With A4's return-code fix this is the
  S3V-blessed shape. **Hard gate: V9XDDP `D3DHalFound=1` before any
  Hellbender launch**; if rejected, revert the flag (keep A4), amend the
  phase-3 decision doc, stop. If accepted, the trace's GUID inventory
  (id 19 logs the first GUID dword) shows what the runtime negotiates
  around the silence; stage 2 re-serves `GUID_D3DCallbacks2` under the
  same gate.
- **C4. Caps discriminator** — two ~10-line variants of
  `ddhal.c:1916-1939`: **C4a** drop the texture format + PERSPECTIVE bit
  (negative control: Hellbender should reject the HAL per handoff §4.2 —
  an identical wedge after rejection exonerates the D3D caps); **C4b**
  keep them but make the advertisement self-consistent per S3V
  (`FilterCaps=NEAREST|LINEAR`, `BlendCaps=DECAL|MODULATE|COPY`,
  `AddressCaps=WRAP|CLAMP`, `TextureCaps += POW2|SQUAREONLY`) —
  advertisement only, no rasterization code. C4b un-wedging means the
  zero-caps inconsistency was the trigger; real sampling becomes the
  follow-on work. C4b wedging identically exonerates the caps.
- **Reference comparison (handoff §7 item 4):** on VM 9870 (stock S3
  ViRGE), confirm Hellbender reaches gameplay and collect the C2
  window-list baseline.

## D. Out of scope / do not do

- No raster or texture-sampling implementation until the pre-context stop
  is explained (handoff §7).
- No re-runs of the rejected Execute-table combos; C3's outcome does not
  license them (those rejections occurred with `GETDRIVERINFOSET` off — a
  separate contract question).
- The texture callback group stays all-four-or-none; never partial.
- No shared-block growth past 2048 bytes; no shared-struct change without
  an ABI bump and a same-job rebuild of both drivers plus both diag tools.
- `FlipPixelOk=0` remains a known, separately tracked result.
- No GDI screenshots during the fullscreen transition or wedge.

## Files to modify

- `src/display32/ddhal.c` — stubs, ring constant use, GetDriverInfo return
  fix, C3/C4 experiment toggles
- `include/velocity9x/win9x_ddraw_abi.h` — trace ids 20-23, ring 32→56,
  size guard 380→572, ABI bump
- `src/display16/dd16.c` — exit-flag branch in `v9x_dd_trace_event`
- `tools/diag/d3d_trace_dump_win32.c` — new event names, ring size
- `tools/diag/ddraw_probe_win32.c` — texture lifecycle section
- New: `tools/diag/window_list_win32.c`
- `CHANGELOG.md`, and the phase-3 decision doc if C3 changes its
  conclusion

## Verification

1. Host: `git diff --check`, `.\scripts\check-tree.ps1`,
   `.\scripts\build-active-package.ps1` — both ABI size guards green.
2. VM 9869: install via `update-associated-driver.ps1` (real boot-counter
   transition plus byte-for-byte verify), then the full V9XDDP regression
   gate per handoff §2.3, then V9XTRACE with zero engine timeout counters.
3. New Game repro per handoff §6 after every gated build; evidence saved
   under `build/driver-results/<build-id>/`.
4. Success criterion for the campaign: the trace (or window list, or GUID
   inventory) positively identifies where Hellbender stops — either a HAL
   callback that enters and never exits, a runtime negotiation it cannot
   complete, or a blocked dialog — so the actual fix is targeted rather
   than speculative.
