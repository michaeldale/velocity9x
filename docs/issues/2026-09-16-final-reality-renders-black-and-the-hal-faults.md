# Final Reality renders black, and the HAL faults on teardown

Date: 2026-09-16
Machine: MICHAEL-NETBOOK, Intel 945GSE, `8086:27AE` revision 03
Build: `f9ec875`
Capture: `C:\temp\intel53`
Status: RESOLVED 2026-09-17, intel56. Final Reality renders on the 945GSE:
1,175,294 RenderPrimitive calls returned and 1,947,273 batches were
submitted with none refused, in the first build with the draw path's
three-page stack frame moved off the stack. Two follow-ons are open and
recorded at the end: every texture draws untextured, and the frame
flickers.

Final Reality detected hardware Direct3D for the first time - the caps
published in `7bb536f` and `f9ec875` are enough for it to accept the device -
started, rendered a black screen, and the machine had to be powered off.

## The ring did not hang

Stated first because it is the thing that would have stopped everything else.
From the event capture, across eight events:

```
boot-enable   RingTail=00000000  RingHead=00000000  RingStart=00000000
mode-switch   RingTail=00000000  RingHead=00000000  RingStart=006B0000
mode-switch   RingTail=00005840  RingHead=00205840  RingStart=006B0000
mode-switch   RingTail=00005840  RingHead=00205840  RingStart=006B0000
  ... three more, all identical ...
disable       RingTail=00005840  RingHead=00205840  RingStart=006B0000
```

`RING_HEAD` masks to exactly `RING_TAIL`, so the GPU consumed everything it
was given. `EngineFifoTimeouts`, `EngineIdleTimeouts` and `EngineResets` are
all zero.

**And the tail did not move across five mode-switch events.** 0x5840 is only
0x230 bytes past intel52's 0x5610 - about two draws. So the engine stopped
submitting almost immediately and the black screen is draws being REFUSED,
not a GPU that stopped.

**Which refusal is not recorded.** `i9xx_refuse_last` exists and would name
the check, but it reaches only `V9XSNAP.INI`, which `V9XTRACE.EXE` writes and
which needs a machine the operator can still drive. The capture's
`V9XSNAP.INI` is from the previous boot. That gap is closed in the fix below.

## The fault, located to the instruction

`V9XTRACE.INI` is written by the HAL's own unhandled-exception filter, so it
survives:

```
FaultCode=0xC0000005
FaultAddress=0xB0403BD4
LastEnterId=0x00000016      (22, DestroySurface)
LastExitId=0x00000021       (33, D3dRenderState)
```

and the trace ring ends:

```
0x1A50 D3dRenderPrimitive enter 0x03080003
0x1A51 DestroySurface enter 0x82F0B8A0
```

`v9xhal.map` places `v9x_d3d_color_key_find_` at `0xB0403383`, which the
`wdis` listing of `d3d_core.obj` puts at module offset zero. So the fault is
at module offset **0x851**, and the listing reads:

```
084D                v9x_d3d_surface_lcl_:
084D  85 C0         test  eax,eax
084F  74 03         je    L$68
0851  8B 40 04      mov   eax,0x4[eax]     <-- faulting instruction
0854                L$68:
0854  C3            ret
```

That is `wrapper->lpLcl` in:

```c
static V9X_DD_SURFACE_LCL *v9x_d3d_surface_lcl(void *surface)
{
    V9X_DD_SURFACE_INT *wrapper = (V9X_DD_SURFACE_INT *)surface;

    return wrapper != 0 ? wrapper->lpLcl : 0;
}
```

The null test passed, so the pointer was **non-null and not a surface**. The
function cannot tell those apart, and this is chip-neutral code that predates
every Intel change.

`D3dRenderPrimitive` entered at 0x1A50 and never logged an exit, and
`DestroySurface` entered after it. The most coherent reading is that the
application began tearing down while a primitive was outstanding, and
`DestroySurface` reached this function with a surface the teardown had already
released. That reading is NOT confirmed: nothing records which caller passed
the pointer.

## What the probe measured in the same boot

`V9XDDP` ran and its results are consistent with the caps working:

```
TexFormatCount=1
TexFormat565=1              the one format published, enumerated correctly
TexFormat1555=0
TexFormat4444=0
D3DTrianglePixelOk=1        the untextured triangle still draws
D3DZCompareOk=0             depth still does nothing
```

