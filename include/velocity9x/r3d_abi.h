/*
 * The private render interface V9XHAL.DLL exports to the OpenGL ICD
 * (docs\plans\opengl-1.1-icd.md, Phase 3), version 1.
 *
 * Stateless: every call carries its target, depth, texture and state, and
 * the HAL keeps no pointer from a call once it returns. The ICD owns every
 * DirectDraw surface it names and every buffer it passes; the HAL reads them
 * synchronously, under the Win16 mutex, and validates each before it
 * dereferences anything past the struct header.
 *
 * Shared by a 32-bit DLL built with Open Watcom, a 32-bit ICD and the host
 * tests, so it is held to what those agree on:
 *   - every field is 32 bits wide - v9x_u32, float or a pointer on the
 *     target - and every struct is a whole number of them, so no packing
 *     pragma is needed and none is used;
 *   - every entry point is __stdcall (V9X_R3D_CALL), the Win32 convention
 *     both compilers default to for exported interfaces;
 *   - no enum (CLAUDE.md), only #define'd values;
 *   - sizes are asserted where pointers are 32 bits, which is the target
 *     and the Watcom host build; a 64-bit host skips the asserts, not the
 *     tests.
 *
 * Negotiation: the ICD calls V9xRenderInterface(V9X_R3D_ABI_VERSION,
 * sizeof(V9X_R3D_INTERFACE)) and gets null for any version or size the HAL
 * does not implement exactly. Every request struct starts with struct_bytes,
 * which the HAL compares with its own sizeof before reading further; a
 * mismatch is V9X_R3D_RESULT_ABI and nothing is read.
 */
#ifndef VELOCITY9X_R3D_ABI_H
#define VELOCITY9X_R3D_ABI_H

#include "velocity9x/types.h"

#define V9X_R3D_ABI_VERSION 1ul

#if defined(__WATCOMC__) || defined(_MSC_VER)
#define V9X_R3D_CALL __stdcall
#else
#define V9X_R3D_CALL
#endif

/*
 * Results. Every entry returns one, and the three failure classes the plan
 * separates are kept apart so the ICD can act on them without guessing:
 *
 *   - a REFUSAL is decided before anything reaches the engine: INVALID
 *     (a malformed descriptor), ABI, STALE (a generation from before a mode
 *     change or a surface lost since), UNSUPPORTED (state this engine will
 *     not approximate) and NOT_READY (no initialised HAL or engine). The
 *     target is untouched; the ICD may rasterize the same batch itself.
 *   - PARTIAL: `submitted` triangles of the batch reached the engine and
 *     the rest did not, all of it known. The ICD must not redraw the
 *     submitted prefix - blended pixels would be applied twice.
 *   - INDETERMINATE and TIMEOUT: work may have executed and completion was
 *     not observed. The target's contents are unknown; nothing may be
 *     replayed, and the ICD stops that operation and reports a device
 *     failure rather than inventing a GL 1.1 context-loss error.
 *   - NO_MEMORY: an allocation the call needed failed before submission.
 */
#define V9X_R3D_RESULT_OK            0ul
#define V9X_R3D_RESULT_INVALID       1ul
#define V9X_R3D_RESULT_ABI           2ul
#define V9X_R3D_RESULT_STALE         3ul
#define V9X_R3D_RESULT_UNSUPPORTED   4ul
#define V9X_R3D_RESULT_NOT_READY     5ul
#define V9X_R3D_RESULT_NO_MEMORY     6ul
#define V9X_R3D_RESULT_PARTIAL       7ul
#define V9X_R3D_RESULT_INDETERMINATE 8ul
#define V9X_R3D_RESULT_TIMEOUT       9ul

/*
 * One batch is at most this many triangles after the ICD's own clipping and
 * primitive expansion: the D3D core's V9X_D3D_INDEXED_BATCH, which is what
 * every engine is sized for. A larger count is INVALID, never truncated.
 */
#define V9X_R3D_ABI_BATCH_MAX 64ul

/* Target and texture layouts, numbered as r3d.h numbers them. */
#define V9X_R3D_ABI_FORMAT_RGB565   1ul
#define V9X_R3D_ABI_FORMAT_XRGB1555 2ul
#define V9X_R3D_ABI_FORMAT_ARGB1555 3ul
#define V9X_R3D_ABI_FORMAT_ARGB4444 4ul

/*
 * A screen-space vertex, the neutral core's layout (D3DTLVERTEX's), with
 * the meanings the CPU rasterizer contract gives it
 * (docs\specifications\cpu-rasterizer-contract.md): sx/sy in pixels with
 * the origin at the target's top-left, sz 0..1 window depth, rhw the
 * texture divisor - 1/w, or q_tex/w for a projective texture - colour
 * packed ARGB, and specular's alpha the fog factor (255 unfogged). The ICD
 * has already lit, clipped and flipped y.
 */
typedef struct v9x_r3d_abi_vertex {
    float sx;
    float sy;
    float sz;
    float rhw;
    v9x_u32 color;
    v9x_u32 specular;
    float tu;
    float tv;
} V9X_R3D_ABI_VERTEX;

