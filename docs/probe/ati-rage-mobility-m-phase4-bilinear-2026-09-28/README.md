# ATI Rage Mobility-M Phase 4 bilinear evidence

Physical target: Gateway Solo 2150, ATI Rage Mobility-M `1002:4C4D`
revision `64`, 4 MiB SGRAM, 1024x768x16 desktop.

Build `ati-phase4-bilinear-20260928-a` retained clamp S/T, the accepted local
8x8 RGB565 texture and the guarded 64x28 target. It enabled both historical
Mach64 linear-filter controls (`TEX_BLEND_FCN_LINEAR` for minification and
`BILINEAR_TEX_EN` for magnification), producing `SCALE_3D_CNTL=0x0A010081`.
Every vertex used S=T=0.5 and W=1, so the triangle sampled the half-texel
intersection of the red, green, blue and white quadrants.

The three separated interior probes all read RGB565 `0x838E` (R=16, G=28,
B=14). Each component is midrange, rejecting every source texel and the
untouched `0xA55A` sentinel. Two same-boot executions passed byte-identically
with 256 changed pixels bounded by `(8,6)` through `(38,21)`, zero interior,
exterior, texture/guard and restoration mismatches, and no timeout/reset:

- TXT CRC32 `F3A0455A`, SHA-256
  `F003171877AD3F324DC1AA32C0AF4923DFBCBD57527E728D8C833F03AAD3AB9E`;
- BMP CRC32 `B26C4257`, SHA-256
  `C3BDBAB55B2F0683CE4E3016F277F22C9A419A93F126715D323D7632425B5A99`.

Together with the nearest seam capture in the sibling wrap evidence set,
this closes Phase 4 item 6.