The depth result is unexplained and matters: this build publishes
`dwDeviceZBufferBitDepth`, `D3DPRASTERCAPS_ZTEST` and `D3DPCMPCAPS_LESS`, and
the engine binds the application's Z surface. `i9xx_depth_draws` and
`i9xx_depth_skipped` would separate "the depth buffer was never bound" from
"it was bound and the test did nothing", and neither was captured.

## What was changed in response

Only the instrumentation, because nothing here identifies a defect to fix.
`ddhal_core.c`'s fault flush now writes the Gen3 draw counters and the texture
refusal counters into `V9XTRACE.INI`, which is the artefact a fault always
produces. Had it done so this time, the refusal reason would be known.

## What is NOT concluded

- **That the machine hung.** The GPU did not; whether Windows was still
  responsive is not recorded and the operator had no way to tell.
- **Why draws stopped.** Five candidate refusals exist and the capture names
  none of them.
- **That `v9x_d3d_surface_lcl` is the cause of the black screen.** It is a
  fault during teardown, after rendering had already produced nothing.

## intel54: reproduced, and the counters settle it

Build `1d2ef2a`, which added the Gen3 counters to the fault flush. Same
sequence, same result, and the trace now answers the question intel53 could
not:

```
FaultCode=0xC0000005
FaultAddress=0xB0403E34      module offset 0x851 - THE SAME INSTRUCTION
I9xxDrawsSubmitted=0x00000000
I9xxDrawsRefused=0x00000000
```

and the ring ends identically:

```
0x0127 D3dRenderPrimitive enter 0x03080003
0x0128 DestroySurface enter 0x834498A0
```

`RingTail` read zero at every event this boot. **Nothing was submitted and
nothing was refused, so `draw_triangles` was never called at all.** That much
is solid, and it clears the Gen3 engine: no triangle reached it.

### The caller is NOT established, and this record claimed it was

The paragraph that stood here said the fault was in
`V9xD3dRenderPrimitive`'s prologue resolving `lpExeBuf` or `lpTLBuf`, and
called the cause identified. **That does not follow from the evidence and it
is withdrawn.**

`v9x_d3d_surface_lcl` has TEN call sites. One of them is inside
`v9x_d3d_textures_forget_surface`:

```c
v9x_d3d_surface_lcl(v9x_d3d_textures[index].surface) == surface
```

which runs over every stored texture pointer, and which `DestroySurface`
calls. A fault inside the helper is consistent with that path as much as with
the primitive one - and the capture's `LastEnterId` is 22, `DestroySurface`,
which if anything favours the teardown scan. The remaining eight sites are
the render target, the depth surface, the colour-key pair and two further
primitive entry points.

The "no PRIMREJECT was pushed" argument does not settle it either. It is
consistent with a fault before the refusal path, and equally consistent with
`RenderPrimitive` having passed its checks and the fault arriving later from
teardown.

What IS established: the faulting instruction, that it read a non-null
non-surface pointer, that it reproduced at the same module offset in two
consecutive boots, and that the engine never saw a triangle.

### What was changed

`v9x_d3d_surface_lcl` now asks `IsBadReadPtr` before dereferencing, counts the
refusal in `surface_int_rejected` and records the value in
`surface_int_last`. A HAL cannot validate a pointer the runtime hands it; it
can decline to die on one. Returning "no surface" produces a refused draw,
which every caller already handles.

### What this does NOT explain

**Which caller, and why the pointer is bad.** The same code serves the ViRGE,
where Final Reality runs. Candidates not yet separated: the texture-teardown
scan holding a released surface; a field offset in
`V9X_D3DHAL_RENDERPRIMITIVEDATA` right for one path and wrong for another; an
execute buffer the runtime never created because `lpDDExeBufCallbacks` is
zero; or a pointer valid only in a context this HAL is not called in.

### The caller is now recorded

`v9x_d3d_surface_lcl` takes a site id, and a refusal records the last site in
`surface_int_site` and a bitmask of every site that has ever rejected in
`surface_int_sites`. Both reach the fault flush and the snapshot. The mask is
there because a capture naming only the last site can be read as naming the
only site, which is the shape of the mistake this record is correcting.

Sites: 1 texture-teardown scan, 2 bound texture, 3 render target, 4 depth
surface, 5 and 6 the colour-key pair, 7 DrawOnePrimitive exe,
8 DrawPrimitives exe, 9 RenderPrimitive exe, 10 RenderPrimitive TL.


## intel55: the caller, measured

Build `77c7951`, which added the call-site tag. **No fault this boot** - the
guard held and `V9XTRACE.INI` was never rewritten. `V9XTRACE.EXE` ran and
`V9XSNAP.INI` says:

