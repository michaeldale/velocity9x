# Final Reality renders black, and the HAL faults on teardown

Date: 2026-09-16
Machine: MICHAEL-NETBOOK, Intel 945GSE, `8086:27AE` revision 03
Build: `f9ec875`
Capture: `C:\temp\intel53`
Status: open. The faulting INSTRUCTION is identified; the CALLER is not.
Guarded and instrumented, not yet re-run.

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
