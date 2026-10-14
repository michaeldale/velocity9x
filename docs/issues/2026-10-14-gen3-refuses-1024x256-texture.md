# Gen3: a 1024x256 RGB565 texture is refused DDERR_INVALIDPARAMS

Date: 2026-10-14

Status: Open. Worked around in ATI3DCIF.DLL, which rebuilds the texture
at 512x128. The cause has not been looked for.

## What is seen

On the netbook (GMA 950, boot 180), ATI3DCIF.DLL's texture surface
creation was refused for Knight VQ's sky texture:
`IDirectDraw::CreateSurface` with DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY,
1024x256, an RGB565 pixel format.
- **The return:** `hwtex create 04000100 format=1 hr=80070057`, which is
  DDERR_INVALIDPARAMS.
- **The engine's describe** gives `hw max=1024`.
- **A 512x512 texture** of the same byte size failed in the same session
  with DDERR_OUTOFVIDEOMEMORY instead. So 1024x256 is refused for its
  shape, not its size.

Evidence: `docs/probe/netbook-gma950-3dcif-2026-10-14/`.

## Where to look, not yet looked at

- `v9x_d3d_i9xx_create_texture` (`d3d_i9xx.c`) places a lone texture
  that passes `v9x_d3d_i9xx_texture_shape`, which allows non-square
  textures. If the placement declines, DirectDraw's own heap gets the
  surface, at the 4 KiB texture alignment Gen3 publishes.
- That fallback, a 2048-byte row in a heap of the display's 2112-byte
  pitch, is one candidate for the INVALIDPARAMS.
- Whether the HAL or DirectDraw returned the error is not known. A
  V9XTRACE snapshot around the create (`texture_placed` and the
  CreateSurface trace) would say.

Direct3D titles that make 1024-wide textures on this machine would see
the same refusal.
