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
    /* Per register, its shadow entry's index plus one, or zero: every
     * write updates the shadow, and a search of it cost a 3D batch more
     * than its FIFO writes (2026-09-29). */
    v9x_u8 shadow_slot[V9X_M64_REGISTER_SLOTS];
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
    /*
     * A mip chain: level_count levels, level 0 the texture above and
     * level_offsets[0] equal to texture_offset.  Zero or one is a single
     * level, sampled with MIP_MAP_DISABLE.  Level n is square, edge >> n,
     * at edge*2 bytes a row and an offset of its own on
     * V9X_M64_TEXTURE_BASE_ALIGN; that
     * pitch rule is the hypothesis the HAL probe's mip scenes measure.
     */
    v9x_u32 level_count;
    v9x_u32 level_offsets[V9X_M64_TEXTURE_LEVELS_MAX];
};

v9x_status v9x_m64_engine_init(struct v9x_m64_engine *engine,
                               const struct v9x_m64_io *io,
                               v9x_u16 fifo_model);
v9x_u32 v9x_m64_fifo_free(v9x_u16 fifo_model, v9x_u32 status);

/*
 * The engine class CONFIG_CHIP_ID names, from its low word (the PCI device
 * id's ASCII pair): the Rage II class - Rage II, II+, IIC, LT, LT-G - with
 * no setup engine and the pre-VTB FIFO, which eng_mach64.c drives as
 * ATI_RAGE2; or the Rage Pro class - Rage Pro, LT Pro, XL, XC, the
 * Mobility parts - with the setup engine and the VTB+ FIFO, driven as
 * ATI_MACH64. Measured on one part of each (GW, LM); the rest are the
 * aliases the manifest binds, unmeasured.
 */
#define V9X_M64_CHIP_CLASS_NONE      0ul
#define V9X_M64_CHIP_CLASS_RAGE2     1ul
#define V9X_M64_CHIP_CLASS_RAGE_PRO  2ul
v9x_u32 v9x_m64_chip_class(v9x_u32 config_chip_id);
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
/* The 3D pixel pipe out of a 2D operation's way: alpha test, Z and
 * SCALE_3D_CNTL to zero, as X.Org's return from 3D does. Emitted ahead of
 * every engine fill; a 3D draw writes its full state itself. */
v9x_status v9x_m64_build_2d_mode(v9x_u32 *offsets, v9x_u32 *values,
                                 v9x_u32 capacity, v9x_u32 *written);
/* The same for a Rage II-class part (264GT2C): Z and SCALE_3D_CNTL only.
 * ALPHA_TST_CNTL is a Rage Pro register (xf86-video-mach64 atiregs.h,
 * "GTPro"), so on this chip a write to it lands on nothing known. */
v9x_status v9x_m64_build_2d_mode_gt(v9x_u32 *offsets, v9x_u32 *values,
                                    v9x_u32 capacity, v9x_u32 *written);
/* The 2D engine's known state on the Rage II class, written once when the
 * engine is first validated: what atyfb's aty_init_engine writes, less
 * the registers the fill and copy builders set on every operation. Without
 * it the engine runs with its power-on contents; on A8U4I5 those included
 * SRC_CNTL 7EA3 (source pattern, rotation and block write enabled), and the
 * first screen copy hung the machine. */
v9x_status v9x_m64_build_engine_init_gt(v9x_u32 *offsets, v9x_u32 *values,
                                        v9x_u32 capacity, v9x_u32 *written);
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
/* `compare` uses D3DCMP_* numbering; source_vertex is zero for texel alpha. */
v9x_status v9x_m64_build_alpha_control(v9x_u32 compare,
                                       v9x_u32 reference,
                                       v9x_u32 source_vertex,
                                       v9x_u32 *value);
/* Factors use the Direct3D D3DBLEND_* numbering.  The returned field enables
 * ADD blending and is ORed into a SCALE_3D_CNTL value after clearing the
 * source, destination, alpha-enable and saturate fields. */