```
SurfaceIntRejected=2
SurfaceIntLast=0x833B39F4
SurfaceIntSite=1
SurfaceIntSites=0x00000002      bit 1, and only bit 1
```

Site 1 is `v9x_d3d_textures_forget_surface`, the teardown scan. Sites 9 and 10
- `RenderPrimitive`'s `lpExeBuf` and `lpTLBuf` - **never rejected once**.

The reading published on 2026-09-16 and withdrawn the same day was wrong, and
the alternative raised in review was right. The mask is what settles it rather
than the last-site field alone: one bit set means one site, which is the
distinction that field was added to make.

### Why it was worse than a fault

`v9x_d3d_textures[].surface` stores the interface WRAPPER the runtime handed
over, and the scan dereferenced it to reach the local half for comparison. A
surface destroyed after another has already gone is compared by reading freed
memory - and the guard's answer, "no surface", compares unequal. **So the
entry is never cleared**, and the stale pointer stays in the table for the
next scan to read again. The two refusals in one boot are that, twice.

### The fix

`V9X_D3D_TEXTURE` gains `lcl`, resolved once at `TextureCreate` while the
wrapper is certainly alive. The teardown scan compares that value and
dereferences nothing; `v9x_d3d_context_texture_surface` returns it; the swap
moves it with its wrapper; every clear site clears it. A stale VALUE compares
unequal, which is harmless. A stale POINTER dereferenced is not.

Sites 1 and 2 are retired and not reused, so a capture from an older build
still reads correctly.

## The rendering question, still open and now separate

Nothing drew this boot either, and for a reason that has nothing to do with
the fault:

```
D3dRenderPrimitiveCalls=0
D3dContextCreates=5      D3dTextureCreates=1
D3dDepthOffered=4        D3dDepthAccepted=4
I9xxDrawsSubmitted=0     I9xxDrawsRefused=0
RingTail=00000000        at every event
```

Contexts, a texture and four depth surfaces were created and accepted, and
then **no primitive was ever issued**. `D3dExecuteCalls` is zero too. So on
this boot the application set up and stopped before drawing anything, and the
engine's own counters cannot say more because it was never called.

Whether that is the same cause as the black screen is not established.

## intel55 re-read: RenderPrimitive was entered, and never returned

The paragraph above says no primitive was issued. The snapshot says
otherwise, and the two are reconciled by where the counter sits:

```
CountD3dRenderPrimitive=1                    the trace's per-id enter count
D3dRenderPrimitiveCalls=0                    the diagnostics counter
Ring38=295 D3dRenderPrimitive enter 0x03080003
Ring39=296 DestroySurface enter 0x833B28A0   the next event; no exit, no reject
```

`render_primitive_calls` is incremented unconditionally at the END of
`V9xD3dRenderPrimitive`, after the draw loop and before the exit event. One
enter, no exit, no `D3dPrimitiveReject`, counter zero: **the HAL was entered
once and did not come back.** intel53 (`0x1A50`) and intel54 (`0x0127`) end
the same way. Three boots, one entry each, none returned.

The refusal path was not taken. Every check before the loop pushes a reject
event on failure, and none is in the ring; so the pointers resolved, the
opcode was TRIANGLE (3), size 8, count 3, and the code reached the loop.
Whatever stopped it is in the vertex read, the clipper or the engine, and it
left no counter because none of those has one before the fact.

### Whose call it was

The probe's `V9XDD.INI` in intel55 is dated 2026-09-16 20:58 - the intel53
run. `V9XDDP` was dying on its first profile write (the file was already at
the KRNL386 boundary; see the probe issue of 2026-09-17), so it took no
measurement in intel54 or intel55, and its `D3DTrianglePixelOk=1` in this
directory is stale. The D3D sequence at the end of the ring - five contexts,
four render states, one texture, one primitive of three triangles - is Final
Reality's, and the teardown that follows it (`DestroySurface`,
`FlipToGDISurface`, `D3dContextDestroyAll`) is the process going away.

### Why "no fault this boot" is consistent with a fault

`V9XTRACE.INI` is written by an UNHANDLED-exception filter. Win9x DDRAW is
understood to wrap its HAL calls in a `try/except` of its own (the DDK
sample era `DOHALCALL`); this is from memory of that source and is NOT
verified against the DLL on the netbook. If it holds, a fault inside
`RenderPrimitive` is caught there, the call fails, and the filter never
runs - which is what an application exiting after one failed draw looks
like. intel53 and intel54 wrote the file because the teardown scan faulted
from a different frame. So the absence of a fault flush in intel55 says the
fault, if there was one, was handled - not that there was none.