/*
 * A DirectDraw surface the ICD created in its own process, named by its
 * LCL (the INT's lpLcl). The HAL reads the layout from the LCL's GBL and
 * validates it on every call; `lcl` is never kept.
 */
typedef struct v9x_r3d_abi_surface {
    void *lcl;
} V9X_R3D_ABI_SURFACE;

/*
 * One texture level in the ICD's memory, the sampler-ready 16-bit copy the
 * plan keeps beside the logical image. `bytes` is the storage the ICD owns
 * at `pixels`, checked against pitch * (height - 1) + width * 2 before a
 * texel is read.
 */
typedef struct v9x_r3d_abi_level {
    void *pixels;
    v9x_u32 bytes;
    v9x_u32 pitch;
    v9x_u32 width;
    v9x_u32 height;
} V9X_R3D_ABI_LEVEL;

#define V9X_R3D_ABI_LEVELS_MAX 10ul

#define V9X_R3D_ABI_TEXTURE_NONE 0ul
#define V9X_R3D_ABI_TEXTURE_CPU  1ul
#define V9X_R3D_ABI_TEXTURE_HW   2ul

/* Filters, level selection, addressing: D3D's numbers, as r3d.h's. */
#define V9X_R3D_ABI_FILTER_NEAREST 1ul
#define V9X_R3D_ABI_FILTER_LINEAR  2ul
#define V9X_R3D_ABI_MIP_NONE       1ul
#define V9X_R3D_ABI_MIP_POINT      2ul
#define V9X_R3D_ABI_MIP_LINEAR     3ul
#define V9X_R3D_ABI_ADDRESS_WRAP   1ul
#define V9X_R3D_ABI_ADDRESS_CLAMP  3ul

/* The two halves of the texture combine the contract separates. */
#define V9X_R3D_ABI_COLOROP_REPLACE    1ul
#define V9X_R3D_ABI_COLOROP_MODULATE   2ul
#define V9X_R3D_ABI_COLOROP_DECALALPHA 3ul
#define V9X_R3D_ABI_COLOROP_BLEND      4ul
#define V9X_R3D_ABI_ALPHAOP_FRAGMENT   0ul
#define V9X_R3D_ABI_ALPHAOP_REPLACE    1ul
#define V9X_R3D_ABI_ALPHAOP_MODULATE   2ul

typedef struct v9x_r3d_abi_texture {
    v9x_u32 storage;
    v9x_u32 format;
    /* HW: the top level's surface; its attached chain supplies the rest. */
    V9X_R3D_ABI_SURFACE surface;
    /* CPU: level 0 first, `level_count` of them. */
    const V9X_R3D_ABI_LEVEL *levels;
    v9x_u32 level_count;
    v9x_u32 min_filter;
    v9x_u32 mag_filter;
    v9x_u32 mip;
    v9x_u32 address;
    v9x_u32 color_op;
    v9x_u32 alpha_op;
    v9x_u32 env_color;
} V9X_R3D_ABI_TEXTURE;

/*
 * Fragment state. Compare functions and blend factors take r3d.h's (D3D's)
 * numbers; the alpha reference is a byte; the scissor is half-open and
 * inside the target, with 0,0,width,height meaning none; write_mask's low
 * three bits are red, green and blue.
 */
#define V9X_R3D_ABI_WRITE_RED   1ul
#define V9X_R3D_ABI_WRITE_GREEN 2ul
#define V9X_R3D_ABI_WRITE_BLUE  4ul
#define V9X_R3D_ABI_WRITE_RGB   7ul

typedef struct v9x_r3d_abi_state {
    v9x_u32 depth_enable;
    v9x_u32 depth_write;
    v9x_u32 depth_func;
    v9x_u32 blend_enable;
    v9x_u32 src_blend;
    v9x_u32 dst_blend;
    v9x_u32 alpha_test_enable;
    v9x_u32 alpha_func;
    v9x_u32 alpha_ref;
    v9x_u32 fog_enable;
    v9x_u32 fog_color;
    v9x_u32 write_mask;
    v9x_u32 scissor_left;
    v9x_u32 scissor_top;
    v9x_u32 scissor_right;
    v9x_u32 scissor_bottom;
} V9X_R3D_ABI_STATE;

/*
 * The HAL's answer to describe. `generation` changes whenever DriverInit
 * runs again - a mode change - and every later request must carry the
 * current one or be STALE.
 */
#define V9X_R3D_ABI_ENGINE_SOFTWARE 1ul
#define V9X_R3D_ABI_ENGINE_VIRGE    2ul
#define V9X_R3D_ABI_ENGINE_GEN3     3ul

typedef struct v9x_r3d_abi_describe {
    v9x_u32 struct_bytes;
    v9x_u32 abi_version;
    v9x_u32 generation;
    v9x_u32 engine;
    v9x_u32 target_formats;     /* 1 << V9X_R3D_ABI_FORMAT_* */
    v9x_u32 texture_formats;
    v9x_u32 texture_size_max;
    v9x_u32 batch_max;
    char renderer[32];          /* NUL-terminated, for GL_RENDERER */
} V9X_R3D_ABI_DESCRIBE;

