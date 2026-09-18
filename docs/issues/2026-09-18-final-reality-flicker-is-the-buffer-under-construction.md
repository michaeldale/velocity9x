# Final Reality on the 945GSE flickers: the panel shows the buffer being drawn

**Status:** OPEN. Not fixed after nine boots (intel71 to intel79) of flip
work on 2026-09-18. **Machine:** MICHAEL-NETBOOK, Intel 945GSE
(8086:27AE), LVDS on pipe B, plane B, 640x480x16 at 60 Hz, 576 active
lines in a 672-line frame. **Software:** Windows 98SE, Final Reality,
Velocity9x 0.8.0 with Intel runtime 3D and the Intel flip on by default.
**Running record:** `2026-09-16-final-reality-renders-black-and-the-hal-faults.md`
(1,300 lines; this file is the summary that record has grown too long to
give). **Decision record for the latch claim:**
`..\decisions\2026-09-18-intel-plane-base-latches-at-vblank-start.md`.

## The symptom, as measured

The operator's phone video (60 fps, 663 frames, an intel7x flip build)
measured frame by frame, mean grey of the middle and lower thirds of the
panel:

```
f408 mid 116.8 bot 175.5   dark: cleared buffer, sky drawn, ground not
f409 mid 179.0 bot 188.7   filling in
f410 mid 191.8 bot 219.4
f411 mid 194.0 bot 224.5   finished picture, held
f412 mid 194.2 bot 224.7
f413 mid 194.3 bot 224.5
f414 mid 193.9 bot 223.7
f415 mid 119.7 bot 176.8   dark again: period 7 video frames, 8.6 Hz
```

Once per game frame the panel shows the buffer the game is drawing into -
the clear, the sky, then the ground filling in - for about one display
frame, then the finished picture. Another frame shows the finished upper
half over a flat grey lower half with a hard horizontal edge: the ground
pass caught part-way. The operator's words for it were "a transparent
overlay that doesn't block the image behind, starts at the bottom and
covers the screen by the end of the scene"; the ground is where the
unfinished part of a frame is, and the scenes fill with ground as they
play.

It is NOT a torn frame, an older frame, wrong texels or missing geometry.
It is an unfinished frame on screen.

## What the counters say, every build

```
DrawsToFront=0          every batch went to the buffer the plane base
                        register did NOT name at that moment (intel74-78)
FlipTakenAtDone=all     the base register held the flipped offset when the
FlipNotTakenAtDone=0    state machine declared each flip done
IsrAfterFlipOr=0        the i915 flip-pending ISR bits never read set
FlipFramesInSubmit=1    the command streamer does not stall on MI_DISPLAY_FLIP
ScanBTickLine=671       the frame counter ticks with DSL at 671; blank is 576-671
```

So: the game alternates buffers 0 and 0x96000 exactly as DirectDraw's
pointer swap says; every draw is aimed at the buffer the register does not
name; and the panel nonetheless shows draws in progress. The register and
the panel disagree for a period after each write. That is the whole
problem in one sentence.

## Hypotheses tried on hardware and the boot that killed each

| Boot | Build | Change | Result |
|---|---|---|---|
| intel65 | | flip written anywhere, waited on afterwards | "tearing, lower half" |
| intel66 | | written inside the blank, released at blank end | worse |
| intel71 | | MI_DISPLAY_FLIP through the ring | tears the same |
| intel72 | audit | i915 v4.4 pending bits 11/10 | never set; base reads new at once |
| intel73 | `86da02c` | complete when the frame counter moves | ISR zero, CS does not stall |
| intel74 | `57f6ccc` | written in the first 24 lines of the blank | FASTER flicker |
| intel75 | `72bea7b` | count draws to front/back | DrawsToFront=0 |
| intel76 | `07ffc98` | MI_FLUSH before each batch (stale render cache) | unchanged |
| intel77 | `00faf7d` | MI_READ_FLUSH before each batch (stale texture cache) | unchanged |
| intel78 | `3d9a103` | base written in ACTIVE video, before the blank-start latch | "a little better" |
| intel79 | `e83a65e` (built as `3d9a103-dirty`) | engine waits for the pending flip before each batch | DrawsFlipWaited=0; picture unchanged |
| intel80 | `73709d2-dirty` | two-tick completion, consistent counter read, in-game layout capture, `/reuse` probe | flicker unchanged; probe CLEAN at every delay; fetch stride is 1280 |
| intel81 | `ca107e2-dirty` (`a1bf160`) | breadcrumb: MI_STORE_DWORD_IMM behind the flush, submit waits for it | HARD LOCK at the first 3D frame; two defects found on the desk, below |
| intel82 | `a1bf160-dirty` (`fbc2f29`) | four-dword MI_STORE_DWORD_IMM to the right graphics address | no lock; 2,458 of 2,458 stores never landed; a frame took a minute |
| intel83 | `fbc2f29-dirty` (`8e4be09`) | HWS_PGA pointed at the reserve's status page; MI_STORE_DWORD_INDEX into it | HWS_PGA took the page; 1,460 of 1,460 stores never landed in the wait |
| intel84 | next (`fe571f4`) | completion channel as a state machine: page proven by a store-only round trip before use; timed-out completions owed to Flip, Lock and Blt; probe records failures and write timing | to boot |