### A defect found on the way, and a hypothesis

`wdis` on `d3d_i9xx.obj` at this tree:

```
v9x_d3d_i9xx_draw_triangles_:
  push ecx / esi / edi / ebp
  mov  ebp,esp
  sub  esp,0x00002d7c        11,644 bytes: stream[1536], xyzw[768], uv[384], colors[192]
  mov  -0x8[ebp],edx         first writes, just under ebp
  ...
  push eax                   first write at the BOTTOM of the frame
```

The HAL is built `-s`: no stack probes. A frame spanning three pages whose
first low write is a `push` skips the thread's guard page whenever fewer
than three pages below the caller's `esp` are committed, and the push lands
on reserved stack. That is an access violation with no C executed - which is
exactly the shape of the evidence: entered, no counter, no reject, no exit,
and every engine counter at zero because `draw_triangles` never began.

**Hypothesis, not a measurement.** Nothing here shows the faulting address.
Against it: the probe reached the same function 404 times in intel52 and
returned, on a stack whose commit depth is unknown. The arrays are moved to
file scope regardless (build `d3d_i9xx.c`, `sub esp,0x8c` after), because a
three-page frame with no probe is wrong whether or not it is this fault.

### What the next capture can say

`V9X_TRACE_D3D_RENDERLOOP` (id 51, "D3dRenderLoop") is pushed once per
`RenderPrimitive`, after the first triangle's vertices are read from the
execute and TL buffers and before they are clipped or drawn.

- Enter, RenderLoop, exit with `D3dRenderPrimitiveCalls` advancing: the
  frame was the fault. Read `I9xxDrawsSubmitted` / `I9xxDrawsRefused` next.
- Enter, RenderLoop, nothing: the clipper or the engine. `I9xxRefuseLast`
  and the engine counters place it further.
- Enter, nothing: the vertex read itself - `lpExeBuf`/`lpTLBuf` resolved
  but their `fpVidMem` is not readable in this process.

Shared-block ABI is `2026091701`; a snapshot from an older `V9XTRACE.EXE`
is refused rather than misread.

## intel56: it draws

Build `1bd1bb7-dirty` (the tree committed as `f2e7655`). Final Reality ran
its benchmark to completion for the first time on this hardware. From
`V9XSNAP.INI`:

```
D3dRenderPrimitiveCalls=1175294     was 0 in intel53, 54 and 55
I9xxDrawsSubmitted=1947273          was 0
I9xxDrawsRefused=0
I9xxDepthDraws=1946897              I9xxDepthSkipped=4
SurfaceIntRejected=0                the texture-table fix held
EngineFifoTimeouts=0  EngineIdleTimeouts=0  EngineResets=0
RingTail=00008190  RingHead=65408190 at disable: head == tail, 0x654 wraps
```

No fault flush was written. `D3dContextCreates=17`, all destroyed.

### What this says about the frame hypothesis

The build differed from `77c7951` on the RenderPrimitive path in exactly
one way: `draw_triangles`' four arrays moved from an 11,644-byte stack
frame to file scope. The trace marker was added to the same path but writes
only to the ring; the colour-constant and probe changes are not on it.
Three consecutive boots did not return from the first call; the fourth
returned from 1.17 million. That is as close to a measurement of the cause
as this can get without a fault address, and the hypothesis is promoted to
the working explanation. It is NOT a proof: nothing recorded where the
earlier boots died.

### Open: every texture draws untextured

```
D3dTextureCreates=645         D3dTextureRefusedFormat=361
D3dTextureRefusedLast=0x10000F00      16 bpp, red mask 0x0F00: ARGB 4:4:4:4
D3dTextureRefusedSysmem=1946869       of 1947273 draws
D3dTextureRefusedCaps=0x04001800      ALLOCONLOAD | TEXTURE | SYSTEMMEMORY
I9xxTextureDraws=1
D3dTextureRefusedVidMem=0x005D0EF0
```

The Gen3 sampler path accepts one format, RGB 5:6:5
(`v9x_d3d_i9xx_texture_format`), and that is the one format the caps
publish (`TexFormatCount=1`, the probe). Final Reality's textures are
4:4:4:4. The 361 refusals are those textures at creation; DirectDraw then
allocated them in system memory, which the bind refuses per draw - hence
1.9 million system-memory refusals against a vidmem heap with 6 MB free.
The one textured draw was the probe's.

