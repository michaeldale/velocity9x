# Final Reality's black wedges on the ViRGE were the texture wrap bit

Date: 2026-09-03
Status: fixed and measured on the emulated ViRGE; awaiting the application's own
confirmation and a run on silicon

## The symptom

Final Reality's 3D scene on the 86Box ViRGE/DX drew with large black wedges:
whole triangles missing, the clear colour showing through, apparently every
other panel along tiled walls and floors and most of the far geometry. The
stock S3 driver on the same emulator drew the whole scene. The software engine
drew every triangle, untextured.

## What it was not, with the evidence that said so

Each of these was a plausible reading of the picture and each was tested on the
emulator before being dropped. They are kept because the next person to see
missing triangles will think of them too.

| Hypothesis | Test | Result |
|---|---|---|
| Today's driver changes | Same scene on this morning's driver pair | Same wedges |
| Mip level running past the chain | Level index clamped to verified levels | Same wedges (fix kept - it is correct) |
| `RenderPrimitive` dropping instructions over 256 triangles | Cap lifted, refusals traced | Same wedges; **zero** refusals traced (fix kept) |
| Clipper refusing out-of-limit triangles | Refusal is traced | Zero |
| FIFO or engine faults | `V9XTRACE` counters | Zero timeouts, zero resets |
| Fog colour reset between draws | Read the three reset sites | Create and destroy only |
| Depth encoding | Compare table checked against the model | Consistent |
| One vertex winding not rendering | Probe: same triangle reversed | Fails on **both** engines - the runtime's default CULLMODE=CCW; with culling off, both windings draw on both |
| Triangle shape (flat, obtuse, sliver, on the border) | Probe: twelve-shape ladder, culling off | 12 of 12 on the ViRGE |

Two things were learned from the misses. The software engine and the ViRGE
engine share everything up to `draw_triangles`, so a scene that is complete on
one and holed on the other is the engine, and only the engine - which is what
made the shape ladder and the winding rungs worth running. And the probe's
own texture fill wrote a **half-empty DWORD pattern** - `0x83e0` where
`0x83e083e0` was meant - so every texture the probe has ever drawn was a stripe
of green and black texels, and several "black fetch" readings earlier today
were black texels. The fills are solid now.

## What it was

The S3D unit returns the **border colour** for any texel coordinate outside the
first repeat unless the command word's wrap bit is set; 86Box models this as a
`_nowrap` variant of every texel fetch (`build\reference-vid_s3_virge.c` around
3618) and the border colour is whatever `TEX_BORDER` holds, which is zero. The
engine set that bit from `D3DRENDERSTATE_WRAPU`/`WRAPV` - a render state about
how coordinates are interpolated across a triangle, which almost no application
touches - and not from the texture **address mode**, whose default is WRAP and
which is what every application that tiles a texture relies on.

Final Reality tiles its walls and floors. Every triangle whose coordinates
reached past 1.0, or below 0, sampled black. Consecutive panels along a tiled
strip alternate in and out of the first repeat, which is the every-other-panel
look; distant geometry is where the repeats pile up, which is the far-geometry
look.

`d3d_virge.c` now sets the bit for the WRAP and CLAMP address modes as well as
for WRAPU/WRAPV. CLAMP gets it because the recorded fact from S3's own driver is
that it sets the bit for both modes, and the alternative is a border colour
nobody asked for.

## Measured

Probe, emulated ViRGE/DX, solid textures, two new rungs - the plain texture
drawn with coordinates 0 to 2 and then -0.5 to 0.5:

```
before fix   D3DTiledTextureOk=1 (positive overflow masks)   D3DTiledNegativeRaw=0     black
after fix    D3DTiledTextureOk=1                             D3DTiledNegativeRaw=992   green
```

Software engine, same probe: both rungs green, as its arithmetic wrap
predicts. Every other key on both engines unchanged.

## Left open, and cleaner than before

**Mip level selection does not switch levels** on the ViRGE: with solid textures
the mip rung reads level 0 (green) where level 1 (blue) was expected, on the
emulator and on the card alike, and the driver's mip-chain counter reports the
mipmapped path was never entered for the probe's two-level texture while it
entered 341,923 times for Final Reality's. The probe reports that texture's top
level with `ddsCaps = 0x10400008` - `MIPMAP | COMPLEX | LOCALVIDMEM`, and
**no `TEXTURE` bit** - which is what `v9x_d3d_texture_info` requires. Whether
DirectDraw genuinely strips `TEXTURE` from a mip chain's top level, or the
probe is creating something other than what it thinks, is the next thing to
find out, on the emulator, with an instrument that records the caps the HAL
actually sees at draw time.

**Vertex alpha on the Trio3D/2X** is unchanged by anything here.
