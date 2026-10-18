# Gen3 textured and blended draws at 32 bpp: exact texels, a correct blend

2026-10-18, MICHAEL-NETBOOK (GMA 950), boot 211, driver `a1d7cc1e-dirty`
(c9529221 code), and the 86Box Trio64 guest's software engine (boot 367)
as a second reference. Closes "textures, blending ... at 32 bpp not
measured" in
[2026-10-18-gen3-direct3d-at-32-bpp.md](2026-10-18-gen3-direct3d-at-32-bpp.md).
Evidence:
[`../probe/gen3-textured-blended-32bpp-2026-10-18/`](../probe/gen3-textured-blended-32bpp-2026-10-18/).
No driver code changed for this record.

## The instrument

`V9XD32` gains two tests, and the earlier T1 to T5 are unchanged:

- **T6:** an 8x8 ARGB1555 texture in four 4x4 quadrants: red `FC00`,
  green `83E0`, blue `801F` and white `FFFF`, all opaque, each channel at
  full scale or zero. It is drawn over a 64x64 quad with DECAL and nearest
  filtering, and each quadrant's centre is read back. ARGB1555 because both
  the Gen3 and the software engine sample it.
- **T7:** an opaque green quad, then red at vertex alpha `80h` over it with
  SRCALPHA / INVSRCALPHA blending.

Both are judged within 8 a channel, since a sampled and a blended colour
carry the engine's own expansion and rounding. The X byte must be zero at
32 bpp.

## Measured

| Engine | Mode | T6 red, green, blue, white | T7 | Result |
|---|---|---|---|---|
| Gen3 | 16 bpp | `F800`, `07E0`, `001F`, `FFFF` | `83E0` | PASS |
| Gen3 | 32 bpp | `00FF0000`, `0000FF00`, `000000FF`, `00FFFFFF` | `00807F00` | PASS |
| Gen3 | 32 bpp, Z24 | as above | `00807F00` | PASS |
| software | 16 bpp | `F800`, `07E0`, `001F`, `FFFF` | `83E0` | PASS |
| software | 32 bpp | `00FF0000`, `0000FF00`, `000000FF`, `00FFFFFF` | `00807E00` | PASS |

- **Gen3 samples a texture into an ARGB8888 target exactly**, with 5-bit
  channels expanded to full scale, and it blends at 32 bpp to `80`/`7F`.
  With the alpha writes disabled, the X byte stays zero through the blend.
- The two engines differ by one level of green in the 32 bpp blend
  (`7F` against `7E`), which is their rounding. Both are within the bound.
- All 33 draws of the three Gen3 runs (11 each, T1-T7) were submitted to
  the engine: `I9xxDrawsSubmitted` rose from 34,573 to 34,606 across them,
  with no context rejects or breadcrumb timeouts. The texture was in video
  memory (`D3dTextureLastCaps=10005000h`, 8 texels square).

## Not measured

- Bilinear filtering, mipmaps, the other texture blends, fog, specular and
  the two-unit path at 32 bpp; T6 is DECAL and nearest only.
- A textured title at 32 bpp. 3D WinBench 98's Z tests ran at
  640x480x32, but they are untextured.
