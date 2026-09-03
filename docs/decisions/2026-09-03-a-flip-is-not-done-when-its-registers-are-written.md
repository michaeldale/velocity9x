# A flip is not done when its registers are written

Date: 2026-09-03
Status: fixed; paced flips measured on the emulated ViRGE; the application's
own judgement of the flicker is pending as this is written

## The symptom

Final Reality on the 86Box ViRGE/DX flickered heavily once its black wedges
were fixed (see `2026-09-03-final-reality-wedges-were-the-wrap-bit.md`). The
stock S3 driver on the same emulator did not. The trace snapshot from the run
that produced the picture read, for the whole benchmark:

```
CountFlip=77   CountGetFlipStatus=821   CountLock=280   CountBlt=170
```

Eleven status polls per flip, and every one of them was answered "done".

## What it was

`V9xHalGetFlipStatus` returned `DD_OK` unconditionally, and `v9x_flip_body`
wrote the new start address and returned. Neither knew that the CRTC only
**latches** a start address at the beginning of the next frame - 86Box models
it exactly that way, `memaddr_latch` copied into the live address at frame
start, which is how the silicon behaves - so from the write to the next
vertical retrace the monitor still shows the old page. DirectDraw asks the
driver, through GetFlipStatus, whether the previous flip has completed before it
lets an application lock or draw into a flip-chain back buffer, and before it
flips again. Told "yes" instantly, Final Reality drew its next frame into the
page that was still being scanned out, and the pieces of two frames on one
screen is the flicker.

The counters say the same thing from the other side: 77 flips for a benchmark
that runs for minutes is an application flipping as fast as the engine lets
it, not one paced by a 60 Hz retrace.

## The fix

`ddhal_core.c` now keeps a pending-flip state, armed by `v9x_flip_body` after
the start address is written and advanced by `V9xHalGetFlipStatus` from the
CRTC's own vertical-blank status bit:

- issued mid-frame: pending until a blank begins;
- issued during a blank: that blank's latch has already passed, so pending
  until the blank ends and the next one begins;
- `DDFLIP_NOVSYNC`: not armed - the application asked for the old behaviour.

While pending, GetFlipStatus answers `DDERR_WASSTILLDRAWING` for both of its
questions (can-flip and is-flip-done), and a second Flip is refused with the
same code, so two start addresses never race for one latch. DirectDraw retries
on that answer unless the application said `DDFLIP_DONOTWAIT`, in which case
the answer is the application's to handle - which is what the flag means.

It is deliberately not a spin inside Flip. The HAL is called with the Win16
lock held and must not stall the machine for a frame; letting the status call
carry the wait puts the wait in DirectDraw's retry loop, where the runtime
already yields.

## Measured

Probe, emulated ViRGE/DX, same probe binary, driver pair before and after:

```
                 before    after
Flip20Ms            0       328      (twenty flips; 16.4 ms each = 61 Hz)
FlipMaxMs           0        17
FlipHr         0x00000000  0x00000000
```

Before, twenty flips took no measurable time: the application was never made to
wait for anything. After, they take one frame each, which is what a flip is.
Every other DirectDraw and Direct3D key on the ViRGE is unchanged by this pair
(the diff against the previous run shows only the probe's own new rungs and the
build id).

Gates: check-tree, vga survey safety gate, host tests and family packages all
green (run-checks).

## What this does not settle

- Whether Final Reality's flicker is gone is the application's call, on the
  ViRGE VM and then on the Trio3D; this build has not yet had either run
  judged by eye.
- `DDFLIP_NOVSYNC` still writes the address and returns, so an application that
  asks for it gets tearing on purpose. That is the documented meaning.
- The blank-status bit is read from INPUT_STATUS_1 on every poll. On a very fast
  host a tight DirectDraw retry loop could in principle poll through a whole
  short blank without seeing it; the state machine would then take one more
  frame than it should, not fewer. Not observed; recorded so it is recognised
  if a frame-rate cap ever looks like 30 Hz where 60 was expected.
