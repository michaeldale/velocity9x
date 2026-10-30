# Signed bump formats on the HD 4600: 3DMark 2001 SE's Nature runs

Date: 2026-10-30. Step 5d of
[`../plans/haswell-ddi8-pixel-shaders.md`](../plans/haswell-ddi8-pixel-shaders.md).
Machine: the Dell i5-4570 (HD 4600), boots 133-134. Evidence:
[`../probe/hsw-4570-ps11-step5d-2026-10-30/`](../probe/hsw-4570-ps11-step5d-2026-10-30/).

## What 3DMark asks for

Step 5c's cubes moved 3DMark from "Nature unsupported" to a stop at
start: `P_D3D::DRV_allocateMap - device does not support bump normal
maps`. The message is in its Direct3D 8 renderer,
`e2driver\e2_d3d8_driver_mfc.dll`. Disassembled where it is used:

- map allocation switches on the map's bump type and reads one of three
  flags of the display mode;
- the flags are `P_DisplayMode::setBumpSupport(a, b, c)`'s, set from
  `IDirect3D8::CheckDeviceFormat` on texture formats in pairs:
  - a, environment bump: V8U8 (60) or V16U16 (64), with a device flag;
  - b, the same with luminance: a and a second device flag;
  - c, "bump normal maps": Q8W8V8U8 (63) or W11V11U10 (65).

No signed format was published, so c was false. Nothing of 3DMark's is
in the repository; the binaries stay off it.

## The change

- **The DDI 8 format list adds Q8W8V8U8 and V8U8** as textures (and so as
  cubes) where pixel shaders run.
- **The sampler reads them as R8G8B8A8_SNORM and R8G8_SNORM.** Bytes run
  U V W Q from the lowest, which is red green blue alpha.
  `v9x_d3d_hsw_signed_format` (`d3d_hsw_map.c`, host-tested first: it
  failed against a stub, then passed) knows them:
  - by D3DFORMAT, under DDPF_D3DFORMAT or DDPF_FOURCC;
  - V8U8 also by DirectX 7's DDPF_BUMPDUDV.

  The fixed-function draw refuses both.
- **The runtime describes Q8W8V8U8 as DDPF_FOURCC, FourCC 3Fh, bit count
  0** (boot 133, `surf-pf`). DirectDraw cannot size that. On a
  surface it placed, V9XD32's new T19 Normal read every channel as U
  (1,028 of 1,040 pixels wrong). So the engine places every non-RGB
  texture itself, one level or a chain, as a tree at its texel size.
  Boot 134: placed at pitch 64, and T19 Normal passes.

## T19 Normal (boot 134)

An 8x8 managed Q8W8V8U8 texture, every channel signed and through both
signs, drawn by `tex t0; mad r0, t0, c0, c0`: 1,040 pixels judged, no
mismatches, max error 4 at R5G6B5 against tolerance 9. T12-T16 and the
other seven T19 programs passed. Not run at 32 bpp.

## 3DMark 2001 SE, Game 4 Nature and the Pixel Shader test (boot 134)

The run completed to its score window, twice:

| Test | Result |
|---|---|
| Game 4 - Nature | 11.1 fps |
| Pixel Shader | 59.8 fps |
| Advanced Pixel Shader | 35.3 fps |

The driver's counters after it:

- 11 PS 1.1 kernels built, none refused;
- 1,231 shader triangles dropped at the guard band;
- 2 shader draws fogged.

The log, until its 6,000-line cap:

- 11 cubes made and 10 placed;
- 18 three-level signed textures made and 13 placed;
- 5,491 shader draws sampling a cube.

The declines are the heap's: mip trees were declined 2,045 times in
the run, the last for reason 4, no room. A texture DirectDraw then places
is sampled at level 0, and a signed one at the wrong pitch.

## Not done, not checked

- **What Nature looks like.** No frame was seen; the agent's screenshots
  miss fullscreen flips. Step 6 has Michael look at 3DMark's image
  quality frame.
- **The 1,231 dropped triangles** are pieces of Nature not drawn. The
  core's clipper knows only the 32-byte vertex.
- **Heap exhaustion.** With 32 MB, Nature's textures do not all get a
  tree. The engine's signed and cube placements fail like any other.
  What the runtime then does with a declined signed texture or cube was
  not looked at.
- **V8U8's blue and alpha.** The sampler gives 0 and 1 for the missing
  channels. Direct3D's convention was not checked; Nature reads only
  du dv from it, if it uses it at all.
- **Environment bump flags a and b** depend on device flags this driver
  may not set. They were not needed for Nature.
- **The default benchmark** and Half-Life Direct3D, still owed from 5b.
