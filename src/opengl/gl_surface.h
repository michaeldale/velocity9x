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
/* SwapBuffers: the back buffer to the window's client area through its
 * clipper. Non-zero on success. */
int v9x_gl_drawable_present(V9X_GL_DRAWABLE *drawable);
/* The drawable for a window if one exists, without making or resizing. */
V9X_GL_DRAWABLE *v9x_gl_drawable_find(void *window);

/* The ICD's log, in gl_icd.c. */
void v9x_gl_log(const char *text);
void v9x_gl_log3(const char *format, v9x_u32 a, v9x_u32 b, v9x_u32 c);

#endif /* VELOCITY9X_GL_SURFACE_H */
