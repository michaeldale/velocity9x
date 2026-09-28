#ifndef VELOCITY9X_ATI_MACH64_ENGINE_H
#define VELOCITY9X_ATI_MACH64_ENGINE_H

#include "velocity9x/status.h"
#include "velocity9x/ati_mach64_regs.h"

#define V9X_M64_FIFO_PRE_VTB 0u
#define V9X_M64_FIFO_VTB_PLUS 1u

typedef v9x_u32 (*v9x_m64_read_fn)(void *context, v9x_u32 offset);
typedef void (*v9x_m64_write_fn)(void *context, v9x_u32 offset,
                                 v9x_u32 value);

struct v9x_m64_io {
    void *context;
    v9x_m64_read_fn read;
    v9x_m64_write_fn write;
};

struct v9x_m64_shadow_entry {
    v9x_u32 offset;
    v9x_u32 value;
};

struct v9x_m64_engine {
    struct v9x_m64_io io;
    v9x_u16 fifo_model;
    v9x_u16 quarantined;
    v9x_u32 fifo_cached;
    v9x_u32 fifo_reads;
    v9x_u32 register_writes;
    v9x_u32 fifo_timeouts;
    v9x_u32 idle_timeouts;
    v9x_u32 reset_count;
    v9x_u32 shadow_count;
    struct v9x_m64_shadow_entry shadow[V9X_M64_SHADOW_ENTRIES];
};

struct v9x_m64_fill {
    v9x_u32 vram_bytes;
    v9x_u32 target_offset;
    v9x_u32 target_pitch_bytes;
    v9x_u32 target_width;
    v9x_u32 target_height;
    v9x_u32 left;
    v9x_u32 top;
    v9x_u32 right;
    v9x_u32 bottom;
    v9x_u32 color;
};

struct v9x_m64_copy {
    v9x_u32 vram_bytes;
    v9x_u32 source_offset;
    v9x_u32 source_pitch_bytes;
    v9x_u32 source_width;
    v9x_u32 source_height;
    v9x_u32 destination_offset;
    v9x_u32 destination_pitch_bytes;
    v9x_u32 destination_width;
    v9x_u32 destination_height;
    v9x_u32 source_left;
    v9x_u32 source_top;
    v9x_u32 destination_left;
    v9x_u32 destination_top;
    v9x_u32 width;
    v9x_u32 height;
};

struct v9x_m64_point {
    v9x_u32 x;
    v9x_u32 y;
};

struct v9x_m64_flat_triangle {
    struct v9x_m64_point vertex[3];
    v9x_u32 color;
};

struct v9x_m64_gouraud_triangle {
    struct v9x_m64_point vertex[3];
    v9x_u32 color[3];
};

struct v9x_m64_depth_triangle {
    struct v9x_m64_point vertex[3];
    v9x_u32 color;
    v9x_u16 depth[3];
};

struct v9x_m64_textured_triangle {
    struct v9x_m64_point vertex[3];
    float s[3];
    float t[3];
    float w[3];
    v9x_u32 color;
};

struct v9x_m64_flat_state {
    v9x_u32 vram_bytes;
    v9x_u32 target_offset;
    v9x_u32 target_pitch_bytes;
    v9x_u32 target_width;
    v9x_u32 target_height;
    v9x_u32 scissor_left;
    v9x_u32 scissor_top;
    v9x_u32 scissor_right;
    v9x_u32 scissor_bottom;
};

struct v9x_m64_depth_state {
    struct v9x_m64_flat_state color;
    v9x_u32 depth_offset;
    v9x_u32 depth_pitch_bytes;
    v9x_u32 depth_width;
    v9x_u32 depth_height;
    v9x_u32 compare;
    v9x_u32 write_enable;
};

struct v9x_m64_texture_state {
    struct v9x_m64_flat_state color;
    v9x_u32 texture_offset;
    v9x_u32 texture_pitch_bytes;
    v9x_u32 texture_width;
    v9x_u32 texture_height;
    /* Zero preserves the first physical gate: clamp and nearest. */
    v9x_u32 wrap_s;
    v9x_u32 wrap_t;
    v9x_u32 bilinear_min;
    v9x_u32 bilinear_mag;
    /* V9X_M64_TEXTURE_FORMAT_*; zero remains RGB565. */
    v9x_u32 texture_format;
};

v9x_status v9x_m64_engine_init(struct v9x_m64_engine *engine,
                               const struct v9x_m64_io *io,
                               v9x_u16 fifo_model);
v9x_u32 v9x_m64_fifo_free(v9x_u16 fifo_model, v9x_u32 status);
v9x_status v9x_m64_reserve(struct v9x_m64_engine *engine,
                           v9x_u32 entries, v9x_u32 spin_limit);
v9x_status v9x_m64_emit_batch(struct v9x_m64_engine *engine,
                              const v9x_u32 *offsets,
                              const v9x_u32 *values,
                              v9x_u32 count, v9x_u32 spin_limit);
v9x_status v9x_m64_wait_idle(struct v9x_m64_engine *engine,
                             v9x_u32 spin_limit);
v9x_status v9x_m64_cpu_read_barrier(struct v9x_m64_engine *engine,
                                    v9x_u32 spin_limit);
v9x_status v9x_m64_reset_replay(struct v9x_m64_engine *engine,
                                v9x_u32 spin_limit);
v9x_status v9x_m64_build_fill(const struct v9x_m64_fill *fill,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_m64_build_fill_origin_repair(
                              const struct v9x_m64_fill *fill,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_m64_build_copy(const struct v9x_m64_copy *copy,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_m64_build_flat_triangle(
                              const struct v9x_m64_flat_triangle *triangle,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_m64_build_gouraud_triangle(
                              const struct v9x_m64_gouraud_triangle *triangle,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_m64_build_depth_triangle(
                              const struct v9x_m64_depth_triangle *triangle,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_m64_build_textured_triangle(
                              const struct v9x_m64_textured_triangle *triangle,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_m64_build_flat_state(
                              const struct v9x_m64_flat_state *state,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_m64_build_gouraud_state(
                              const struct v9x_m64_flat_state *state,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_m64_build_depth_state(
                              const struct v9x_m64_depth_state *state,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
v9x_status v9x_m64_build_texture_state(
                              const struct v9x_m64_texture_state *state,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
/* `compare` uses the Direct3D D3DCMP_* numbering, 1 through 8. */
v9x_status v9x_m64_build_z_control(v9x_u32 compare,
                                   v9x_u32 write_enable,
                                   v9x_u32 *value);

#endif
