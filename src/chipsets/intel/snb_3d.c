#include "velocity9x/intel_snb_3d.h"

/*
 * Sandy Bridge (Gen6) 3D command and state builders. Every opcode, length
 * and field position is Mesa 24.0.9's gen6.xml (or gen5/gen4.xml, which it
 * imports); the rules each builder enforces cite where they come from.
 * See intel_snb_3d.h for what each one emits.
 */

#define V9X_SNB_GFXPIPE      ((v9x_u32)0x60000000ul) /* type 3 at 31:29 */
#define V9X_SNB_DW0_FIELDS   ((v9x_u32)0x0000ff00ul)
#define V9X_SNB_VARIABLE     ((v9x_u8)0u)

struct v9x_snb_command {
    v9x_u8 subtype;
    v9x_u8 opcode;
    v9x_u8 subop;
    v9x_u8 total;   /* fixed total dwords, or V9X_SNB_VARIABLE */
};

/*
 * Index = V9X_SNB_CMD_*. Subtype, opcode and sub-opcode are each
 * command's DW0 defaults in gen6.xml (CLEAR_PARAMS from gen5.xml,
 * DRAWING_RECTANGLE, VERTEX_BUFFERS and ELEMENTS from gen4.xml); total is
 * the DWord Length default + 2. PIPELINE_SELECT is a single dword.
 */
static const struct v9x_snb_command v9x_snb_commands[V9X_SNB_CMD_COUNT] = {
    { 1u, 1u, 0x04u, 1u },                 /* PIPELINE_SELECT */
    { 0u, 1u, 0x01u, 10u },                /* STATE_BASE_ADDRESS */
    { 3u, 0u, 0x05u, 3u },                 /* 3DSTATE_URB */
    { 3u, 0u, 0x10u, 6u },                 /* 3DSTATE_VS */
    { 3u, 0u, 0x11u, 7u },                 /* 3DSTATE_GS */
    { 3u, 0u, 0x12u, 4u },                 /* 3DSTATE_CLIP */
    { 3u, 0u, 0x13u, 20u },                /* 3DSTATE_SF */
    { 3u, 0u, 0x14u, 9u },                 /* 3DSTATE_WM */
    { 3u, 0u, 0x15u, 5u },                 /* 3DSTATE_CONSTANT_VS */
    { 3u, 0u, 0x16u, 5u },                 /* 3DSTATE_CONSTANT_GS */
    { 3u, 0u, 0x17u, 5u },                 /* 3DSTATE_CONSTANT_PS */
    { 3u, 0u, 0x01u, 4u },                 /* BINDING_TABLE_POINTERS */
    { 3u, 0u, 0x02u, 4u },                 /* SAMPLER_STATE_POINTERS */
    { 3u, 0u, 0x0du, 4u },                 /* VIEWPORT_STATE_POINTERS */
    { 3u, 0u, 0x0eu, 4u },                 /* CC_STATE_POINTERS */
    { 3u, 0u, 0x0fu, 2u },                 /* SCISSOR_STATE_POINTERS */
    { 3u, 0u, 0x18u, 2u },                 /* 3DSTATE_SAMPLE_MASK */
    { 3u, 0u, 0x08u, V9X_SNB_VARIABLE },   /* 3DSTATE_VERTEX_BUFFERS */
    { 3u, 0u, 0x09u, V9X_SNB_VARIABLE },   /* 3DSTATE_VERTEX_ELEMENTS */
    { 3u, 1u, 0x00u, 4u },                 /* DRAWING_RECTANGLE */
    { 3u, 1u, 0x05u, 7u },                 /* 3DSTATE_DEPTH_BUFFER */
    { 3u, 1u, 0x0du, 3u },                 /* 3DSTATE_MULTISAMPLE */
    { 3u, 1u, 0x0eu, 3u },                 /* 3DSTATE_STENCIL_BUFFER */
    { 3u, 1u, 0x0fu, 3u },                 /* 3DSTATE_HIER_DEPTH_BUFFER */
    { 3u, 1u, 0x10u, 2u },                 /* 3DSTATE_CLEAR_PARAMS */
    { 3u, 3u, 0x00u, 6u },                 /* 3DPRIMITIVE */
    { 3u, 2u, 0x00u, 5u }                  /* PIPE_CONTROL */
};

