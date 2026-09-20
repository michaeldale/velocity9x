# 3DMark99 never sets a filter, and the driver point-samples

> **Corrected 2026-09-20.** The claim that draws "sampled bilinear" is
> withdrawn. `DrawsMagLinear` and `DrawsMinLinear` are incremented where the
> driver submits a batch whose sampler state names a linear filter. That
> establishes the state this driver sent; it does not establish which filter
> produced any pixel, which nothing in this project has measured. Read them
> as "textured submissions carrying linear filter state".
>
> Two things this document treats as outstanding were fixed after it was
> written and should not be read as open: the missing
> `DDHALINFO_GETDRIVERINFOSET` advertisement, and `V9xD3dRenderState`
> discarding blocks longer than 64 states.


2026-09-20, 86Box `Win86SE` guest (ViRGE/DX, Win98SE, DirectX 4.06.03.0518 -
6.1), boot 613, build `facf2f5`. 3DMark 99 Max Pro ran the full suite to
completion: **353 3DMarks, 1780 CPU 3DMarks**, no crash and no hang.
Attached: `2026-09-20-virge-3dmark99-V9XSNA4.txt`.

## What the application is told

System Info names the driver's own tab and lists, from
`dpcTriCaps.dwTextureFilterCaps` one line per bit:

```
Point Sampling                      NEAREST
Point Sampling With Mip-Mapping     MIPNEAREST
Bilinear Filtering                  LINEAR
Bilinear Filtering With Mip-Mapping LINEARMIPNEAREST
Trilinear Filtering                 LINEARMIPLINEAR
```

The ViRGE publishes all five. So 3DMark99's feature list is a direct readout
of that field, which settles how the report is produced.

## And what it then does

```
D3dRenderPrimitiveCalls=24033   D3dRenderStateCalls=0   D3dExecuteCalls=0
FilterMagSeen=0x00000000        FilterMinSeen=0x00000000
DrawsMagLinear=0                DrawsMinLinear=0
```

**Not one render state reached this driver.** Twenty-four thousand primitive
calls and zero `V9xD3dRenderState` calls, so `TEXTUREMAG` and `TEXTUREMIN`
were never applied and every context kept the `V9X_D3DFILTER_NEAREST` its
creation sets.

The driver told 3DMark99 it does bilinear and trilinear filtering, and then
point-sampled the entire benchmark.

That is a real defect and it is the driver's, not the application's. Whatever
path 3DMark99 uses to change state - it is neither the RenderState callback
nor execute buffers, both of which read zero - this driver does not see it,
so the filter, the blend, the address mode and everything else the
application asks for are silently discarded and the defaults stand.

## This corrects what was said after intel96

intel96 measured `FilterMagSeen=0x4` and 7,631,631 textured submissions
carrying linear filter state, and that was reported here as "the driver
filters bilinear throughout, the complaint is about a capability list" - an
overstatement twice over, since a submission is not a fetched texel. On this
guest the same benchmark sets no filter at all.

Both measurements are sound; they cannot both describe 3DMark99. The
difference reinforces the open question about which application produced
intel96's capture - it made 3,399 render-state calls and 405 texture
creates, against zero and 22,246 here.

So the earlier conclusion is withdrawn. What is established is that **on the
ViRGE, 3DMark99's states do not reach the driver**. Whether the netbook
behaves the same is unmeasured, and the capture that appeared to say
otherwise is of uncertain provenance.

## The state-block clamp is still untested, for a new reason

`StateMaxCount=0`, `StateClamped=0`, `StateExeBytesLast=0`,
`RenderStateDropped=0` - not because the clamp does not work, but because
nothing walked that path at all. The 276-state block intel96 recorded came
from whatever produced that capture, and it was not this.

## An instrument of mine is wrong

```
UptimeDriverInit=415339   UptimeFirstD3d=39723   UptimeFirstFlip=41922
```

Driver init reads *later* than the first Direct3D context, which is
impossible as a first-event bracket. `uptime_driver_init` is written on every
`DriverInit` and the benchmark's mode change ran one, so the field holds the
LAST init and not the first. It cannot bracket a start-up as written. The
other two are set once and are sound.

Also `D3dPidDistinct=2`: the probe ran earlier in this boot, so these
counters mix two applications. It does not weaken the finding - zero render
states across both means 3DMark99 made none - but no per-application total
here is 3DMark99's alone.

## Standing

The ViRGE advertises four filters it cannot honour (`D3dMipChainLevels=0` on
every capture, and the bind drops the mip part of every `MIP*` value), and
now also advertises two it never gets asked for. The Intel path publishes
`NEAREST | LINEAR` only, so its list loses the three mip and trilinear
entries - which is a likelier reading of "bilinear filtering is not
supported" on the netbook than the literal one, and is answerable by
photographing that same dialog there.