ARGB1555 and ARGB4444 are now built, decoded and published beside RGB565:
the MS3 type codes 1 and 2 in bits 5:3, from Mesa's `i915_reg.h`
(`MT_16BIT_ARGB1555`, `MT_16BIT_ARGB4444`) and xf86-video-intel's format
table, recorded in `intel_gen3_3d.h`. The fragment program samples RGBA
whatever the type, so nothing else in the stream changes. The builder
refuses a format nobody stated, the decoder checks the declared type in
the same MS3 equality that checks the pitch, and the scene streams are
unchanged (the generator verified byte for byte). **Unmeasured**: no
capture has yet sampled either alpha format on this part. The next Final
Reality boot answers it - `D3dTextureRefusedFormat` should read 0 and
`I9xxTextureDraws` should be most of the draws.

### Open: the frame flickers

The Intel family declares no `V9X_DD_ENGINE_CAP_FLIP`, so `V9xHalFlip`
returns NOTHANDLED for every one of its 2,092 calls and DirectDraw presents
each frame by copying the back buffer to the primary itself. That copy is
a CPU write through the aperture with no relation to the retrace. The flip
gate exists because the only display-start code writes S3's CR69; an Intel
path would write the plane base and needs its own vblank source. Both are
hardware claims this record does not make - the register write is
unmeasured on this part and goes through the probe-and-record loop first.

Two things intel56's read-only MMIO capture already settles. The panel is
on **pipe B**: pipe A's config, plane control and plane address all read
zero, pipe B's config has bit 31 set, plane B's control reads
`0x95000000` and its stride `0x800`. So the flip register on this machine
is plane B's base, not plane A's. And plane B's base reads zero with the
desktop at offset zero, which is what a scanout pointer looks like.

What it does not settle is the vblank source. From 2026-09-17 the HAL
reads both pipes' display-line register and frame counter 4096 times after
the first submitted draw of the boot and records, per pipe, the line range,
how many readings changed and the frames elapsed (`ScanA*`, `ScanB*` in
the snapshot; registers and masks cited in `intel_gma.h` from Linux
`i915_reg.h`; summary in `i9xx_scanline.c` with a host test). Expected on
this machine: pipe B's line sweeping 0 to about 623 with thousands of
changes and at least one frame elapsed, pipe A flat. A flat pipe B means
the registers are not what the header says on this part, and the flip work
stops until that is understood. Reads only; nothing is written to the
display until that answer is in a capture.

## intel57 and intel58: the machine hangs, and two changes share the boot

Build `bb2cff9-dirty`, the package built 18:23 on 2026-09-17: the first
to carry BOTH the ARGB1555/4444 texture formats (`6d60788`) and the
scanout watch (`4c32510`). Both boots hung to a blank screen and nothing
could be run afterwards; the machine was powered off.

- **intel57**: `V9XDDP` hung. Its result file ends at `ExclusiveVBlankHr`,
  the key before `SetDisplayMode`. Profile writes are cached, so the keys
  after it may have been written and lost; the file does not place the
  hang more precisely than "after entering exclusive mode".
- **intel58**: Final Reality hung. The event capture holds boot-enable and
  five mode switches, all completed, `RingTail=0` at every one.

So the mode switch is not it: five completed. The hang is at or after the
first Direct3D work of the boot. `V9XSNAP.INI` in both directories is the
09:45 file from intel56 and carries nothing from these boots. No fault
flush was written - a hang, not a fault.

**Which change is not established.** The two were first executed in the
same boot, which is the one-experiment-per-boot rule this record exists
to enforce, broken here by me. Of the two, the watch is the more
suspicious: 24,576 reads of display-line and frame-counter registers that
had never been read on this part, half of them on pipe A, which intel56's
MMIO capture shows powered down (`PIPEA_CONF=0`). A read into a
powered-down block hanging the bus is a known shape on Intel parts; it is
not measured here. The formats are one MAP_STATE word in a stream the
decoder accepted and the texture-packet audit describes.

**What changes.** The watch is compiled out (`V9X_I9XX_SCAN_WATCH 0` in
`d3d_i9xx.c`), and when it returns it reads only a pipe whose PIPECONF
enable bit is set. The next boot runs the texture formats alone. If it
survives, the watch was the hang and comes back on pipe B only; if it
hangs, the formats are, and the 4444 MAP_STATE word is the next thing to
question.

## intel59: the watch was the hang; the formats are accepted; still untextured

