# Intel Gen3: an application drew a triangle, and the GPU executed it

Date: 2026-09-16
Machine: MICHAEL-NETBOOK, Intel 945GSE, `8086:27AE` revision 03
Build: `ef4eae8`
Capture: `C:\temp\intel52`

The first Direct3D draw on this hardware that was not built from a generated
table. An application called `DrawPrimitive`, the HAL built a stream from its
geometry, the ring executed it, and the pixel came back the colour the
application asked for.

Authorised by the sustained-3D amendment to
`2026-09-15-intel-phase5-errata-gate.md`. **Unarmed boot**: no one-shot token,
`IntelEnableThisBoot=0`, permission carried by `IntelRuntime3D=1` alone.

## What was measured

The instrument is `V9XDDP.EXE`, which was already in the package: it enumerates
Direct3D devices, creates a target, and draws one untextured `TLVERTEX`
triangle with `rhw = 1.0f`, `sz = 0.0f` and a flat red vertex colour, then
reads the pixel at (16, 16) back off the surface.

### The device enumerated

```
D3DHalFound=1
D3DDeviceCount=4
D3DDevice2Description=Microsoft Direct3D Hardware acceleration through Direct3D HAL
D3DDevice2HwFlags=195          (0xC3: COLORMODEL|DEVCAPS|TRICAPS|DEVICERENDERBITDEPTH)
D3DDevice2HwDevCaps=9297       (0x2451: our 0x451, plus 0x2000 the runtime adds)
D3DDevice2HwTriRaster=32       (SUBPIXEL)
D3DDevice2HwTriShade=10        (COLORFLATRGB|COLORGOURAUDRGB)
D3DDevice2HwRenderDepth=1024   (DDBD_16)
D3DDevice2HwTriTexture=0
D3DDevice2HwZDepth=0
D3DMainIsHardware=1
```

Every published field came back exactly as `v9x_d3d_i9xx_describe_caps` wrote
it. The runtime added `0x2000` to the device caps, which is its own hardware
marker rather than anything this driver claimed.

**This kills a hypothesis I had stated twice.** I expected a device with zero
texture formats might not enumerate at all, and said so. It enumerates, and an
application can create a device on it.

### The draw succeeded

```
D3DTargetHr=0x00000000
D3DBeginSceneHr=0x00000000
D3DDrawPrimitiveHr=0x00000000
D3DEndSceneHr=0x00000000
D3DTrianglePixelRaw=63488
D3DTrianglePixelOk=1
```

`63488` is `0xF800`: pure red in 5:6:5, which is the vertex colour
`0xFFFF0000` converted to the target format. The probe's own comparison agrees.

### The GPU consumed the commands

From the event capture, across the boot:

```
boot-enable   RingStart=00000000  RingCtl=00000000  RingTail=00000000  RingHead=00000000
mode-switch   RingStart=006B0000  RingCtl=0000F001  RingTail=00000000  RingHead=00000000
mode-switch   RingStart=006B0000  RingCtl=0000F001  RingTail=00005610  RingHead=00205610
disable       RingStart=006B0000  RingCtl=0000F001  RingTail=00005610  RingHead=00205610
```

`RING_HEAD` carries a wrap count in its high bits; its address field is masked
with `0x001ffffc`, and `0x00205610 & 0x001ffffc` is `0x00005610` — **exactly
`RING_TAIL`**. The head reached the tail. 0x5610 is 22,032 bytes, so 5,508
dwords of commands were fetched and retired.

This is the fact that had never been true before. In intel48 through intel51
`RING_TAIL` read zero at every event.

### Nothing went wrong on the way

```
EngineFifoTimeouts=0
EngineIdleTimeouts=0
EngineResets=0
D3dContextCreates=9      D3dContextDestroys=9      D3dContextRejects=0
D3dRenderPrimitiveCalls=404
DriverApertureReads=00000015
```

No bounded wait expired, no reset was attempted, no context was refused. The
machine did not hang and shut down normally.

## What did not work, and why each is expected

- **Textures.** `Tex4444PixelOk=0`. `dwNumTextureFormats` is zero and
  `texture_format` refuses every surface, so a textured draw has nowhere to
  sample from. `D3dTextureCreates=41` says DirectDraw made texture surfaces
  anyway; `D3dTextureRefusedFormat=0` says none of them reached the engine's
  refusal path, because the engine was never asked.
- **Depth.** `D3DZCompareOk=0`, `D3DZWriteMaskOk=0`. `D3DZRejectRaw=63488` —
  the triangle that should have been rejected by the depth test drew red, which
  is what no depth test looks like. The runtime state block binds no depth
  buffer and `dwDeviceZBufferBitDepth` is zero.

  Worth recording: `D3DZSurfaceHr=0x00000000` and `D3dDepthOffered=6`,
  `D3dDepthAccepted=6`. DirectDraw created a Z surface and the core accepted
  it **despite this engine publishing no Z capability at all**. An application
  can therefore get a Z buffer it will find has no effect, which is a promise
  nobody made and nothing refuses.
- **Specular.** `D3DSpecularGouraudOk=0`. `D3DPSHADECAPS_SPECULARGOURAUDRGB`
  is not claimed and the fragment program emits diffuse only.

## What is NOT established

- **How many of the 404 `RenderPrimitive` calls actually submitted.** 5,508
  dwords across 404 calls is roughly 13 dwords per call, and one draw costs
  around 68, so the large majority produced no submission. Whether those were
  refused by `draw_triangles`, and by which of its eight refusal paths, is not
  recorded anywhere. `V9X_TRACE_D3D_PRIMREJECT` is pushed into the ring buffer
  on refusal but the trace was not correlated here.
- **Gouraud interpolation.** `COLORGOURAUDRGB` is claimed and the only
  measurement is a flat-coloured triangle, where flat and Gouraud are
  indistinguishable. Nothing has drawn a triangle with three different vertex
  colours through the runtime path.
- **Any rhw other than 1.0f.** The builder now accepts them
  (`cea8c33`) and no vertex carrying one has reached this silicon. Whether Gen3
  divides X and Y by the fourth component remains unmeasured; the
  `_3DSTATE_DRAWRECT` scissor is what bounds writes to the target either way,
  and that is measured.
- **Sustained load.** One probe run and part of one benchmark. Erratum 7
  concerns *extended* 3D operation and nothing here approaches it.

## What this changes

The engine works. Everything after this is about widening what it will accept
rather than about whether the path exists, and the two things an application
of this era needs next are both **measured working on this silicon in the
scene table and unbuilt in the runtime path**: texture sampling (intel45) and
the depth test (intel46).

The cost of each is not the packets, which are known correct. It is getting
application texel data somewhere the GPU can read it, and that is a separate
decision: the CPU's only route into stolen memory is the GMADR aperture, and
bulk CPU writes there immediately before a GPU read is erratum 12's own
description — the sequence the Phase 5 decision removed as a condition of the
gate opening.