typedef struct v9x_r3d_abi_draw {
    v9x_u32 struct_bytes;
    v9x_u32 generation;
    V9X_R3D_ABI_SURFACE target;
    V9X_R3D_ABI_SURFACE depth;  /* lcl null: no depth buffer */
    V9X_R3D_ABI_TEXTURE texture;
    V9X_R3D_ABI_STATE state;
    const V9X_R3D_ABI_VERTEX *vertices;  /* 3 * triangle_count, a list */
    v9x_u32 triangle_count;
} V9X_R3D_ABI_DRAW;

/* Triangles that reached the engine, meaningful for OK and PARTIAL. */
typedef struct v9x_r3d_abi_outcome {
    v9x_u32 result;
    v9x_u32 submitted;
} V9X_R3D_ABI_OUTCOME;

typedef struct v9x_r3d_abi_rect {
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 right;
    v9x_u32 bottom;
} V9X_R3D_ABI_RECT;

typedef struct v9x_r3d_abi_clear {
    v9x_u32 struct_bytes;
    v9x_u32 generation;
    V9X_R3D_ABI_SURFACE target;
    V9X_R3D_ABI_SURFACE depth;
    v9x_u32 clear_color;
    v9x_u32 clear_depth;
    v9x_u32 color_value;        /* 0x00RRGGBB */
    v9x_u32 depth_value;        /* low 16 bits */
    v9x_u32 write_mask;         /* V9X_R3D_ABI_WRITE_* */
    v9x_u32 write_depth;
    const V9X_R3D_ABI_RECT *rects;  /* drawable and scissor, intersected */
    v9x_u32 rect_count;
} V9X_R3D_ABI_CLEAR;

typedef v9x_u32 (V9X_R3D_CALL *V9X_R3D_DESCRIBE_FN)(
    V9X_R3D_ABI_DESCRIBE *out);
typedef v9x_u32 (V9X_R3D_CALL *V9X_R3D_DRAW_FN)(
    const V9X_R3D_ABI_DRAW *draw, V9X_R3D_ABI_OUTCOME *outcome);
typedef v9x_u32 (V9X_R3D_CALL *V9X_R3D_CLEAR_FN)(
    const V9X_R3D_ABI_CLEAR *clear);
/* Submit pending work for progress; no completion is implied. */
typedef v9x_u32 (V9X_R3D_CALL *V9X_R3D_FLUSH_FN)(v9x_u32 generation);
/* Completion and CPU visibility of everything submitted before it: OK only
 * when observed, TIMEOUT or INDETERMINATE otherwise. */
typedef v9x_u32 (V9X_R3D_CALL *V9X_R3D_FINISH_FN)(v9x_u32 generation);

typedef struct v9x_r3d_interface {
    v9x_u32 abi_version;
    v9x_u32 struct_bytes;
    V9X_R3D_DESCRIBE_FN describe;
    V9X_R3D_DRAW_FN draw;
    V9X_R3D_CLEAR_FN clear;
    V9X_R3D_FLUSH_FN flush;
    V9X_R3D_FINISH_FN finish;
} V9X_R3D_INTERFACE;

/* The export: null unless `abi_version` and `struct_bytes` are exactly
 * this header's. */
typedef const V9X_R3D_INTERFACE *(V9X_R3D_CALL *V9X_R3D_ENTRY_FN)(
    v9x_u32 abi_version, v9x_u32 struct_bytes);

/*
 * The layout, frozen. A change to any of these is a new ABI version, not
 * an edit. Checked only where pointers are 32 bits.
 */
#if defined(__WATCOMC__) || defined(_M_IX86) || defined(__i386__)
typedef char v9x_r3d_abi_assert_vertex[sizeof(V9X_R3D_ABI_VERTEX) == 32 ? 1 : -1];
typedef char v9x_r3d_abi_assert_level[sizeof(V9X_R3D_ABI_LEVEL) == 20 ? 1 : -1];
typedef char v9x_r3d_abi_assert_texture[sizeof(V9X_R3D_ABI_TEXTURE) == 48 ? 1 : -1];
typedef char v9x_r3d_abi_assert_state[sizeof(V9X_R3D_ABI_STATE) == 64 ? 1 : -1];
typedef char v9x_r3d_abi_assert_describe[sizeof(V9X_R3D_ABI_DESCRIBE) == 64 ? 1 : -1];
typedef char v9x_r3d_abi_assert_draw[sizeof(V9X_R3D_ABI_DRAW) == 136 ? 1 : -1];
typedef char v9x_r3d_abi_assert_clear[sizeof(V9X_R3D_ABI_CLEAR) == 48 ? 1 : -1];
typedef char v9x_r3d_abi_assert_interface[sizeof(V9X_R3D_INTERFACE) == 28 ? 1 : -1];
#endif

#endif /* VELOCITY9X_R3D_ABI_H */
