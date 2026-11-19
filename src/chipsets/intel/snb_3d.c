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