v9x_status v9x_m64_build_blend_control(v9x_u32 source_factor,
                                       v9x_u32 destination_factor,
                                       v9x_u32 *value);

/*
 * The draw-acceptance policy: what the Mobility-M 3D path may emit, decided
 * before any FIFO reservation or register write.  Numbering is Direct3D's,
 * which the neutral V9X_R3D_* constants share: compare 1-8, D3DBLEND_*,
 * D3DTBLEND_*, D3DTADDRESS_*, D3DFILTER_*, shade 1 flat / 2 Gouraud, target
 * 1 RGB565 / 2 XRGB1555.  texture_format is V9X_M64_TEXTURE_FORMAT_* and is
 * read only when `textured` is non-zero.  The scissor is half-open.
 */
struct v9x_m64_draw_request {
    v9x_u32 target_format;
    v9x_u32 target_width;
    v9x_u32 target_height;
    v9x_u32 scissor_left;
    v9x_u32 scissor_top;
    v9x_u32 scissor_right;
    v9x_u32 scissor_bottom;
    v9x_u32 write_mask;
    v9x_u32 shade_mode;
    v9x_u32 depth_enable;
    v9x_u32 depth_bits;
    v9x_u32 depth_func;
    v9x_u32 depth_write;
    v9x_u32 textured;
    v9x_u32 texture_format;
    v9x_u32 texture_width;
    v9x_u32 texture_height;
    v9x_u32 texture_levels;
    v9x_u32 texture_min_filter;
    v9x_u32 texture_mag_filter;
    v9x_u32 texture_address;
    v9x_u32 texture_wrap_u;
    v9x_u32 texture_wrap_v;
    v9x_u32 texture_op;
    v9x_u32 blend_enable;
    v9x_u32 src_blend;
    v9x_u32 dst_blend;
    v9x_u32 alpha_test_enable;
    v9x_u32 alpha_func;
    v9x_u32 alpha_ref;
    v9x_u32 fog_enable;
    v9x_u32 specular_enable;
    v9x_u32 color_key_enable;
    v9x_u32 alpha_force;
    /* Non-zero when every vertex of the batch has alpha 255, so a factor
     * of the vertex alpha is one. Zero when unknown. */
    v9x_u32 vertex_alpha_opaque;
    /* The least vertex alpha of the batch, 0..255; zero when unknown. */
    v9x_u32 vertex_alpha_min;
    /*
     * A second texture (the render interface's texture1), read only when
     * `composite` is non-zero. Its filters and address take the numbering
     * above; its combine takes the render interface's V9X_R3D_ABI_COLOROP_*
     * and _ALPHAOP_* numbers, since Direct3D's single texture op has no
     * second unit to describe.
     */
    v9x_u32 composite;
    v9x_u32 composite_format;
    v9x_u32 composite_width;
    v9x_u32 composite_height;
    v9x_u32 composite_min_filter;
    v9x_u32 composite_mag_filter;
    v9x_u32 composite_address;
    v9x_u32 composite_color_op;
    v9x_u32 composite_alpha_op;
};

/* What an accepted draw emits for the texture stage, so the draw path
 * cannot reach a different conclusion from the policy.  light_fcn is a
 * V9X_M64_TEX_LIGHT_FCN_* field and texture_alpha is V9X_M64_TEX_MAP_AEN or
 * zero.  Both are zero for an untextured or refused draw. */
struct v9x_m64_draw_decision {
    v9x_u32 light_fcn;
    v9x_u32 texture_alpha;
    /* Non-zero: the request's alpha test can discard nothing and is not
     * to be sent (no texel alpha, and every vertex alpha passes). */
    v9x_u32 alpha_test_dropped;
};