Three of those (intel76, intel77, and the skipped-depth reading that
intel77's counters refuted) were guesses from a verbal description and
could never have put a cleared buffer on screen. They cost three boots and
are the reason this file exists. The two flushes stay in the driver: the
game rebuilds about 117 textures a frame into the same memory, so the
texture-cache invalidate is a correctness requirement on its own.

## The current model, and what would prove or kill it

The plane base register is double-buffered: a write goes to a pending copy
and reads back at once (intel73's "applies immediately"); the live copy
the panel fetches from takes the pending value at a latch point once per
frame. The one latch point consistent with every boot above is the START
of the vertical blank (line 576): a write in the blank (intel66, 74-77)
then waits a whole frame, and a release at that blank's tick (671) hands
the game a buffer still on screen for 16.7 ms. i915 v4.4 writes plane
registers in the 100 us BEFORE `crtc_vblank_start` for this reason
(`intel_pipe_update_start`, `VBLANK_EVASION_TIME_US`).

intel78 moved the write into active video and the flicker got "a little
better", not gone. The gap that leaves: Flip returns as soon as the base
is written. DirectDraw holds Lock and Blt on GetFlipStatus (55 asks a
frame in intel78) so the CPU clears wait for the latch; Direct3D does
not ask, RenderPrimitive goes straight to the engine, and the frame's
first batches land in the buffer the panel is still fetching until the
latch - one display frame of sky on a cleared buffer, whichever side of
the blank the base was written. `e83a65e` makes the engine wait for the
pending flip before a batch and counts the batches that waited
(`DrawsFlipWaited`, shared ABI 2026091710).

What the intel79 boot said:

```
FlipHandled=756  FlipStillDrawing=75104  FlipTakenAtDone=756  FlipNotTakenAtDone=0
DrawsFlipWaited=0  DrawsFlipWaitTimeouts=0  DrawsToFront=0  DrawsToBack=748226
CountGetFlipStatus=32299 (43 a frame)  CountFlip=10324 (14 a frame)  CountBlt=1528
```

Not one batch arrived with a flip pending. The zero is not the third
reading above as written: the runtime asks GetFlipStatus 43 times a frame
and holds the game's colour clear (a Blt) on it, so by the time the first
batch comes the state machine has long said "done". The draws never race
the pending state because the CLEAR already waited for it - and the
cleared buffer is what the panel shows. So everything the game does in a
frame comes after the first frame tick following the write, and the panel
switches after that. The completion rule is early, not the gate: the
second reading, reached by a different route. The Direct3D gate stays; it
is correct and free.

The next build calls a flip complete at the SECOND frame tick after the
write (`V9X_I9XX_FLIP_TICKS_TO_COMPLETE`). One frame of latency, and it
cannot be early under any latch that is at most a frame late. If intel80
still shows construction, the frame counter is not counting the event
the panel switches on, and the readback instrument below comes before any
further code change.

## Review of 2026-09-18, and what it changed

A review of this record and the code made five points. Taken in order:

1. **The frame counter was read inconsistently.** High and low registers
   read once each; a carry between the reads composes a count off by 256,
   which the masked subtraction reads as hundreds of frames elapsed and
   completes a flip at once. Fixed: high, low, high, retried while the
   highs differ, as i915's `i915_get_vblank_counter` does. Rare, so not
   the once-a-frame flicker, but the two-tick experiment was not
   trustworthy without it.
2. **`DrawsToFront=0` proves two base addresses differ, not that the
   buffers are disjoint.** The buffers are 0x96000 apart, exactly
   1280 x 480. A plane stride left at the desktop's 2048 would fetch 480
   rows across 0xF0000 bytes and run 0x5A000 into the next buffer,
   showing its construction at the bottom of the frame with both bases
   different. The desktop capture cannot rule that out. From this build
   the snapshot records, as read during the game, the plane stride,
   plane control and pipe source registers at flip issue
   (`FlipStrideLast`, `FlipDspCntrLast`, `FlipPipeSrcLast`) and the render
   target's pitch and width<<16|height (`DrawsPitchLast`,
   `DrawsExtentLast`). Shared ABI 2026091711. If stride and pitch
   disagree, that is the fault and no timing change will touch it.
3. **Memory readback cannot measure the latch.** Reading the front
   surface says what that allocation holds, not which allocation the
   panel is fetching. Replaced by a controlled visual probe,
   `V9XDDP.EXE /reuse`: front red and back blue with a white marker every
   32nd row (a wrong stride slants or respaces them), both surfaces'
   addresses and pitches written to the result, then for delays of 0, 17,
   34 and 100 ms after GetFlipStatus says done, the RETIRED buffer is
   painted solid green for 50 ms, thirty times per delay. Green on the
   panel is the display fetching a buffer the driver has released; the
   smallest delay at which it stops is the latch delay. A camera or the
   eye is the instrument. `ReuseDoneMsMax` records the longest wait for
   "done".
4. **Two ticks is a workaround, not proof.** Agreed and recorded so. If
   it removes the flicker it says timing, not where the latch is; if it
   does not, the next look is layout and presentation order, not more
   delay.
5. **Aggregate counts cannot establish that every clear waited.** Not
   done in this build: a small event trace of flip issue and completion,
   clear destination and extent, and the first draw after, with frame
   count and scanline. Noted as the next instrument if the probe and the
   layout capture do not settle it.

## intel80: the layout registers during the game

`C:\temp\intel80\beforeProbe` (Final Reality, then V9XTRACE) and
`afterprobe` / `afterprobetrace` (V9XDDP `/reuse`, then V9XTRACE). Build
`73709d2-dirty`, which has the two-tick completion. The operator: still
flickering; a video is coming.

```
FlipHandled=634  FlipRingIssued=654  FlipTakenAtDone=634  DrawsFlipWaited=0
DrawsToFront=0  DrawsToBack=632195  DrawsTargetLast=0  DrawsDisplayedLast=0x96000
FlipStrideLast=0x00000800     plane stride register: 2048 bytes
FlipDspCntrLast=0x95000000    plane enabled, 16 bpp 565, pipe select B
FlipPipeSrcLast=0x03FF023F    pipe source 1024 x 576
DrawsPitchLast=0x00000500     render target pitch 1280
DrawsExtentLast=0x028001E0    render target 640 x 480
```

Read at flip issue, so during the game: the one enabled plane on the one
enabled pipe has stride 2048 and the pipe's source size is 1024 x 576 -
the desktop's values, unchanged by the 640 x 480 mode set - while the
render target and the flipping buffers are 640 x 480 at pitch 1280. The
MMIO capture taken back at the desktop shows the same pipe B values, and
pipe A holding 640 x 480 timings (HTOTAL 800/640, VTOTAL 525/480, SRC
640 x 480) with pipe A DISABLED and plane A off: the VBIOS programmed a
640 x 480 pipe somewhere and it is not the one the panel is on.

This is the review's second point, and worse than it feared: not a
stride a little wide, but a display that by its registers is fetching
1024 x 576 rows of 2048 bytes from buffers holding 640 x 480 rows of
1280. Fetched that way the picture would be sheared beyond recognition,
and the video shows a coherent, panel-filling, 16:9-stretched scene. So
either these registers are not what the panel is fetching by - the
VBIOS presents 640 x 480 through some path these six registers do not
describe (the gen3 panel fitter and the plane size and position
registers are not captured) - or the coherent picture is not coming
from where the driver's model says. Either way the flip path has been
programming a plane whose stride register says 2048 and sending that
2048 as the pitch of every MI_DISPLAY_FLIP, and the picture did not
break, which means the pitch in that command is not what the panel
uses either.

Unresolved, and it now comes before the latch question: until the
registers that actually describe the panel's fetch in this mode are
known, no reasoning about what the panel shows when is grounded. The
`/reuse` probe's white row every 32nd row is the direct test: at stride
1280 they are level and 32 rows apart on the panel; at 2048 they break
into slanted segments. The video will say which.

```
Probe: ReuseFrontAddress=4237930496 ReuseBackAddress=4237316096 (0x96000 apart)
       ReuseFrontPitch=1280 ReuseBackPitch=1280 ReuseDoneMsMax=34 FlipPixelOk=0
```

`FlipPixelOk=0` is the GDI readback and has been 0 on every Intel flip
run; the camera is the instrument for this probe.

### The /reuse video: the flips are right and the stride is right

The operator's video of the probe (60 fps, 2,774 frames), each frame
classified by its dominant colour:

```
stage 0 (0 ms):   red 5 frames / blue 5 frames, alternating, 30 cycles
stage 1 (17 ms):  red 5-6 / blue 6-7
stage 2 (34 ms):  red 6-7 / blue 8-9
stage 3 (100 ms): red 11-12 / blue 11
GREEN: never during any stage (the one green run, frames 857-897, is the
       earlier hardware-fill rung's colour before the probe began)
```

Red and blue alternate cleanly at every delay, including zero: after
GetFlipStatus says done the retired buffer was painted green thirty times
per stage and the panel never showed it. The flip presents the buffer the
driver says it does, and the driver's "done" is not early - not by a
frame, not by a millisecond that a 60 fps camera can see. The fifteen
white marker rows are level, full width and evenly spaced: the panel is
fetching 640 x 480 rows at pitch 1280, whatever the stride register
reads, and the registers of intel80 describe something other than the
panel's fetch. So the review's second point is answered in the negative
by the picture, and its stride registers stay an open oddity rather than
the cause.

What the probe does that the game does not: it fills with the CPU. Every
one of its writes is complete before Flip is called. The game's frame is
drawn by the GPU, and the driver's notion of "this batch is drawn" is
that the ring head has reached the tail. That says the command parser
has CONSUMED the batch; it does not say the pixels have landed. If the
3D pipeline is still working when Flip presents the buffer, the panel
shows the frame finishing on screen: the clear (a CPU fill, complete),
then the sky, then the ground filling in - which is the video of the
game, exactly, and is consistent with every counter: DrawsToFront=0
(the target was not the displayed buffer when the batch was ISSUED),
DrawsFlipWaited=0 (nothing raced the flip), a clean probe (no GPU
involved), and eight timing changes that moved nothing (the flip was
never the problem). It also explains why more regular flips made the
flicker faster in intel74.

## The breadcrumb build (intel81)

Every runtime batch now ends: MI_FLUSH, then MI_STORE_DWORD_IMM of a
sequence number to a dword in the reserve's status page (the page
HWS_PGA was meant for and never pointed at; intel80 reads the BIOS
value there). The store is pipelined behind the rendering ahead of it -
i915's gen3 request emission is flush then store for this reason - so
the value arriving in memory means the drawing is finished. The submit
waits for the head as before and THEN for the breadcrumb, and counts the
polls between the two (`BreadcrumbLagPollsMax`, `BreadcrumbLagPollsTotal`
over `BreadcrumbSubmits`; `BreadcrumbTimeouts` for a store that never
lands, drawn anyway). The decoder licenses exactly one store to exactly
that address and no other, with a host test. Shared ABI 2026091712.

