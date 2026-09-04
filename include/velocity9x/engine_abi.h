/*
 * Engine identity and capability values.
 *
 * These are the one vocabulary shared by all three execution contexts: the
 * 16-bit chip modules that fill the engine descriptor, the 16-bit driver that
 * clamps its DirectDraw caps from it, and the flat 32-bit HAL that selects its
 * engine ops from it. They are therefore in a header of their own, with no
 * includes and no dependency on <windows.h>, so that
 * include\velocity9x\hw16.h can stay free of the OS boundary and
 * include\velocity9x\win9x_ddraw_abi.h no longer has to reach into the
 * 16-bit hardware layer to find them.
 *
 * engine_type names the chip's drawing engine and selects a code path.
 * engine_caps says what that engine will do and is a mask, so a chip can
 * carry an engine with only part of the family's capability set.
 */
#ifndef VELOCITY9X_ENGINE_ABI_H
#define VELOCITY9X_ENGINE_ABI_H

#define V9X_DD_ENGINE_TYPE_NONE         0ul
#define V9X_DD_ENGINE_TYPE_S3_VIRGE_DX  1ul
#define V9X_DD_ENGINE_TYPE_S3_TRIO64    2ul

#define V9X_DD_ENGINE_CAP_SOLID_FILL    0x00000001ul
#define V9X_DD_ENGINE_CAP_SCREEN_COPY   0x00000002ul
#define V9X_DD_ENGINE_CAP_FLIP          0x00000004ul
#define V9X_DD_ENGINE_CAP_VBLANK        0x00000008ul
#define V9X_DD_ENGINE_CAP_D3D           0x00000010ul
/*
 * Serve Direct3D from the CPU rasterizer rather than from the chip.
 *
 * Set by the 16-bit driver when [Velocity9x] Direct3D selects the software
 * mode, on any chip, and never by a chip module - no silicon has this
 * capability, the driver does. It is what the 32-bit HAL selects the software
 * V9X_D3D_ENGINE_OPS on, and it implies V9X_DD_ENGINE_CAP_D3D so that the
 * existing clamp keeps publishing the Direct3D tables.
 *
 * That makes this word carry a mode decision and not only a chip fact, which
 * is a departure from the header comment above - and the departure predates
 * this bit: mode 1 already clears CAP_D3D from a user setting. The rule that
 * survives is the useful one: the 16-bit side is the single authority on what
 * may be advertised, whether it is reading silicon or a setting.
 */
#define V9X_DD_ENGINE_CAP_D3D_SOFTWARE  0x00000020ul
/*
 * Two passes over one triangle, blended together, produce the right answer on
 * this part.
 *
 * A chip fact, and the first one this word has carried that is a negative. The
 * engine synthesises trilinear filtering that way - level N, then level N+1
 * blended over it at the LOD fraction - and on the emulated ViRGE/DX the
 * result is a clean mix of the two levels, three times over. On the S3
 * Trio3D/2X it is not: the second pass lands wrong, and it does so on a boot
 * where every single-pass alpha blend the probe can measure is correct
 * (docs\decisions\2026-09-04-the-trilinear-two-pass-and-a-retraction.md).
 *
 * The bit is deliberately about the two-pass form and not about alpha. An
 * earlier version of it said the part could not blend at all; a controlled A/B
 * on one machine, two boots apart, disproved that and is recorded in the
 * decision above. What survives the A/B is only this: on a part without this
 * bit, trilinear has to degrade to a filter the chip does in one pass rather
 * than emit a second pass whose result is wrong.
 */
#define V9X_DD_ENGINE_CAP_S3D_TWO_PASS  0x00000040ul
/*
 * A draw with TEXTURE_UNLIT and the alpha field enabled keeps its texel's
 * colour on this part.
 *
 * The ViRGE/DX does. The Trio3D/2X samples the texel's alpha, honours it, and
 * drops the colour: an opaque texel draws black over whatever was there, while
 * an alpha-zero texel correctly leaves the destination alone. Measured over
 * eight cells crossing depth, filter and shade mode, where depth and filter
 * made no difference and the shade mode made all of it
 * (docs\decisions\2026-09-04-an-unlit-blend-loses-its-texture-on-the-trio3d.md).
 *
 * TEXTURE_UNLIT *without* the alpha field is correct on both parts, so this is
 * about the pair and not about the unlit path in general.
 */
#define V9X_DD_ENGINE_CAP_S3D_UNLIT_ALPHA 0x00000080ul

#endif /* VELOCITY9X_ENGINE_ABI_H */
