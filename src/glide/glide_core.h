/*
 * The Glide engine shared by GLIDE2X.DLL and GLIDE3X.DLL
 * (docs\plans\glide-3x-wrapper.md): the census log, the texture engine and
 * its surface cache, batching, drawing, clears, the window and the frame
 * buffer, all over the render interface. Each DLL links it statically and
 * keeps only its own exports in a front-end file (glide_dll.c for Glide 2),
 * which translates its API into these calls.
 *
 * These names are shared between files inside one DLL; none is exported
 * from it. The engine works in Glide 2's terms throughout: GrVertex floats,
 * Glide 2 LOD and aspect numbers in GrTexInfo, and V9X_GLIDE_STATE. A
 * Glide 3 front end converts into them.
 */
#ifndef VELOCITY9X_GLIDE_CORE_H
#define VELOCITY9X_GLIDE_CORE_H

#include "velocity9x/types.h"
#include "glide_state.h"

/* The most exports a front end may count: GLIDE2X.DLL has 130, GLIDE3X.DLL
 * 99. A front end checks its count against this at compile time. */
#define V9X_GLIDE_CORE_EXPORTS_MAX 160u

/* The Glide state as the game set it, and what the window opened with.
 * Front ends store state calls straight into it. */
extern V9X_GLIDE_STATE v9x_glide_state;

/* ---- the log and census ------------------------------------------- */

/*
 * Commits the texture tables, sets the state for a 640x480 window until
 * one opens, and makes `log_path` the log and `names` (count entries) the
 * export names the census counts by index. Zero when the tables cannot be
 * had (logged). Called from DLL_PROCESS_ATTACH, before any other call.
 */
int v9x_glide_core_attach(const char *log_path, const char *const *names,
                          unsigned int count);
/* Writes the final summary and frees what attach and the LFB took. */
void v9x_glide_core_detach(void);

void v9x_glide_log(const char *text);
void v9x_glide_log3(const char *format, v9x_u32 a, v9x_u32 b, v9x_u32 c);
/* The export's name and call number, then the formatted arguments. */
void v9x_glide_logf(unsigned int ix, v9x_u32 call, const char *format, ...);
/* Counts a call of export ix; returns its call number. */
v9x_u32 v9x_glide_count(unsigned int ix);
/* Whether a draw or poll call's nth call is logged. */
int v9x_glide_sampled(v9x_u32 call);
/* Whether a state or texture call with this argument key is logged. */
int v9x_glide_noted(unsigned int ix, v9x_u32 key);
v9x_u32 v9x_glide_key(v9x_u32 a, v9x_u32 b, v9x_u32 c, v9x_u32 d);
v9x_u32 v9x_glide_checksum(const v9x_u32 *words, unsigned int count);
/* Every export called so far, with the draw outcomes and cycle counts. */
void v9x_glide_summary(const char *why);
/* A generated stub's hook: logs its first call. */
void v9x_glide_stub_called(unsigned int ix);
v9x_u32 v9x_glide_swap_count(void);

/* ---- the window and frame ----------------------------------------- */

/* Closes any window and opens one at a Glide resolution; non-zero when
 * the device opened. */
int v9x_glide_open(void *window, v9x_u32 resolution, v9x_u32 color_format,
                   v9x_u32 origin, v9x_u32 color_buffers, v9x_u32 aux_buffers);
/* Draws what is held, drops the texture surfaces and closes the device. */
void v9x_glide_close(void);
/* The swap after the export has logged it: flip, profile, summaries and
 * the next frame's capture decision. */
void v9x_glide_swap(v9x_u32 interval);
/* grBufferClear's work: colour, and depth when it is on and writable. */
void v9x_glide_clear(v9x_u32 color, v9x_u32 depth);
/* GrBuffer_t FRONT or BACK; others are ignored. */
void v9x_glide_set_render_buffer(v9x_u32 buffer);
/* Draws whatever triangles are held. */
void v9x_glide_flush(void);

/* ---- drawing ------------------------------------------------------ */

/* GrVertex pointers (glide_vertex.h's layout); nothing when no device. */
void v9x_glide_triangle(const float *a, const float *b, const float *c);
void v9x_glide_line(const float *a, const float *b);

/* grConstantColorValue4's colour as ARGB. */
void v9x_glide_set_constant_argb(v9x_u32 argb);

/* ---- textures ----------------------------------------------------- */

/* GrTexInfo in Glide 2's terms: smallLod, largeLod, aspectRatio, format,
 * data. */
void v9x_glide_texture_source(v9x_u32 address, v9x_u32 even_odd,
                              const v9x_u32 *info);
void v9x_glide_texture_download(v9x_u32 address, v9x_u32 even_odd,
                                const v9x_u32 *info);
/* 256 FxU32 palette entries. */
void v9x_glide_palette_load(const v9x_u32 *palette);
/* V9X_GLIDE_FOG_TABLE_SIZE bytes. */
void v9x_glide_fog_load(const v9x_u8 *table);

/* ---- the linear frame buffer -------------------------------------- */

/*
 * grLfbLock, grLfbUnlock, grLfbReadRegion and grLfbWriteRegion, whole: ix and call are the
 * calling export's, for the log. `info` is GrLfbInfo_t as FxU32s.
 */
v9x_u32 v9x_glide_lfb_lock(unsigned int ix, v9x_u32 call, v9x_u32 type,
                           v9x_u32 buffer, v9x_u32 write_mode, v9x_u32 origin,
                           v9x_u32 pipeline, v9x_u32 *info);
v9x_u32 v9x_glide_lfb_unlock(unsigned int ix, v9x_u32 call, v9x_u32 type,
                             v9x_u32 buffer);
v9x_u32 v9x_glide_lfb_read(unsigned int ix, v9x_u32 call, v9x_u32 buffer,
                           v9x_u32 x, v9x_u32 y, v9x_u32 width,
                           v9x_u32 height, v9x_u32 dst_stride, v9x_u8 *dst);
v9x_u32 v9x_glide_lfb_write(unsigned int ix, v9x_u32 call, v9x_u32 buffer,
                            v9x_u32 x, v9x_u32 y, v9x_u32 src_format,
                            v9x_u32 width, v9x_u32 height,
                            v9x_u32 src_stride, const v9x_u8 *src);

#endif /* VELOCITY9X_GLIDE_CORE_H */