The count is the measurement:

- Lag near zero everywhere and the flicker unchanged: the head was
  already the truth, this model is dead, and what remains is the event
  trace the review's fifth point asked for.
- Lag large and the flicker gone: the frames were being presented
  unfinished, and the record closes on that.
- Lag large and the flicker still there: the drawing was late AND
  something else is wrong; the trace follows with this fixed.

### intel81: hard lock at the first 3D frame

The machine locked as Final Reality's first 3D scene was about to
appear - the first runtime batch, the first breadcrumb. `C:\temp\intel81`
is the capture from the reboot (ring head and tail both 0x110, nothing
of the run). Two defects in the build, both found by reading it back
against the sources afterwards, and either sufficient:

1. **The command was the wrong length.** The build emitted three dwords
   (header with length 1, address, data). MI_STORE_DWORD_IMM on this part
   is four: header with length 2, a reserved zero, the graphics address,
   the data - igt's `intel_reg.h` defines it as `(0x20 << 23) | 2` and
   `gem_storedw_loop` emits `header | 1 << 22, 0, address, data` for every
   part before gen8. The parser read the address as the reserved dword
   and the data (sequence 1) as the address, and stored the next dword to
   graphics address 1.
2. **The graphics address was garbage.** It was computed as the ring's
   linear address minus the framebuffer's linear address, on the belief
   that both came from one mapping of the aperture. They do not: the
   mini-VDD maps the reserve on its own (`V9xMini_I9xx_Ring_Open`, the
   whole megabyte), so the difference was two unrelated kernel linear
   addresses and the store was aimed at whatever that came to. The ring
   offset the HAL should have used is `fb.vram_bytes`, which the family's
   `reserve_video_memory` already sets to the reserve boundary; the code
   two functions above it in the same file says so.

