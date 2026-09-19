# intel94: the run draws untextured, and nothing was refused

2026-09-20, MICHAEL-NETBOOK (945GSE), build `5d9fbf6-dirty`. Reported
symptom: textures missing from the picture, and the application very slow to
open. Attached: `C:\temp\intel94`.

> **Corrected 2026-09-20.** This was filed as a 3DMark99 capture because
> that is how it was handed over. `intel94\V9XSNAP.INI` is byte-identical to
> `intel95\FinalReality\V9XSNAP.INI` (md5 `c7c82ff...`), so the same file is
> now filed under Final Reality. One of the two labels is wrong and the
> capture does not say which: nothing in a snapshot names the application.
>
> Every measurement below is unaffected - the counters are the driver's, not
> the application's - but the title and the references to 3DMark99 were an
> assertion the evidence never supported, so they are removed rather than
> swapped for the other guess.

## The measurement

```
D3dTextureCreates=468        D3dTextureDestroys=468
I9xxDrawsSubmitted=252084    I9xxTextureDraws=8868
I9xxDrawsRefused=7           I9xxDepthDraws=252084
```

**Three and a half per cent of draws sampled a texture**, so 243,216 of
those draws are the missing textures, one triangle at a time. The draws are
reaching the ring - the
frame is being built, the geometry is there, the depth test runs on every
one of them. What is not there is the map.

## Every refusal counter is zero

```
D3dTextureRefusedFormat=0    D3dTextureRefusedShape=0
D3dTextureRefusedSysmem=0    D3dTextureRefusedNoCap=0
D3dTextureRefusedBounds=0    D3dTextureRefusedOther=0
D3dTextureRefusedCaps=0x00000000
```

This is the whole finding. `v9x_d3d_i9xx_bind_texture` counts every way it
can decline a surface - format, shape, system memory, missing capability,
and a footprint that does not fit the aperture - and none of them fired,
while 468 textures were created and 468 destroyed without a single create
being refused either.

A surface that is never refused and never sampled is a surface the engine
was never offered. `bind_texture` returns zero before any of those checks
when `v9x_d3d_context_texture_surface` gives it nothing, and that function
had no counter at all.

It has two ways to return nothing, and they are different defects:

- the context's `texture_handle` is zero, so nothing was ever bound;
- the handle is non-zero but names no live texture of this context, so a
  binding the application made was lost.

A third candidate sits upstream of both. `V9xD3dRenderState` applies the
whole state block or none of it: unless the context AND the execute buffer
both resolve and the count is within the loop's 64, it returns handled
having applied nothing. A texture handle inside a block dropped that way is
a binding the application believes it made and the driver never saw. That
path had no counter either.

Two supporting readings point the same way rather than at the engine:

```
D3dTextureGetSurfs=0         D3dTextureSwaps=0
D3dRenderStateCalls=4071     D3dRenderPrimitiveCalls=246481
```

Not one `GetSurf` and not one swap across 468 textures.

## What was done

Five counters, appended to `V9X_D3D_DIAGNOSTICS` (stamp 2026092001) and in
the shared core rather than the Intel backend, because the same question
will be asked of the ViRGE:

| key | what it answers |
|---|---|
| `DrawsNoHandle` | the context carried no handle at the draw |
| `DrawsHandleUnresolved` | a handle that named no live texture |
| `RenderStateDropped` | a state block applied nothing at all |
| `TextureHandleSets` | how often TEXTUREHANDLE was seen |
| `TextureHandleLast` | what it was last set to |

`DrawsNoHandle` and `DrawsHandleUnresolved` partition the 243,216 exactly,
so the next capture names the defect rather than narrowing it again.

**Nothing is fixed.** This adds the instrument the last capture needed and
no more. Which of the three it is has not been measured, and guessing
between them is how the flicker investigation spent two days.

## The slow start is not explained

`CountCanCreateSurface=941` against `CountCreateSurface=941`, and
`CountDestroySurface=5130` against 941 creates. The destroy count exceeding
the create count by more than five to one is not accounted for by anything
here and may not be related to the start-up time at all. Nothing in this
capture measures elapsed time, so the slowness has no evidence in it yet.

## Not from this build

The capture was taken on `5d9fbf6-dirty`, which predates both the DSPARB
correction and the watermark programming: `Wm0Want` and `Wm1Want` read zero
for the reason intel93 gives. It carries no information about either.