Build `4f80185-dirty`, formats in and watch out. The machine ran Final
Reality and the probe to completion and took a snapshot. **The scanout
watch hung intel57 and intel58**: it was the only thing removed, and the
boot survived. It comes back reading pipe B alone, in its own boot.

```
D3dTextureRefusedFormat=0        was 361: every 4:4:4:4 texture accepted
TexFormatCount=3                 565, 1555 and 4444 enumerated (probe)
D3dTextureCreates=344
I9xxDrawsSubmitted=1118721       I9xxDrawsRefused=0
D3dTextureRefusedSysmem=1118317  still: of 1118721 draws
D3dTextureRefusedCaps=0x04001800 ALLOCONLOAD | TEXTURE | SYSTEMMEMORY
D3dTextureRefusedVidMem=0x005B0DE8   a user-space address, so truly system memory
I9xxTextureDraws=362             the probe's
I9xxDepthSkipped=663556          of 1118721: a Z comparison other than LESS
```

### Why the textures are in system memory

Final Reality's device textures are `DDSCAPS_ALLOCONLOAD`: the runtime
gives them memory at `Load`, and places them where the device caps say it
can texture from. The Intel `dwDevCaps` word claimed neither
`TEXTUREVIDEOMEMORY` nor `TEXTURESYSTEMMEMORY` - the probe read it as
`0x2451` in intel52, 56 and 59 - so the runtime had no permission to use
video memory and chose system memory, which the bind refuses. The probe's
own texture asked for `DDSCAPS_VIDEOMEMORY` explicitly and drew. The ViRGE
claims `TEXTUREVIDEOMEMORY` and the same game textures there.

This is a reading of the runtime's behaviour from three consistent
captures and the ViRGE control, not a trace of its code. The bit is added.
Two counters are added with it so the next capture can confirm the
mechanism rather than the outcome: `D3dTextureCreateSysmem`, the number of
TextureCreate calls whose surface already carried `SYSTEMMEMORY`, and the
last create's caps. Expected next boot: `D3dTextureRefusedSysmem` near
zero, `I9xxTextureDraws` most of the draws, and `D3dTextureCreateSysmem`
small - the game's own source copies, if it keeps any.

### intel60: textured

Build `aca5d25-dirty`, TEXTUREVIDEOMEMORY claimed. The operator reports
Final Reality textured correctly. The event capture shows the ring
advancing (`RingTail=00005E50`) across two mode switches and a clean
disable. **The counters were not collected**: `V9XSNAP.INI` and `V9XDD.INI`
in `intel60` are the intel59 files (19:42 and 19:44), so
`D3dTextureCreateSysmem` and `I9xxTextureDraws` for this boot are not on
record. The visual is the evidence; the mechanism reading stands
unconfirmed by counter until a snapshot is taken on a textured boot.

## The flip: two boots, one package

The Intel flip path exists from 2026-09-17 and runs only on a boot that
carries `IntelFlip=1` in `C:\V9XDIAG\INTELARM.TXT`, written by `V9X3D
FLIP`. `V9X3D ON` and `OFF` leave it clear. It has NOT been run.

**Boot 1, `V9X3D ON` (read-only).** The scanout watch is compiled back in,
reading only a pipe whose PIPECONF enable bit is set (`4f80185` records
why). After the first draw it samples the live pipe's display line and
frame counter 4096 times. Take a snapshot. Expected: `ScanBLineChanges` in
the thousands, `ScanBLineMax` near 623, `ScanBFrames` at least 1,
`ScanA*` zero with `ScanSamples=4096`. A hang here says reading pipe B's
line register is itself the problem and the flip work stops. A flat pipe B
says the registers are not what `intel_gma.h` claims on this part; stop.

**Boot 2, `V9X3D FLIP` (the write).** With the bit stamped, the 16-bit side
claims `V9X_DD_ENGINE_CAP_FLIP`, and the HAL's `v9x_set_display_start` and
`v9x_in_vblank` dispatch to `engines\i9xx_scanout.c`:

- The live pipe is the one pipe with PIPECONF bit 31 set, and the plane
  is the one plane whose DSPCNTR is enabled and whose pipe-select bits
  (25:24) name that pipe - a plane can drive either pipe on Gen3, so the
  letter is not the routing. Anything but exactly one of each declines the
  flip. Flip writes that plane's base (`DSPBADDR 0x71184` here) with the
  framebuffer byte offset and reads it back to post, as i915's gen3 path
  does. Intel60's capture read the plane base as 0 with the desktop at
  offset 0, which is why a framebuffer offset is taken as the graphics
  address without translation. If that is wrong the picture moves to the
  wrong place, not to nowhere: the aperture is 256 MB and every offset a
  surface can have is inside it.
