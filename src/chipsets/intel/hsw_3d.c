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
#define V9X_HSW_MI_FLUSH_DW         ((v9x_u32)(0x26ul << 23))
/* MI_FLUSH_DW's Post-Sync Operation 1h, write immediate data, in 15:14, and
 * its Destination Address Type GGTT in DW1 bit 2 (Vol 2b). */
#define V9X_HSW_MI_FLUSH_DW_WRITE   ((v9x_u32)(1ul << 14))
#define V9X_HSW_MI_FLUSH_DW_GGTT    ((v9x_u32)(1ul << 2))
#define V9X_HSW_MI_LOAD_REGISTER_IMM ((v9x_u32)(0x22ul << 23))
#define V9X_HSW_MI_BATCH_BUFFER_START ((v9x_u32)(0x31ul << 23))
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
    { 3u, 0u, 0x17u, 7u },   /* 3DSTATE_CONSTANT_PS */
    { 3u, 0u, 0x07u, 3u },   /* 3DSTATE_HIER_DEPTH_BUFFER */
    { 3u, 0u, 0x06u, 3u },   /* 3DSTATE_STENCIL_BUFFER */
    { 3u, 0u, 0x04u, 3u },   /* 3DSTATE_CLEAR_PARAMS */
    { 3u, 0u, 0x0au, 3u }    /* 3DSTATE_INDEX_BUFFER */
};

/* The chip the builders target, and the dword Ivy Bridge's workaround
 * PIPE_CONTROLs write (intel_hsw_3d.h). */
static v9x_u8 v9x_hsw_3d_chip = V9X_HSW_3D_CHIP_HASWELL;
static v9x_u32 v9x_hsw_3d_wa_address = 0ul;

