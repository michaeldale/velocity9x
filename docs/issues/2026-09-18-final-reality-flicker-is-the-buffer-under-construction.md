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
| intel84 | `273bfa4-dirty` (`89bf4bd`) | completion channel as a state machine; store-only round trip before use | round trip FAILED (2,000,000 polls); no breadcrumbs after; flicker intermittent, "an improvement but far from fixed" |
| intel85 | `2347f59-dirty` (`e29dddf`) | ACTHD/INSTDONE raw window; in-game layout kept through the restore | layout RIGHT in the game (H4 closed); ACTHD never moved, INSTDONE constant; the 2,000-poll window cut the run to 89 flips |
| intel86 | next | breadcrumb as a one-pixel XY_COLOR_BLT, the GPU write Phase 4 measured reaching the CPU; ACTHD window removed | to boot |

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

## intel84: the round trip fails; the picture varies within a run

```
HwsSelfTest=2  HwsSelfTestPolls=2000000  HwsCpuProbe=1
HwsPgaBefore=0x1FFFF000  HwsPgaWritten=0x7FEC0000  HwsPgaAfter=0x7FEC0000
BreadcrumbSubmits=0  BreadcrumbTimeouts=0  BreadcrumbOutstanding=0
RenderDrainWaits=0  I9xxDrawsSubmitted=600978  FlipHandled=592
```

The store-only round trip did not land in two million polls. The CPU's
mapping of the page is real (a CPU write reads back), the register took
the page, and the one MI_STORE_DWORD_INDEX the self-test submitted was
consumed by the parser (the submit returned) and never appeared. So the
channel failed once, as designed, and every batch after went without a
breadcrumb: the run the operator saw was the pre-intel81 runtime with a
two-million-poll stall at the first frame ("it started slow").

The operator's account of the rest: "worked for a bit, some flicker,
then the flicker went completely away, then it came back ... overall an
improvement but far from fixed." The runtime after the self-test was the
two-tick, active-video-write build of intel80, which was "no better"
then. What is new in the account is the VARIATION: whole stretches
without flicker, then its return. A defect that comes and goes with the
scene is a timing defect under a varying load, which is what an
unfinished frame at presentation would do - light scenes finish, heavy
ones do not - and is not what a fixed wrong register would do. It is an
account, not a measurement; the event trace (review H5) is what would
turn it into one.

Three GPU-to-CPU stores by two mechanisms have now failed to be
observed. The next build stops asking the GPU to write and reads the
engine instead. A first cut compared ACTHD with the tail as an address
and would have called the result completion evidence; the review of that
commit pointed out that ACTHD's Gen3 address form and idle meaning are
not validated on this part, that i915 uses it for progress and not for
completion, and that a wrong interpretation would have produced exactly
the zero the record was about to read as disproof. So the build records
the register RAW: its value when RING_HEAD reaches the tail and after
2,000 polls, whether it changed in between (`ActhdMoved`, `ActhdStill`,
`ActhdChangesMax`), the range of every value seen, and INSTDONE at both
moments. A register that keeps changing after the parser is done is an
engine still working, and that needs no interpretation; what it is
working on, and when it stops, is for an instrument that has validated
the register first. `ActhdStill` equal to the submit count says nothing
either way until the register is understood.

The same build captures the 24 display registers of review H4, taken
only at a flip to a buffer other than offset zero - the desktop
restoration flips to zero and would otherwise overwrite the sample on the
way out of the game (the second finding of that review) - with the
sample's target offset, frame counter and count kept beside it. The
self-test bound drops to 200,000 polls so a failing channel costs a
moment, not the start of the run.

## intel85: the layout is right in the game, and the engine registers do not move

```
ScanLayoutSamples=45  ScanSampleOffset=0x00096000  ScanSampleFrame=12979
PIPEB_CONF=0x80000000  PIPEB_SRC=0x027F01DF (640x480)  PIPEB_HTOTAL=0x053F03FF  PIPEB_VTOTAL=0x029F023F
DSPB_CNTR=0x95000000  DSPB_ADDR=0x00096000  DSPB_STRIDE=0x00000500 (1280)  DSPB_SIZE=0x01DF027F (480x640)
PFIT_CONTROL=0x80002668 (enabled)  LVDS=0xC0300300  VGACNTRL=0xA2C4008E  pipe A off, plane A off
```

