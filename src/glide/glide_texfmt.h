/*
 * Glide texel formats to the render interface's 16-bit formats
 * (docs\plans\glide-2x-wrapper.md, Phase 1). Only the formats NFS II SE
 * downloaded are converted (census): RGB_565, ARGB_1555, ARGB_4444, and
 * P_8 through the 256-entry palette from grTexDownloadTable.
 *
 * Chroma key is approximated here. Glide compares the combined colour
 * with the key and discards matches; the render interface has no key.
 * With key_enable a texel whose colour equals the key gets alpha 0, and
 * the format becomes one with alpha (565 and P_8 to ARGB1555), so the
 * alpha test the state module adds discards it. That matches Glide
 * exactly when the combine passes the texel's colour through unchanged,
 * which is the decal or white-Gouraud case (docs\decisions\
 * 2026-10-08-nfs2se-glide-census.md, "What render interface ABI 4
 * cannot express").
 *
 * Pure: no Windows headers.
 */
#ifndef VELOCITY9X_GLIDE_TEXFMT_H
#define VELOCITY9X_GLIDE_TEXFMT_H

#include "velocity9x/types.h"
#include "velocity9x/r3d_abi.h"
#include "glide_api.h"

v9x_u16 v9x_glide_texfmt_supported(v9x_u32 format);

/*
 * Converts `count` texels. 8-bit formats read bytes from `src`, 16-bit
 * formats read 16-bit words. `palette` (256 entries, 0xAARRGGBB as
 * grTexDownloadTable gives them) is read for P_8 only. key_rgb is
 * 0x00RRGGBB. Writes V9X_R3D_ABI_FORMAT_* to *abi_format. V9X_FALSE for
 * a format this module does not convert, or P_8 without a palette.
 */
v9x_u16 v9x_glide_texfmt_convert(v9x_u32 format, const void *src,
                                 v9x_u32 count, const v9x_u32 *palette,
                                 v9x_u32 key_enable, v9x_u32 key_rgb,
                                 v9x_u16 *dst, v9x_u32 *abi_format);

#endif /* VELOCITY9X_GLIDE_TEXFMT_H */