- Vertical blank is `DSL >= vactive`, with vactive from VTOTAL bits 11:0
  plus one (intel56: 576 on pipe B). The existing flip state machine is
  unchanged; only its two primitives moved.
- `FlipToGDISurface` writes offset 0 through the same path.

Expected: the flicker gone, `CountFlip` still in the thousands but the
`Flip` events in the ring returning `HANDLED`. If the display goes dark or
shifts: power off, DOS, `V9X3D ON`, collect. The register write is the
unmeasured claim; everything else in the path ran on the ViRGE.

### intel61: boot 1 done - textures confirmed by counter, pipe B's line register moves

Build `9d2b6d9-dirty`, `V9X3D ON` (IntelFlip absent, `EngineCaps=0x10`, so
the flicker this boot is the CPU copy and expected). Snapshot taken.

```
I9xxDrawsSubmitted=239970    I9xxTextureDraws=239970   every draw textured
D3dTextureRefusedSysmem=0    D3dTextureCreateSysmem=0  no texture in system memory
I9xxDepthDraws=239970        I9xxDepthSkipped=0        every draw depth-tested, LESS
ScanSamples=4096
ScanBLineMin=293  ScanBLineMax=429  ScanBLineChanges=136  ScanBFrames=0
ScanALineMin=0    ScanALineMax=0    ScanALineChanges=0    (pipe A not read: off)
```

**Textures.** The TEXTUREVIDEOMEMORY reading is confirmed by the counters
it was added for: no TextureCreate arrived in system memory and no draw
was refused for one. intel59's 1,118,317 refusals are gone.

**Depth.** Nothing skipped this boot. intel59's 663,556 skips are not
explained by this capture and the open item below stays open with less
weight; `I9xxDepthLastFunc` read 0 because nothing was recorded.

**The vblank source.** Pipe B's display line went from 293 to 429 across
4096 samples with 136 changes: one change per line, a monotonic sweep of
136 lines in the few milliseconds the watch ran. The frame counter did not
tick because the window never reached the blank (576 and above); the flip
path tests `DSL >= vactive`, which is the register that moved. Reading
these registers on the live pipe did not hang, so intel57/58's hang was
the powered-down pipe A, as suspected.

Boot 2 is now licensed: `V9X3D FLIP`.

### intel62 and intel63: the flip runs; the sampler is the wrong shape; 3DMark99 draws bare

Build `9d2b6d9-dirty`, `V9X3D FLIP` (`IntelFlip=1`, `EngineCaps=0x14`).
intel62 is Final Reality; intel63 is 3DMark99 run afterwards in the same
boot, so its counters include intel62's.

**The flip.** The operator reports the flicker "greatly improved but not
perfect". The plane-base write and the line-register vblank test ran for
the first time on this part and the display stayed up; `ScanBLineMin=0`,
`ScanBLineMax=671`, a full sweep. Final Reality made 591 Flips against
51,865 GetFlipStatus polls. What was not captured is how many Flips were
HANDLED against declined, and the residual tearing is not placed. Five
counters are added for the next boot: `FlipHandled`, `FlipStillDrawing`,
`FlipDeclined`, `FlipForcedIdle`, `ScanoutUnresolved`.

**A pending flip that never completed.** intel63's ring holds 18 Flips in
a row returning WASSTILLDRAWING and `CountFlip` rose from 591 to 54,688
across 3DMark's run: the flip state machine was waiting on a retrace that
never came, most likely armed under one mode and polled under another
after 3DMark's mode switch. Handled by name: DriverInit resets the flip
state on every mode change, and a flip is armed and kept pending only
while the vblank source can see a scanout (one live pipe and plane on the
Intel path; always on the VGA port). What remains is a source that answers
wrongly, and for that a last-resort bound of a million polls, large
because a poll count is no promise of elapsed time and a small one would
release the visible buffer early and put the flicker back.

Either way a pending flip is ABANDONED, not completed: the state goes
idle, `FlipForcedIdle` counts it, and from then until the next mode change
every Flip is DECLINED (`NOTHANDLED`, counted in `FlipDeclined`), so
DirectDraw presents by its own copy and no application is told a flip
completed against a retrace nobody saw. GetFlipStatus answers "nothing
pending", which is then true. Recovery is not presentation at the
interface, not only in a counter.