v9x_status v9x_intel_snb_3d_header(v9x_u16 command, v9x_u32 total_dwords,
                                   v9x_u32 dw0_bits, v9x_u32 *header)
{
    const struct v9x_snb_command *cmd;

    if (header == 0 || command >= V9X_SNB_CMD_COUNT ||
        (dw0_bits & ~V9X_SNB_DW0_FIELDS) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    cmd = &v9x_snb_commands[command];
    if (cmd->total != V9X_SNB_VARIABLE && total_dwords != cmd->total) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *header = V9X_SNB_GFXPIPE | ((v9x_u32)cmd->subtype << 27) |
              ((v9x_u32)cmd->opcode << 24) | ((v9x_u32)cmd->subop << 16) |
              dw0_bits;
    if (cmd->total == 1u) {
        return V9X_STATUS_OK;
    }
    if (total_dwords < 2ul || total_dwords - 2ul > 0xfful) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *header |= total_dwords - 2ul;
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_snb_3d_emit(struct v9x_hsw_batch *batch,
                                 v9x_u16 command, v9x_u32 dw0_bits,
                                 const v9x_u32 *payload, v9x_u32 count)
{
    v9x_u32 header;
    v9x_u32 *at;
    v9x_u32 index;

    if (batch == 0 || command >= V9X_SNB_CMD_COUNT ||
        v9x_snb_commands[command].total == V9X_SNB_VARIABLE ||
        (count != 0ul && payload == 0)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_intel_snb_3d_header(command, count + 1ul, dw0_bits, &header) !=
        V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Whole or nothing: a batch too small is marked and left as it was. */
    if (batch->dw == 0 || batch->used > batch->capacity ||
        count + 1ul > batch->capacity - batch->used) {
        batch->overflow = 1u;
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at = batch->dw + batch->used;
    at[0] = header;
    for (index = 0ul; index < count; ++index) {
        at[index + 1ul] = payload[index];
    }
    batch->used += count + 1ul;
    return V9X_STATUS_OK;
}

static void v9x_snb_zero(v9x_u32 *dw, v9x_u32 count)
{
    v9x_u32 index;

    for (index = 0ul; index < count; ++index) {
        dw[index] = 0ul;
    }
}

/*
 * PIPE_CONTROL. The CS-stall rule is the SNB PRM's, as crocus enforces it
 * (crocus_state.c, the "CS Stall ... must be set with at least one of"
 * list): RT flush, depth flush, stall at scoreboard, depth stall or a
 * post-sync operation. Depth stall with an RT or depth-cache flush is
 * refused because crocus refuses it before Haswell. Refusing, not
 * repairing: every caller here is ours, and a silently added bit would
 * hide a sequence that needs rethinking.
 */
#define V9X_SNB_PC_POST_SYNC_MASK ((v9x_u32)0x0000c000ul)
#define V9X_SNB_PC_ALLOWED ((v9x_u32)0x001fffdful)
#define V9X_SNB_PC_CS_COMPANIONS (V9X_HSW_PC_RT_CACHE_FLUSH | \
                                  V9X_HSW_PC_DEPTH_CACHE_FLUSH | \
                                  V9X_HSW_PC_STALL_AT_SCOREBOARD | \
                                  V9X_HSW_PC_DEPTH_STALL | \
                                  V9X_SNB_PC_POST_SYNC_MASK)

v9x_status v9x_intel_snb_pipe_control(struct v9x_hsw_batch *batch,
                                      v9x_u32 flags, v9x_u32 address,
                                      v9x_u32 data)
{
    v9x_u32 payload[4];
    v9x_u32 dw2_type = 0ul;

    if ((flags & V9X_HSW_PC_GGTT) != 0ul) {
        flags &= ~V9X_HSW_PC_GGTT;
        dw2_type = V9X_SNB_PC_GGTT_DW2;
    }
    if ((flags & ~V9X_SNB_PC_ALLOWED) != 0ul ||
        (flags & V9X_HSW_PC_DC_FLUSH) != 0ul || (address & 7ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((flags & V9X_HSW_PC_CS_STALL) != 0ul &&
        (flags & V9X_SNB_PC_CS_COMPANIONS) == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((flags & V9X_HSW_PC_DEPTH_STALL) != 0ul &&
        (flags & (V9X_HSW_PC_RT_CACHE_FLUSH |
                  V9X_HSW_PC_DEPTH_CACHE_FLUSH)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = flags;
    payload[1] = address | dw2_type;
    payload[2] = data;
    payload[3] = 0ul;
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_PIPE_CONTROL, 0ul,
                                 payload, 4ul);
}

v9x_status v9x_intel_snb_post_sync_nonzero(struct v9x_hsw_batch *batch,
                                           v9x_u32 scratch_address)
{
    v9x_status status;

    status = v9x_intel_snb_pipe_control(batch,
                                        V9X_HSW_PC_CS_STALL |
                                        V9X_HSW_PC_STALL_AT_SCOREBOARD,
                                        0ul, 0ul);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    return v9x_intel_snb_pipe_control(batch,
                                      V9X_HSW_PC_WRITE_IMMEDIATE |
                                      V9X_HSW_PC_GGTT,
                                      scratch_address, 0ul);
}

/* 3DSTATE_URB DW1: VS URB Entry Allocation Size 23:16 (rows - 1), VS
 * Number of URB Entries 15:0; DW2 the GS's, 0. The multiple of 4 is
 * crocus's (gfx6_upload_urb asserts it, quoting the PRM). */
v9x_status v9x_intel_snb_urb(struct v9x_hsw_batch *batch,
                             v9x_u16 vs_entries, v9x_u8 vs_rows)
{
    v9x_u32 payload[2];

    if ((vs_entries & 3u) != 0u || vs_entries < V9X_SNB_URB_VS_MIN_ENTRIES ||
        vs_entries > V9X_SNB_URB_VS_MAX_ENTRIES || vs_rows == 0u ||
        vs_rows > V9X_SNB_URB_MAX_ROWS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = ((v9x_u32)(vs_rows - 1u) << 16) | (v9x_u32)vs_entries;
    payload[1] = 0ul;
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_URB, 0ul, payload, 2ul);
}

v9x_status v9x_intel_snb_vs_disabled(struct v9x_hsw_batch *batch)
{
    v9x_u32 payload[5];

    v9x_snb_zero(payload, 5ul);
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_VS, 0ul, payload, 5ul);
}

/* 3DSTATE_GS DW4 Dispatch GRF Start Register For URB Data 3:0; DW5
 * Statistics Enable 10, Rendering Enabled 8 (crocus_state.c, the GFX_VER
 * 5/6 "RenderingEnabled" with no GS program); DW6 Enable 15 clear. */
#define V9X_SNB_GS_DW5_OFF ((v9x_u32)0x00000500ul)

v9x_status v9x_intel_snb_gs_disabled(struct v9x_hsw_batch *batch)
{
    v9x_u32 payload[6];

    v9x_snb_zero(payload, 6ul);
    payload[3] = 1ul;
    payload[4] = V9X_SNB_GS_DW5_OFF;
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_GS, 0ul, payload, 6ul);
}

/* The Gen7 path's clip word for pre-transformed vertices; gen6.xml has
 * Clip Enable 31, Clip Mode 15:13 and Perspective Divide Disable 9 at the
 * same positions. */
#define V9X_SNB_CLIP_SCREEN_DW2 ((v9x_u32)0x80008200ul)

v9x_status v9x_intel_snb_clip_screen(struct v9x_hsw_batch *batch)
{
    v9x_u32 payload[3];

    payload[0] = 0ul;
    payload[1] = V9X_SNB_CLIP_SCREEN_DW2;
    payload[2] = 0ul;
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_CLIP, 0ul, payload, 3ul);
}

/* Cull Mode (DW3 30:29): CULLMODE_NONE 1, CULLMODE_BACK 3; Front Winding
 * (DW2 bit 0): CW 0, CCW 1. The Gen7 SF's values, moved one dword. */
#define V9X_SNB_SF_CULL_NONE ((v9x_u32)0x20000000ul)
#define V9X_SNB_SF_CULL_BACK ((v9x_u32)0x60000000ul)
#define V9X_SNB_SF_FRONT_CCW ((v9x_u32)0x00000001ul)
#define V9X_SNB_SF_PAYLOAD   19ul

v9x_status v9x_intel_snb_sf(struct v9x_hsw_batch *batch, v9x_u8 outputs,
                            v9x_u8 read_offset, v9x_u8 read_length,
                            v9x_u8 cull, v9x_u32 constant_mask)
{
    v9x_u32 payload[V9X_SNB_SF_PAYLOAD];

    if (outputs > 32u || read_offset > 0x3fu || read_length == 0u ||
        read_length > 16u || cull > V9X_HSW_DRAW_CULL_CCW) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_snb_zero(payload, V9X_SNB_SF_PAYLOAD);
    payload[0] = ((v9x_u32)outputs << 22) | ((v9x_u32)read_length << 11) |
                 ((v9x_u32)read_offset << 4);
    payload[2] = V9X_SNB_SF_CULL_NONE;
    if (cull == V9X_HSW_DRAW_CULL_CW) {
        payload[1] = V9X_SNB_SF_FRONT_CCW;
        payload[2] = V9X_SNB_SF_CULL_BACK;
    } else if (cull == V9X_HSW_DRAW_CULL_CCW) {
        payload[2] = V9X_SNB_SF_CULL_BACK;
    }
    payload[16] = constant_mask;
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_SF, 0ul, payload,
                                 V9X_SNB_SF_PAYLOAD);
}

/* 3DSTATE_WM DW5: Maximum Number of Threads 31:25 (count - 1), Pixel
 * Shader Kills Pixel 22, Thread Dispatch Enable 19, 8 Pixel Dispatch
 * Enable 0. */
#define V9X_SNB_WM_KILL_PIXEL ((v9x_u32)0x00400000ul)
#define V9X_SNB_WM_DISPATCH   ((v9x_u32)0x00080000ul)
#define V9X_SNB_WM_SIMD8      ((v9x_u32)0x00000001ul)

v9x_status v9x_intel_snb_wm(struct v9x_hsw_batch *batch, v9x_u32 kernel,
                            v9x_u8 binding_entries, v9x_u8 samplers,
                            v9x_u8 attributes, v9x_u8 grf_start,
                            v9x_u8 barycentric, v9x_u8 kills)
{
    v9x_u32 payload[8];

    if ((kernel & 0x3ful) != 0ul || samplers > 4u || attributes > 0x3fu ||
        grf_start > 0x7fu || barycentric > 0x3fu) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_snb_zero(payload, 8ul);
    payload[0] = kernel;
    payload[1] = ((v9x_u32)samplers << 27) |
                 ((v9x_u32)binding_entries << 18);
    payload[3] = (v9x_u32)grf_start << 16;
    payload[4] = ((V9X_SNB_WM_MAX_THREADS - 1ul) << 25) |
                 V9X_SNB_WM_DISPATCH | V9X_SNB_WM_SIMD8;
    if (kills != 0u) {
        payload[4] |= V9X_SNB_WM_KILL_PIXEL;
    }
    payload[5] = ((v9x_u32)attributes << 20) | ((v9x_u32)barycentric << 10);
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_WM, 0ul, payload, 8ul);
}

/* DW0 Buffer 0 Valid 12; the body's buffer 0: pointer 31:5, Read Length
 * 4:0 holding the count - 1 (crocus gfx6 constant upload). */
#define V9X_SNB_CONSTANT_BUFFER0_VALID ((v9x_u32)0x00001000ul)
#define V9X_SNB_CONSTANT_MAX_REGISTERS ((v9x_u8)32u)

v9x_status v9x_intel_snb_constant_ps(struct v9x_hsw_batch *batch,
                                     v9x_u32 pointer, v9x_u8 registers)
{
    v9x_u32 payload[4];
    v9x_u32 valid = 0ul;

    if ((pointer & 0x1ful) != 0ul ||
        registers > V9X_SNB_CONSTANT_MAX_REGISTERS ||
        (registers == 0u && pointer != 0ul)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_snb_zero(payload, 4ul);
    if (registers != 0u) {
        valid = V9X_SNB_CONSTANT_BUFFER0_VALID;
        payload[0] = pointer | (v9x_u32)(registers - 1u);
    }
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_CONSTANT_PS, valid,
                                 payload, 4ul);
}

/* Change bits in DW0 for the PS (12) and the viewports CLIP 10, SF 11,
 * CC 12. */
#define V9X_SNB_PTRS_PS_CHANGE        ((v9x_u32)0x00001000ul)
#define V9X_SNB_PTRS_VIEWPORTS_CHANGE ((v9x_u32)0x00001c00ul)

static v9x_status v9x_snb_ps_pointer(struct v9x_hsw_batch *batch,
                                     v9x_u16 command, v9x_u32 offset)
{
    v9x_u32 payload[3];

    if ((offset & 0x1ful) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = 0ul;
    payload[1] = 0ul;
    payload[2] = offset;
    return v9x_intel_snb_3d_emit(batch, command, V9X_SNB_PTRS_PS_CHANGE,
                                 payload, 3ul);
}

v9x_status v9x_intel_snb_binding_table_ptrs_ps(struct v9x_hsw_batch *batch,
                                               v9x_u32 offset)
{
    return v9x_snb_ps_pointer(batch, V9X_SNB_CMD_BINDING_TABLE_PTRS,
                              offset);
}

v9x_status v9x_intel_snb_sampler_ptrs_ps(struct v9x_hsw_batch *batch,
                                         v9x_u32 offset)
{
    return v9x_snb_ps_pointer(batch, V9X_SNB_CMD_SAMPLER_PTRS, offset);
}

v9x_status v9x_intel_snb_viewport_ptrs(struct v9x_hsw_batch *batch,
                                       v9x_u32 clip, v9x_u32 sf,
                                       v9x_u32 cc)
{
    v9x_u32 payload[3];

    if (((clip | sf | cc) & 0x1ful) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = clip;
    payload[1] = sf;
    payload[2] = cc;
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_VIEWPORT_PTRS,
                                 V9X_SNB_PTRS_VIEWPORTS_CHANGE, payload,
                                 3ul);
}

/* Each pointer 31:6 with bit 0 its change (BLEND, DEPTH_STENCIL) or
 * valid (COLOR_CALC) flag. */
v9x_status v9x_intel_snb_cc_ptrs(struct v9x_hsw_batch *batch,
                                 v9x_u32 blend, v9x_u32 depth_stencil,
                                 v9x_u32 color_calc)
{
    v9x_u32 payload[3];

    if (((blend | depth_stencil | color_calc) & 0x3ful) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = blend | 1ul;
    payload[1] = depth_stencil | 1ul;
    payload[2] = color_calc | 1ul;
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_CC_PTRS, 0ul, payload,
                                 3ul);
}

/* 3DSTATE_DEPTH_BUFFER DW1: Surface Type 31:29, Tiled Surface 27, Tile
 * Walk 26 (1 = Y-major), Surface Format 20:18, Pitch 16:0 (bytes - 1).
 * Bits 22 (HiZ) and 21 (separate stencil) stay 0 together, as the SNB PRM
 * requires them to agree. */
#define V9X_SNB_DEPTH_TILED_Y ((v9x_u32)0x0c000000ul)
#define V9X_SNB_DEPTH_PITCH_MAX ((v9x_u32)0x00020000ul)

v9x_status v9x_intel_snb_depth_buffer(struct v9x_hsw_batch *batch,
                                      v9x_u8 format, v9x_u32 base,
                                      v9x_u32 pitch, v9x_u16 width,
                                      v9x_u16 height)
{
    v9x_u32 payload[6];

    v9x_snb_zero(payload, 6ul);
    if (base == 0ul && width == 0u) {
        payload[0] = ((v9x_u32)V9X_HSW_SURFTYPE_NULL << 29) |
                     ((v9x_u32)V9X_HSW_DEPTH_D32_FLOAT << 18);
        return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_DEPTH_BUFFER, 0ul,
                                     payload, 6ul);
    }
    if ((format != V9X_HSW_DEPTH_D32_FLOAT &&
         format != V9X_HSW_DEPTH_D24_UNORM_X8 &&
         format != V9X_HSW_DEPTH_D16_UNORM) ||
        (base & 0xffful) != 0ul || pitch == 0ul ||
        pitch > V9X_SNB_DEPTH_PITCH_MAX || width == 0u || height == 0u ||
        width > 8192u || height > 8192u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = ((v9x_u32)V9X_HSW_SURFTYPE_2D << 29) |
                 V9X_SNB_DEPTH_TILED_Y | ((v9x_u32)format << 18) |
                 (pitch - 1ul);
    payload[1] = base;
    payload[2] = ((v9x_u32)(height - 1u) << 19) |
                 ((v9x_u32)(width - 1u) << 6);
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_DEPTH_BUFFER, 0ul,
                                 payload, 6ul);
}

v9x_status v9x_intel_snb_clear_params(struct v9x_hsw_batch *batch)
{
    v9x_u32 payload = 0ul;

    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_CLEAR_PARAMS, 0ul,
                                 &payload, 1ul);
}

/* DW1 Number of Multisamples 3:1 = 0 (one), Pixel Location 4 = 0 (centre);
 * DW2 the sample offsets, unused at one sample. */
v9x_status v9x_intel_snb_multisample(struct v9x_hsw_batch *batch)
{
    v9x_u32 payload[2];

    payload[0] = 0ul;
    payload[1] = 0ul;
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_MULTISAMPLE, 0ul,
                                 payload, 2ul);
}