The CPU side was not the fault: the reserve mapping covers the status
page, and the read was in bounds. The next build has the four-dword form,
the address from `fb.vram_bytes`, a decoder that refuses a non-zero
reserved dword (which is exactly where the three-dword form would be
caught, with a host test), and a breadcrumb wait of 200,000 polls so a
store that never lands costs a slow frame rather than a machine that
looks hung. It is still an unmeasured command on this part, and the
lock is a possibility the operator should weigh before booting it.

### intel82: the corrected store never lands

Build `a1bf160-dirty` (`fbc2f29`). No lock. One 3D frame appeared and the
game then crawled; the operator quit and ran V9XTRACE:

```
I9xxDrawsSubmitted=2458  I9xxDrawsRefused=0  FlipHandled=4
BreadcrumbSubmits=0  BreadcrumbTimeouts=2458  BreadcrumbLagPollsMax=0
CountLock=502  CountBlt=24
```

Every batch reached head == tail (the timeout counter is only reached
from there) and not one breadcrumb arrived in 200,000 polls. The
slowness is that wait: 2,458 batches at a fifth of a second each. The
memory is not a stale cache line - `V9XBOOT.INI` shows MTRR 1 covering
7F800000 for 8 MiB as type 0, uncached, and the read goes through the
reserve mapping the ring writes go through. So MI_STORE_DWORD_IMM with
the virtual-address bit, four dwords, to a GTT address the display and
the ring both resolve, does not write there on this part by this path.
Whether it wrote somewhere else is not known.

