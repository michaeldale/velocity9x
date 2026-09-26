/*
 * The ICD's DirectDraw side (gl_surface.c): the device - DirectDraw, the
 * HAL's render interface and its description - and one drawable per window
 * with its back and depth surfaces. Shared by the ICD's two platform files
 * only; it names no Windows type, so gl_icd.c's GL code and this contract
 * stay readable without <windows.h> (the window is a void pointer here).
 *
 * Everything here runs under the ICD's critical section, taken by
 * gl_icd.c; nothing here takes the Win16 mutex, which the HAL takes inside
 * each interface call.
 */
#ifndef VELOCITY9X_GL_SURFACE_H
#define VELOCITY9X_GL_SURFACE_H

#include "velocity9x/types.h"
#include "velocity9x/r3d_abi.h"

typedef struct v9x_gl_drawable V9X_GL_DRAWABLE;

/* Open the device once per process: DirectDraw first, because the HAL's
 * DriverInit runs when DirectDraw starts, then LoadLibrary of V9XHAL.DLL,
 * which returns the shared instance, then negotiation and describe. Zero
 * when any step fails; the ICD then offers no pixel format. */
int v9x_gl_device_open(void);
const V9X_R3D_INTERFACE *v9x_gl_device_interface(void);
const V9X_R3D_ABI_DESCRIBE *v9x_gl_device_description(void);
/* describe again - after STALE, a mode change - and drop every drawable's
 * surfaces so they are made again in the new mode. */
int v9x_gl_device_redescribe(void);
/* The target layout the pixel format offers: V9X_R3D_ABI_FORMAT_RGB565 or
 * _XRGB1555, or zero for none. */
v9x_u32 v9x_gl_device_format(void);

/* The drawable for a window, made on first use, its surfaces sized to the
 * client area and remade when that has changed. `resized` is set non-zero
 * when the surfaces were (re)made. Null when the surfaces cannot be made. */
V9X_GL_DRAWABLE *v9x_gl_drawable_bind(void *window, int *resized);
void v9x_gl_drawable_size(const V9X_GL_DRAWABLE *drawable,
                          v9x_u32 *width, v9x_u32 *height);
/* The IDirectDrawSurface pointers the render interface names. */
void *v9x_gl_drawable_back(const V9X_GL_DRAWABLE *drawable);
void *v9x_gl_drawable_depth(const V9X_GL_DRAWABLE *drawable);
/*
 * The front buffer as a surface the engine can draw: made on first use as
 * a copy of what the window shows, and kept equal to it by every present,
 * which copies the back buffer into it too. Null when it cannot be made.
 * show_front puts it on the window through the clipper, which is how a
 * draw to GL_FRONT becomes visible. front_existing does not make one.
 */
void *v9x_gl_drawable_front(V9X_GL_DRAWABLE *drawable);
void *v9x_gl_drawable_front_existing(const V9X_GL_DRAWABLE *drawable);
int v9x_gl_drawable_show_front(V9X_GL_DRAWABLE *drawable);
/* SwapBuffers: the back buffer to the window's client area through its
 * clipper. Non-zero on success. */
int v9x_gl_drawable_present(V9X_GL_DRAWABLE *drawable);
/* The back buffer for the CPU: a DirectDraw Lock, whose HAL side drains
 * the engine first, so completed rendering is visible. Row 0 is the top.
 * Zero when the surface is lost or cannot be locked; every successful
 * lock is paired with an unlock before the ICD returns to the caller. */
int v9x_gl_drawable_lock(V9X_GL_DRAWABLE *drawable, int front,
                         const void **pixels, v9x_u32 *pitch);
void v9x_gl_drawable_unlock(V9X_GL_DRAWABLE *drawable, int front);
/*
 * A texture the engine samples from video memory: a DirectDraw texture of
 * `width` by `height`, `levels` levels (a mip chain when more than one), in
 * V9X_R3D_ABI_FORMAT_* `format`. Null when DirectDraw or the HAL's
 * placement refuses it. Upload copies each level of `source` (level 0
 * first, the interface's CPU description) through a Lock; the Lock's HAL
 * side drains the engine first, so no queued draw reads a half-written
 * texture. With `to_1555` the source is RGB565 and each texel is written
 * as ARGB1555 with alpha one (v9x_gl_tex_565_to_1555). Zero when a level
 * cannot be locked.
 */
void *v9x_gl_hwtex_create(v9x_u32 width, v9x_u32 height, v9x_u32 levels,
                          v9x_u32 format);
int v9x_gl_hwtex_upload(void *surface, v9x_u32 levels,
                        const V9X_R3D_ABI_LEVEL *source, int to_1555);
void v9x_gl_hwtex_release(void *surface);
/* The drawable for a window if one exists, without making or resizing. */
V9X_GL_DRAWABLE *v9x_gl_drawable_find(void *window);

/* The ICD's log, in gl_icd.c. */
void v9x_gl_log(const char *text);
void v9x_gl_log3(const char *format, v9x_u32 a, v9x_u32 b, v9x_u32 c);

#endif /* VELOCITY9X_GL_SURFACE_H */