/* DW0 Vertex Access Type 15 (1 = random, indexed), Primitive Topology
 * Type 14:10; DW1-5 count, start vertex, instance count, start instance,
 * base vertex. */
#define V9X_SNB_PRIM_RANDOM ((v9x_u32)0x00008000ul)

v9x_status v9x_intel_snb_3dprimitive(struct v9x_hsw_batch *batch,
                                     v9x_u8 topology, v9x_u32 count,
                                     v9x_u32 start, v9x_u8 indexed)
{
    v9x_u32 payload[5];
    v9x_u32 dw0;

    if (topology == 0u || topology > 0x1fu) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    dw0 = (v9x_u32)topology << 10;
    if (indexed != 0u) {
        dw0 |= V9X_SNB_PRIM_RANDOM;
    }
    payload[0] = count;
    payload[1] = start;
    payload[2] = 1ul;
    payload[3] = 0ul;
    payload[4] = 0ul;
    return v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_3DPRIMITIVE, dw0,
                                 payload, 5ul);
}

/* RENDER_SURFACE_STATE: DW0 Surface Type 31:29, Format 26:18; DW2 Height
 * 31:19 and Width 18:6, each - 1, MIP Count 5:2 = 0; DW3 Pitch 19:3
 * (bytes - 1), Tiled Surface 1 = 0 (linear). */
#define V9X_SNB_SURFACE_DIM_MAX   8192u
#define V9X_SNB_SURFACE_PITCH_MAX ((v9x_u32)0x00020000ul)