v9x_status v9x_intel_hsw_3d_set_chip(v9x_u8 chip,
                                     v9x_u32 workaround_address)
{
    if (chip != V9X_HSW_3D_CHIP_HASWELL &&
        chip != V9X_HSW_3D_CHIP_IVYBRIDGE) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (chip == V9X_HSW_3D_CHIP_IVYBRIDGE &&
        (workaround_address == 0ul || (workaround_address & 7ul) != 0ul)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_hsw_3d_chip = chip;
    v9x_hsw_3d_wa_address = workaround_address;
    return V9X_STATUS_OK;
}

static int v9x_hsw_3d_is_ivb(void)
{
    return v9x_hsw_3d_chip == V9X_HSW_3D_CHIP_IVYBRIDGE;
}

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
    batch->pc_since_cs_stall = 0u;
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

/*
 * MI_FLUSH_DW with a DWord post-sync write (Vol 2b, BlitterCS): the parser
 * waits until every drawing engine on the ring has finished and its dirty
 * data is in memory, then writes `value` at `ggtt_address`. "After this
 * command is completed with a Store DWord enabled, CPU access to graphics
 * memory will be coherent." Three dwords, DWord Length 1 for a DWord, and a
 * no-op to keep the qword. The address can only be qword aligned.
 */
v9x_status v9x_intel_hsw_mi_flush_dw_store(struct v9x_hsw_batch *batch,
                                           v9x_u32 ggtt_address,
                                           v9x_u32 value)
{
    v9x_u32 dw[4];

    if ((ggtt_address & 7ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    dw[0] = V9X_HSW_MI_FLUSH_DW | V9X_HSW_MI_FLUSH_DW_WRITE | 1ul;
    dw[1] = ggtt_address | V9X_HSW_MI_FLUSH_DW_GGTT;
    dw[2] = value;
    dw[3] = V9X_HSW_MI_NOOP;
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

/* Two dwords, every flag clear: first level, privileged, GGTT. A GGTT
 * batch must be privileged for its MI_STORE_DATA_IMM and post-sync writes
 * to be allowed (Vol 2b, Non-Privileged bit 13). */
v9x_status v9x_intel_hsw_mi_batch_buffer_start(struct v9x_hsw_batch *batch,
                                               v9x_u32 ggtt_address)
{
    v9x_u32 dw[2];

    if ((ggtt_address & 3ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    dw[0] = V9X_HSW_MI_BATCH_BUFFER_START;
    dw[1] = ggtt_address;
    return v9x_hsw_mi(batch, dw, 2ul);
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

/* 3DPRIMITIVE DW1 Vertex Access Type, bit 8: RANDOM fetches each vertex
 * through the index buffer (Vol 2b). */
#define V9X_HSW_PRIM_ACCESS_RANDOM ((v9x_u32)0x00000100ul)

/* The same command with Vertex Access Type RANDOM: DW2 the index count,
 * DW3 the first index, 0, and DW6 the base vertex added to each. */
v9x_status v9x_intel_hsw_3dprimitive_indexed(struct v9x_hsw_batch *batch,
                                             v9x_u8 topology,
                                             v9x_u32 indices,
                                             v9x_u32 base_vertex)
{
    v9x_u32 payload[6];

    if (topology == 0u || topology > 0x3fu || indices == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = V9X_HSW_PRIM_ACCESS_RANDOM | (v9x_u32)topology;
    payload[1] = indices;
    payload[2] = 0ul;
    payload[3] = 1ul;
    payload[4] = 0ul;
    payload[5] = base_vertex;
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_3DPRIMITIVE,
                                 payload, 6ul);
}

/* 3DSTATE_INDEX_BUFFER DW0 Index Format 9:8, INDEX_WORD = 1 (Vol 2b). */
#define V9X_HSW_INDEX_WORD ((v9x_u32)0x00000100ul)

/*
 * DW0 Memory Object Control State 15:12 left 0 as the vertex buffer's is,
 * the format WORD; DW1 the first index's address, DW2 the last valid
 * byte. Vol 2b's Haswell note: the ending address must be the last byte
 * of a cache line, the bytes past the last index zero - the caller pads,
 * this refuses an end that is not so placed, and a start that is not word
 * aligned.
 */
v9x_status v9x_intel_hsw_index_buffer(struct v9x_hsw_batch *batch,
                                      v9x_u32 start, v9x_u32 end)
{
    v9x_u32 header;
    v9x_u32 *at;

    if ((start & 1ul) != 0ul || end <= start || (end & 0x3ful) != 0x3ful) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_intel_hsw_3d_header(V9X_HSW_CMD_INDEX_BUFFER, 3ul, &header) !=
        V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    at = v9x_hsw_reserve(batch, 3ul);
    if (at == 0) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    at[0] = header | V9X_HSW_INDEX_WORD;
    at[1] = start;
    at[2] = end;
    return V9X_STATUS_OK;
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
    /*
     * Ivy Bridge, WaCsStallAtEveryFourthPipecontrol (crocus, i965): every
     * fourth PIPE_CONTROL carries CS stall. A CS stall needs a companion
     * bit (Vol 7, as above), so one is added where none of the allowed
     * ones is set: stall at scoreboard, which every other CS stall here
     * already uses.
     */
    if (v9x_hsw_3d_is_ivb() && batch != 0) {
        if ((flags & V9X_HSW_PC_CS_STALL) != 0ul) {
            batch->pc_since_cs_stall = 0u;
        } else if (++batch->pc_since_cs_stall >= 4u) {
            flags |= V9X_HSW_PC_CS_STALL;
            if ((flags & (V9X_HSW_PC_RT_CACHE_FLUSH |
                          V9X_HSW_PC_DEPTH_CACHE_FLUSH |
                          V9X_HSW_PC_DEPTH_STALL |
                          V9X_HSW_PC_STALL_AT_SCOREBOARD |
                          V9X_HSW_PC_WRITE_IMMEDIATE)) == 0ul) {
                flags |= V9X_HSW_PC_STALL_AT_SCOREBOARD;
            }
            batch->pc_since_cs_stall = 0u;
        }
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
    /* Ivy Bridge has no channel selects: DW7 is its clear colours and min
     * LOD, all 0. */
    if (v9x_hsw_3d_is_ivb()) {
        return V9X_STATUS_OK;
    }
    out[7] = ((v9x_u32)V9X_HSW_SCS_RED << 25) |
             ((v9x_u32)V9X_HSW_SCS_GREEN << 22) |
             ((v9x_u32)V9X_HSW_SCS_BLUE << 19) |
             ((v9x_u32)V9X_HSW_SCS_ALPHA << 16);
    return V9X_STATUS_OK;
}

/* RENDER_SURFACE_STATE DW5 3:0, MIP Count/LOD (Vol 2d): for a sampled
 * surface, the levels reachable from Surface Min LOD (0), less one. */
v9x_status v9x_intel_hsw_surface_mips(v9x_u32 out[8], v9x_u8 levels)
{
    if (out == 0 || levels == 0u || levels > 16u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    out[5] = (out[5] & ~0xful) | (v9x_u32)(levels - 1u);
    return V9X_STATUS_OK;
}

/* The i and j of Vol 5's 2D layout for the alignments surface_2d encodes
 * (DW0 15 HALIGN_4, 17:16 VALIGN_2), and the linear pitch granule Mesa's
 * i965 uses. */
#define V9X_HSW_MIP_ALIGN_W     4ul
#define V9X_HSW_MIP_ALIGN_H     2ul
#define V9X_HSW_MIP_PITCH_ALIGN 64ul

static v9x_u32 v9x_hsw_mip_align(v9x_u32 value, v9x_u32 unit)
{
    return (value + unit - 1ul) & ~(unit - 1ul);
}

v9x_status v9x_intel_hsw_miptree(v9x_u16 width, v9x_u16 height,
                                 v9x_u8 levels, v9x_u8 bytes_per_texel,
                                 struct v9x_hsw_miptree *tree)
{
    v9x_u32 w = (v9x_u32)width;
    v9x_u32 h = (v9x_u32)height;
    v9x_u32 x = 0ul;
    v9x_u32 y = 0ul;
    v9x_u32 total_width;
    v9x_u32 longest;
    v9x_u32 reachable = 1ul;
    v9x_u32 image_height;
    v9x_u32 level;

    if (tree == 0 || width == 0u || height == 0u || levels == 0u ||
        levels > V9X_HSW_MIP_LEVELS_MAX || bytes_per_texel == 0u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    longest = w > h ? w : h;
    while ((longest >> reachable) != 0ul) {
        ++reachable;
    }
    if ((v9x_u32)levels > reachable) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Level 1 and level 2 side by side can be wider than level 0. */
    total_width = w;
    if (levels > 1u) {
        v9x_u32 w1 = w > 1ul ? w >> 1 : 1ul;
        v9x_u32 w2 = w1 > 1ul ? w1 >> 1 : 1ul;
        v9x_u32 pair = v9x_hsw_mip_align(w1, V9X_HSW_MIP_ALIGN_W);

        if (levels > 2u) {
            pair += v9x_hsw_mip_align(w2, V9X_HSW_MIP_ALIGN_W);
        }
        if (pair > total_width) {
            total_width = pair;
        }
    }
    tree->pitch = v9x_hsw_mip_align(total_width * bytes_per_texel,
                                    V9X_HSW_MIP_PITCH_ALIGN);
    tree->rows = 0ul;

    /* The rule as Mesa's brw_miptree_layout_2d states it: after level 1
     * step right by its aligned width, after every other level step down
     * by its aligned height. The last level placed need not be the lowest,
     * so the rows are the deepest any level reaches. */
    for (level = 0ul; level < (v9x_u32)levels; ++level) {
        tree->level_offset[level] = y * tree->pitch + x * bytes_per_texel;
        image_height = v9x_hsw_mip_align(h, V9X_HSW_MIP_ALIGN_H);
        if (y + image_height > tree->rows) {
            tree->rows = y + image_height;
        }
        if (level == 1ul) {
            x += v9x_hsw_mip_align(w, V9X_HSW_MIP_ALIGN_W);
        } else {
            y += image_height;
        }
        w = w > 1ul ? w >> 1 : 1ul;
        h = h > 1ul ? h >> 1 : 1ul;
    }
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

/* 2D client 2 at 31:29, opcode at 28:22; the 32 bpp byte mask is 21:20. */
#define V9X_HSW_XY_COLOR_BLT ((v9x_u32)0x54000004ul) /* 2<<29|50h<<22|4 */
#define V9X_HSW_XY_SRC_COPY_BLT ((v9x_u32)0x54c00006ul) /* 2<<29|53h<<22|6 */
#define V9X_HSW_BLT_BYTE_MASK ((v9x_u32)0x00300000ul)
#define V9X_HSW_BLT_ROP_PATCOPY ((v9x_u32)0xf0ul)
#define V9X_HSW_BLT_ROP_SRCCOPY ((v9x_u32)0xccul)

/* BR13 colour depth for bytes per pixel: 00b 8, 01b 565, 11b 32. */
static int v9x_hsw_blt_depth(v9x_u8 bytes_per_pixel, v9x_u32 *depth)
{
    switch (bytes_per_pixel) {
    case 1u:
        *depth = 0ul;
        return 1;
    case 2u:
        *depth = 1ul;
        return 1;
    case 4u:
        *depth = 3ul;
        return 1;
    default:
        return 0;
    }
}

v9x_status v9x_intel_hsw_xy_color_blt(struct v9x_hsw_batch *batch,
                                      v9x_u8 bytes_per_pixel,
                                      v9x_u16 pitch_field,
                                      v9x_u16 x1, v9x_u16 y1,
                                      v9x_u16 x2, v9x_u16 y2,
                                      v9x_u32 base, v9x_u32 color)
{
    v9x_u32 dw[6];
    v9x_u32 depth;

    if (x2 <= x1 || y2 <= y1 || pitch_field == 0u ||
        !v9x_hsw_blt_depth(bytes_per_pixel, &depth)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    dw[0] = V9X_HSW_XY_COLOR_BLT;
    if (bytes_per_pixel == 4u) {
        dw[0] |= V9X_HSW_BLT_BYTE_MASK;
    }
    dw[1] = (depth << 24) | (V9X_HSW_BLT_ROP_PATCOPY << 16) |
            (v9x_u32)pitch_field;
    dw[2] = ((v9x_u32)y1 << 16) | (v9x_u32)x1;
    dw[3] = ((v9x_u32)y2 << 16) | (v9x_u32)x2;
    dw[4] = base;
    dw[5] = color;
    return v9x_hsw_mi(batch, dw, 6ul);
}

v9x_status v9x_intel_hsw_xy_src_copy_blt(struct v9x_hsw_batch *batch,
                                         v9x_u8 bytes_per_pixel,
                                         v9x_u16 dst_pitch,
                                         v9x_u16 dx1, v9x_u16 dy1,
                                         v9x_u16 dx2, v9x_u16 dy2,
                                         v9x_u32 dst_base,
                                         v9x_u16 src_pitch,
                                         v9x_u16 sx1, v9x_u16 sy1,
                                         v9x_u32 src_base)
{
    v9x_u32 dw[8];
    v9x_u32 depth;

    if (dx2 <= dx1 || dy2 <= dy1 || dst_pitch == 0u || src_pitch == 0u ||
        !v9x_hsw_blt_depth(bytes_per_pixel, &depth)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    dw[0] = V9X_HSW_XY_SRC_COPY_BLT;
    if (bytes_per_pixel == 4u) {
        dw[0] |= V9X_HSW_BLT_BYTE_MASK;
    }
    dw[1] = (depth << 24) | (V9X_HSW_BLT_ROP_SRCCOPY << 16) |
            (v9x_u32)dst_pitch;
    dw[2] = ((v9x_u32)dy1 << 16) | (v9x_u32)dx1;
    dw[3] = ((v9x_u32)dy2 << 16) | (v9x_u32)dx2;
    dw[4] = dst_base;
    dw[5] = ((v9x_u32)sy1 << 16) | (v9x_u32)sx1;
    dw[6] = (v9x_u32)src_pitch;
    dw[7] = src_base;
    return v9x_hsw_mi(batch, dw, 8ul);
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
            } else if (mi == 0x26ul && (header & 0x3ful) == 1ul) {
                /* MI_FLUSH_DW with a DWord write. */
                length = 3ul;
            } else if (header == V9X_HSW_MI_BATCH_BUFFER_START) {
                /* Only the flag-free form the builder emits. */
                length = 2ul;
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
        } else if (type == 2ul) {
            /* 2D client: XY_COLOR_BLT and XY_SRC_COPY_BLT, at their own
             * lengths; the byte mask and tiling bits may vary. */
            if ((header & 0xffc000fful) == V9X_HSW_XY_COLOR_BLT) {
                length = 6ul;
            } else if ((header & 0xffc000fful) == V9X_HSW_XY_SRC_COPY_BLT) {
                length = 8ul;
            } else {
                return v9x_hsw_refuse(check, V9X_HSW_CHECK_UNKNOWN_COMMAND,
                                      at, header);
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

/* Phase C state values. */
#define V9X_HSW_FLOAT_ONE            ((v9x_u32)0x3f800000ul)
#define V9X_HSW_DEPTH_FORMAT_D32_FLOAT ((v9x_u32)1ul) /* Vol 2b 20:18 */
#define V9X_HSW_DEPTH_WRITE_ENABLE   ((v9x_u32)0x10000000ul) /* DW1 28 */
#define V9X_HSW_POINTER_MBO          ((v9x_u32)1ul)
#define V9X_HSW_NO_BOUND             ((v9x_u32)0xfffff000ul)
#define V9X_HSW_PAGE_MASK            ((v9x_u32)0x00000ffful)
#define V9X_HSW_INIT_WIDTH           ((v9x_u16)640u)
#define V9X_HSW_INIT_HEIGHT          ((v9x_u16)480u)

/*
 * GT2's push constant space is 16 KB (Vol 2b 3DSTATE_PUSH_CONSTANT_ALLOC_*:
 * offset 0-15 KB, size 0-16 KB). VS and PS take 8 KB each, HS, DS and GS
 * none, Mesa's Gen7 split. The URB starts after it, in 8 KB units, so at
 * 2. The VS URB takes 64 entries of one 512-bit row: GT2's minimum is 64
 * ("[64,1664]", divisible by 8 below 9 rows). HS, DS and GS get 0 entries,
 * valid while they are disabled, starting after VS's 4 KB.
 */
#define V9X_HSW_INIT_PUSH_KB         ((v9x_u8)8u)
#define V9X_HSW_INIT_URB_START       ((v9x_u8)2u)
#define V9X_HSW_INIT_URB_NEXT        ((v9x_u8)3u)
#define V9X_HSW_INIT_VS_ENTRIES      ((v9x_u16)64u)

v9x_status v9x_intel_hsw_init_state(v9x_u32 *image, v9x_u32 dwords)
{
    v9x_u32 index;

    if (image == 0 || dwords < V9X_HSW_INIT_STATE_BYTES / 4ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < dwords; ++index) {
        image[index] = 0ul;
    }
    /* CC_VIEWPORT (Vol 2d): minimum depth 0.0, maximum depth 1.0. */
    image[V9X_HSW_INIT_CC_VIEWPORT / 4ul + 1ul] = V9X_HSW_FLOAT_ONE;
    /* A NULL RENDER_SURFACE_STATE, the format Mesa gives its null
     * surface; binding table entry 0 is its offset from the surface state
     * base, which is the state base. */
    image[V9X_HSW_INIT_NULL_SURFACE / 4ul] =
        ((v9x_u32)V9X_HSW_SURFTYPE_NULL << 29) |
        ((v9x_u32)V9X_HSW_FMT_B8G8R8A8_UNORM << 18);
    image[V9X_HSW_INIT_BINDING_TABLE / 4ul] = V9X_HSW_INIT_NULL_SURFACE;
    return V9X_STATUS_OK;
}

/* One table command with an all-zero payload of its fixed length. */
static v9x_status v9x_hsw_emit_zero(struct v9x_hsw_batch *batch,
                                    v9x_u16 command)
{
    static const v9x_u32 zero[13] = { 0ul };

    return v9x_intel_hsw_3d_emit(batch, command, zero,
                                 (v9x_u32)v9x_hsw_commands[command].total -
                                     1ul);
}

/*
 * The order is i915's and Mesa's: pipeline, bases, URB, stages, then the
 * pointers. Every PIPE_CONTROL with CS stall also sets one of the bits
 * Vol 7 requires beside it (RT flush, scoreboard stall or a post-sync
 * operation). Vol 2b's MI_BATCH_BUFFER_START note fixes the end of every
 * 3D batch: CC pointers, then an RT-flush CS-stall PIPE_CONTROL. The
 * breadcrumb follows a CS-stall scoreboard PIPE_CONTROL, as i915 orders a
 * Gen7 post-sync write.
 */
/* 3DSTATE_SF DW2 Cull Mode 30:29, CULLMODE_NONE = 1 (Vol 2b). */
#define V9X_HSW_SF_CULL_NONE         ((v9x_u32)0x20000000ul)
/* 3DSTATE_WM DW1: Thread Dispatch Enable 29, Barycentric Mode 16:11. */
#define V9X_HSW_WM_DISPATCH          ((v9x_u32)0x20000000ul)
/* 3DSTATE_PS DW4: Maximum Number of Threads 31:23 is the count minus 1
 * and must be odd; GT2 allows 7-203. 63 = 64 threads. */
#define V9X_HSW_PS_MAX_THREADS_FIELD ((v9x_u32)63ul)
#define V9X_HSW_PS_SAMPLE_MASK_ONE   ((v9x_u32)0x00001000ul) /* 19:12 = 1 */
#define V9X_HSW_PS_ATTRIBUTE_ENABLE  ((v9x_u32)0x00000400ul)
#define V9X_HSW_PS_SIMD8             ((v9x_u32)0x00000001ul)
#define V9X_HSW_DRAW_SETUP_GRF       ((v9x_u8)4u)

/* 3DSTATE_CLIP DW2: Clip Enable 31, Clip Mode 15:13 (ACCEPT_ALL 4),
 * Perspective Divide Disable 9 (Vol 2b). With the divide on, the unit
 * divided x and y by the rhw D3D puts in w, even with clipping off: the
 * 4570 drew V9XDDP's rhw 0.05-8 scenes moved or not at all. */
#define V9X_HSW_CLIP_SCREEN_DW2 ((v9x_u32)0x80008200ul)

v9x_status v9x_intel_hsw_clip_screen(struct v9x_hsw_batch *batch)
{
    v9x_u32 payload[3];

    payload[0] = 0ul;
    payload[1] = V9X_HSW_CLIP_SCREEN_DW2;
    payload[2] = 0ul;
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_CLIP, payload, 3ul);
}

v9x_status v9x_intel_hsw_sf_screen(struct v9x_hsw_batch *batch)
{
    return v9x_intel_hsw_sf_screen_depth(batch, 0u);
}

/* DW1 Depth Buffer Surface Format 14:12 matches the depth buffer's. */
v9x_status v9x_intel_hsw_sf_screen_depth(struct v9x_hsw_batch *batch,
                                         v9x_u8 depth_format)
{
    return v9x_intel_hsw_sf_screen_cull(batch, depth_format,
                                        V9X_HSW_DRAW_CULL_NONE);
}

/* 3DSTATE_SF DW2 CULLMODE_BACK = 3, and DW1 Front Winding, bit 0:
 * FRONTWINDING_CW = 0, CCW = 1 (Vol 2b). */
#define V9X_HSW_SF_CULL_BACK         ((v9x_u32)0x60000000ul)
#define V9X_HSW_SF_FRONT_CCW         ((v9x_u32)0x00000001ul)

/*
 * The cull as a back-face cull: the winding Direct3D discards is made the
 * back face. Screen-space winding, the same space the core's list path
 * judges it in (v9x_d3d_triangle_culled); V9XD32 T11's cull matrix checks
 * the direction on the chip, which is where the Rage 128's was found
 * reversed (c696d43a).
 */
v9x_status v9x_intel_hsw_sf_screen_cull(struct v9x_hsw_batch *batch,
                                        v9x_u8 depth_format, v9x_u8 cull)
{
    v9x_u32 payload[6];
    v9x_u32 index;

    if (depth_format > 7u || cull > V9X_HSW_DRAW_CULL_CCW) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < 6ul; ++index) {
        payload[index] = 0ul;
    }
    payload[0] = (v9x_u32)depth_format << 12;
    payload[1] = V9X_HSW_SF_CULL_NONE;
    if (cull == V9X_HSW_DRAW_CULL_CW) {
        payload[0] |= V9X_HSW_SF_FRONT_CCW;
        payload[1] = V9X_HSW_SF_CULL_BACK;
    } else if (cull == V9X_HSW_DRAW_CULL_CCW) {
        payload[1] = V9X_HSW_SF_CULL_BACK;
    }
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_SF, payload, 6ul);
}

/* DW1: Number of SF Output Attributes 27:22, Vertex URB Entry Read
 * Length 15:11, Read Offset 9:4; DW2-13 swizzles and enables, all 0. */
v9x_status v9x_intel_hsw_sbe(struct v9x_hsw_batch *batch, v9x_u8 outputs,
                             v9x_u8 read_offset, v9x_u8 read_length)
{
    return v9x_intel_hsw_sbe_constant(batch, outputs, read_offset,
                                      read_length, 0ul);
}

v9x_status v9x_intel_hsw_sbe_constant(struct v9x_hsw_batch *batch,
                                      v9x_u8 outputs, v9x_u8 read_offset,
                                      v9x_u8 read_length,
                                      v9x_u32 constant_mask)
{
    v9x_u32 payload[13];
    v9x_u32 index;

    if (outputs > 32u || read_offset > 0x3fu || read_length == 0u ||
        read_length > 16u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < 13ul; ++index) {
        payload[index] = 0ul;
    }
    payload[0] = ((v9x_u32)outputs << 22) | ((v9x_u32)read_length << 11) |
                 ((v9x_u32)read_offset << 4);
    payload[10] = constant_mask;
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_SBE, payload, 13ul);
}

v9x_status v9x_intel_hsw_wm(struct v9x_hsw_batch *batch, v9x_u8 barycentric)
{
    v9x_u32 payload[2];

    if (barycentric > 0x3fu) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = V9X_HSW_WM_DISPATCH | ((v9x_u32)barycentric << 11);
    payload[1] = 0ul;
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_WM, payload, 2ul);
}

/* DW1 Kernel Start Pointer[0] 31:6; DW2 Sampler Count 29:27 (in fours),
 * Binding Table Entry Count 25:18; DW3 no scratch; DW4 threads, sample
 * mask (HSW, matching 3DSTATE_SAMPLE_MASK), attribute enable, 8 Pixel
 * Dispatch Enable; DW5 Dispatch GRF Start Register for Constant/Setup
 * Data [0] 22:16. */
v9x_status v9x_intel_hsw_ps_simd8(struct v9x_hsw_batch *batch,
                                  v9x_u32 kernel, v9x_u8 binding_entries,
                                  v9x_u8 samplers, v9x_u8 attributes,
                                  v9x_u8 grf_start)
{
    v9x_u32 payload[7];

    if ((kernel & 0x3ful) != 0ul || grf_start > 0x7fu || samplers > 4u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    payload[0] = kernel;
    payload[1] = ((v9x_u32)samplers << 27) |
                 ((v9x_u32)binding_entries << 18);
    payload[2] = 0ul;
    if (v9x_hsw_3d_is_ivb()) {
        /* IVB: threads at 31:24 (GT2 has 172, Mesa), no Sample Mask. */
        payload[3] = (V9X_HSW_PS_MAX_THREADS_FIELD << 24) | V9X_HSW_PS_SIMD8;
    } else {
        payload[3] = (V9X_HSW_PS_MAX_THREADS_FIELD << 23) |
                     V9X_HSW_PS_SAMPLE_MASK_ONE | V9X_HSW_PS_SIMD8;
    }
    if (attributes != 0u) {
        payload[3] |= V9X_HSW_PS_ATTRIBUTE_ENABLE;
    }
    payload[4] = (v9x_u32)grf_start << 16;
    payload[5] = 0ul;
    payload[6] = 0ul;
    return v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_PS, payload, 7ul);
}

v9x_status v9x_intel_hsw_draw_state(v9x_u32 *image, v9x_u32 dwords,
                                    const v9x_u32 rt[2], v9x_u16 width,
                                    v9x_u16 height, v9x_u32 pitch,
                                    const v9x_u32 vertices[18])
{
    v9x_u32 index;
    v9x_u32 n;

    if (image == 0 || rt == 0 || vertices == 0 ||
        dwords < V9X_HSW_DRAW_STATE_BYTES / 4ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (n = 0ul; n < 2ul; ++n) {
        if (v9x_intel_hsw_surface_2d(
                image + V9X_HSW_DRAW_RT_SURFACE(n) / 4ul,
                V9X_HSW_FMT_B8G8R8A8_UNORM, rt[n], width, height, pitch) !=
            V9X_STATUS_OK) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        image[V9X_HSW_DRAW_BINDING_TABLE(n) / 4ul] =
            V9X_HSW_DRAW_RT_SURFACE(n);
    }
    for (index = 0ul; index < 18ul; ++index) {
        image[V9X_HSW_DRAW_VERTICES / 4ul + index] = vertices[index];
    }
    return V9X_STATUS_OK;
}

/* SAMPLER_STATE DW3 TCX 8:6, TCY 5:3, TCZ 2:0 (Vol 2d); TEXCOORDMODE_CLAMP
 * is 2 (Vol 2a). BLEND_STATE's alpha test function is COMPAREFUNCTION_
 * GREATER, 5 (Vol 2a 3D_Compare_Function). */
#define V9X_HSW_TEXCOORD_CLAMP_ALL   ((v9x_u32)((2ul << 6) | (2ul << 3) | 2ul))
#define V9X_HSW_TEXCOORD_WRAP_ALL    ((v9x_u32)0ul)
#define V9X_HSW_TEXCOORD_MIRROR_ALL  ((v9x_u32)((1ul << 6) | (1ul << 3) | 1ul))
#define V9X_HSW_COMPARE_GREATER      ((v9x_u8)5u)
/* MAPFILTER_LINEAR (Vol 2d), BLENDFACTOR_SRC_ALPHA and INV_SRC_ALPHA,
 * BLENDFUNCTION_ADD (Vol 2a). */
#define V9X_HSW_MAPFILTER_LINEAR     ((v9x_u8)1u)
#define V9X_HSW_BLENDFACTOR_SRC_ALPHA ((v9x_u8)0x03u)
#define V9X_HSW_BLENDFACTOR_INV_SRC_ALPHA ((v9x_u8)0x13u)

/* One SAMPLER_STATE of E's kind - no mips, the shared border colour - as
 * samplers 0 and 1 of the set at offset: the generator samples texture 1
 * with sampler 1 since render interface step 4, and the probe's two-stage
 * draws were measured with one sampler for both. */
static void v9x_hsw_shade_sampler(v9x_u32 *image, v9x_u32 offset,
                                  v9x_u8 filter, v9x_u32 address)
{
    struct v9x_hsw_sampler sampler;

    sampler.mip_filter = 0u;
    sampler.mag_filter = filter;
    sampler.min_filter = filter;
    sampler.min_lod = 0u;
    sampler.max_lod = 0u;
    sampler.border_color_pointer = V9X_HSW_SHADE_BORDER_COLOR;
    sampler.dw3 = address;
    v9x_intel_hsw_sampler(image + offset / 4ul, &sampler);
    v9x_intel_hsw_sampler(image + (offset + V9X_HSW_SAMPLER_BYTES) / 4ul,
                          &sampler);
}

v9x_status v9x_intel_hsw_shade_state(v9x_u32 *image, v9x_u32 dwords,
                                     v9x_u32 state_base, const v9x_u32 rt[2],
                                     v9x_u16 width, v9x_u16 height,
                                     v9x_u32 pitch, v9x_u8 alpha_ref)
{
    struct v9x_hsw_blend blend;
    v9x_u32 n;

    if (image == 0 || rt == 0 || dwords < V9X_HSW_SHADE_STATE_BYTES / 4ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (n = 0ul; n < 2ul; ++n) {
        if (v9x_intel_hsw_surface_2d(
                image + V9X_HSW_DRAW_RT_SURFACE(n) / 4ul,
                V9X_HSW_FMT_B8G8R8A8_UNORM, rt[n], width, height, pitch) !=
                V9X_STATUS_OK ||
            v9x_intel_hsw_surface_2d(
                image + V9X_HSW_SHADE_TEX_SURFACE(n) / 4ul,
                V9X_HSW_FMT_B8G8R8A8_UNORM,
                state_base + V9X_HSW_SHADE_TEXTURE(n),
                V9X_HSW_SHADE_TEXTURE_SIDE, V9X_HSW_SHADE_TEXTURE_SIDE,
                (v9x_u32)V9X_HSW_SHADE_TEXTURE_SIDE * 4ul) !=
                V9X_STATUS_OK) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        image[V9X_HSW_SHADE_BINDING_TABLE(n) / 4ul] =
            V9X_HSW_DRAW_RT_SURFACE(n);
        image[V9X_HSW_SHADE_BINDING_TABLE(n) / 4ul + 1ul] =
            V9X_HSW_SHADE_TEX_SURFACE(0);
        image[V9X_HSW_SHADE_BINDING_TABLE(n) / 4ul + 2ul] =
            V9X_HSW_SHADE_TEX_SURFACE(1);
    }

    v9x_hsw_shade_sampler(image, V9X_HSW_SHADE_SAMPLER, 0u,
                          V9X_HSW_TEXCOORD_CLAMP_ALL);

    blend.enable = 0u;
    blend.color_function = 0u;
    blend.src_factor = 0u;
    blend.dst_factor = 0u;
    blend.independent_alpha = 0u;
    blend.alpha_function = 0u;
    blend.src_alpha_factor = 0u;
    blend.dst_alpha_factor = 0u;
    blend.write_disable = 0u;
    blend.alpha_test = 1u;
    blend.alpha_function_test = V9X_HSW_COMPARE_GREATER;
    v9x_intel_hsw_blend(image + V9X_HSW_SHADE_BLEND_ALPHA_TEST / 4ul, &blend);

    v9x_hsw_shade_sampler(image, V9X_HSW_SHADE_SAMPLER_LINEAR,
                          V9X_HSW_MAPFILTER_LINEAR,
                          V9X_HSW_TEXCOORD_CLAMP_ALL);
    v9x_hsw_shade_sampler(image, V9X_HSW_SHADE_SAMPLER_WRAP, 0u,
                          V9X_HSW_TEXCOORD_WRAP_ALL);
    v9x_hsw_shade_sampler(image, V9X_HSW_SHADE_SAMPLER_MIRROR, 0u,
                          V9X_HSW_TEXCOORD_MIRROR_ALL);
    /* Alpha uses the colour's factors while Independent Alpha Blend is
     * off (Vol 2d BLEND_STATE). */
    blend.alpha_test = 0u;
    blend.alpha_function_test = 0u;
    blend.enable = 1u;
    blend.color_function = 0u;
    blend.src_factor = V9X_HSW_BLENDFACTOR_SRC_ALPHA;
    blend.dst_factor = V9X_HSW_BLENDFACTOR_INV_SRC_ALPHA;
    v9x_intel_hsw_blend(image + V9X_HSW_SHADE_BLEND_ALPHA / 4ul, &blend);
    /* Phase F2b: blending off, every channel's write disabled. */
    blend.enable = 0u;
    blend.src_factor = 0u;
    blend.dst_factor = 0u;
    blend.write_disable = 0xfu;
    v9x_intel_hsw_blend(image + V9X_HSW_SHADE_BLEND_NO_COLOUR / 4ul, &blend);

    /* COMPAREFUNCTION_ALWAYS 0, LESS 2 (Vol 2a). */
    v9x_intel_hsw_depth_stencil(image + V9X_HSW_SHADE_DS_ALWAYS / 4ul, 1u,
                                0u, 1u);
    v9x_intel_hsw_depth_stencil(image + V9X_HSW_SHADE_DS_LESS / 4ul, 1u,
                                2u, 1u);
    /* LEQUAL 4 (Vol 2a), Phase F2b. */
    v9x_intel_hsw_depth_stencil(image + V9X_HSW_SHADE_DS_LEQUAL / 4ul, 1u,
                                4u, 1u);
    return v9x_intel_hsw_cc_state(image + V9X_HSW_SHADE_CC / 4ul, alpha_ref);
}

/*
 * The VUE the VF writes with the VS disabled: a header of four zeros,
 * position (x, y, 0, 1.0), then the attributes (Vol 7 VUE layout; the
 * order i915's Gen7 render copy uses). One attribute is D's colour; four
 * are E's diffuse, specular and two texture coordinate pairs (u, v, 0,
 * 1.0).
 */
static v9x_status v9x_hsw_draw_vertices(struct v9x_hsw_batch *batch,
                                        const struct v9x_hsw_draw *draw)
{
    struct v9x_hsw_vertex_element elements[6];
    v9x_u32 index;
    v9x_u32 count;
    v9x_u32 vertices;
    v9x_u16 stride;

    count = draw->attributes == V9X_HSW_SHADE_ATTRIBUTES ? 6ul : 3ul;
    stride = draw->tl_vertices ? (draw->tl_uv1 ? V9X_HSW_TL2_VERTEX_BYTES :
                                                 V9X_HSW_TL_VERTEX_BYTES) :
             draw->attributes == V9X_HSW_SHADE_ATTRIBUTES ?
                 V9X_HSW_SHADE_VERTEX_BYTES :
             draw->depth_buffer != 0ul ? V9X_HSW_DEPTH_VERTEX_BYTES :
                                         V9X_HSW_DRAW_VERTEX_BYTES;
    for (index = 0ul; index < count; ++index) {
        elements[index].buffer = 0u;
        elements[index].format = V9X_HSW_FMT_R32G32B32A32_FLOAT;
        elements[index].offset = 0u;
        elements[index].component[0] = V9X_HSW_VFCOMP_STORE_SRC;
        elements[index].component[1] = V9X_HSW_VFCOMP_STORE_SRC;
        elements[index].component[2] = V9X_HSW_VFCOMP_STORE_SRC;
        elements[index].component[3] = V9X_HSW_VFCOMP_STORE_SRC;
    }
    for (index = 0ul; index < 4ul; ++index) {
        elements[0].component[index] = V9X_HSW_VFCOMP_STORE_0;
    }
    elements[1].format = V9X_HSW_FMT_R32G32_FLOAT;
    elements[1].component[2] = V9X_HSW_VFCOMP_STORE_0;
    elements[1].component[3] = V9X_HSW_VFCOMP_STORE_1_FP;
    elements[2].offset = 8u;
    if (draw->depth_buffer != 0ul) {
        /* Position x, y, z, 1.0; colour after it. */
        elements[1].format = V9X_HSW_FMT_R32G32B32_FLOAT;
        elements[1].component[2] = V9X_HSW_VFCOMP_STORE_SRC;
        elements[2].offset = 12u;
    }
    if (draw->tl_vertices) {
        /* sx sy sz rhw as the position, so w carries rhw; the colours as
         * B8G8R8A8_UNORM, D3DCOLOR's byte order; tu tv for both texture
         * attributes. */
        elements[1].format = V9X_HSW_FMT_R32G32B32A32_FLOAT;
        elements[1].component[2] = V9X_HSW_VFCOMP_STORE_SRC;
        elements[1].component[3] = V9X_HSW_VFCOMP_STORE_SRC;
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
        /* Two units: tu1 tv1 follow the TL vertex. */
        if (draw->tl_uv1) {
            elements[5].offset = V9X_HSW_TL_VERTEX_BYTES;
        }
    } else if (count == 6ul) {
        elements[3].offset = 24u;
        for (index = 4ul; index < 6ul; ++index) {
            elements[index].format = V9X_HSW_FMT_R32G32_FLOAT;
            elements[index].offset = (v9x_u16)(40u + 8u * (index - 4ul));
            elements[index].component[2] = V9X_HSW_VFCOMP_STORE_0;
            elements[index].component[3] = V9X_HSW_VFCOMP_STORE_1_FP;
        }
    }

    if (draw->depth_buffer != 0ul && draw->depth_clear != 0ul) {
        /* The clear, then the scene under the depth test; a pointer change
         * between primitives needs no flush. */
        v9x_intel_hsw_vertex_buffer(
            batch, 0u, stride, draw->vertices,
            draw->vertices +
                (V9X_HSW_DEPTH_CLEAR_VERTICES + V9X_HSW_DEPTH_SCENE_VERTICES) *
                    stride - 1ul);
        v9x_intel_hsw_vertex_elements(batch, elements, (v9x_u8)count);
        v9x_intel_hsw_3dprimitive(batch, V9X_HSW_PRIM_TRILIST,
                                  V9X_HSW_DEPTH_CLEAR_VERTICES, 0ul);
        v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_DEPTH_STENCIL_PTRS,
                                    draw->depth_test | V9X_HSW_POINTER_MBO);
        return v9x_intel_hsw_3dprimitive(batch, V9X_HSW_PRIM_TRILIST,
                                         V9X_HSW_DEPTH_SCENE_VERTICES,
                                         V9X_HSW_DEPTH_CLEAR_VERTICES);
    }
    vertices = draw->vertex_count != 0ul ? draw->vertex_count : 3ul;
    v9x_intel_hsw_vertex_buffer(batch, 0u, stride, draw->vertices,
                                draw->vertices + vertices * stride - 1ul);
    v9x_intel_hsw_vertex_elements(batch, elements, (v9x_u8)count);
    if (draw->indices != 0ul) {
        /* The index buffer to the end of the cache line holding its last
         * index, which the caller has zeroed (Vol 2b's Haswell note). */
        v9x_intel_hsw_index_buffer(
            batch, draw->indices,
            ((draw->indices + draw->index_count * 2ul + 0x3ful) &
             ~(v9x_u32)0x3ful) - 1ul);
        return v9x_intel_hsw_3dprimitive_indexed(
            batch, draw->strip ? V9X_HSW_PRIM_TRISTRIP : V9X_HSW_PRIM_TRILIST,
            draw->index_count, 0ul);
    }
    return v9x_intel_hsw_3dprimitive(batch, V9X_HSW_PRIM_TRILIST, vertices,
                                     0ul);
}

/* Ivy Bridge's two workaround PIPE_CONTROLs (intel_hsw_3d.h): crocus's
 * gen7_emit_cs_stall_flush and gen7_emit_vs_workaround_flush, both writing
 * 0 to the workaround dword. */
static void v9x_hsw_ivb_cs_stall_flush(struct v9x_hsw_batch *batch)
{
    v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_CS_STALL |
                               V9X_HSW_PC_WRITE_IMMEDIATE | V9X_HSW_PC_GGTT,
                               v9x_hsw_3d_wa_address, 0ul);
}

static void v9x_hsw_ivb_vs_flush(struct v9x_hsw_batch *batch)
{
    v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_DEPTH_STALL |
                               V9X_HSW_PC_WRITE_IMMEDIATE | V9X_HSW_PC_GGTT,
                               v9x_hsw_3d_wa_address, 0ul);
}

/*
 * The init batch (draw == 0) and the draw batch share one sequence; the
 * draw replaces the zeroed CLIP-PS block with real state, the drawing
 * rectangle and binding table with the target's, and adds the vertices
 * and the primitive before the end.
 */
static v9x_status v9x_hsw_workload(struct v9x_hsw_batch *batch,
                                   v9x_u32 state_base, v9x_u32 state_bytes,
                                   const struct v9x_hsw_draw *draw,
                                   v9x_u32 done_address, v9x_u32 done_value)
{
    static const v9x_u16 disabled[6] = {
        V9X_HSW_CMD_VS, V9X_HSW_CMD_HS, V9X_HSW_CMD_TE,
        V9X_HSW_CMD_DS, V9X_HSW_CMD_GS, V9X_HSW_CMD_STREAMOUT
    };
    static const v9x_u16 zeroed[8] = {
        V9X_HSW_CMD_CLIP, V9X_HSW_CMD_SF, V9X_HSW_CMD_SBE, V9X_HSW_CMD_WM,
        V9X_HSW_CMD_PS, V9X_HSW_CMD_MULTISAMPLE, V9X_HSW_CMD_CONSTANT_PS,
        V9X_HSW_CMD_COUNT
    };
    v9x_u32 depth[6];
    v9x_u32 bound;
    v9x_u32 mask;
    v9x_u32 index;
    v9x_u32 blend;
    v9x_u32 cc;
    v9x_u32 sampler;
    v9x_u16 vue_rows;
    v9x_u8 depth_format;
    int shade;
    v9x_status status;

    if (batch == 0 || (state_base & V9X_HSW_PAGE_MASK) != 0ul ||
        (state_bytes & V9X_HSW_PAGE_MASK) != 0ul ||
        state_bytes < V9X_HSW_INIT_STATE_BYTES ||
        (done_address & 7ul) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    bound = state_base + state_bytes;
    status = V9X_STATUS_OK;
    /* A zero offset in the draw means the init state's. E's four
     * attributes make a 24-dword VUE, two 512-bit rows (field 1); D's
     * 12 dwords fit one (field 0). */
    shade = draw != 0 && draw->attributes == V9X_HSW_SHADE_ATTRIBUTES;
    blend = draw != 0 && draw->depth_only ? V9X_HSW_SHADE_BLEND_NO_COLOUR :
            draw != 0 && draw->blend != 0ul ? draw->blend :
                                              V9X_HSW_INIT_BLEND;
    cc = draw != 0 && draw->cc != 0ul ? draw->cc : V9X_HSW_INIT_CC;
    sampler = draw != 0 && draw->sampler != 0ul ? draw->sampler :
                                                  V9X_HSW_INIT_SAMPLER;
    vue_rows = shade ? 1u : 0u;
    depth_format = draw != 0 && draw->depth_format != 0u ?
                       draw->depth_format : V9X_HSW_DEPTH_D32_FLOAT;

    v9x_intel_hsw_pipeline_select_3d(batch);
    if (v9x_hsw_3d_is_ivb()) {
        /* IVB PRM PIPELINE_SELECT, as i965 quotes it: "a pipe_control with
         * a CS stall and a post sync operation and then a dummy DRAW ...
         * after any PIPELINE_SELECT that is enabling 3D mode". */
        v9x_hsw_ivb_cs_stall_flush(batch);
        v9x_intel_hsw_3dprimitive(batch, V9X_HSW_PRIM_POINTLIST, 0ul, 0ul);
    }
    v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_CS_STALL |
                               V9X_HSW_PC_STALL_AT_SCOREBOARD, 0ul, 0ul);
    v9x_intel_hsw_state_base_address(batch, state_base, state_base,
                                     state_base, 0ul, state_base,
                                     V9X_HSW_NO_BOUND, bound,
                                     V9X_HSW_NO_BOUND, bound);
    /* The VF cache too: without it the 4570 fetched the previous run's
     * vertices from the same address (Phase F1a, docs\decisions\
     * 2026-10-17-haswell-phase-f1a-filter-wrap-blend.md). */
    v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_CS_STALL |
                               V9X_HSW_PC_STALL_AT_SCOREBOARD |
                               V9X_HSW_PC_STATE_CACHE_INVALIDATE |
                               V9X_HSW_PC_CONST_CACHE_INVALIDATE |
                               V9X_HSW_PC_VF_CACHE_INVALIDATE |
                               V9X_HSW_PC_TEXTURE_CACHE_INVALIDATE |
                               V9X_HSW_PC_INSTRUCTION_CACHE_INVALIDATE,
                               0ul, 0ul);

    v9x_intel_hsw_push_constant_alloc(batch,
                                      V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_VS, 0u,
                                      V9X_HSW_INIT_PUSH_KB);
    v9x_intel_hsw_push_constant_alloc(batch,
                                      V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_HS,
                                      V9X_HSW_INIT_PUSH_KB, 0u);
    v9x_intel_hsw_push_constant_alloc(batch,
                                      V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_DS,
                                      V9X_HSW_INIT_PUSH_KB, 0u);
    v9x_intel_hsw_push_constant_alloc(batch,
                                      V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_GS,
                                      V9X_HSW_INIT_PUSH_KB, 0u);
    v9x_intel_hsw_push_constant_alloc(batch,
                                      V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_PS,
                                      V9X_HSW_INIT_PUSH_KB,
                                      V9X_HSW_INIT_PUSH_KB);
    if (v9x_hsw_3d_is_ivb()) {
        /* IVB PRM 3DSTATE_PUSH_CONSTANT_ALLOC_PS (crocus): "A PIPE_CONTROL
         * command with the CS Stall bit set must be programmed in the ring
         * after this instruction"; then the VS workaround before URB_VS. */
        v9x_hsw_ivb_cs_stall_flush(batch);
        v9x_hsw_ivb_vs_flush(batch);
    }
    v9x_intel_hsw_urb(batch, V9X_HSW_CMD_URB_VS, V9X_HSW_INIT_URB_START,
                      vue_rows, V9X_HSW_INIT_VS_ENTRIES);
    v9x_intel_hsw_urb(batch, V9X_HSW_CMD_URB_HS, V9X_HSW_INIT_URB_NEXT, 0u,
                      0u);
    v9x_intel_hsw_urb(batch, V9X_HSW_CMD_URB_DS, V9X_HSW_INIT_URB_NEXT, 0u,
                      0u);
    v9x_intel_hsw_urb(batch, V9X_HSW_CMD_URB_GS, V9X_HSW_INIT_URB_NEXT, 0u,
                      0u);

    /* IVB: the VS workaround before 3DSTATE_VS too (crocus). */
    if (v9x_hsw_3d_is_ivb()) {
        v9x_hsw_ivb_vs_flush(batch);
    }
    for (index = 0ul; index < 6ul; ++index) {
        v9x_intel_hsw_stage_disabled(batch, disabled[index]);
    }
    if (draw == 0) {
        for (index = 0ul; zeroed[index] != V9X_HSW_CMD_COUNT; ++index) {
            v9x_hsw_emit_zero(batch, zeroed[index]);
        }
    } else {
        /* CLIP stays zero: clipping off, so the triangle passes through.
         * The attributes follow the VUE's header and position: read
         * offset 1, in 256-bit units, two attributes per unit. Setup data
         * follows r0, r1 and the SIMD8 barycentrics r2-r3. */
        if (draw->tl_vertices) {
            v9x_intel_hsw_clip_screen(batch);
        } else {
            v9x_hsw_emit_zero(batch, V9X_HSW_CMD_CLIP);
        }
        v9x_intel_hsw_sf_screen_cull(
            batch, draw->depth_buffer != 0ul ? depth_format : (v9x_u8)0u,
            draw->cull);
        if (shade) {
            /* Flat: diffuse and specular (attributes 0 and 1) constant. */
            v9x_intel_hsw_sbe_constant(batch, V9X_HSW_SHADE_ATTRIBUTES, 1u,
                                       2u, draw->flat ? 3ul : 0ul);
        } else {
            v9x_intel_hsw_sbe(batch, 1u, 1u, 1u);
        }
        /* A depth-only draw still dispatches the shader; its colour
         * writes are masked by the blend state below. A zero WM and PS,
         * with or without SBE's attribute, never retired on the 4570
         * (Phase F2b, runs 1 and 2). */
        v9x_intel_hsw_wm(batch, V9X_HSW_BARY_PERSPECTIVE_PIXEL);
        v9x_intel_hsw_ps_simd8(batch, draw->kernel,
                               shade ? V9X_HSW_SHADE_BINDING_ENTRIES : 1u,
                               shade ? 1u : 0u, 1u, V9X_HSW_DRAW_SETUP_GRF);
        v9x_hsw_emit_zero(batch, V9X_HSW_CMD_MULTISAMPLE);
        v9x_hsw_emit_zero(batch, V9X_HSW_CMD_CONSTANT_PS);
    }

    /* A NULL depth buffer: SURFTYPE_NULL 31:29, D32_FLOAT 20:18. */
    for (index = 0ul; index < 6ul; ++index) {
        depth[index] = 0ul;
    }
    depth[0] = ((v9x_u32)V9X_HSW_SURFTYPE_NULL << 29) |
               (V9X_HSW_DEPTH_FORMAT_D32_FLOAT << 18);
    if (draw != 0 && draw->depth_buffer != 0ul) {
        /* Vol 2b: before changing depth buffer state, a depth stall, a
         * depth cache flush, another depth stall. Then a 2D D32_FLOAT or
         * D16_UNORM buffer with depth write (DW1 31:29, 28, 20:18, pitch -
         * 1 17:0),
         * its base, height - 1 31:18 and width - 1 17:4; then HiZ,
         * stencil and clear parameters, all off. */
        v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_DEPTH_STALL, 0ul, 0ul);
        v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_DEPTH_CACHE_FLUSH, 0ul,
                                   0ul);
        v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_DEPTH_STALL, 0ul, 0ul);
        depth[0] = ((v9x_u32)V9X_HSW_SURFTYPE_2D << 29) |
                   V9X_HSW_DEPTH_WRITE_ENABLE |
                   ((v9x_u32)depth_format << 18) |
                   (draw->depth_pitch != 0ul ? draw->depth_pitch - 1ul :
                                               (v9x_u32)draw->width * 4ul -
                                                   1ul);
        depth[1] = draw->depth_buffer;
        depth[2] = ((v9x_u32)(draw->height - 1u) << 18) |
                   ((v9x_u32)(draw->width - 1u) << 4);
    }
    v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_DEPTH_BUFFER, depth, 6ul);
    if (draw != 0 && draw->depth_buffer != 0ul) {
        v9x_hsw_emit_zero(batch, V9X_HSW_CMD_HIER_DEPTH_BUFFER);
        v9x_hsw_emit_zero(batch, V9X_HSW_CMD_STENCIL_BUFFER);
        v9x_hsw_emit_zero(batch, V9X_HSW_CMD_CLEAR_PARAMS);
    }
    mask = 1ul;
    v9x_intel_hsw_3d_emit(batch, V9X_HSW_CMD_SAMPLE_MASK, &mask, 1ul);
    if (draw == 0) {
        v9x_intel_hsw_drawing_rectangle(batch, 0u, 0u,
                                        (v9x_u16)(V9X_HSW_INIT_WIDTH - 1u),
                                        (v9x_u16)(V9X_HSW_INIT_HEIGHT - 1u));
    } else if (draw->scissor) {
        /* Render interface step 2: the drawing rectangle clips every pixel
         * (Vol 2b), so the scissor is this rectangle, with the origin 0. */
        v9x_intel_hsw_drawing_rectangle(batch, draw->scissor_x0,
                                        draw->scissor_y0, draw->scissor_x1,
                                        draw->scissor_y1);
    } else {
        v9x_intel_hsw_drawing_rectangle(batch, 0u, 0u,
                                        (v9x_u16)(draw->width - 1u),
                                        (v9x_u16)(draw->height - 1u));
    }

    v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_VIEWPORT_PTRS_CC,
                                V9X_HSW_INIT_CC_VIEWPORT);
    v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_VIEWPORT_PTRS_SF_CLIP,
                                V9X_HSW_INIT_SF_CLIP_VIEWPORT);
    v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_SCISSOR_PTRS,
                                V9X_HSW_INIT_SCISSOR);
    /* Bit 0 of these three is printed "MB0" in Vol 2b; Mesa writes 1. */
    v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_BLEND_PTRS,
                                blend | V9X_HSW_POINTER_MBO);
    v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_DEPTH_STENCIL_PTRS,
                                (draw != 0 && draw->depth_buffer != 0ul ?
                                     (draw->depth_clear != 0ul ?
                                          draw->depth_clear :
                                          draw->depth_test) :
                                     V9X_HSW_INIT_DEPTH_STENCIL) |
                                    V9X_HSW_POINTER_MBO);
    v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_CC_PTRS,
                                cc | V9X_HSW_POINTER_MBO);
    v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_BINDING_TABLE_PTRS_PS,
                                draw == 0 ? V9X_HSW_INIT_BINDING_TABLE :
                                            draw->binding_table);
    v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_SAMPLER_PTRS_PS,
                                sampler);
    if (draw != 0) {
        v9x_hsw_draw_vertices(batch, draw);
    }

    v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_RT_CACHE_FLUSH |
                               V9X_HSW_PC_CS_STALL, 0ul, 0ul);
    v9x_intel_hsw_state_pointer(batch, V9X_HSW_CMD_CC_PTRS,
                                cc | V9X_HSW_POINTER_MBO);
    v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_RT_CACHE_FLUSH |
                               V9X_HSW_PC_CS_STALL, 0ul, 0ul);

    v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_CS_STALL |
                               V9X_HSW_PC_STALL_AT_SCOREBOARD, 0ul, 0ul);
    v9x_intel_hsw_pipe_control(batch, V9X_HSW_PC_CS_STALL |
                               V9X_HSW_PC_WRITE_IMMEDIATE | V9X_HSW_PC_GGTT,
                               done_address, done_value);
    v9x_intel_hsw_mi_batch_buffer_end(batch);
    if ((batch->used & 1ul) != 0ul) {
        v9x_intel_hsw_mi_noop(batch);
    }

    /* Each builder above refuses only on overflow with these arguments,
     * and overflow is sticky, so one test covers them all. */
    if (batch->overflow != 0u) {
        status = V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    return status;
}

