# intel95: the textures are there, and the filter claim is not explained

2026-09-20, MICHAEL-NETBOOK (945GSE), build `36d0d7f-dirty`. Two captures,
`3dmark99\` and `FinalReality\`. Reported: 3DMark99 says bilinear filtering
is not supported.

## The stored handles give a clean partition, not an explanation

```
I9xxDrawsSubmitted=8458692   I9xxTextureDraws=8209582   I9xxDrawsRefused=70
DrawsNoHandle=249180         DrawsHandleUnresolved=0    RenderStateDropped=6
TextureHandleSets=391028     TextureHandleLast=0x00000000
```

The counters close exactly:

```
8458692 - 8209582 = 249110 untextured draws
249110 + 70 refused = 249180 = DrawsNoHandle
```

**97.1 per cent of successful submissions were textured.** The remaining
counts match the draws for which the driver's stored texture handle was
zero. `DrawsHandleUnresolved=0` says no nonzero handle lookup failed; it
does not say every binding the application requested was applied.

A dropped TEXTUREHANDLE block can leave that stored handle at zero and
produce this same partition. Six dropped blocks can affect many later
draws because state persists until changed. Their effect is unmeasured.
The intel94 hypotheses remain open: this is a different workload whose
application attribution is also uncertain (see the before-and-after record).

## Which leaves intel94 itself unexplained

intel94 measured 3.5 per cent textured on the same code path. It is not the
same run: 468 texture creates against 64,761 here, and 252,084 draws against
8,458,692. That is a fraction of a benchmark, so the likeliest reading is
that intel94 captured a startup or menu phase rather than the tests.

That is a guess and it is recorded as one. The counters that would have
settled it did not exist when intel94 was taken, and they exist now, so a
repeat of whatever intel94 actually was will say.

## The filter claim has no measurement behind it either way

The driver publishes the capability:

```c
shared->d3d_global.hwCaps.dwFlags |= V9X_D3DDD_TRICAPS;
shared->d3d_global.hwCaps.dpcTriCaps.dwTextureFilterCaps =
    V9X_D3DPTFILTERCAPS_NEAREST | V9X_D3DPTFILTERCAPS_LINEAR;
```

and the sampler honours it: `bind_texture` sets `map.mag_linear` and
`map.min_linear` from the application's TEXTUREMAG and TEXTUREMIN states.
So the reported claim and the published capability disagree, and nothing in
the capture says which is describing the guest.

Two candidates, neither measured:

- **The application never asks.** If 3DMark99 concludes bilinear is
  unavailable from something else it read, it will set NEAREST throughout
  and the driver will faithfully deliver nearest. The picture would then be
  correct-but-aliased, not wrong.
- **The capability it reads is one the driver does not fill.**
  `dpcLineCaps` is zero but for its `dwSize`, and `D3DDD_LINECAPS` is not in
  `dwFlags`. `V9xHalGetDriverInfo` answers `GUID_D3DCallbacks2` alone and
  declines every other GUID with `0x88760028`, including
  `GUID_D3DExtendedCaps`, from which a DirectX 6 application's device
  description is partly built.

The second is a plausible mechanism and that is all it is. No capture has
ever recorded which GUIDs the runtime asks this driver for.

## What was done

Six counters, ABI stamp 2026092002, appended:

| key | what it answers |
|---|---|
| `FilterMagSeen` | one bit per D3DFILTER value set for magnification |
| `FilterMinSeen` | the same for minification |
| `DrawsMagLinear` | successful textured submissions with magnification set to linear |
| `DrawsMinLinear` | successful textured submissions with minification set to linear (2026092005) |
| `DriverInfoCalls` | how often GetDriverInfo was called |
| `DriverInfoDeclined` | how many of those were turned away |
| `DriverInfoLast` | the first four bytes of the last GUID asked for |

`FilterMagSeen` and `FilterMinSeen` record states the driver applied, not
requests hidden inside rejected blocks. A LINEAR bit with no corresponding
submission can also mean no textured draw followed that state, or that the
draw was refused. These counters alone cannot isolate a capability or
sampler defect.

From ABI 2026092005, both linear counters increment alongside successful
textured submissions, after validation and submission. The earlier MAG
counter incremented at bind time and included later-refused draws. Neither
counter proves which filter the GPU used for any particular pixel.

The latest rejected state block now records `StateDropReason`,
`StateDropContext`, `StateDropCount`, `StateDropOffset` and the retained
`StateDropHandle`. Reasons are 1=context, 2=surface, 3=global surface,
4=memory pointer and 5=count over 64. The retained handle is the driver's
state, not the rejected request. No rejected block is dereferenced to
collect this diagnostic, and earlier rejections can be overwritten.

**Nothing is fixed.** The `dpcLineCaps` and `GUID_D3DExtendedCaps` gaps are
real and are candidates, and filling either before knowing which one the
application reads would be a change made on a hunch.

## The slow start now has a number

```
CountCanCreateSurface=12569   CountCreateSurface=12569
CountDestroySurface=16911     CountD3dTextureCreate=64761
```

64,761 textures created and destroyed in one run, and 12,569 surface
creations. Whether that is 3DMark99's normal behaviour or a churn this
driver provokes is not established, and nothing here measures elapsed time,
so the slowness still has no direct evidence.