**The textures, from the photograph.** Sky and terrain smeared into
horizontal bands; the floor aliased toward the horizon; the robots right.
That is a tiled texture under clamp-to-edge and a nearest filter at high
minification - the two sampler states the Intel path published and
emitted, while the game asked for WRAP and LINEAR through render states
the core already recorded and the Intel bind ignored. SAMPLER_STATE now
carries `TEXCOORDMODE_WRAP` and bilinear MIN/MAG from the bound map, the
decoder checks the declared words, and the caps publish WRAP and LINEAR
beside CLAMP and NEAREST. Constants from the texture-packet audit's SS2/SS3
tables and i915_reg.h. **Unmeasured** until the next boot; the photograph
is the prediction's test.

**3DMark99 drew no textures, and asked for none.** Across its run:
`CreateSurface` +7, `D3dTextureCreate` +0, `D3dContextCreate` +2, draws
+627, every one untextured, nothing refused. The driver saw no texture
request to refuse. 3DMark decided from the caps not to use them, or ran a
test that needs none; which, this capture cannot say. What the Intel caps
lack against the ViRGE, where 3DMark textures: every blend cap
(`dwSrcBlendCaps`, `dwDestBlendCaps`, `dwAlphaCmpCaps`),
`D3DPTEXTURECAPS_ALPHA` and `TRANSPARENCY`, `DECAL`, and the mip filters.
Blending is measured on the armed scenes
(`2026-09-16-intel-gen3-alpha-test-and-blend-measured.md`) and not built
into the runtime state block; that is the next candidate, and it is a
runtime-state change with an audit behind it rather than a guess.

### Blending on the runtime path (2026-09-18, unmeasured)

The blend scene measured the packet in intel47 to the exact product:
`BLENDFUNC_ADD`, `SRC_ALPHA`, `INV_SRC_ALPHA`, with the IAB disable ahead
of the state load. The runtime builder now carries the same two dwords
when the application enables blending. The source and destination factors
are two S6 fields and are carried as two codes from the four the audit
sources - ZERO, ONE, SRC_ALPHA, INV_SRC_ALPHA - so every pairing the caps
promise is built as itself: the capability definitions make the two sides
independently selectable, and a first cut that honoured only
SRC_ALPHA/INV_SRC_ALPHA drew SRCALPHA/ZERO and ONE/INVSRCALPHA opaque
while advertising them. Only SRC_ALPHA/INV_SRC_ALPHA is measured (intel47);
the other pairings are the same fields with other codes. ONE/ZERO is opaque
by arithmetic and is passed as off, because it is Direct3D's default with
the enable set. A factor outside the four (the destination-alpha codes the
audit excludes) draws opaque and is counted in `D3dBlendSkipped` with the
pair in `D3dBlendLastPair`. The decoder requires the declared codes and
the IAB dword, and refuses either without the other.

**MODULATE's alpha.** The one textured program multiplied all four
channels, which is `D3DTBLEND_MODULATEALPHA`; legacy `MODULATE` takes the
alpha from the texture when the format has one and from the vertex when it
does not, so a half-alpha texel over a half-alpha vertex blended at a
quarter where it should blend at a half - invisible until blending was on.
Two more programs: the modulate program's four instructions, a MUL masked
to xyz, and a W-only MOV from the texel (1555, 4444) or from the vertex
colour (565). The HAL picks by `TEXTUREMAPBLEND` and the map format; the
decoder pins a runtime stream to the declared program's length. Caps
publish MODULATE and MODULATEALPHA, and `D3DPTEXTURECAPS_ALPHA`.

What the next boot should show: 3DMark99 creating textures
(`D3dTextureCreate` above zero) if the blend caps were what it wanted, and
Final Reality's transparent surfaces blended. `D3dBlendSkipped` names any
pair this engine does not have. Destination-alpha factors stay excluded:
the audit records that neither reference tree says what a 565 target's
alpha reads as.

### Open: the depth test is skipped on most draws

The Intel S6 state carries one comparison and this build emits `LESS`;
the game asks for another on 663,556 of 1,118,721 draws and gets an
un-Z'd draw. `I9xxDepthLastFunc` now records which. `LESSEQUAL` is the
usual suspect for a DirectX 5 title, and it is a one-field change in the
state block once the value is in a capture.

### Probe

The probe completed (`ResultFiles=2`, `Result=COMPLETE` in the second
file). The first file still read `INCOMPLETE`: the verdict followed the
rollover. Fixed the same day; Result is now written to the first file.
