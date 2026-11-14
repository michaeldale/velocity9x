# GT218 GDI on the 2D engine, first runs: fill and copy draw right on the card

Date: 2026-11-14

Step 5.4 of [docs/plans/nvidia-gt218-gdi-engine.md](../plans/nvidia-gt218-gdi-engine.md),
on M-BXMFA8 (GeForce 210, GT218 A8). Evidence in
`build/driver-results/m-bxmfa8-gt218-boot-5.4/`; the files the first
install replaced (build d2aee239, boot 108) in
`build/driver-results/m-bxmfa8-gt218-boot108/installed-before-5.4/`.

Five builds were installed in turn, each by WININIT.INI plain renames
and the agent's reset a minute after the last write.

## a0048fed: the arm never went live

Boot 109 came up. V9XHW.INI: `GdiAcceleration=none`. gdi_accel.c asked
the chip's reserve hook for the engine reserve at Enable, but
gt218_hw16.c learns the reserve only when DirectDraw first sizes its heap
(dd16.c, `v9x_dd_refresh_framebuffer`), after Enable. The hook answered
0 and the arm stayed off for the boot. Fixed in d96e5bb8 by asking the
hook per operation. No GDI test was run on this boot.

## d96e5bb8: the machine froze on the boot screen

Reset at 02:17 UTC. The agent did not answer for the next three and a
half hours. When it next answered (about 05:50 UTC) it was running
d96e5bb8, with `Stage=enable-ok` and
`GdiAcceleration=gdi-fill-copy-overlap`
(`d96e5bb8-V9XHW.INI`). The owner reported by chat (05:49 UTC) that it
**froze on the Windows boot screen** and came back after a power-cycle.
So the boot after the agent's warm reset froze, and the boot after a
cold start did not.

d96e5bb8 was the first build to read a PFIFO register from GDI at
Enable (IB get, 3334h), with PFIFO as the BIOS leaves it; a0048fed never
reached that read and booted. ffa2a368 removed the read, and every build
since has come through the agent's warm reset (boots 111 to 115). That
fits the read, but d96e5bb8 also completed Enable with it on the cold
start, so **the freeze is not explained**: the read after a warm reset
is the suspect, and nothing has tested it in that state.

## ffa2a368 and later, boots 111 to 115: GDI draws on the engine

Each boot came back about 2 min 20 s after the reset, and the installed
files hash-matched the package each time.

Boot 111 (ffa2a368), in the order the tests were run:

| Step | Result |
| --- | --- |
| V9XGDI /accel, before DirectDraw | `Enabled=7`, 3216 calls, all declined (319 at the engine gate: no channel yet), `Compared=PASS`. The tool's `Result=FAIL` is its rule for an enabled engine that drew nothing. |
| V9XDDP | `COMPLETE`, the same seven pixel checks 1 as step 4.4, `GblHalVidMemTotal=07D80000` |
| V9XGDI /accel | **`Result=PASS`: 160 fills and 131 copies on the engine**, `Compared=PASS`, no idle or FIFO timeout |
| V9XN5CH /check | channel live at the reserve (2604h `8007F042`, 40032Ch `80007F00`), IB get = put = 16Bh, pusher drained, 3228h no error, 2100h, 400100h, 400700h all 0 |

16Bh is 363 IB entries, the HAL's 8 (bind and DirectDraw's seven) plus
the 355 GDI operations counted by then: every one went through the ring
and finished.

PGRAPH's trap words hold status 80000000, class 502D, method 308DCh
(subchannel 3, 8DCh, the copy's last word, which starts it), data A3h.
Every PGRAPH bring-up in the earlier probe logs leaves status 80000000
with an address and no interrupt (`boot108/check2-V9XN5CH.LOG`), so this
reads as a record of PGRAPH's last method, not a fault: 400100h and the
trap-pending word are 0. That reading is not measured.

Boot 115 (2e35b466), with V9XGDI /accel moved to the end (`order/`):

| Step | Result |
| --- | --- |
| V9XDDP | `COMPLETE`, seven pixel checks 1 |
| V9XMSW /cycle:4, /depth:4 | PASS, 4 of 4 depth cycles, GDI live throughout |
| V9XGDI /auto | PASS |
| V9XDDP again | `COMPLETE`, seven pixel checks 1 |
| V9XGDI /accel | **`Result=PASS`**, 160 fills and 131 copies, `Compared=PASS`, no timeout; 79 more fills had already gone to the engine during the cycles and /auto |

`GdiAcceleration` read `gdi-fill-copy-overlap` after every step until the
last.

## A misreading, corrected

V9XGDI /accel ends by arming the driver's fault injector and drawing
once more, to prove that an expired wait poisons GDI and leaves the
picture right. It writes `InjectArmed=1`, `PoisonedAfterInject=1` and
`IdleTimeoutsAfterInject=1`, and passes. Every /accel run here did that,
and the session was poisoned afterwards by design.

Boots 111 and 112 were read at first as a GDI operation timing out
"deterministically after 355": that was the injected one. e226e383 and
2e35b466 added a record of what the arm sees when it poisons
(`[Velocity9xGdiPoison] Nv50Poison` in V9XHW.INI, out of the section
every Enable deletes). It stayed empty on boots 113 and 114, because an
injected fault skips the waits it would capture. It stays in the driver
for a real timeout. The /auto and V9XMSW runs on boots 111 to 114 came
after an /accel and so did not exercise the engine; boot 115's order
does.

## Not covered

- 8 bpp on the engine: the depth cycles pass through 8 bpp with GDI live,
  but no run has compared pixels drawn there.
- A full-screen DOS box, standby and resume.
- The picture: nobody has looked at the monitor.
- Timings: V9XGDI reports counts and comparisons, not speed.

The nvidia family's `_TEXT` is 57105 bytes after 2e35b466, 239 under its
budget.

The machine is left on 2e35b466, boot 115, with GDI poisoned for the
session by V9XGDI's injection (the CPU draws everything until the next
boot).