**H4 is closed.** Read at a game flip to 0x96000, protected from the
desktop restore: the pipe source is 640 x 480, the plane stride 1280, the
plane size 640 x 480 and the panel fitter on, scaling to the 1024 x 576
timings. The intel80 readings of stride 2048 and source 1024 x 576 were
the desktop's, written over the in-game sample by the flip to offset zero
on the way out, exactly as the review of 2347f59 said they could be. The
registers agree with the buffers and the picture; there is no fetch
discrepancy.

```
ActhdStill=82249  ActhdMoved=0  ActhdChangesMax=0
ActhdAtHeadLast=0x37A04A10  ActhdAfterLast=0x37A04A10  TailLast=0x00004A10
ActhdRawMin=0x00000010  ActhdRawMax=0x37A04A10
InstdoneAtHeadLast=0x7FFFFFC0  InstdoneAfterLast=0x7FFFFFC0
FlipHandled=89  I9xxDrawsSubmitted=82139
```

In 82,249 submits ACTHD never changed in the 2,000 polls after the head
reached the tail, and INSTDONE read 0x7FFFFFC0 at both moments every
time. The low sixteen bits of ACTHD equal the tail (0x4A10) and the high
bits are not the ring's graphics address, so the register's form is
still not understood; but a register that never moves after the parser
is done gives no sign of an engine still working. That is not proof of
completion (review of 2347f59), and it is the second reading that fails
to find the asynchrony the unfinished-frame model needs. The 2,000-poll
window cost the run: 89 flips, and the operator saw "frame swapping"
rather than an overlay, which at that rate is what any flicker looks like.

**HwsSelfTest=2 again** (200,000 polls). Three MI stores by two
mechanisms, consumed by the parser and never seen. The next build stops
using MI stores. The breadcrumb becomes a one-pixel XY_COLOR_BLT whose
colour is the sequence number, into the status page's dword - the one GPU
write this machine has MEASURED reaching memory the CPU reads back
through the aperture (Phase 4 S09/S10, 2026-09-14, ScratchGuard=PASS).
The self-test uses the same packet. HWS_PGA is no longer written. The
decoder licenses exactly one such fill, of exactly that shape, at that
address, behind a flush and followed only by padding, and refuses both MI
store forms. The packet is the measured one in full - blit, MI_FLUSH,
MI_NOOP, eight dwords - because the flush after the blit is what writes it
out of the render cache to where the CPU reads. A first cut left the
suffix off: the runtime's preceding flush cannot flush a write that
follows it, and the self-test submits the packet alone, so a buffered
breadcrumb could have failed the self-test and disabled the channel for a
reason that was not the mapping's (review of 43c00be). The ACTHD window is removed; a single raw read of the last
submit remains.

If `HwsSelfTest` reads 1 on intel86 the completion channel exists for the
first time, and `BreadcrumbTimeouts`, `BreadcrumbLate` and the drain
counters finally measure the drawing against the presentation. If it
reads 2, the GPU's writes through the GTT are not reaching what the CPU
reads at that page, which the 2026-09-14 measurement says they should,
and the difference between that scratch page and this one is the next
question.

## intel86: it reads 1, and the model it was built to test is dead

2026-09-19, netbook, Final Reality robot benchmark.
`docs\decisions\2026-09-19-intel86-the-completion-channel-works-and-the-
flicker-is-not-unfinished-drawing.md`.

`HwsSelfTest=1` at zero polls, then 575,868 breadcrumbs with
`BreadcrumbTimeouts=0`, `BreadcrumbLate=0`, `BreadcrumbAbandoned=0`. The
channel exists. What it measures is that the drawing was always finished:
`RenderDrainWaits=0`, so no flip on this run presented a frame the GPU had
not completed. The unfinished-frame model is closed.

Two others closed with it. `DrawsFlipWaited=0` - no batch ever arrived
while a flip was pending, so intel78's exposure did not occur.
`DrawsToFront=0` against 575,868 batches - the buffers are right, as they
were in intel74. And the active-video write window, which stood
"UNMEASURED as a fix until the next boot", ran 585 presents and the
operator saw the same flicker.

