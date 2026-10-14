/*
 * ATI3DCIF.DLL's DirectDraw side (docs\plans\ati-3dcif-wrapper.md,
 * Phase 3): one of the DLL's two platform files (scripts\check-tree.ps1
 * lists it).
 *
 * Unlike Glide, 3DCIF does not own the screen: the application creates
 * its own DirectDraw chain, in exclusive mode, and hands the DLL pointers
 * into it. The DLL's own DirectDraw object, at the normal cooperative
 * level, holds only what the application never sees - the converted
 * texture surfaces - and reads the display mode the application set. The
 * application's surfaces reach the render interface by address (ABI 5's
 * raw target and depth).
 */
#ifndef VELOCITY9X_CIF_SURFACE_H
#define VELOCITY9X_CIF_SURFACE_H

#include "velocity9x/types.h"
#include "velocity9x/r3d_abi.h"

/* cif_dll.c's log. */
void v9x_cif_log_text(const char *text);
void v9x_cif_log3(const char *format, v9x_u32 a, v9x_u32 b, v9x_u32 c);

/* Opens DirectDraw and the render interface; non-zero on success. */
int v9x_cif_device_open(void);
void v9x_cif_device_close(void);

const V9X_R3D_INTERFACE *v9x_cif_device_interface(void);
const V9X_R3D_ABI_DESCRIBE *v9x_cif_device_description(void);
/* After STALE: describe again (a mode change bumped the generation). */
int v9x_cif_device_redescribe(void);

/*
 * The current display mode: the "Desktop Surface" the guide gives as the
 * default draw surface, which in an application's exclusive mode is its
 * own. Pitch in bytes; format V9X_R3D_ABI_FORMAT_RGB565 or _XRGB1555, 0
 * for any other layout. Non-zero on success.
 */
int v9x_cif_device_display(v9x_u32 *width, v9x_u32 *height, v9x_u32 *pitch,
                           v9x_u32 *format);

/* A one-level video-memory texture surface in a V9X_R3D_ABI_FORMAT_*
 * layout, as Glide's (glide_surface.c); null if refused. */
void *v9x_cif_hwtex_create(v9x_u32 width, v9x_u32 height, v9x_u32 format);
/* Fills it from tightly packed 16-bit texels; non-zero on success. A
 * surface lost to the application's mode change is restored and refilled. */
int v9x_cif_hwtex_upload(void *surface, const v9x_u16 *texels,
                         v9x_u32 width, v9x_u32 height);
void v9x_cif_hwtex_release(void *surface);

#endif /* VELOCITY9X_CIF_SURFACE_H */
