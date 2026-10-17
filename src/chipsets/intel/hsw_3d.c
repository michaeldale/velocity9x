/*
 * Haswell (Gen7.5) render-engine command and state encoding. See
 * include\velocity9x\intel_hsw_3d.h for scope and sources.
 */
#include "velocity9x/intel_hsw_3d.h"

/* A GFXPIPE header: type 3 in 31:29, then subtype 28:27, opcode 26:24,
 * sub-opcode 23:16, and DWord Length (total - 2) in 7:0 (Vol 2b). */
#define V9X_HSW_GFXPIPE ((v9x_u32)0x60000000ul)

/* MI commands, type 0: opcode in 28:23 (Vol 2b). */
#define V9X_HSW_MI_NOOP             ((v9x_u32)0x00000000ul)
#define V9X_HSW_MI_BATCH_BUFFER_END ((v9x_u32)(0x0aul << 23))
#define V9X_HSW_MI_STORE_DATA_IMM   ((v9x_u32)(0x20ul << 23))
#define V9X_HSW_MI_LOAD_REGISTER_IMM ((v9x_u32)(0x22ul << 23))
#define V9X_HSW_MI_USE_GGTT         ((v9x_u32)0x00400000ul) /* bit 22 */

/* Variable-length commands are 1 + per * n dwords. */
#define V9X_HSW_VARIABLE ((v9x_u8)0u)
#define V9X_HSW_VB_DWORDS ((v9x_u32)4ul)
#define V9X_HSW_VE_DWORDS ((v9x_u32)2ul)
#define V9X_HSW_VB_MAX    ((v9x_u8)33u)
#define V9X_HSW_VE_MAX    ((v9x_u8)34u)

struct v9x_hsw_command {
    v9x_u8 subtype;
    v9x_u8 opcode;
    v9x_u8 subop;
    v9x_u8 total;   /* fixed total dwords, or V9X_HSW_VARIABLE */
};

/*
 * Index = V9X_HSW_CMD_*. Subtype, opcode and sub-opcode from each
 * command's DWord 0 table in Vol 2b; total = DWord Length default + 2.
 * PIPELINE_SELECT is GFXPIPE_SINGLE_DW and has no length field.
 * 3DSTATE_MULTISAMPLE's default length is not printed in Vol 2b; its 4
 * dwords are from Vol 7's render context table.
 */
static const struct v9x_hsw_command v9x_hsw_commands[V9X_HSW_CMD_COUNT] = {
    { 1u, 1u, 0x04u, 1u },   /* PIPELINE_SELECT */
    { 0u, 1u, 0x01u, 10u },  /* STATE_BASE_ADDRESS */
    { 0u, 1u, 0x02u, 2u },   /* STATE_SIP */
    { 3u, 1u, 0x12u, 2u },   /* 3DSTATE_PUSH_CONSTANT_ALLOC_VS */
    { 3u, 1u, 0x13u, 2u },   /* _HS */
    { 3u, 1u, 0x14u, 2u },   /* _DS */
    { 3u, 1u, 0x15u, 2u },   /* _GS */
    { 3u, 1u, 0x16u, 2u },   /* _PS */
    { 3u, 0u, 0x30u, 2u },   /* 3DSTATE_URB_VS */
    { 3u, 0u, 0x31u, 2u },   /* _HS */
    { 3u, 0u, 0x32u, 2u },   /* _DS */
    { 3u, 0u, 0x33u, 2u },   /* _GS */
    { 3u, 0u, 0x10u, 6u },   /* 3DSTATE_VS */
    { 3u, 0u, 0x1bu, 7u },   /* 3DSTATE_HS */
    { 3u, 0u, 0x1cu, 4u },   /* 3DSTATE_TE */
    { 3u, 0u, 0x1du, 6u },   /* 3DSTATE_DS */
    { 3u, 0u, 0x11u, 7u },   /* 3DSTATE_GS */
    { 3u, 0u, 0x1eu, 3u },   /* 3DSTATE_STREAMOUT */
    { 3u, 0u, 0x12u, 4u },   /* 3DSTATE_CLIP */
    { 3u, 0u, 0x13u, 7u },   /* 3DSTATE_SF */
    { 3u, 0u, 0x1fu, 14u },  /* 3DSTATE_SBE */
    { 3u, 0u, 0x14u, 3u },   /* 3DSTATE_WM */
    { 3u, 0u, 0x20u, 8u },   /* 3DSTATE_PS */
    { 3u, 0u, 0x08u, V9X_HSW_VARIABLE }, /* 3DSTATE_VERTEX_BUFFERS */
    { 3u, 0u, 0x09u, V9X_HSW_VARIABLE }, /* 3DSTATE_VERTEX_ELEMENTS */
    { 3u, 3u, 0x00u, 7u },   /* 3DPRIMITIVE */
    { 3u, 2u, 0x00u, 5u },   /* PIPE_CONTROL */
    { 3u, 1u, 0x00u, 4u },   /* 3DSTATE_DRAWING_RECTANGLE */
    { 3u, 0u, 0x23u, 2u },   /* 3DSTATE_VIEWPORT_STATE_POINTERS_CC */
    { 3u, 0u, 0x21u, 2u },   /* 3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP */
    { 3u, 0u, 0x0fu, 2u },   /* 3DSTATE_SCISSOR_STATE_POINTERS */
    { 3u, 0u, 0x24u, 2u },   /* 3DSTATE_BLEND_STATE_POINTERS */
    { 3u, 0u, 0x25u, 2u },   /* 3DSTATE_DEPTH_STENCIL_STATE_POINTERS */
    { 3u, 0u, 0x0eu, 2u },   /* 3DSTATE_CC_STATE_POINTERS */
    { 3u, 0u, 0x2au, 2u },   /* 3DSTATE_BINDING_TABLE_POINTERS_PS */
    { 3u, 0u, 0x2fu, 2u },   /* 3DSTATE_SAMPLER_STATE_POINTERS_PS */
    { 3u, 0u, 0x05u, 7u },   /* 3DSTATE_DEPTH_BUFFER */
    { 3u, 1u, 0x0du, 4u },   /* 3DSTATE_MULTISAMPLE */
    { 3u, 0u, 0x18u, 2u },   /* 3DSTATE_SAMPLE_MASK */
    { 3u, 0u, 0x17u, 7u }    /* 3DSTATE_CONSTANT_PS */
};