#define V9X_M64_REFUSE_NONE             0ul
#define V9X_M64_REFUSE_ARGUMENT         1ul
#define V9X_M64_REFUSE_TARGET_FORMAT    2ul
#define V9X_M64_REFUSE_SCISSOR          3ul
#define V9X_M64_REFUSE_WRITE_MASK       4ul
#define V9X_M64_REFUSE_SHADE            5ul
#define V9X_M64_REFUSE_DEPTH            6ul
#define V9X_M64_REFUSE_TEXTURE_FORMAT   7ul
#define V9X_M64_REFUSE_TEXTURE_SHAPE    8ul
#define V9X_M64_REFUSE_TEXTURE_MIP      9ul
#define V9X_M64_REFUSE_TEXTURE_FILTER   10ul
#define V9X_M64_REFUSE_TEXTURE_ADDRESS  11ul
#define V9X_M64_REFUSE_TEXTURE_OP       12ul
#define V9X_M64_REFUSE_BLEND_FACTOR     13ul
#define V9X_M64_REFUSE_ALPHA_TEST       14ul
#define V9X_M64_REFUSE_FOG_WITH_BLEND   15ul
#define V9X_M64_REFUSE_FOG_WITH_TEXTURE 16ul
#define V9X_M64_REFUSE_SPECULAR         17ul
#define V9X_M64_REFUSE_COLOR_KEY        18ul
#define V9X_M64_REFUSE_ALPHA_FORCE      19ul
#define V9X_M64_REFUSE_COMPOSITE        20ul

/* Returns V9X_M64_REFUSE_NONE and fills `decision` when the draw is inside
 * the measured boundary, otherwise the first refusal reason.  Passive: it
 * counts nothing and touches no hardware. */
v9x_u32 v9x_m64_check_draw(const struct v9x_m64_draw_request *request,
                           struct v9x_m64_draw_decision *decision);

/*
 * One accepted draw's complete engine state, for v9x_m64_build_draw_state.
 * `color` is the target and scissor, as for the flat state.  The depth and
 * texture fields are read only when enabled; compares and factors use the
 * D3D numbering; fog_color is RGB in its low 24 bits.  The request must
 * already have passed v9x_m64_check_draw, whose decision is passed beside it.
 */
struct v9x_m64_draw_state {
    struct v9x_m64_flat_state color;
    v9x_u32 depth_enable;
    v9x_u32 depth_offset;
    v9x_u32 depth_pitch_bytes;
    v9x_u32 depth_compare;
    v9x_u32 depth_write;
    v9x_u32 textured;
    v9x_u32 texture_offset;
    v9x_u32 texture_pitch_bytes;
    v9x_u32 texture_width;
    v9x_u32 texture_height;
    v9x_u32 texture_format;
    v9x_u32 wrap_s;
    v9x_u32 wrap_t;
    v9x_u32 bilinear_min;
    v9x_u32 bilinear_mag;
    /* As in struct v9x_m64_texture_state. */
    v9x_u32 level_count;
    v9x_u32 level_offsets[V9X_M64_TEXTURE_LEVELS_MAX];
    v9x_u32 blend_enable;
    v9x_u32 src_blend;
    v9x_u32 dst_blend;
    v9x_u32 alpha_test_enable;
    v9x_u32 alpha_compare;
    v9x_u32 alpha_reference;
    v9x_u32 fog_enable;
    v9x_u32 fog_color;
    /* Add the vertex specular colour (ALPHA_TST_CNTL SPECULAR_LIGHT_EN). */
    v9x_u32 specular_enable;
    /*
     * A second texture modulated with the first (TEX_CNTL
     * TEXTURE_COMPOSITE), read only when `composite` is non-zero and the
     * draw is textured. One level, edge*2 bytes a row, on the base
     * alignment: SECONDARY_TEX_OFF is a single offset. The first texture
     * keeps its chain and selects a level, but is not trilinear
     * (bilinear_min at most 1): the composite takes that blend function.
     */
    v9x_u32 composite;
    v9x_u32 composite_offset;
    v9x_u32 composite_pitch_bytes;
    v9x_u32 composite_width;
    v9x_u32 composite_height;
    v9x_u32 composite_format;
    v9x_u32 composite_wrap_s;
    v9x_u32 composite_wrap_t;
    v9x_u32 composite_bilinear_min;
    v9x_u32 composite_bilinear_mag;
};