So every mechanism above is now measured out and the picture is unchanged.
`FlipHandled=585` of `CountFlip=53193`: ninety-nine per cent of Flips were
refused, and `flip_still_drawing` counted two tests at once so the cause of
that refusal cannot be named from this capture. `FlipWindowClosed` splits
them from ABI stamp 2026091901.

What has never been read is the latch: whether the plane base takes effect
where it is written or at the blank. No register on this part has been
found that reports it, and the flip-completion bit is not one - ISR read
before and after all 605 flips ORs to zero, as in intel71 and intel72.

## 2026-09-19 evening: the flicker is photographed, and the S3 half has a fix

`docs\decisions6-09-19-the-trio3d-flicker-is-a-completion-signal-that-
does-not-exist.md` (its headline explanation withdrawn in place),
`...-the-edge-write-made-it-worse-and-what-the-upstream-record-says.md`.

The panel was recorded on the Trio3D and the flicker is a single display
frame, recurring every eight or nine frames, in which only the cleared
buffer and the sky band are on screen. So the symptom is the buffer under
construction, confirmed rather than inferred.

Two explanations were tried and are dead. The missing 3D completion signal
is real - `D3dDoneSeen=0`, `D3dDoneSkipped=273597` on this part - but
`v9x_virge_settled` already waits on the IDLE bit, and the gap the done bit
exists to close was measured on 86Box, so it is not established as this
flicker. Moving the start-address write to the blank-to-active edge made
the flicker measurably WORSE - 23 dips per twelve seconds became 70, deeper
- and is reverted.

What is in now, unverified on silicon: the S3 flip completes when the
retrace ENDS rather than when it starts, and the ViRGE draw path takes the
intel78 wait before its first batch. Between them those are the standard
VGA page-flip discipline, which this path followed neither half of. The old
`VirgeDrawsFlipPending=0` was an artefact of completion at blank onset and
should not be read as evidence the guard is unneeded.

On the Intel side nothing is fixed. Three things were added to be read on
the next netbook boot: `Ecoskpd` (0x021D0 bit 0 declares, on Gen3 only,
whether the flip-pending bit means done or queued - never read here across
three investigations into that bit never setting), and
`FlipIssueLine{Last,Min,Max}` against `FlipIssueVactive`, which settle
whether the plane base readback is the active or the pending value. i915
relies on it being the active value; this project's latch model says the
opposite and was inferred from a video.

## How to run the next boot

1. Deploy the package as usual and run Final Reality; capture as usual.
   Read `FlipWindowClosed` against `FlipStillDrawing` first: the two
   together are the 52,608 refusals of intel86, and which one carries them
   says whether presents are throttled by the beam or by the previous
   flip.
2. Run `V9XDDP.EXE /reuse` from the package directory with the camera on
   the panel. Note at which delays green appears. The run takes about
   two minutes and ends with the normal probe result files.
3. The latch is the open question and no register reports it, so the
   camera is the instrument: record the panel through several flips and
   read where in the frame the buffer changes.

### Trio3D, to judge the S3 fix

Record the panel through one Robots pass and count single-frame luminance
dips over a twelve-second window, the way the two 2026-09-19 recordings
were counted: frames below three quarters of the local median, in the
capture area only. 23 dips is the unfixed baseline and 70 is the edge-write
regression. Well below 23 means the fix holds; 23 means it does not, and
the next suspect is the latch itself.

Read `DrawsFlipWaited` beside it. Zero again would mean the wait still
never fires and the completion definition is still wrong, not that the
hazard is absent - that is the mistake the first reading of
`VirgeDrawsFlipPending` made.

### Netbook, to settle the readback

`Ecoskpd`, and `FlipIssueLineMin`/`Max` against `FlipIssueVactive`. Lines
scattered through active video, with `FlipBaseImmediate` still at 100%, say
the plane base readback is the pending value and that every conclusion
drawn from it needs revisiting.

## Not in scope here

- 3DMark99 shows one or two textures and complains about 800x600: open,
  deferred until the flicker is closed.
- The 117 texture creates a frame (76,006 in 650 frames, one Lock each):
  a cost, not a fault; noted for later.