void v9x_intel_hsw_batch_init(struct v9x_hsw_batch *batch, v9x_u32 *dw,
                              v9x_u32 capacity)
{
    if (batch == 0) {
        return;
    }
    batch->dw = dw;
    batch->capacity = dw != 0 ? capacity : 0ul;
    batch->used = 0ul;
    batch->overflow = 0u;
}

/* Reserve count dwords; on overflow the batch is marked and left as it
 * was, so a short buffer never holds half a command. */
static v9x_u32 *v9x_hsw_reserve(struct v9x_hsw_batch *batch, v9x_u32 count)
{
    v9x_u32 *at;

    if (batch == 0 || batch->dw == 0 || batch->overflow != 0u ||
        count > batch->capacity - batch->used) {
        if (batch != 0) {
            batch->overflow = 1u;
        }
        return 0;
    }
    at = batch->dw + batch->used;
    batch->used += count;
    return at;
}

v9x_status v9x_intel_hsw_3d_header(v9x_u16 command, v9x_u32 total_dwords,
                                   v9x_u32 *header)
{
    const struct v9x_hsw_command *cmd;

    if (header == 0 || command >= V9X_HSW_CMD_COUNT) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    cmd = &v9x_hsw_commands[command];
    if (cmd->total != V9X_HSW_VARIABLE && total_dwords != cmd->total) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *header = V9X_HSW_GFXPIPE | ((v9x_u32)cmd->subtype << 27) |
              ((v9x_u32)cmd->opcode << 24) | ((v9x_u32)cmd->subop << 16);
    if (cmd->total == 1u) {
        return V9X_STATUS_OK;
    }
    if (total_dwords < 2ul || total_dwords - 2ul > 0xfful) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *header |= total_dwords - 2ul;
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_hsw_3d_emit(struct v9x_hsw_batch *batch,
                                 v9x_u16 command, const v9x_u32 *payload,
                                 v9x_u32 count)
{
    v9x_u32 header;
    v9x_u32 *at;
    v9x_u32 index;

    if (command >= V9X_HSW_CMD_COUNT ||
        v9x_hsw_commands[command].total == V9X_HSW_VARIABLE ||
        (count != 0ul && payload == 0)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_intel_hsw_3d_header(command, count + 1ul, &header) !=
        V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    at = v9x_hsw_reserve(batch, count + 1ul);
    if (at == 0) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at[0] = header;
    for (index = 0ul; index < count; ++index) {
        at[index + 1ul] = payload[index];
    }
    return V9X_STATUS_OK;
}

static v9x_status v9x_hsw_mi(struct v9x_hsw_batch *batch,
                             const v9x_u32 *dw, v9x_u32 count)
{
    v9x_u32 *at;
    v9x_u32 index;

    at = v9x_hsw_reserve(batch, count);
    if (at == 0) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    for (index = 0ul; index < count; ++index) {
        at[index] = dw[index];
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_hsw_mi_noop(struct v9x_hsw_batch *batch)
{
    v9x_u32 dw = V9X_HSW_MI_NOOP;

    return v9x_hsw_mi(batch, &dw, 1ul);
}

v9x_status v9x_intel_hsw_mi_batch_buffer_end(struct v9x_hsw_batch *batch)
{
    v9x_u32 dw = V9X_HSW_MI_BATCH_BUFFER_END;

    return v9x_hsw_mi(batch, &dw, 1ul);
}

/* Four dwords: DWord Length 2 "for DWord" (Vol 2b); DW1 reserved, DW2 the
 * address in 31:2, DW3 the data. */
v9x_status v9x_intel_hsw_mi_store_data_imm(struct v9x_hsw_batch *batch,
                                           v9x_u32 ggtt_address,
                                           v9x_u32 value)
{
    v9x_u32 dw[4];

    if ((ggtt_address & 3ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    dw[0] = V9X_HSW_MI_STORE_DATA_IMM | V9X_HSW_MI_USE_GGTT | 2ul;
    dw[1] = 0ul;
    dw[2] = ggtt_address;
    dw[3] = value;
    return v9x_hsw_mi(batch, dw, 4ul);
}

/* Three dwords: register offset in 22:2, then the data. */
v9x_status v9x_intel_hsw_mi_load_register_imm(struct v9x_hsw_batch *batch,
                                              v9x_u32 offset,
                                              v9x_u32 value)
{
    v9x_u32 dw[3];

    if ((offset & ~(v9x_u32)0x007ffffcul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    dw[0] = V9X_HSW_MI_LOAD_REGISTER_IMM | 1ul;
    dw[1] = offset;
    dw[2] = value;
    return v9x_hsw_mi(batch, dw, 3ul);
}

/* Pipeline Selection 1:0 = 0, the 3D pipeline. */
v9x_status v9x_intel_hsw_pipeline_select_3d(struct v9x_hsw_batch *batch)
{
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_PIPELINE_SELECT, 0, 0ul);
}

/* Each base is 31:12 with Modify Enable in bit 0; each bound 31:12 with
 * its Modify Enable in bit 0 (Vol 2b STATE_BASE_ADDRESS). Memory object
 * control is left 0. */
v9x_status v9x_intel_hsw_state_base_address(struct v9x_hsw_batch *batch,
                                            v9x_u32 general,
                                            v9x_u32 surface,
                                            v9x_u32 dynamic,
                                            v9x_u32 indirect,
                                            v9x_u32 instruction,
                                            v9x_u32 general_bound,
                                            v9x_u32 dynamic_bound,
                                            v9x_u32 indirect_bound,
                                            v9x_u32 instruction_bound)
{
    v9x_u32 payload[9];
    v9x_u32 all;

    all = general | surface | dynamic | indirect | instruction |
          general_bound | dynamic_bound | indirect_bound | instruction_bound;
    if ((all & 0xffful) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = general | 1ul;
    payload[1] = surface | 1ul;
    payload[2] = dynamic | 1ul;
    payload[3] = indirect | 1ul;
    payload[4] = instruction | 1ul;
    payload[5] = general_bound | 1ul;
    payload[6] = dynamic_bound | 1ul;
    payload[7] = indirect_bound | 1ul;
    payload[8] = instruction_bound | 1ul;
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_STATE_BASE_ADDRESS,
                                 payload, 9ul);
}

/* DW1: Constant Buffer Offset 20:16, Constant Buffer Size 5:0. */
v9x_status v9x_intel_hsw_push_constant_alloc(struct v9x_hsw_batch *batch,
                                             v9x_u16 stage,
                                             v9x_u8 offset_kb,
                                             v9x_u8 size_kb)
{
    v9x_u32 dw1;

    if (stage < V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_VS ||
        stage > V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_PS || offset_kb > 0x1fu ||
        size_kb > 0x3fu) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    dw1 = ((v9x_u32)offset_kb << 16) | (v9x_u32)size_kb;
    return v9x_intel_hsw_3d_emit(batch, stage, &dw1, 1ul);
}

/* DW1: Starting Address 30:25, Entry Allocation Size 24:16, Number of
 * Entries 15:0. */
v9x_status v9x_intel_hsw_urb(struct v9x_hsw_batch *batch, v9x_u16 stage,
                             v9x_u8 start, v9x_u16 alloc_size,
                             v9x_u16 entries)
{
    v9x_u32 dw1;

    if (stage < V9X_HSW_CMD_URB_VS || stage > V9X_HSW_CMD_URB_GS ||
        start > 0x3fu || alloc_size > 0x1ffu) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    dw1 = ((v9x_u32)start << 25) | ((v9x_u32)alloc_size << 16) |
          (v9x_u32)entries;
    return v9x_intel_hsw_3d_emit(batch, stage, &dw1, 1ul);
}

v9x_status v9x_intel_hsw_stage_disabled(struct v9x_hsw_batch *batch,
                                        v9x_u16 stage)
{
    static const v9x_u32 zero[13] = { 0ul };

    if (stage != V9X_HSW_CMD_VS && stage != V9X_HSW_CMD_HS &&
        stage != V9X_HSW_CMD_TE && stage != V9X_HSW_CMD_DS &&
        stage != V9X_HSW_CMD_GS && stage != V9X_HSW_CMD_STREAMOUT) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return v9x_intel_hsw_3d_emit(batch, stage, zero,
                                 (v9x_u32)v9x_hsw_commands[stage].total - 1ul);
}

v9x_status v9x_intel_hsw_state_pointer(struct v9x_hsw_batch *batch,
                                       v9x_u16 command, v9x_u32 value)
{
    if (command < V9X_HSW_CMD_VIEWPORT_PTRS_CC ||
        command > V9X_HSW_CMD_SAMPLER_PTRS_PS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return v9x_intel_hsw_3d_emit(batch, command, &value, 1ul);
}

/* DW1 Y min 31:16 / X min 15:0, DW2 the maxima, DW3 the origin (0). */
v9x_status v9x_intel_hsw_drawing_rectangle(struct v9x_hsw_batch *batch,
                                           v9x_u16 xmin, v9x_u16 ymin,
                                           v9x_u16 xmax, v9x_u16 ymax)
{
    v9x_u32 payload[3];

    if (xmax < xmin || ymax < ymin) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = ((v9x_u32)ymin << 16) | (v9x_u32)xmin;
    payload[1] = ((v9x_u32)ymax << 16) | (v9x_u32)xmax;
    payload[2] = 0ul;
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_DRAWING_RECTANGLE,
                                 payload, 3ul);
}

/* VERTEX_BUFFER_STATE (Vol 2d): DW0 index 31:26, Address Modify Enable 14,
 * pitch 11:0; DW1 start; DW2 inclusive end; DW3 instance step rate 0. */
v9x_status v9x_intel_hsw_vertex_buffer(struct v9x_hsw_batch *batch,
                                       v9x_u8 index, v9x_u16 pitch,
                                       v9x_u32 start, v9x_u32 end)
{
    v9x_u32 header;
    v9x_u32 *at;

    if (index >= V9X_HSW_VB_MAX || pitch > 0x7ffu || end < start) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_intel_hsw_3d_header(V9X_HSW_CMD_VERTEX_BUFFERS,
                                1ul + V9X_HSW_VB_DWORDS, &header) !=
        V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    at = v9x_hsw_reserve(batch, 1ul + V9X_HSW_VB_DWORDS);
    if (at == 0) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at[0] = header;
    at[1] = ((v9x_u32)index << 26) | 0x00004000ul | (v9x_u32)pitch;
    at[2] = start;
    at[3] = end;
    at[4] = 0ul;
    return V9X_STATUS_OK;
}

/* VERTEX_ELEMENT_STATE (Vol 2d): DW0 buffer 31:26, Valid 25, format
 * 24:16, offset 11:0; DW1 component controls 30:28, 26:24, 22:20,
 * 18:16. */
v9x_status v9x_intel_hsw_vertex_elements(
    struct v9x_hsw_batch *batch,
    const struct v9x_hsw_vertex_element *elements, v9x_u8 count)
{
    v9x_u32 header;
    v9x_u32 *at;
    v9x_u32 total;
    v9x_u8 index;
    const struct v9x_hsw_vertex_element *e;

    if (elements == 0 || count == 0u || count > V9X_HSW_VE_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0u; index < count; ++index) {
        e = &elements[index];
        if (e->buffer >= V9X_HSW_VB_MAX || e->format > 0x1ffu ||
            e->offset > 0x7ffu || e->component[0] > 7u ||
            e->component[1] > 7u || e->component[2] > 7u ||
            e->component[3] > 7u) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    total = 1ul + V9X_HSW_VE_DWORDS * (v9x_u32)count;
    if (v9x_intel_hsw_3d_header(V9X_HSW_CMD_VERTEX_ELEMENTS, total,
                                &header) != V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    at = v9x_hsw_reserve(batch, total);
    if (at == 0) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at[0] = header;
    for (index = 0u; index < count; ++index) {
        e = &elements[index];
        at[1u + index * 2u] = ((v9x_u32)e->buffer << 26) | 0x02000000ul |
                              ((v9x_u32)e->format << 16) |
                              (v9x_u32)e->offset;
        at[2u + index * 2u] = ((v9x_u32)e->component[0] << 28) |
                              ((v9x_u32)e->component[1] << 24) |
                              ((v9x_u32)e->component[2] << 20) |
                              ((v9x_u32)e->component[3] << 16);
    }
    return V9X_STATUS_OK;
}

/* DW1 Vertex Access Type 8 (0 = sequential) and topology 5:0; DW2 vertex
 * count, DW3 start vertex, DW4 instance count, DW5 start instance, DW6
 * base vertex. */
v9x_status v9x_intel_hsw_3dprimitive(struct v9x_hsw_batch *batch,
                                     v9x_u8 topology, v9x_u32 vertices,
                                     v9x_u32 start)
{
    v9x_u32 payload[6];

    if (topology == 0u || topology > 0x3fu) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = (v9x_u32)topology;
    payload[1] = vertices;
    payload[2] = start;
    payload[3] = 1ul;
    payload[4] = 0ul;
    payload[5] = 0ul;
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_3DPRIMITIVE,
                                 payload, 6ul);
}

/* DW1 flags, DW2 address 31:2, DW3/DW4 immediate data. */
v9x_status v9x_intel_hsw_pipe_control(struct v9x_hsw_batch *batch,
                                      v9x_u32 flags, v9x_u32 address,
                                      v9x_u32 data)
{
    v9x_u32 payload[4];

    if ((address & 3ul) != 0ul || (flags & 0xf0000000ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = flags;
    payload[1] = address;
    payload[2] = data;
    payload[3] = 0ul;
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_PIPE_CONTROL,
                                 payload, 4ul);
}

/*
 * RENDER_SURFACE_STATE (Vol 2d): DW0 type 31:29, format 26:18; DW1 base;
 * DW2 height-1 29:16, width-1 13:0; DW3 pitch-1 17:0; DW7 shader channel
 * selects R 27:25, G 24:22, B 21:19, A 18:16. The rest 0: linear, one mip,
 * no array, memory object control 0.
 */
v9x_status v9x_intel_hsw_surface_2d(v9x_u32 out[8], v9x_u16 format,
                                    v9x_u32 base, v9x_u16 width,
                                    v9x_u16 height, v9x_u32 pitch)
{
    v9x_u32 index;

    if (out == 0 || format > 0x1ffu || width == 0u || height == 0u ||
        width > 0x4000u || height > 0x4000u || pitch == 0ul ||
        pitch > 0x40000ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < V9X_HSW_SURFACE_STATE_DWORDS; ++index) {
        out[index] = 0ul;
    }
    out[0] = ((v9x_u32)V9X_HSW_SURFTYPE_2D << 29) | ((v9x_u32)format << 18);
    out[1] = base;
    out[2] = ((v9x_u32)(height - 1u) << 16) | (v9x_u32)(width - 1u);
    out[3] = pitch - 1ul;
    out[7] = ((v9x_u32)V9X_HSW_SCS_RED << 25) |
             ((v9x_u32)V9X_HSW_SCS_GREEN << 22) |
             ((v9x_u32)V9X_HSW_SCS_BLUE << 19) |
             ((v9x_u32)V9X_HSW_SCS_ALPHA << 16);
    return V9X_STATUS_OK;
}

/* SAMPLER_STATE (Vol 2d): DW0 mip 21:20, mag 19:17, min 16:14; DW1 min
 * LOD 31:20, max LOD 19:8; DW2 border colour pointer 31:5; DW3 raw. */
v9x_status v9x_intel_hsw_sampler(v9x_u32 out[4],
                                 const struct v9x_hsw_sampler *sampler)
{
    if (out == 0 || sampler == 0 || sampler->mip_filter > 3u ||
        sampler->mag_filter > 7u || sampler->min_filter > 7u ||
        sampler->min_lod > 0xfffu || sampler->max_lod > 0xfffu ||
        (sampler->border_color_pointer & 0x1ful) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    out[0] = ((v9x_u32)sampler->mip_filter << 20) |
             ((v9x_u32)sampler->mag_filter << 17) |
             ((v9x_u32)sampler->min_filter << 14);
    out[1] = ((v9x_u32)sampler->min_lod << 20) |
             ((v9x_u32)sampler->max_lod << 8);
    out[2] = sampler->border_color_pointer;
    out[3] = sampler->dw3;
    return V9X_STATUS_OK;
}

/* BLEND_STATE (Vol 2d): DW0 enable 31, independent alpha 30, alpha
 * function 28:26, source/destination alpha factors 24:20 and 19:15, colour
 * function 13:11, source/destination factors 9:5 and 4:0; DW1 write
 * disables 27:24 (A R G B), alpha test enable 16, function 15:13. */
v9x_status v9x_intel_hsw_blend(v9x_u32 out[2],
                               const struct v9x_hsw_blend *blend)
{
    if (out == 0 || blend == 0 || blend->color_function > 7u ||
        blend->alpha_function > 7u || blend->src_factor > 0x1fu ||
        blend->dst_factor > 0x1fu || blend->src_alpha_factor > 0x1fu ||
        blend->dst_alpha_factor > 0x1fu || blend->write_disable > 0xfu ||
        blend->alpha_function_test > 7u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    out[0] = ((v9x_u32)(blend->enable != 0u) << 31) |
             ((v9x_u32)(blend->independent_alpha != 0u) << 30) |
             ((v9x_u32)blend->alpha_function << 26) |
             ((v9x_u32)blend->src_alpha_factor << 20) |
             ((v9x_u32)blend->dst_alpha_factor << 15) |
             ((v9x_u32)blend->color_function << 11) |
             ((v9x_u32)blend->src_factor << 5) |
             (v9x_u32)blend->dst_factor;
    out[1] = ((v9x_u32)blend->write_disable << 24) |
             ((v9x_u32)(blend->alpha_test != 0u) << 16) |
             ((v9x_u32)blend->alpha_function_test << 13);
    return V9X_STATUS_OK;
}

/* DEPTH_STENCIL_STATE (Vol 2d): DW2 depth test 31, function 29:27, write
 * 26; stencil off. */
v9x_status v9x_intel_hsw_depth_stencil(v9x_u32 out[3], v9x_u8 depth_test,
                                       v9x_u8 depth_function,
                                       v9x_u8 depth_write)
{
    if (out == 0 || depth_function > 7u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    out[0] = 0ul;
    out[1] = 0ul;
    out[2] = ((v9x_u32)(depth_test != 0u) << 31) |
             ((v9x_u32)depth_function << 27) |
             ((v9x_u32)(depth_write != 0u) << 26);
    return V9X_STATUS_OK;
}

/* COLOR_CALC_STATE (Vol 2d): DW0 Alpha Test Format bit 0 = 0 (UNORM8),
 * DW1 the reference in 7:0 of the UNORM8 form, DW2-5 blend constant. */
v9x_status v9x_intel_hsw_cc_state(v9x_u32 out[6], v9x_u8 alpha_ref)
{
    v9x_u32 index;

    if (out == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < V9X_HSW_CC_STATE_DWORDS; ++index) {
        out[index] = 0ul;
    }
    out[1] = (v9x_u32)alpha_ref;
    return V9X_STATUS_OK;
}

/* The table entry whose header this is, or V9X_HSW_CMD_COUNT. */
static v9x_u16 v9x_hsw_lookup(v9x_u32 header)
{
    v9x_u16 index;
    v9x_u8 subtype;
    v9x_u8 opcode;
    v9x_u8 subop;

    subtype = (v9x_u8)((header >> 27) & 3ul);
    opcode = (v9x_u8)((header >> 24) & 7ul);
    subop = (v9x_u8)((header >> 16) & 0xfful);
    for (index = 0u; index < V9X_HSW_CMD_COUNT; ++index) {
        if (v9x_hsw_commands[index].subtype == subtype &&
            v9x_hsw_commands[index].opcode == opcode &&
            v9x_hsw_commands[index].subop == subop) {
            return index;
        }
    }
    return V9X_HSW_CMD_COUNT;
}

static v9x_status v9x_hsw_refuse(struct v9x_hsw_check *check,
                                 v9x_u16 result, v9x_u32 at,
                                 v9x_u32 header)
{
    check->result = result;
    check->at = at;
    check->header = header;
    return V9X_STATUS_INVALID_STATE;
}

/*
 * Ending on MI_BATCH_BUFFER_END with the total a whole number of qwords
 * (an MI_NOOP after it as padding) is i915's discipline for batch
 * buffers; the public PRM excerpts read here do not state it, and it is
 * kept as the conservative rule.
 */
v9x_status v9x_intel_hsw_3d_check(const v9x_u32 *dwords, v9x_u32 count,
                                  struct v9x_hsw_check *check)
{
    v9x_u32 at;
    v9x_u32 header;
    v9x_u32 length;
    v9x_u32 type;
    v9x_u32 mi;
    v9x_u16 command;

    if (dwords == 0 || check == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    check->result = V9X_HSW_CHECK_OK;
    check->at = 0ul;
    check->commands = 0ul;
    check->header = 0ul;

    at = 0ul;
    while (at < count) {
        header = dwords[at];
        type = header >> 29;
        if (type == 0ul) {
            mi = (header >> 23) & 0x3ful;
            if (header == V9X_HSW_MI_NOOP) {
                length = 1ul;
            } else if (header == V9X_HSW_MI_BATCH_BUFFER_END) {
                ++check->commands;
                ++at;
                /* At most one MI_NOOP may pad the end to a qword. */
                if (at < count && dwords[at] == V9X_HSW_MI_NOOP) {
                    ++at;
                }
                if (at != count) {
                    return v9x_hsw_refuse(check, V9X_HSW_CHECK_TRUNCATED, at,
                                          dwords[at]);
                }
                if ((count & 1ul) != 0ul) {
                    return v9x_hsw_refuse(check, V9X_HSW_CHECK_NOT_QWORD,
                                          at - 1ul, header);
                }
                return V9X_STATUS_OK;
            } else if (mi == 0x20ul && (header & 0xfful) == 2ul) {
                length = 4ul;
            } else if (mi == 0x22ul && (header & 0xfful) == 1ul) {
                length = 3ul;
            } else {
                return v9x_hsw_refuse(check, V9X_HSW_CHECK_UNKNOWN_COMMAND,
                                      at, header);
            }
        } else if (type == 3ul) {
            command = v9x_hsw_lookup(header);
            if (command == V9X_HSW_CMD_COUNT) {
                return v9x_hsw_refuse(check, V9X_HSW_CHECK_UNKNOWN_COMMAND,
                                      at, header);
            }
            if (v9x_hsw_commands[command].total == 1u) {
                length = 1ul;
            } else {
                length = (header & 0xfful) + 2ul;
                if (command == V9X_HSW_CMD_VERTEX_BUFFERS) {
                    if ((length - 1ul) % V9X_HSW_VB_DWORDS != 0ul) {
                        return v9x_hsw_refuse(check,
                                              V9X_HSW_CHECK_BAD_LENGTH, at,
                                              header);
                    }
                } else if (command == V9X_HSW_CMD_VERTEX_ELEMENTS) {
                    if ((length - 1ul) % V9X_HSW_VE_DWORDS != 0ul) {
                        return v9x_hsw_refuse(check,
                                              V9X_HSW_CHECK_BAD_LENGTH, at,
                                              header);
                    }
                } else if (length != v9x_hsw_commands[command].total) {
                    return v9x_hsw_refuse(check, V9X_HSW_CHECK_BAD_LENGTH,
                                          at, header);
                }
            }
        } else {
            return v9x_hsw_refuse(check, V9X_HSW_CHECK_UNKNOWN_COMMAND, at,
                                  header);
        }
        if (length > count - at) {
            return v9x_hsw_refuse(check, V9X_HSW_CHECK_TRUNCATED, at, header);
        }
        at += length;
        ++check->commands;
    }
    return v9x_hsw_refuse(check, V9X_HSW_CHECK_NO_END, at, 0ul);
}
