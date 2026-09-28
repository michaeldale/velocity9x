# ATI Rage Mobility-M Phase 4 nearest-wrap evidence

Physical target: Gateway Solo 2150, ATI Rage Mobility-M `1002:4C4D`
revision `64`, 4 MiB SGRAM, 1024x768x16 desktop.

Build `ati-phase4-wrap-20260928-a` retained the accepted 8x8 RGB565 texture,
nearest filtering, W=1 coordinates and guarded target from item 4, but cleared
the `TEXTURE_CLAMP_S` and `TEXTURE_CLAMP_T` bits. The coordinates span -0.25
through 1.25. Exact probes showed negative S/T together, negative T alone,
and negative S alone repeating into the texture's bottom-right white quadrant.

Two same-boot executions passed byte-identically with `Status=0x0001FFFF`,
256 changed pixels bounded by `(8,6)` through `(38,21)`, zero interior,
exterior, texture/guard and restoration mismatches, and no timeout/reset:

- TXT CRC32 `BF999F33`, SHA-256
  `776D31CAC1723B54AF3C862BE1AF8BCA7E3897822089930E269B7E27B1ABCCDA`;
- BMP CRC32 `1FA18AC5`, SHA-256
  `2E2C25F863D77003C7C3A03F9A253CC7A9EF4C285C1240FD247B43FD90D00BC8`.

This closes the wrap-S/T half of Phase 4 item 6. Bilinear filtering remains
open and will use a separate constant half-texel scene so interpolation cannot
be confused with the nearest seam result.