v9x_status v9x_intel_snb_surface_2d(v9x_u32 out[6], v9x_u16 format,
                                    v9x_u32 base, v9x_u16 width,
                                    v9x_u16 height, v9x_u32 pitch)
{
    if (out == 0 || format > 0x1ffu || width == 0u || height == 0u ||
        width > V9X_SNB_SURFACE_DIM_MAX ||
        height > V9X_SNB_SURFACE_DIM_MAX || pitch == 0ul ||
        pitch > V9X_SNB_SURFACE_PITCH_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_snb_zero(out, V9X_SNB_SURFACE_DWORDS);
    out[0] = ((v9x_u32)V9X_HSW_SURFTYPE_2D << 29) | ((v9x_u32)format << 18);
    out[1] = base;
    out[2] = ((v9x_u32)(height - 1u) << 19) | ((v9x_u32)(width - 1u) << 6);
    out[3] = (pitch - 1ul) << 3;
    return V9X_STATUS_OK;
}

#define V9X_SNB_SURFACE_MIP_MASK   ((v9x_u32)0x0000003cul)
#define V9X_SNB_SURFACE_LEVELS_MAX 14u

v9x_status v9x_intel_snb_surface_mips(v9x_u32 out[6], v9x_u8 levels)
{
    if (out == 0 || levels == 0u || levels > V9X_SNB_SURFACE_LEVELS_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    out[2] = (out[2] & ~V9X_SNB_SURFACE_MIP_MASK) |
             ((v9x_u32)(levels - 1u) << 2);
    return V9X_STATUS_OK;
}

/*
 * The sampler's surface padding (SNB PRM Vol 4 Part 1, "Surface Padding
 * Requirements", as isl_apply_surface_padding quotes it): a compressed
 * surface pads to an even row of blocks, a cube gets two more rows. The
 * array pitch before Gen7 is h0 + h1 + 11 j, always: Gen6 has no
 * Surface Array Spacing field, so a one-level cube spaces its faces the
 * same way (isl_calc_array_pitch_el_rows_gfx4_2d). j is VALIGN, 2, for
 * texels; 4 for BC blocks.
 */
#define V9X_SNB_CUBE_QPITCH_J  11ul
#define V9X_SNB_CUBE_PAD_ROWS  2ul
#define V9X_SNB_MIP_ALIGN_H    2ul
#define V9X_SNB_BC_BLOCK       4ul
#define V9X_SNB_CUBE_FACES     6ul
#define V9X_SNB_SURFTYPE_CUBE  ((v9x_u32)3ul)
#define V9X_SNB_CUBE_FACE_BITS ((v9x_u32)0x0000003ful)

static v9x_u32 v9x_snb_align(v9x_u32 value, v9x_u32 unit)
{
    return (value + unit - 1ul) & ~(unit - 1ul);
}

v9x_status v9x_intel_snb_miptree_bc(v9x_u16 width, v9x_u16 height,
                                    v9x_u8 levels, v9x_u8 block_bytes,
                                    struct v9x_hsw_miptree *tree)
{
    v9x_status status;

    status = v9x_intel_hsw_miptree_bc(width, height, levels, block_bytes,
                                      tree);
    if (status == V9X_STATUS_OK) {
        tree->rows = v9x_snb_align(tree->rows, 2ul);
    }
    return status;
}

/* bc: block_bytes per 4 x 4 block, else bytes per texel. */
static v9x_status v9x_snb_cubetree(v9x_u16 side, v9x_u8 levels, v9x_u8 unit,
                                   int bc, struct v9x_hsw_cubetree *tree)
{
    struct v9x_hsw_miptree face;
    v9x_u32 h0 = (v9x_u32)side;
    v9x_u32 h1 = h0 > 1ul ? h0 >> 1 : 1ul;
    v9x_u32 j = bc ? V9X_SNB_BC_BLOCK : V9X_SNB_MIP_ALIGN_H;
    v9x_u32 level;

    if (tree == 0 || side == 0u ||
        (bc && (h0 & (V9X_SNB_BC_BLOCK - 1ul)) != 0ul)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((bc ? v9x_intel_hsw_miptree_bc(side, side, levels, unit, &face) :
              v9x_intel_hsw_miptree(side, side, levels, unit, &face)) !=
        V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* In pixel rows, then in the tree's rows: a block is four. */
    tree->qpitch = v9x_snb_align(h0, j) + v9x_snb_align(h1, j) +
                   V9X_SNB_CUBE_QPITCH_J * j;
    if (bc) {
        tree->qpitch /= V9X_SNB_BC_BLOCK;
    }
    tree->pitch = face.pitch;
    tree->rows = (V9X_SNB_CUBE_FACES - 1ul) * tree->qpitch + face.rows +
                 V9X_SNB_CUBE_PAD_ROWS;
    if (bc) {
        tree->rows = v9x_snb_align(tree->rows, 2ul);
    }
    for (level = 0ul; level < (v9x_u32)levels; ++level) {
        tree->level_offset[level] = face.level_offset[level];
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_snb_cubetree(v9x_u16 side, v9x_u8 levels,
                                  v9x_u8 bytes_per_texel,
                                  struct v9x_hsw_cubetree *tree)
{
    return v9x_snb_cubetree(side, levels, bytes_per_texel, 0, tree);
}

v9x_status v9x_intel_snb_cubetree_bc(v9x_u16 side, v9x_u8 levels,
                                     v9x_u8 block_bytes,
                                     struct v9x_hsw_cubetree *tree)
{
    if (block_bytes != 8u && block_bytes != 16u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return v9x_snb_cubetree(side, levels, block_bytes, 1, tree);
}

v9x_status v9x_intel_snb_surface_cube(v9x_u32 out[6], v9x_u16 format,
                                      v9x_u32 base, v9x_u16 side,
                                      v9x_u32 pitch, v9x_u8 levels)
{
    v9x_status status;

    status = v9x_intel_snb_surface_2d(out, format, base, side, side, pitch);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    out[0] = (out[0] & ~((v9x_u32)7ul << 29)) |
             (V9X_SNB_SURFTYPE_CUBE << 29) | V9X_SNB_CUBE_FACE_BITS;
    return v9x_intel_snb_surface_mips(out, levels);
}

void v9x_intel_snb_sf_viewport(v9x_u32 out[8], v9x_u32 m00, v9x_u32 m11,
                               v9x_u32 m22, v9x_u32 m30, v9x_u32 m31,
                               v9x_u32 m32)
{
    out[0] = m00;
    out[1] = m11;
    out[2] = m22;
    out[3] = m30;
    out[4] = m31;
    out[5] = m32;
    out[6] = 0ul;
    out[7] = 0ul;
}

void v9x_intel_snb_clip_viewport(v9x_u32 out[4], v9x_u32 x_min,
                                 v9x_u32 x_max, v9x_u32 y_min,
                                 v9x_u32 y_max)
{
    out[0] = x_min;
    out[1] = x_max;
    out[2] = y_min;
    out[3] = y_max;
}

/* VERTEX_BUFFER_STATE (gen6.xml): index 31:26, pitch 11:0; bit 14 is
 * reserved on Gen6, where Gen7 has Address Modify Enable. */
#define V9X_SNB_VB_MAX ((v9x_u8)33u)

v9x_status v9x_intel_snb_vertex_buffer(struct v9x_hsw_batch *batch,
                                       v9x_u8 index, v9x_u16 pitch,
                                       v9x_u32 start, v9x_u32 end)
{
    v9x_u32 header;
    v9x_u32 *at;

    if (batch == 0 || index >= V9X_SNB_VB_MAX || pitch > 0x7ffu ||
        end < start) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_intel_snb_3d_header(V9X_SNB_CMD_VERTEX_BUFFERS, 5ul, 0ul,
                                &header) != V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (batch->dw == 0 || batch->used > batch->capacity ||
        5ul > batch->capacity - batch->used) {
        batch->overflow = 1u;
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at = batch->dw + batch->used;
    at[0] = header;
    at[1] = ((v9x_u32)index << 26) | (v9x_u32)pitch;
    at[2] = start;
    at[3] = end;
    at[4] = 0ul;
    batch->used += 5ul;
    return V9X_STATUS_OK;
}

/* SAMPLER_STATE (gen6.xml). Gen7's U4.8 LODs become U4.6 by dropping two
 * fraction bits; 13.0 in U4.6 is crocus's Gen6 maximum. */
#define V9X_SNB_SAMPLER_MIN_MAG_DIFFER ((v9x_u32)0x08000000ul)
#define V9X_SNB_SAMPLER_LOD_MAX        ((v9x_u32)(13ul << 6))
#define V9X_SNB_SAMPLER_ADDRESS_MODES  ((v9x_u32)0x000001fful)

v9x_status v9x_intel_snb_sampler(v9x_u32 out[4],
                                 const struct v9x_hsw_sampler *sampler)
{
    v9x_u32 min_lod;
    v9x_u32 max_lod;

    if (out == 0 || sampler == 0 || sampler->mip_filter > 3u ||
        sampler->mag_filter > 7u || sampler->min_filter > 7u ||
        sampler->min_lod > 0xfffu || sampler->max_lod > 0xfffu ||
        (sampler->border_color_pointer & 0x1ful) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    min_lod = (v9x_u32)sampler->min_lod >> 2;
    max_lod = (v9x_u32)sampler->max_lod >> 2;
    if (min_lod > V9X_SNB_SAMPLER_LOD_MAX) {
        min_lod = V9X_SNB_SAMPLER_LOD_MAX;
    }
    if (max_lod > V9X_SNB_SAMPLER_LOD_MAX) {
        max_lod = V9X_SNB_SAMPLER_LOD_MAX;
    }
    out[0] = ((v9x_u32)sampler->mip_filter << 20) |
             ((v9x_u32)sampler->mag_filter << 17) |
             ((v9x_u32)sampler->min_filter << 14);
    if (sampler->mag_filter != sampler->min_filter) {
        out[0] |= V9X_SNB_SAMPLER_MIN_MAG_DIFFER;
    }
    out[1] = (min_lod << 22) | (max_lod << 12) |
             (sampler->dw3 & V9X_SNB_SAMPLER_ADDRESS_MODES);
    out[2] = sampler->border_color_pointer;
    out[3] = sampler->dw3 & ~V9X_SNB_SAMPLER_ADDRESS_MODES;
    return V9X_STATUS_OK;
}

#define V9X_SNB_FLOAT_ONE ((v9x_u32)0x3f800000ul)

v9x_status v9x_intel_snb_init_state(v9x_u32 *image, v9x_u32 dwords)
{
    if (image == 0 || dwords < V9X_SNB_INIT_STATE_BYTES / 4ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_snb_zero(image, dwords);
    /* CC_VIEWPORT: minimum depth 0.0, maximum 1.0. */
    image[V9X_SNB_INIT_CC_VIEWPORT / 4ul + 1ul] = V9X_SNB_FLOAT_ONE;
    /* A NULL RENDER_SURFACE_STATE in Mesa's null-surface format; the
     * binding table entry is its offset from the surface state base,
     * which is the state base. */
    image[V9X_SNB_INIT_NULL_SURFACE / 4ul] =
        ((v9x_u32)V9X_HSW_SURFTYPE_NULL << 29) |
        ((v9x_u32)V9X_HSW_FMT_B8G8R8A8_UNORM << 18);
    image[V9X_SNB_INIT_BINDING_TABLE / 4ul] = V9X_SNB_INIT_NULL_SURFACE;
    return V9X_STATUS_OK;
}

/* One table command with an all-zero payload of its fixed length. */
static v9x_status v9x_snb_emit_zero(struct v9x_hsw_batch *batch,
                                    v9x_u16 command)
{
    static const v9x_u32 zero[19] = { 0ul };

    return v9x_intel_snb_3d_emit(batch, command, 0ul, zero,
                                 (v9x_u32)v9x_snb_commands[command].total -
                                     1ul);
}

/* Every RT flush and depth stall behind the post-sync non-zero pair. */
static void v9x_snb_flush(struct v9x_hsw_batch *batch, v9x_u32 flags,
                          v9x_u32 scratch)
{
    if ((flags & (V9X_HSW_PC_RT_CACHE_FLUSH |
                  V9X_HSW_PC_DEPTH_STALL)) != 0ul) {
        v9x_intel_snb_post_sync_nonzero(batch, scratch);
    }
    v9x_intel_snb_pipe_control(batch, flags, 0ul, 0ul);
}

#define V9X_SNB_PAGE_MASK      ((v9x_u32)0x00000ffful)
#define V9X_SNB_NO_BOUND       ((v9x_u32)0xfffff000ul)
#define V9X_SNB_URB_ENTRIES    ((v9x_u16)256u)
#define V9X_SNB_URB_ROWS       ((v9x_u8)1u)
#define V9X_SNB_INIT_WIDTH     ((v9x_u16)640u)
#define V9X_SNB_INIT_HEIGHT    ((v9x_u16)480u)
/* The setup data's first GRF: r0, r1, then SIMD8 perspective-pixel
 * barycentrics in r2-r3 (SNB PRM Vol 2 Part 1, 7.7.3.1). */
#define V9X_SNB_SETUP_GRF      ((v9x_u8)4u)
/* The TL vertex's VUE: header, position, diffuse, specular, two texture
 * coordinates; the attributes are read from 256-bit unit 1, two per
 * unit. */
#define V9X_SNB_TL_ELEMENTS    6u
#define V9X_SNB_TL_READ_OFFSET ((v9x_u8)1u)
#define V9X_SNB_TL_READ_LENGTH ((v9x_u8)2u)
/* Flat: diffuse and specular (attributes 0 and 1) constant. */
#define V9X_SNB_TL_FLAT_MASK   ((v9x_u32)3ul)
/* A PS 1.1 draw's wide vertex: header, position, six four-float
 * attributes, three units of two from unit 1; 128 bytes, one URB row. */
#define V9X_SNB_WIDE_ELEMENTS    8u
#define V9X_SNB_WIDE_READ_LENGTH ((v9x_u8)3u)

/* The draw's vertex elements, V9X_SNB_TL_ELEMENTS or, for the wide form,
 * V9X_SNB_WIDE_ELEMENTS; the count. */
static v9x_u32 v9x_snb_tl_elements(struct v9x_hsw_vertex_element *elements,
                                   const struct v9x_hsw_draw *draw)
{
    v9x_u32 count = draw->attributes == V9X_HSW_WIDE_ATTRIBUTES ?
                        V9X_SNB_WIDE_ELEMENTS : V9X_SNB_TL_ELEMENTS;
    v9x_u32 index;
    v9x_u32 c;

    for (index = 0ul; index < count; ++index) {
        elements[index].buffer = 0u;
        elements[index].format = V9X_HSW_FMT_R32G32B32A32_FLOAT;
        elements[index].offset = 0u;
        for (c = 0ul; c < 4ul; ++c) {
            elements[index].component[c] = V9X_HSW_VFCOMP_STORE_SRC;
        }
    }
    /* The VUE header, all zero; sx sy sz rhw as the position, so w
     * carries rhw; the colours as B8G8R8A8_UNORM, D3DCOLOR's byte order;
     * tu tv twice, or tu1 tv1 at 32 for a second unit. */
    for (c = 0ul; c < 4ul; ++c) {
        elements[0].component[c] = V9X_HSW_VFCOMP_STORE_0;
    }
    if (count == V9X_SNB_WIDE_ELEMENTS) {
        /* As the Gen7 path's wide form: every attribute four floats. */
        for (index = 2ul; index < count; ++index) {
            elements[index].offset = (v9x_u16)(16u + 16u * (index - 2ul));
        }
        return count;
    }
    elements[2].format = V9X_HSW_FMT_B8G8R8A8_UNORM;
    elements[2].offset = 16u;
    elements[3].format = V9X_HSW_FMT_B8G8R8A8_UNORM;
    elements[3].offset = 20u;
    for (index = 4ul; index < 6ul; ++index) {
        elements[index].format = V9X_HSW_FMT_R32G32_FLOAT;
        elements[index].offset = 24u;
        elements[index].component[2] = V9X_HSW_VFCOMP_STORE_0;
        elements[index].component[3] = V9X_HSW_VFCOMP_STORE_1_FP;
    }
    if (draw->tl_uv1) {
        elements[5].offset = V9X_HSW_TL_VERTEX_BYTES;
    }
    return count;
}

static void v9x_snb_draw_vertices(struct v9x_hsw_batch *batch,
                                  const struct v9x_hsw_draw *draw,
                                  v9x_u32 blend, v9x_u32 cc)
{
    struct v9x_hsw_vertex_element elements[V9X_SNB_WIDE_ELEMENTS];
    v9x_u32 vertices;
    v9x_u32 count;
    v9x_u16 stride;

    count = v9x_snb_tl_elements(elements, draw);
    stride = count == V9X_SNB_WIDE_ELEMENTS ? V9X_HSW_WIDE_VERTEX_BYTES :
             draw->tl_uv1 ? V9X_HSW_TL2_VERTEX_BYTES :
                            V9X_HSW_TL_VERTEX_BYTES;
    if (draw->depth_buffer != 0ul && draw->depth_clear != 0ul) {
        /* The clear, then the scene under the depth test. */
        v9x_intel_snb_vertex_buffer(
            batch, 0u, stride, draw->vertices,
            draw->vertices +
                (V9X_HSW_DEPTH_CLEAR_VERTICES + V9X_HSW_DEPTH_SCENE_VERTICES) *
                    stride - 1ul);
        v9x_intel_hsw_vertex_elements(batch, elements,
                                      (v9x_u8)count);
        v9x_intel_snb_3dprimitive(batch, V9X_HSW_PRIM_TRILIST,
                                  V9X_HSW_DEPTH_CLEAR_VERTICES, 0ul, 0u);
        v9x_intel_snb_cc_ptrs(batch, blend, draw->depth_test, cc);
        v9x_intel_snb_3dprimitive(batch, V9X_HSW_PRIM_TRILIST,
                                  V9X_HSW_DEPTH_SCENE_VERTICES,
                                  V9X_HSW_DEPTH_CLEAR_VERTICES, 0u);
        return;
    }
    vertices = draw->vertex_count != 0ul ? draw->vertex_count : 3ul;
    v9x_intel_snb_vertex_buffer(batch, 0u, stride, draw->vertices,
                                draw->vertices + vertices * stride - 1ul);
    v9x_intel_hsw_vertex_elements(batch, elements,
                                  (v9x_u8)count);
    if (draw->indices != 0ul) {
        /* To the end of the cache line holding the last index, which the
         * caller has zeroed: Haswell's rule, kept for one index layout. */
        v9x_intel_hsw_index_buffer(
            batch, draw->indices,
            ((draw->indices + draw->index_count * 2ul + 0x3ful) &
             ~(v9x_u32)0x3ful) - 1ul);
        v9x_intel_snb_3dprimitive(batch,
                                  draw->strip ? V9X_HSW_PRIM_TRISTRIP :
                                                V9X_HSW_PRIM_TRILIST,
                                  draw->index_count, 0ul, 1u);
        return;
    }
    v9x_intel_snb_3dprimitive(batch, V9X_HSW_PRIM_TRILIST, vertices, 0ul,
                              0u);
}

static v9x_status v9x_snb_workload(struct v9x_hsw_batch *batch,
                                   v9x_u32 state_base, v9x_u32 state_bytes,
                                   const struct v9x_hsw_draw *draw,
                                   const struct v9x_hsw_ps11_draw *ps,
                                   v9x_u32 scratch, v9x_u32 done_address,
                                   v9x_u32 done_value)
{
    v9x_u32 bound;
    v9x_u32 blend;
    v9x_u32 cc;
    v9x_u32 sampler;
    v9x_u32 depth_stencil;
    v9x_u32 mask;
    v9x_u8 depth_format;

    if (batch == 0 || (state_base & V9X_SNB_PAGE_MASK) != 0ul ||
        (state_bytes & V9X_SNB_PAGE_MASK) != 0ul ||
        state_bytes < V9X_SNB_INIT_STATE_BYTES ||
        (scratch & 7ul) != 0ul || (done_address & 7ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    bound = state_base + state_bytes;
    blend = draw != 0 && draw->depth_only ? V9X_HSW_SHADE_BLEND_NO_COLOUR :
            draw != 0 && draw->blend != 0ul ? draw->blend :
                                              V9X_SNB_INIT_BLEND;
    cc = draw != 0 && draw->cc != 0ul ? draw->cc : V9X_SNB_INIT_CC;
    sampler = draw != 0 && draw->sampler != 0ul ? draw->sampler :
                                                  V9X_SNB_INIT_SAMPLER;
    depth_stencil = draw != 0 && draw->depth_buffer != 0ul ?
                        (draw->depth_clear != 0ul ? draw->depth_clear :
                                                    draw->depth_test) :
                        V9X_SNB_INIT_DEPTH_STENCIL;
    depth_format = draw != 0 && draw->depth_format != 0u ?
                       draw->depth_format : V9X_HSW_DEPTH_D32_FLOAT;

    /* Before PIPELINE_SELECT, crocus flushes the render and depth caches
     * with a CS stall and then invalidates the read-only ones. */
    v9x_snb_flush(batch, V9X_HSW_PC_RT_CACHE_FLUSH |
                  V9X_HSW_PC_DEPTH_CACHE_FLUSH | V9X_HSW_PC_CS_STALL,
                  scratch);
    v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_PIPELINE_SELECT, 0ul, 0, 0ul);
    v9x_intel_hsw_state_base_address(batch, state_base, state_base,
                                     state_base, 0ul, state_base,
                                     V9X_SNB_NO_BOUND, bound,
                                     V9X_SNB_NO_BOUND, bound);
    /* The VF cache too: the 4570 fetched the previous run's vertices from
     * the same address without it (Haswell Phase F1a). */
    v9x_snb_flush(batch, V9X_HSW_PC_CS_STALL | V9X_HSW_PC_STALL_AT_SCOREBOARD |
                  V9X_HSW_PC_STATE_CACHE_INVALIDATE |
                  V9X_HSW_PC_CONST_CACHE_INVALIDATE |
                  V9X_HSW_PC_VF_CACHE_INVALIDATE |
                  V9X_HSW_PC_TEXTURE_CACHE_INVALIDATE |
                  V9X_HSW_PC_INSTRUCTION_CACHE_INVALIDATE, scratch);

    v9x_intel_snb_urb(batch, V9X_SNB_URB_ENTRIES, V9X_SNB_URB_ROWS);
    v9x_snb_emit_zero(batch, V9X_SNB_CMD_CONSTANT_VS);
    v9x_intel_snb_vs_disabled(batch);
    /* crocus's "post VS const" flush after 3DSTATE_VS. */
    v9x_snb_flush(batch, V9X_HSW_PC_DEPTH_STALL |
                  V9X_HSW_PC_INSTRUCTION_CACHE_INVALIDATE |
                  V9X_HSW_PC_STATE_CACHE_INVALIDATE, scratch);
    v9x_snb_emit_zero(batch, V9X_SNB_CMD_CONSTANT_GS);
    v9x_intel_snb_gs_disabled(batch);

    if (draw == 0) {
        v9x_snb_emit_zero(batch, V9X_SNB_CMD_CLIP);
        v9x_snb_emit_zero(batch, V9X_SNB_CMD_SF);
        v9x_snb_emit_zero(batch, V9X_SNB_CMD_WM);
    } else if (ps != 0) {
        /* A PS 1.1 draw: the six wide attributes; entry 0 the target and
         * 1 + N stage N; the push constants from the dispatch GRF, the
         * setup data after them (intel_hsw_ps11.h). */
        v9x_intel_snb_clip_screen(batch);
        v9x_intel_snb_sf(batch, V9X_HSW_WIDE_ATTRIBUTES,
                         V9X_SNB_TL_READ_OFFSET, V9X_SNB_WIDE_READ_LENGTH,
                         draw->cull, 0ul);
        v9x_intel_snb_wm(batch, draw->kernel, (v9x_u8)(1u + ps->stages),
                         ps->stages != 0u ? 1u : 0u, V9X_HSW_WIDE_ATTRIBUTES,
                         V9X_SNB_SETUP_GRF, V9X_HSW_BARY_PERSPECTIVE_PIXEL,
                         ps->kill);
    } else {
        v9x_intel_snb_clip_screen(batch);
        v9x_intel_snb_sf(batch, V9X_HSW_SHADE_ATTRIBUTES,
                         V9X_SNB_TL_READ_OFFSET, V9X_SNB_TL_READ_LENGTH,
                         draw->cull, draw->flat ? V9X_SNB_TL_FLAT_MASK : 0ul);
        /* The sampler count field is in groups of four. A depth-only draw
         * still dispatches; its colour writes are masked by the blend. */
        v9x_intel_snb_wm(batch, draw->kernel, V9X_HSW_SHADE_BINDING_ENTRIES,
                         1u, V9X_HSW_SHADE_ATTRIBUTES, V9X_SNB_SETUP_GRF,
                         V9X_HSW_BARY_PERSPECTIVE_PIXEL, 0u);
    }
    if (ps != 0) {
        v9x_intel_snb_constant_ps(batch, ps->constants, ps->constant_grfs);
    } else {
        v9x_intel_snb_constant_ps(batch, 0ul, 0u);
    }

    /* Before depth buffer state, a depth stall, a depth cache flush and a
     * depth stall (crocus_emit_depth_stall_flushes); always, since the
     * NULL buffer is a change too. */
    v9x_snb_flush(batch, V9X_HSW_PC_DEPTH_STALL, scratch);
    v9x_snb_flush(batch, V9X_HSW_PC_DEPTH_CACHE_FLUSH, scratch);
    v9x_snb_flush(batch, V9X_HSW_PC_DEPTH_STALL, scratch);
    if (draw != 0 && draw->depth_buffer != 0ul) {
        v9x_intel_snb_depth_buffer(batch, depth_format, draw->depth_buffer,
                                   draw->depth_pitch != 0ul ?
                                       draw->depth_pitch :
                                       (v9x_u32)draw->width * 4ul,
                                   draw->width, draw->height);
    } else {
        v9x_intel_snb_depth_buffer(batch, 0u, 0ul, 0ul, 0u, 0u);
    }
    v9x_snb_emit_zero(batch, V9X_SNB_CMD_HIER_DEPTH_BUFFER);
    v9x_snb_emit_zero(batch, V9X_SNB_CMD_STENCIL_BUFFER);
    v9x_intel_snb_clear_params(batch);

    mask = 1ul;
    v9x_intel_snb_3d_emit(batch, V9X_SNB_CMD_SAMPLE_MASK, 0ul, &mask, 1ul);
    if (draw == 0) {
        v9x_intel_hsw_drawing_rectangle(batch, 0u, 0u,
                                        (v9x_u16)(V9X_SNB_INIT_WIDTH - 1u),
                                        (v9x_u16)(V9X_SNB_INIT_HEIGHT - 1u));
    } else if (draw->scissor) {
        v9x_intel_hsw_drawing_rectangle(batch, draw->scissor_x0,
                                        draw->scissor_y0, draw->scissor_x1,
                                        draw->scissor_y1);
    } else {
        v9x_intel_hsw_drawing_rectangle(batch, 0u, 0u,
                                        (v9x_u16)(draw->width - 1u),
                                        (v9x_u16)(draw->height - 1u));
    }
    v9x_intel_snb_multisample(batch);

    v9x_intel_snb_viewport_ptrs(batch, V9X_SNB_INIT_CLIP_VIEWPORT,
                                V9X_SNB_INIT_SF_VIEWPORT,
                                V9X_SNB_INIT_CC_VIEWPORT);
    v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_SCISSOR_PTRS,
                                V9X_SNB_INIT_SCISSOR);
    v9x_intel_snb_cc_ptrs(batch, blend, depth_stencil, cc);
    v9x_intel_snb_binding_table_ptrs_ps(batch,
                                        draw == 0 ?
                                            V9X_SNB_INIT_BINDING_TABLE :
                                            draw->binding_table);
    v9x_intel_snb_sampler_ptrs_ps(batch, sampler);
    if (draw != 0) {
        v9x_snb_draw_vertices(batch, draw, blend, cc);
    }

    v9x_snb_flush(batch, V9X_HSW_PC_RT_CACHE_FLUSH | V9X_HSW_PC_CS_STALL,
                  scratch);
    v9x_snb_flush(batch, V9X_HSW_PC_CS_STALL | V9X_HSW_PC_STALL_AT_SCOREBOARD,
                  scratch);
    v9x_intel_snb_pipe_control(batch, V9X_HSW_PC_CS_STALL |
                               V9X_HSW_PC_WRITE_IMMEDIATE | V9X_HSW_PC_GGTT,
                               done_address, done_value);
    v9x_intel_hsw_mi_batch_buffer_end(batch);
    if ((batch->used & 1ul) != 0ul) {
        v9x_intel_hsw_mi_noop(batch);
    }

    /* Each builder above refuses only on overflow with these arguments,
     * and overflow is sticky, so one test covers them all. */
    return batch->overflow != 0u ? V9X_STATUS_INSUFFICIENT_MEMORY :
                                   V9X_STATUS_OK;
}

v9x_status v9x_intel_snb_init_workload(struct v9x_hsw_batch *batch,
                                       v9x_u32 state_base,
                                       v9x_u32 state_bytes,
                                       v9x_u32 scratch_address,
                                       v9x_u32 done_address,
                                       v9x_u32 done_value)
{
    return v9x_snb_workload(batch, state_base, state_bytes, 0, 0,
                            scratch_address, done_address, done_value);
}

v9x_status v9x_intel_snb_draw_workload(struct v9x_hsw_batch *batch,
                                       v9x_u32 state_base,
                                       v9x_u32 state_bytes,
                                       const struct v9x_hsw_draw *draw,
                                       v9x_u32 scratch_address,
                                       v9x_u32 done_address,
                                       v9x_u32 done_value)
{
    v9x_u8 depth_format;

    if (draw == 0 || !draw->tl_vertices ||
        draw->attributes != V9X_HSW_SHADE_ATTRIBUTES ||
        (draw->kernel & 0x3ful) != 0ul ||
        (draw->binding_table & 0x1ful) != 0ul ||
        (draw->sampler & 0x1ful) != 0ul ||
        ((draw->blend | draw->cc | draw->depth_clear | draw->depth_test) &
         0x3ful) != 0ul ||
        draw->width == 0u || draw->height == 0u ||
        (draw->vertices & 3ul) != 0ul ||
        state_bytes < V9X_HSW_SHADE_STATE_BYTES ||
        (draw->indices == 0ul && draw->vertex_count % 3ul != 0ul) ||
        (draw->indices != 0ul &&
         ((draw->indices & 1ul) != 0ul || draw->index_count == 0ul)) ||
        draw->cull > V9X_HSW_DRAW_CULL_CCW ||
        (draw->scissor &&
         (draw->scissor_x1 < draw->scissor_x0 ||
          draw->scissor_y1 < draw->scissor_y0))) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (draw->depth_buffer != 0ul) {
        depth_format = draw->depth_format != 0u ? draw->depth_format :
                                                  V9X_HSW_DEPTH_D32_FLOAT;
        if ((draw->depth_buffer & V9X_SNB_PAGE_MASK) != 0ul ||
            draw->depth_test == 0ul ||
            (depth_format != V9X_HSW_DEPTH_D32_FLOAT &&
             depth_format != V9X_HSW_DEPTH_D24_UNORM_X8 &&
             depth_format != V9X_HSW_DEPTH_D16_UNORM) ||
            (draw->depth_clear != 0ul && draw->indices != 0ul)) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    return v9x_snb_workload(batch, state_base, state_bytes, draw, 0,
                            scratch_address, done_address, done_value);
}

v9x_status v9x_intel_snb_draw_workload_ps11(struct v9x_hsw_batch *batch,
                                            v9x_u32 state_base,
                                            v9x_u32 state_bytes,
                                            const struct v9x_hsw_draw *draw,
                                            const struct v9x_hsw_ps11_draw *ps,
                                            v9x_u32 scratch_address,
                                            v9x_u32 done_address,
                                            v9x_u32 done_value)
{
    v9x_u8 depth_format;

    if (draw == 0 || ps == 0 || (ps->constants & 0x1ful) != 0ul ||
        ps->constant_grfs == 0u ||
        ps->constant_grfs > V9X_SNB_CONSTANT_MAX_REGISTERS ||
        ps->stages > 4u || ps->kill > 1u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* As the Gen7 form (v9x_intel_hsw_draw_workload_ps11): sequential wide
     * vertices, whole triangles, depth under depth_test alone, a scissor
     * inside the target. */
    if ((draw->kernel & 0x3ful) != 0ul ||
        (draw->binding_table & 0x1ful) != 0ul ||
        (draw->sampler & 0x1ful) != 0ul ||
        ((draw->blend | draw->cc | draw->depth_test) & 0x3ful) != 0ul ||
        draw->width == 0u || draw->height == 0u ||
        (draw->vertices & 3ul) != 0ul ||
        state_bytes < V9X_HSW_SHADE_STATE_BYTES ||
        draw->attributes != V9X_HSW_WIDE_ATTRIBUTES || draw->tl_vertices ||
        draw->tl_uv1 || draw->indices != 0ul ||
        draw->vertex_count == 0ul || draw->vertex_count % 3ul != 0ul ||
        draw->depth_clear != 0ul || draw->cull > V9X_HSW_DRAW_CULL_CCW) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (draw->scissor &&
        (draw->scissor_x1 < draw->scissor_x0 ||
         draw->scissor_y1 < draw->scissor_y0 ||
         draw->scissor_x1 >= draw->width ||
         draw->scissor_y1 >= draw->height)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (draw->depth_buffer != 0ul) {
        depth_format = draw->depth_format != 0u ? draw->depth_format :
                                                  V9X_HSW_DEPTH_D32_FLOAT;
        if ((draw->depth_buffer & V9X_SNB_PAGE_MASK) != 0ul ||
            draw->depth_test == 0ul ||
            (depth_format != V9X_HSW_DEPTH_D32_FLOAT &&
             depth_format != V9X_HSW_DEPTH_D24_UNORM_X8 &&
             depth_format != V9X_HSW_DEPTH_D16_UNORM)) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    return v9x_snb_workload(batch, state_base, state_bytes, draw, ps,
                            scratch_address, done_address, done_value);
}

/*
 * The checker. MI commands are type 0 at 31:29; GFXPIPE type 3, matched
 * to the table by subtype, opcode and sub-opcode. A command counts as
 * sent once it has been seen; 3DPRIMITIVE needs all of V9X_SNB_NEEDED.
 */
#define V9X_SNB_MI_NOOP              ((v9x_u32)0x00000000ul)
#define V9X_SNB_MI_BATCH_BUFFER_END  ((v9x_u32)0x05000000ul)
#define V9X_SNB_PC_POST_SYNC_ONLY    V9X_HSW_PC_WRITE_IMMEDIATE

static const v9x_u16 v9x_snb_needed[] = {
    V9X_SNB_CMD_PIPELINE_SELECT, V9X_SNB_CMD_STATE_BASE_ADDRESS,
    V9X_SNB_CMD_URB, V9X_SNB_CMD_VS, V9X_SNB_CMD_GS, V9X_SNB_CMD_CLIP,
    V9X_SNB_CMD_SF, V9X_SNB_CMD_WM, V9X_SNB_CMD_DEPTH_BUFFER,
    V9X_SNB_CMD_VIEWPORT_PTRS, V9X_SNB_CMD_CC_PTRS,
    V9X_SNB_CMD_BINDING_TABLE_PTRS, V9X_SNB_CMD_SAMPLER_PTRS,
    V9X_SNB_CMD_VERTEX_BUFFERS, V9X_SNB_CMD_VERTEX_ELEMENTS
};

static v9x_u16 v9x_snb_lookup(v9x_u32 header)
{
    v9x_u16 command;
    const struct v9x_snb_command *cmd;

    for (command = 0u; command < V9X_SNB_CMD_COUNT; ++command) {
        cmd = &v9x_snb_commands[command];
        if (((header >> 27) & 3ul) == cmd->subtype &&
            ((header >> 24) & 7ul) == cmd->opcode &&
            ((header >> 16) & 0xfful) == cmd->subop) {
            return command;
        }
    }
    /* 3DSTATE_INDEX_BUFFER is Gen7's builder's (780Ah, 3 dwords). */
    return V9X_SNB_CMD_COUNT;
}

#define V9X_SNB_INDEX_BUFFER_OP ((v9x_u32)0x780a0000ul)

v9x_status v9x_intel_snb_3d_check(const v9x_u32 *dw, v9x_u32 count,
                                  v9x_u32 *bad_at)
{
    v9x_u8 seen[V9X_SNB_CMD_COUNT];
    v9x_u32 at;
    v9x_u32 length;
    v9x_u32 index;
    v9x_u32 header;
    v9x_u32 flags;
    v9x_u16 command;
    v9x_u8 previous_pc;   /* 0 none, 1 CS+scoreboard, 2 the pair */
    v9x_u8 ended;

    if (dw == 0 || bad_at == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < V9X_SNB_CMD_COUNT; ++index) {
        seen[index] = 0u;
    }
    previous_pc = 0u;
    ended = 0u;
    at = 0ul;
    while (at < count) {
        *bad_at = at;
        header = dw[at];
        if (ended) {
            /* Only the qword pad may follow the end. */
            if (header != V9X_SNB_MI_NOOP || at + 1ul != count) {
                return V9X_STATUS_UNSUPPORTED;
            }
            ++at;
            continue;
        }
        if ((header >> 29) == 0ul) {
            if (header == V9X_SNB_MI_BATCH_BUFFER_END) {
                ended = 1u;
            } else if (header != V9X_SNB_MI_NOOP) {
                return V9X_STATUS_UNSUPPORTED;
            }
            previous_pc = 0u;
            ++at;
            continue;
        }
        if ((header >> 29) != 3ul) {
            return V9X_STATUS_UNSUPPORTED;
        }
        if ((header & 0xffff0000ul) == V9X_SNB_INDEX_BUFFER_OP) {
            if ((header & 0xfful) != 1ul || at + 3ul > count) {
                return V9X_STATUS_UNSUPPORTED;
            }
            previous_pc = 0u;
            at += 3ul;
            continue;
        }
        command = v9x_snb_lookup(header);
        if (command == V9X_SNB_CMD_COUNT) {
            return V9X_STATUS_UNSUPPORTED;
        }
        length = command == V9X_SNB_CMD_PIPELINE_SELECT ?
                     1ul : (header & 0xfful) + 2ul;
        if (v9x_snb_commands[command].total == V9X_SNB_VARIABLE) {
            if ((command == V9X_SNB_CMD_VERTEX_BUFFERS &&
                 (length - 1ul) % 4ul != 0ul) ||
                (command == V9X_SNB_CMD_VERTEX_ELEMENTS &&
                 (length - 1ul) % 2ul != 0ul)) {
                return V9X_STATUS_UNSUPPORTED;
            }
        } else if (length != v9x_snb_commands[command].total) {
            return V9X_STATUS_UNSUPPORTED;
        }
        if (at + length > count) {
            return V9X_STATUS_UNSUPPORTED;
        }
        if (command == V9X_SNB_CMD_PIPE_CONTROL) {
            flags = dw[at + 1ul];
            if ((flags & (V9X_HSW_PC_RT_CACHE_FLUSH |
                          V9X_HSW_PC_DEPTH_STALL)) != 0ul &&
                previous_pc != 2u) {
                return V9X_STATUS_UNSUPPORTED;
            }
            if (flags == (V9X_HSW_PC_CS_STALL |
                          V9X_HSW_PC_STALL_AT_SCOREBOARD)) {
                previous_pc = 1u;
            } else if (flags == V9X_SNB_PC_POST_SYNC_ONLY &&
                       previous_pc == 1u) {
                previous_pc = 2u;
            } else {
                previous_pc = 0u;
            }
        } else {
            previous_pc = 0u;
        }
        if (command == V9X_SNB_CMD_3DPRIMITIVE) {
            for (index = 0ul;
                 index < sizeof(v9x_snb_needed) / sizeof(v9x_snb_needed[0]);
                 ++index) {
                if (!seen[v9x_snb_needed[index]]) {
                    return V9X_STATUS_UNSUPPORTED;
                }
            }
        }
        seen[command] = 1u;
        at += length;
    }
    *bad_at = count;
    if (!ended || (count & 1ul) != 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}
