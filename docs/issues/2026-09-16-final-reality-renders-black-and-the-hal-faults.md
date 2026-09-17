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

### Probe

The probe completed (`ResultFiles=2`, `Result=COMPLETE` in the second
file). The first file still read `INCOMPLETE`: the verdict followed the
rollover. Fixed the same day; Result is now written to the first file.
