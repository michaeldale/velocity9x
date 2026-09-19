# intel95: the textures are there, and the filter claim is not explained

2026-09-20, MICHAEL-NETBOOK (945GSE), build `36d0d7f-dirty`. Two captures,
`3dmark99\` and `FinalReality\`. Reported: 3DMark99 says bilinear filtering
is not supported.

## The intel94 question is answered, and the answer is a clean partition

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

Every draw that sampled nothing is a draw where the application had bound
nothing, to the last one. `DrawsHandleUnresolved=0`: not a single binding
was lost. **97.1 per cent of draws sampled a texture.**

So the intel94 hypothesis list is settled without a fix being needed: it was
neither a lost handle nor a dropped state block. Six state blocks were
dropped across the whole run, against 391,028 texture-handle sets - real,
and far too few to matter to the picture.

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
| `DrawsMagLinear` | draws that actually sampled bilinear |
| `DriverInfoCalls` | how often GetDriverInfo was called |
| `DriverInfoDeclined` | how many of those were turned away |
| `DriverInfoLast` | the first four bytes of the last GUID asked for |

`FilterMagSeen` carrying no LINEAR bit means the application never asked,
which points at the capability path. Carrying one while `DrawsMagLinear` is
zero means it asked and the driver dropped it, which points at the sampler.
The two cannot both be true, so one run separates them.

`DrawsMagLinear` is counted at the bind and not at the render state, so it
reports what was sampled with rather than what was requested.

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
