# DirectDraw offers no YUV surfaces and no overlay

Date: 2026-11-06. Feature gap, found in a feature review. Status: open,
nothing written, nothing measured.

## Gap

No family publishes a FourCC YUV surface format or an overlay. The HAL
table has an `UpdateOverlay` slot (`include/velocity9x/win9x_ddraw_abi.h`)
and nothing fills it. Video players (Media Player, codec renderers) then
convert YUV to RGB in DirectDraw's emulation or in the codec, on the CPU,
and blit RGB.

## Feature

1. Off-screen surfaces in YUY2, UYVY and YV12, placed in the video memory
   heap.
2. Blits from them to RGB surfaces and the primary at 15, 16, 24 and
   32 bpp, with BT.601 conversion and stretching. On a CPU-drawn family
   this is CPU code in the HAL; families whose engine converts YUV
   (Rage 128 scaler, Gen3/Gen4 blitter or overlay, S3 streams) can take it
   later through the backend.
3. One overlay, emulated where there is no hardware overlay: a YUV or RGB
   surface composited onto the primary with a destination colour key at
   each flip or update, moved and stretched like a hardware one.

The conversion and the colour-key composite are pure arithmetic and belong
in host-tested C.

## Acceptance

A host test pins the conversion against BT.601 values. On one guest, a
player that asks for YUY2 or an overlay plays a clip through it, with the
HAL's counters showing the YUV blits and the CPU time compared with the
present path.
