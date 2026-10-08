/*
 * GLIDE2X.DLL's DirectDraw side (docs\plans\glide-2x-wrapper.md, Phase 2):
 * the second of its two platform files (scripts\check-tree.ps1 lists it).
 *
 * grSstWinOpen takes the screen as a Voodoo does: exclusive fullscreen at
 * the resolution asked for, 16 bits, a flip chain of the colour buffers
 * asked for, and a 16-bit depth surface for the aux buffer. The render
 * interface draws into the chain's surfaces as it draws into a Direct3D
 * render target. Whether it accepts a flip-chain buffer as a target is
 * the Phase 2 question; the ICD only ever drew into offscreen surfaces.
 */
#ifndef VELOCITY9X_GLIDE_SURFACE_H
#define VELOCITY9X_GLIDE_SURFACE_H

#include "velocity9x/types.h"
#include "velocity9x/r3d_abi.h"

/* GrBuffer_t. */
#define V9X_GLIDE_BUFFER_FRONT 0ul
#define V9X_GLIDE_BUFFER_BACK  1ul
#define V9X_GLIDE_BUFFER_AUX   2ul

/* glide_dll.c's log, shared as gl_icd.c's is with gl_surface.c. */
void v9x_glide_log(const char *text);
void v9x_glide_log3(const char *format, v9x_u32 a, v9x_u32 b, v9x_u32 c);

/* Opens the device; non-zero on success. `window` null: the process's
 * active window, or one made for it. */
int v9x_glide_device_open(void *window, v9x_u32 width, v9x_u32 height,
                          v9x_u32 color_buffers, v9x_u32 aux_buffers);
void v9x_glide_device_close(void);
int v9x_glide_device_is_open(void);

const V9X_R3D_INTERFACE *v9x_glide_device_interface(void);
const V9X_R3D_ABI_DESCRIBE *v9x_glide_device_description(void);
v9x_u32 v9x_glide_device_generation(void);
/* After STALE: describe again (a mode change bumped the generation). */
int v9x_glide_device_redescribe(void);

/* The surface behind a GrBuffer_t: the primary for FRONT, its attached
 * back buffer for BACK, the depth surface for AUX; null when absent. A
 * flip exchanges memory under these interfaces, so they stay valid. */
void *v9x_glide_device_buffer(v9x_u32 buffer);

/* Flips the chain, after submitting what was drawn. */
int v9x_glide_device_swap(v9x_u32 interval);

int v9x_glide_device_lock(v9x_u32 buffer, int read_only, void **pixels,
                          v9x_u32 *pitch);
void v9x_glide_device_unlock(v9x_u32 buffer);

/* A one-level video-memory texture surface in a V9X_R3D_ABI_FORMAT_*
 * layout, as the ICD makes them (gl_surface.c); null if refused. */
void *v9x_glide_hwtex_create(v9x_u32 width, v9x_u32 height, v9x_u32 format);
/* Fills it from tightly packed 16-bit texels; non-zero on success. A
 * surface lost to a mode change is restored and refilled. */
int v9x_glide_hwtex_upload(void *surface, const v9x_u16 *texels,
                           v9x_u32 width, v9x_u32 height);
void v9x_glide_hwtex_release(void *surface);

#endif /* VELOCITY9X_GLIDE_SURFACE_H */
