# GT218 GDI on the 2D engine, first runs: it draws, then one operation times out

Date: 2026-11-14

Step 5.4 of [docs/plans/nvidia-gt218-gdi-engine.md](../plans/nvidia-gt218-gdi-engine.md),
on M-BXMFA8 (GeForce 210, GT218 A8). Evidence in
`build/driver-results/m-bxmfa8-gt218-boot-5.4/`; the files the first
install replaced (build d2aee239, boot 108) in
`build/driver-results/m-bxmfa8-gt218-boot108/installed-before-5.4/`.

Three builds were installed in turn, each by WININIT.INI plain renames
and the agent's reset a minute after the last write.

## a0048fed: the arm never went live

Boot 109 came up. V9XHW.INI: `GdiAcceleration=none`. gdi_accel.c asked
the chip's reserve hook for the engine reserve at Enable, but
gt218_hw16.c learns the reserve only when DirectDraw first sizes its heap
(dd16.c, `v9x_dd_refresh_framebuffer`), after Enable. The hook answered
0 and the arm stayed off for the boot. Fixed in d96e5bb8 by asking the
hook per operation. No GDI test was run on this boot.

## d96e5bb8: the machine was unreachable for hours

Reset at 02:17 UTC. The agent did not answer for the next three and a
half hours. When it next answered (about 05:50 UTC) it was running
d96e5bb8, with `Stage=enable-ok` and
`GdiAcceleration=gdi-fill-copy-overlap`
(`d96e5bb8-V9XHW.INI`). The owner reported by chat (05:49 UTC) that it
**froze on the Windows boot screen** and came back after a power-cycle.
So the boot after the agent's warm reset froze, and the boot after a
cold start did not.

At the time the likeliest cause looked like d96e5bb8's Enable-time read
of PFIFO's IB get (3334h), with PFIFO as the BIOS leaves it, since
a0048fed never reached that read and booted. ffa2a368 removed it. The
evidence since does not support it: d96e5bb8 itself completed Enable,
with the arm enabled, on the boot that came back, though that was a
cold start, where PFIFO may be left differently from after a warm
reset. ffa2a368 has since come through two warm resets (boots 111 and
112). **The freeze is not explained**: the read after a warm reset is
the remaining suspect, and nothing has tested it in that state.

## ffa2a368, boot 111: GDI draws on the engine

The machine came back 2 min 16 s after the reset. All four installed
files hash-match the package (`installed/`).

| Step | Result |
| --- | --- |
| V9XGDI /accel, before DirectDraw | `Enabled=7`, 3216 calls, all declined (319 at the engine gate: no channel yet), `Compared=PASS`, no timeouts. The tool's `Result=FAIL` is its rule for an enabled engine that drew nothing. |
| V9XDDP | `COMPLETE`, the same seven pixel checks 1 as step 4.4, `GblHalVidMemTotal=07D80000` |
| V9XGDI /accel | **`Result=PASS`: 160 fills and 131 copies on the engine**, `Compared=PASS`, no idle or FIFO timeouts |
| V9XN5CH /check | channel live at the reserve (2604h `8007F042`, 40032Ch `80007F00`), IB get = put = 16Bh, pusher drained, 3228h no error, 2100h, 400100h, 400700h all 0 |
| V9XGDI /auto, V9XMSW /cycle:4, /depth:4 | PASS each; depth 4 of 4 |
| V9XGDI /accel | **`Poisoned=1`, `IdleTimeouts=1`**, `Enabled=0`, totals 200 fills and 155 copies, `Compared=PASS` |

The poisoned run had no new engine work: the totals had stopped at 200
and 155. 16Bh is 363 IB entries: the HAL's 8 (bind and DirectDraw's
seven) plus 200 + 155. **Every operation GDI counted went through the
ring and the card finished it.** The one that timed out never reached
the ring, or reached USERD without PFIFO taking the new PUT; both bump
the same counter, so this run cannot tell which. The registers after the
poison are the clean, idle set above; nothing latched.

PGRAPH's trap words hold status 80000000, class 502D, method 308DCh
(subchannel 3, 8DCh, the copy's last word, which starts it), data A3h.
Every PGRAPH bring-up in the earlier probe logs leaves status 80000000
with an address and no interrupt (`boot108/check2-V9XN5CH.LOG`), so this
reads as a record of PGRAPH's last method, not a fault: 400100h and the
trap-pending word are 0. That reading is not measured.

## ffa2a368, boot 112: the same operation, again

A fresh reset and a sequence without V9XN5CH, reading V9XHW.INI after
every step (`narrow/`):

```
06:07:47 boot   gdi-fill-copy-overlap
06:08:07 ddp    gdi-fill-copy-overlap
06:08:24 accel1 gdi-poisoned
06:08:40 auto   gdi-poisoned   ... and poisoned to the end
```

accel1's own report is a PASS with exactly the boot 111 counts: 176
fills, 131 copies, no timeout. The poison came between V9XGDI writing
that report and the state read 16 s later, and the final totals are
again **200 fills and 155 copies**. So:

- It is deterministic: the same operation, the 356th GDI operation
  after the channel is built, times out on both boots.
- It is not V9XN5CH /check, which boot 112 never ran.
- It comes after V9XGDI's measured part ends, which is when its window
  closes and the desktop repaints. The 24 fills and 24 copies between
  the report and the poison are that repaint.

## What is not known, and the next step

Which wait expires (the idle wait before a draw, or the PUT wait after a
kick), what the registers read at that moment, and what the operation
is. The driver records none of it. The next step is to have the poison
path write the wait that expired, the eight registers the idle test
reads, and the last operation's kind and geometry to V9XHW.INI, then
rerun boot 112's first three steps.

Not covered: 8 bpp through either side, a full-screen DOS box, standby,
and the picture: nobody has looked at the monitor.

The machine is left on ffa2a368, boot 112, with GDI acceleration
poisoned for the session (the CPU draws everything; the picture checks
pass).
