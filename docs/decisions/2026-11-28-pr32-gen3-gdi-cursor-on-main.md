# PR 32 (Gen3 GDI copies and hardware cursor) on 1.0's main, run on the netbook

Date: 2026-11-28

GitHub PR 32 by arturaslt (head `f2fd05a4`): GDI screen-to-screen
`SRCCOPY` on the Gen3 blitter and the Gen3 hardware cursor, `intel-gma`
only ([copies plan](../plans/gen3-gdi-blitter.md),
[cursor plan](../plans/gen3-hardware-cursor.md)). Left for 1.1 on
2026-11-25. As submitted it had not run on hardware.

Evidence: [`../probe/pr32-gen3-gdi-cursor-netbook-2026-11-28/`](../probe/pr32-gen3-gdi-cursor-netbook-2026-11-28/).

## Merge

Merged onto main at `943fecfa` (95 commits after the PR's base, 0.60.0)
on branch `pr32-merge`. The code merged without conflict. Three docs
conflicted and take main's text: STATUS.md, the issues index (the PR's
one new line added under Closed) and the changelog (the PR's entries
rewritten under 1.1.0 from this run).

## Changes on top of the PR

- **`_TEXT` budget.** On today's main the PR took intel-gma's `_TEXT`
  to 57,365 bytes, 21 past its 57,344 budget (the PR had 0xDF79 against
  0.60.0's main). `d3dmode.c` moves to LEAFCODE, as the ati family did
  for the same reason. Now 0xDF55 (57,173).
- **The cursor thunks' guard.** `audit-family-binary.ps1`, which the PR
  could not run, checks that `SetCursor` and `MoveCursor` return without
  calling anything while `_v9x_driver_pdevice` is null. The PR moved that
  check into `cursor16.c`. It is back in the two Gen3 thunks before the
  jump to C, and the audit accepts `V9XCURSORSET` and `V9XCURSORMOVE` as
  the targets.
- **A late breadcrumb (review).** `v9x_gdi_gen3_submit` returned 0 both
  before TAIL moved and after, so a breadcrumb timeout on the first
  stream declined the BitBlt and the DIB Engine redrew a copy that was
  still in the ring and could run late over it. A timeout after TAIL
  moves is now "done, engine poisoned", as a later stream's already was.
  Found by reading; no such timeout has been seen.
- **A stale overlay (review).** `v9x_cursor_configure` cleared its
  state before any refusal, without turning a shown overlay off, and
  nothing could turn it off after. It now calls `v9x_cursor_off` first.
  Found by reading; not reproduced.
- **Fault injection.** V9XGDI `/accel` arms a fault to drive the
  poison path. Every other engine spends it in a bounded wait; the Gen3
  arm had none, so the harness failed `injection-not-consumed`. It is now
  spent as a ring-space timeout before anything is queued.

Not changed, from the review, as plausible and minor: GDI's ring-space
bound is 200,000 polls against the HAL's 1,000,000, so under heavy
windowed Direct3D a GDI copy may poison the copies for the session
early; and a `MoveCursor` at interrupt time can show one torn cursor
frame while `v9x_cursor_on` rewrites the image. The review also found
the ring shared safely with the HAL (both write TAIL under the Win16
mutex, and GDI's breadcrumb at 0x100 is not the HAL's at 0x80), with
the caveat that the mutex was measured only for DirectX 6.1a's draw
callbacks.

## Measured on the netbook (GMA 950, 945GSE)

W9QI2BE2, 1024x576x16, installed by `run-release-checks.ps1` (WININIT
rename of all eight files, `reset`, hashes checked).

- **Build `4825d458-dirty`** (the merge with the first two changes and
  the review fixes), boot 373: `GdiAcceleration=gdi-copy-overlap`,
  `HwCursor=on base=7FFAC000 pipes=2`. All seven probe runs exit 0 and
  no verdict changed against 1.0.0 (V9XD32 at DDI 5 and 6, V9XGLP,
  V9XGLIDP, V9XR3DP, V9XWPRS, V9XFBCMP). V9XGDI `/accel`: 131 copies on
  the engine in its run, 41 of its operations overlapping, 24 clipped
  copies accelerated, 20 comparisons against the CPU's drawing PASS, no
  timeout or poison, but `Result=FAIL` on `injection-not-consumed`.
- One V9XWPRS run in those checks timed the windowed OpenGL swap at
  6.48 ms against 1.0.0's 1.24. Four more runs on the same boot, two
  with 1.0.0's `V9XGL.DLL` put back, gave 1.11 and 1.12 ms (this build)
  and 1.33 and 1.19 ms (1.0.0's ICD); the next build's checks gave 0.56
  ms. A one-off, not this change.
- **Build `88283d67-dirty`** (with the injection hook), boot 374: the
  same seven probes pass with no verdict changed; V9XGDI `/accel`
  `Result=PASS`: 131 copies, 20 comparisons equal, the injected fault
  spent as a ring-space timeout (`FifoTimeoutsAfterInject=1`,
  `PoisonedAfterInject=1`), no real timeout. The netbook's GDI copies
  stay off for the rest of that boot, as poisoning means.

## Not checked

- The hardware cursor by eye: the agent's screenshot reads the frame
  buffer, which the overlay never touches, and no capture is on the
  netbook. The PR's author saw it right on their netbook.
- The speed figures in the PR (13 to 47 times the CPU's copies) were not
  measured again.
- A full-screen DOS box with the cursor on (the netbook's DOS box return
  hard-locks, a known issue), FIFO watermarks, the GMA 900, 3150 and
  3100.