Rather than a third guess at that command's semantics, the next build
uses what i915 v4.4 actually does on gen3 to mark a request complete:
`HWS_PGA` pointed at a physical status page and `MI_STORE_DWORD_INDEX`
into it (`i9xx_add_request`). The page is the reserve's status page, the
one the layout always set aside; its physical address is taken from the
GTT's own entry for it through BAR3 rather than from arithmetic on BSM,
and `HWS_PGA` is written once before the first batch and read back
(`HwsPgaBefore`, `HwsPgaWritten`, `HwsPgaAfter`; shared ABI 2026091713).
If the page cannot be resolved the batch goes without a breadcrumb and
the submit waits on the head alone, as before intel81. The decoder now
knows only the INDEX form.

### intel83: the status page took, and the INDEX store did not land either

```
HwsPgaBefore=0x1FFFF000  HwsPgaWritten=0x7FEC0000  HwsPgaAfter=0x7FEC0000
I9xxDrawsSubmitted=1460  BreadcrumbSubmits=0  BreadcrumbTimeouts=1460
```

The register took the page. The store into it, by the mechanism i915
uses on this generation, was not seen by the CPU inside the wait on any
of 1,460 batches - the same result as the IMM form to a GTT address. Two
different store commands both "never landing" is no longer evidence
about the store. It is evidence about the wait or the read: either the
CPU is not looking at the memory the GPU writes (a mapping that is not
what it appears), or the store is real and lands later than 200,000
polls, which would mean the drawing behind the flush is still running
that long after the head passed it - which is the flicker hypothesis
itself, measured. The next build separates those: a CPU write to the
page read back through the same mapping (`HwsCpuProbe`), the breadcrumb
dword as read at each timeout (`HwsValueLast`: zero means nothing was
ever stored; an older sequence means the store works and lands late),
and a count of batches whose predecessor's breadcrumb had arrived by the
time the next one was built (`BreadcrumbLate`). The wait drops to 20,000
polls so the game is slow rather than stopped while it measures.

## After the independent review (2026-09-18)

`2026-09-18-final-reality-flicker-independent-review.md` found the
breadcrumb build's counters, setup, validation and failure handling
unfit to measure with, and said so plainly: a breadcrumb never observed
is a failed measurement, not evidence about rendering. Its R1 to R5 are
done on the host (`fe571f4`) and listed there. The consequence for the
next boot: `HwsSelfTest` is the first thing to read. A 2 means the
store-only round trip failed and every later count is about a channel
that does not work; a 1 with `HwsSelfTestPolls` says the round trip works
and how long it took, and only then do `BreadcrumbTimeouts`,
`BreadcrumbLate`, `BreadcrumbOutstanding` and the drain counters mean
what they say. The review's H3 stands: the IMM history is unresolved and
stays out of the runtime. H4 and H5 are open.

## How to run the next boot

1. Deploy the package as usual and run Final Reality; capture as usual.
   Read `FlipStrideLast` against `DrawsPitchLast` first.
2. Run `V9XDDP.EXE /reuse` from the package directory with the camera on
   the panel. Note at which delays green appears. The run takes about
   two minutes and ends with the normal probe result files.

## Not in scope here

- 3DMark99 shows one or two textures and complains about 800x600: open,
  deferred until the flicker is closed.
- The 117 texture creates a frame (76,006 in 650 frames, one Lock each):
  a cost, not a fault; noted for later.