/*
 * One screen-space vertex.  x and y are 14.2 fixed point, the setup
 * engine's own form, and z is 0..65535.  They arrive as integers because
 * this file is also built by the MSVC host pass, and Open Watcom lowers a
 * float-to-int cast to a runtime helper the nodefaultlibs HAL cannot link;
 * the engine converts with the HAL's inline fistp.  rhw is D3D's 1/w, s and
 * t are texture coordinates before perspective division, and the specular
 * alpha is the fog factor.
 */
struct v9x_m64_setup_vertex {
    v9x_u32 x_fixed;
    v9x_u32 y_fixed;
    v9x_u32 z16;
    float rhw;
    float s;
    float t;
    v9x_u32 argb;
    v9x_u32 specular;
    /* The second texture's coordinates, read only under
     * V9X_M64_SETUP_SECONDARY; its W is rhw, as Mesa's driver sends it. */
    float s1;
    float t1;
};

/* A candidate cache of the setup engine's three vertex slots.  It is
 * caller-owned and deliberately starts invalid; only
 * v9x_m64_build_reused_setup makes a slot known. */
struct v9x_m64_setup_slot {
    v9x_u32 word[6];
    v9x_u32 specular;
    v9x_u32 known;
    /* SECONDARY_S, _T and _W, compared only under V9X_M64_SETUP_SECONDARY. */
    v9x_u32 secondary[3];
};

/* The largest pixel coordinate a setup packet takes: the flat state's
 * 4096-pixel target limit, which also keeps the 14.2 cross product inside
 * a signed 32-bit integer. */
#define V9X_M64_SETUP_COORD_MAX_FIXED (4096ul * 4ul)

v9x_status v9x_m64_build_draw_state(
                              const struct v9x_m64_draw_state *state,
                              const struct v9x_m64_draw_decision *decision,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written);
/* V9X_STATUS_UNSUPPORTED with nothing written is a zero-area triangle,
 * which draws no pixel; V9X_STATUS_INVALID_STATE is a textured triangle
 * whose W, S or T is non-finite or W not positive, which cannot be drawn.
 * Either is skipped alone; every other failure is a caller error. `fog` is a
 * set of V9X_M64_SETUP_* flags: either sends the three specular words
 * first, FOG with the vertex specular alpha and SPECULAR with its RGB.
 * SECONDARY sends each vertex's second-texture S, T and W after its own
 * six words, Mesa's order, and premultiplies every S and T by W for the
 * composite's TEX_ST_DIRECT; the triangle must be textured. */
#define V9X_M64_SETUP_FOG       1ul
#define V9X_M64_SETUP_SPECULAR  2ul
#define V9X_M64_SETUP_SECONDARY 4ul
v9x_status v9x_m64_build_setup(const struct v9x_m64_setup_vertex *vertex,
                               v9x_u32 textured, v9x_u32 fog,
                               v9x_u32 *offsets, v9x_u32 *values,
                               v9x_u32 capacity, v9x_u32 *written);
/* Build one setup packet while retaining equal vertices already resident in
 * the three hardware slots.  The area follows the resulting slot order.
 * The Gateway retains the slots within one draw batch (docs/decisions/
 * 2026-09-29-mach64-vertex-register-reuse-physical.md); nothing measured
 * retention across another engine operation, so a caller starts each batch
 * with every slot unknown. */
v9x_status v9x_m64_build_reused_setup(
                               const struct v9x_m64_setup_vertex *vertex,
                               v9x_u32 textured, v9x_u32 fog,
                               struct v9x_m64_setup_slot *slot,
                               v9x_u32 *offsets, v9x_u32 *values,
                               v9x_u32 capacity, v9x_u32 *written);

#endif