v9x_status v9x_intel_hsw_init_workload(struct v9x_hsw_batch *batch,
                                       v9x_u32 state_base,
                                       v9x_u32 state_bytes,
                                       v9x_u32 done_address,
                                       v9x_u32 done_value)
{
    return v9x_hsw_workload(batch, state_base, state_bytes, 0, done_address,
                            done_value);
}

v9x_status v9x_intel_hsw_draw_workload(struct v9x_hsw_batch *batch,
                                       v9x_u32 state_base,
                                       v9x_u32 state_bytes,
                                       const struct v9x_hsw_draw *draw,
                                       v9x_u32 done_address,
                                       v9x_u32 done_value)
{
    if (draw == 0 || (draw->kernel & 0x3ful) != 0ul ||
        (draw->binding_table & 0x1ful) != 0ul || draw->width == 0u ||
        draw->height == 0u || (draw->vertices & 3ul) != 0ul ||
        state_bytes < V9X_HSW_DRAW_STATE_BYTES ||
        (draw->indices == 0ul && draw->vertex_count % 3ul != 0ul) ||
        (draw->tl_vertices &&
         draw->attributes != V9X_HSW_SHADE_ATTRIBUTES) ||
        (draw->tl_uv1 && !draw->tl_vertices) ||
        draw->cull > V9X_HSW_DRAW_CULL_CCW) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* Indexed: TL vertices only, outside the depth-clear form, word
     * aligned, at least one triangle, and a list a whole number of them. */
    if (draw->indices != 0ul &&
        (!draw->tl_vertices || draw->vertex_count == 0ul ||
         (draw->depth_buffer != 0ul && draw->depth_clear != 0ul) ||
         (draw->indices & 1ul) != 0ul || draw->index_count < 3ul ||
         (!draw->strip && draw->index_count % 3ul != 0ul))) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* A scissor inside the target, not inverted: the drawing rectangle
     * is inclusive. */
    if (draw->scissor &&
        (draw->scissor_x1 < draw->scissor_x0 ||
         draw->scissor_y1 < draw->scissor_y0 ||
         draw->scissor_x1 >= draw->width ||
         draw->scissor_y1 >= draw->height)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    return v9x_hsw_workload(batch, state_base, state_bytes, draw,
                            done_address, done_value);
}
