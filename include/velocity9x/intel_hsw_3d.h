/*
 * Haswell (Gen7.5) command and state encoding for the render engine.
 *
 * Phase F groundwork of docs\plans\intel-haswell-direct3d.md. Pure
 * encoding, no I/O: command packets built into a caller's dword buffer,
 * state structures built into caller dwords, and a checker that walks a
 * finished batch and refuses anything it does not recognise - the role
 * i9xx_3d_decode.c plays for Gen3.
 *
 * Sources: HSW PRM Vol 2b (command headers and fields), Vol 2d
 * (structures), Vol 2a (enumerations; read in `pdftotext -raw` mode,
 * because the layout mode misaligns their columns). Every opcode,
 * sub-opcode and length in the command table was read from Vol 2b.
 *
 * What is NOT here yet: field-level builders for 3DSTATE_CLIP, SF, SBE, WM
 * and PS. They have fixed lengths in the table and can be emitted raw with
 * v9x_intel_hsw_3d_emit; their fields are Phase D's first task. Nothing in
 * this file has been submitted to hardware.
 */
#ifndef VELOCITY9X_INTEL_HSW_3D_H
#define VELOCITY9X_INTEL_HSW_3D_H

#include "velocity9x/status.h"
#include "velocity9x/types.h"

struct v9x_hsw_batch {
    v9x_u32 *dw;
    v9x_u32 capacity;
    v9x_u32 used;
    v9x_u16 overflow;
};

/* Commands the builders and the checker know, by index into the table in
 * hsw_3d.c. */
#define V9X_HSW_CMD_PIPELINE_SELECT        ((v9x_u16)0u)
#define V9X_HSW_CMD_STATE_BASE_ADDRESS     ((v9x_u16)1u)
#define V9X_HSW_CMD_STATE_SIP              ((v9x_u16)2u)
#define V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_VS ((v9x_u16)3u)
#define V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_HS ((v9x_u16)4u)
#define V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_DS ((v9x_u16)5u)
#define V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_GS ((v9x_u16)6u)
#define V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_PS ((v9x_u16)7u)
#define V9X_HSW_CMD_URB_VS                 ((v9x_u16)8u)
#define V9X_HSW_CMD_URB_HS                 ((v9x_u16)9u)
#define V9X_HSW_CMD_URB_DS                 ((v9x_u16)10u)
#define V9X_HSW_CMD_URB_GS                 ((v9x_u16)11u)
#define V9X_HSW_CMD_VS                     ((v9x_u16)12u)
#define V9X_HSW_CMD_HS                     ((v9x_u16)13u)
#define V9X_HSW_CMD_TE                     ((v9x_u16)14u)
#define V9X_HSW_CMD_DS                     ((v9x_u16)15u)
#define V9X_HSW_CMD_GS                     ((v9x_u16)16u)
#define V9X_HSW_CMD_STREAMOUT              ((v9x_u16)17u)
#define V9X_HSW_CMD_CLIP                   ((v9x_u16)18u)
#define V9X_HSW_CMD_SF                     ((v9x_u16)19u)
#define V9X_HSW_CMD_SBE                    ((v9x_u16)20u)
#define V9X_HSW_CMD_WM                     ((v9x_u16)21u)
#define V9X_HSW_CMD_PS                     ((v9x_u16)22u)
#define V9X_HSW_CMD_VERTEX_BUFFERS         ((v9x_u16)23u)
#define V9X_HSW_CMD_VERTEX_ELEMENTS        ((v9x_u16)24u)
#define V9X_HSW_CMD_3DPRIMITIVE            ((v9x_u16)25u)
#define V9X_HSW_CMD_PIPE_CONTROL           ((v9x_u16)26u)
#define V9X_HSW_CMD_DRAWING_RECTANGLE      ((v9x_u16)27u)
#define V9X_HSW_CMD_VIEWPORT_PTRS_CC       ((v9x_u16)28u)
#define V9X_HSW_CMD_VIEWPORT_PTRS_SF_CLIP  ((v9x_u16)29u)
#define V9X_HSW_CMD_SCISSOR_PTRS           ((v9x_u16)30u)
#define V9X_HSW_CMD_BLEND_PTRS             ((v9x_u16)31u)
#define V9X_HSW_CMD_DEPTH_STENCIL_PTRS     ((v9x_u16)32u)
#define V9X_HSW_CMD_CC_PTRS                ((v9x_u16)33u)
#define V9X_HSW_CMD_BINDING_TABLE_PTRS_PS  ((v9x_u16)34u)
#define V9X_HSW_CMD_SAMPLER_PTRS_PS        ((v9x_u16)35u)
#define V9X_HSW_CMD_DEPTH_BUFFER           ((v9x_u16)36u)
#define V9X_HSW_CMD_MULTISAMPLE            ((v9x_u16)37u)
#define V9X_HSW_CMD_SAMPLE_MASK            ((v9x_u16)38u)
#define V9X_HSW_CMD_CONSTANT_PS            ((v9x_u16)39u)
#define V9X_HSW_CMD_COUNT                  ((v9x_u16)40u)

/* 3DPRIMITIVE topologies, Vol 2a "3DPRIM_*". */
#define V9X_HSW_PRIM_POINTLIST ((v9x_u8)0x01u)
#define V9X_HSW_PRIM_LINELIST  ((v9x_u8)0x02u)
#define V9X_HSW_PRIM_LINESTRIP ((v9x_u8)0x03u)
#define V9X_HSW_PRIM_TRILIST   ((v9x_u8)0x04u)
#define V9X_HSW_PRIM_TRISTRIP  ((v9x_u8)0x05u)
#define V9X_HSW_PRIM_TRIFAN    ((v9x_u8)0x06u)
#define V9X_HSW_PRIM_RECTLIST  ((v9x_u8)0x0fu)

/* PIPE_CONTROL DW1, Vol 2b. */
#define V9X_HSW_PC_DEPTH_CACHE_FLUSH ((v9x_u32)0x00000001ul)
#define V9X_HSW_PC_STALL_AT_SCOREBOARD ((v9x_u32)0x00000002ul)
#define V9X_HSW_PC_STATE_CACHE_INVALIDATE ((v9x_u32)0x00000004ul)
#define V9X_HSW_PC_CONST_CACHE_INVALIDATE ((v9x_u32)0x00000008ul)
#define V9X_HSW_PC_VF_CACHE_INVALIDATE ((v9x_u32)0x00000010ul)
#define V9X_HSW_PC_DC_FLUSH          ((v9x_u32)0x00000020ul)
#define V9X_HSW_PC_TEXTURE_CACHE_INVALIDATE ((v9x_u32)0x00000400ul)
#define V9X_HSW_PC_INSTRUCTION_CACHE_INVALIDATE ((v9x_u32)0x00000800ul)
#define V9X_HSW_PC_RT_CACHE_FLUSH    ((v9x_u32)0x00001000ul)
#define V9X_HSW_PC_DEPTH_STALL       ((v9x_u32)0x00002000ul)
#define V9X_HSW_PC_WRITE_IMMEDIATE   ((v9x_u32)0x00004000ul) /* 15:14 = 1 */
#define V9X_HSW_PC_CS_STALL          ((v9x_u32)0x00100000ul)
#define V9X_HSW_PC_GGTT              ((v9x_u32)0x01000000ul) /* bit 24 */

/* Surface formats, Vol 2a SURFACE_FORMAT (raw-mode extraction). */
#define V9X_HSW_FMT_R32G32B32A32_FLOAT ((v9x_u16)0x000u)
#define V9X_HSW_FMT_R32G32B32_FLOAT    ((v9x_u16)0x040u)
#define V9X_HSW_FMT_R32G32_FLOAT       ((v9x_u16)0x085u)
#define V9X_HSW_FMT_B8G8R8A8_UNORM     ((v9x_u16)0x0c0u)
#define V9X_HSW_FMT_R8G8B8A8_UNORM     ((v9x_u16)0x0c7u)
#define V9X_HSW_FMT_R32_FLOAT          ((v9x_u16)0x0d8u)
#define V9X_HSW_FMT_R24_UNORM_X8       ((v9x_u16)0x0d9u)
#define V9X_HSW_FMT_B8G8R8X8_UNORM     ((v9x_u16)0x0e9u)
#define V9X_HSW_FMT_B5G6R5_UNORM       ((v9x_u16)0x100u)
#define V9X_HSW_FMT_B5G5R5A1_UNORM     ((v9x_u16)0x102u)
#define V9X_HSW_FMT_B4G4R4A4_UNORM     ((v9x_u16)0x104u)
#define V9X_HSW_FMT_R16_UNORM          ((v9x_u16)0x10au)
#define V9X_HSW_FMT_L8A8_UNORM         ((v9x_u16)0x114u)
#define V9X_HSW_FMT_B5G5R5X1_UNORM     ((v9x_u16)0x11au)
#define V9X_HSW_FMT_R8_UNORM           ((v9x_u16)0x140u)
#define V9X_HSW_FMT_A8_UNORM           ((v9x_u16)0x144u)
#define V9X_HSW_FMT_I8_UNORM           ((v9x_u16)0x145u)
#define V9X_HSW_FMT_L8_UNORM           ((v9x_u16)0x146u)

/* VERTEX_ELEMENT_STATE component controls, Vol 2a VFCOMP_*. */
#define V9X_HSW_VFCOMP_NOSTORE   ((v9x_u8)0u)
#define V9X_HSW_VFCOMP_STORE_SRC ((v9x_u8)1u)
#define V9X_HSW_VFCOMP_STORE_0   ((v9x_u8)2u)
#define V9X_HSW_VFCOMP_STORE_1_FP ((v9x_u8)3u)

/* SURFACE_STATE surface types, DW0 31:29. */
#define V9X_HSW_SURFTYPE_2D   ((v9x_u8)1u)
#define V9X_HSW_SURFTYPE_NULL ((v9x_u8)7u)

/* Shader channel selects, RENDER_SURFACE_STATE DW7. */
#define V9X_HSW_SCS_ZERO  ((v9x_u8)0u)
#define V9X_HSW_SCS_ONE   ((v9x_u8)1u)
#define V9X_HSW_SCS_RED   ((v9x_u8)4u)
#define V9X_HSW_SCS_GREEN ((v9x_u8)5u)
#define V9X_HSW_SCS_BLUE  ((v9x_u8)6u)
#define V9X_HSW_SCS_ALPHA ((v9x_u8)7u)

#define V9X_HSW_SURFACE_STATE_DWORDS ((v9x_u32)8ul)
#define V9X_HSW_SAMPLER_STATE_DWORDS ((v9x_u32)4ul)
#define V9X_HSW_BLEND_STATE_DWORDS   ((v9x_u32)2ul)
#define V9X_HSW_DEPTH_STENCIL_DWORDS ((v9x_u32)3ul)
#define V9X_HSW_CC_STATE_DWORDS      ((v9x_u32)6ul)

/* Checker refusals, in the order they are tested. */
#define V9X_HSW_CHECK_OK              ((v9x_u16)0u)
#define V9X_HSW_CHECK_UNKNOWN_COMMAND ((v9x_u16)1u)
#define V9X_HSW_CHECK_BAD_LENGTH      ((v9x_u16)2u)
#define V9X_HSW_CHECK_TRUNCATED       ((v9x_u16)3u)
#define V9X_HSW_CHECK_NO_END          ((v9x_u16)4u)
#define V9X_HSW_CHECK_NOT_QWORD       ((v9x_u16)5u)

struct v9x_hsw_check {
    v9x_u16 result;
    v9x_u32 at;        /* dword index of the offending command */
    v9x_u32 commands;  /* commands accepted before it */
    v9x_u32 header;    /* the offending header dword */
};

void v9x_intel_hsw_batch_init(struct v9x_hsw_batch *batch, v9x_u32 *dw,
                              v9x_u32 capacity);

/* Header dword for a table command of total_dwords (for variable-length
 * commands; fixed ones use their table length). */
v9x_status v9x_intel_hsw_3d_header(v9x_u16 command, v9x_u32 total_dwords,
                                   v9x_u32 *header);

/* Any table command with its payload (dwords after the header). The
 * payload count must equal the command's fixed length minus one. */
v9x_status v9x_intel_hsw_3d_emit(struct v9x_hsw_batch *batch,
                                 v9x_u16 command, const v9x_u32 *payload,
                                 v9x_u32 count);

v9x_status v9x_intel_hsw_mi_noop(struct v9x_hsw_batch *batch);
v9x_status v9x_intel_hsw_mi_batch_buffer_end(struct v9x_hsw_batch *batch);
v9x_status v9x_intel_hsw_mi_store_data_imm(struct v9x_hsw_batch *batch,
                                           v9x_u32 ggtt_address,
                                           v9x_u32 value);
v9x_status v9x_intel_hsw_mi_load_register_imm(struct v9x_hsw_batch *batch,
                                              v9x_u32 offset,
                                              v9x_u32 value);

v9x_status v9x_intel_hsw_pipeline_select_3d(struct v9x_hsw_batch *batch);

/* All five bases with their modify enables set; addresses 4 KB aligned.
 * The general and dynamic upper bounds are set too (0 = no bound is not
 * a meaning the PRM gives, so a bound must be passed). */
v9x_status v9x_intel_hsw_state_base_address(struct v9x_hsw_batch *batch,
                                            v9x_u32 general,
                                            v9x_u32 surface,
                                            v9x_u32 dynamic,
                                            v9x_u32 indirect,
                                            v9x_u32 instruction,
                                            v9x_u32 general_bound,
                                            v9x_u32 dynamic_bound,
                                            v9x_u32 indirect_bound,
                                            v9x_u32 instruction_bound);

/* stage: V9X_HSW_CMD_PUSH_CONSTANT_ALLOC_VS..PS. KB units. */
v9x_status v9x_intel_hsw_push_constant_alloc(struct v9x_hsw_batch *batch,
                                             v9x_u16 stage,
                                             v9x_u8 offset_kb,
                                             v9x_u8 size_kb);

/* stage: V9X_HSW_CMD_URB_VS..GS. */
v9x_status v9x_intel_hsw_urb(struct v9x_hsw_batch *batch, v9x_u16 stage,
                             v9x_u8 start, v9x_u16 alloc_size,
                             v9x_u16 entries);

/* The disabled form of VS, HS, TE, DS, GS or STREAMOUT: every payload
 * dword zero, which leaves each stage's enable clear (VS Function Enable,
 * HS Enable, TE Enable, DS Function Enable, GS Enable, SO Function Enable,
 * Vol 2b). */
v9x_status v9x_intel_hsw_stage_disabled(struct v9x_hsw_batch *batch,
                                        v9x_u16 stage);

/* One of the 2-dword state pointer commands (viewport, scissor, blend,
 * depth/stencil, CC, binding table, sampler); value is DW1 as the caller
 * computed it. */
v9x_status v9x_intel_hsw_state_pointer(struct v9x_hsw_batch *batch,
                                       v9x_u16 command, v9x_u32 value);

v9x_status v9x_intel_hsw_drawing_rectangle(struct v9x_hsw_batch *batch,
                                           v9x_u16 xmin, v9x_u16 ymin,
                                           v9x_u16 xmax, v9x_u16 ymax);

/* One vertex buffer, index 0-32, with Address Modify Enable set. end is
 * the inclusive last byte address. */
v9x_status v9x_intel_hsw_vertex_buffer(struct v9x_hsw_batch *batch,
                                       v9x_u8 index, v9x_u16 pitch,
                                       v9x_u32 start, v9x_u32 end);

struct v9x_hsw_vertex_element {
    v9x_u8 buffer;
    v9x_u16 format;
    v9x_u16 offset;
    v9x_u8 component[4];
};

v9x_status v9x_intel_hsw_vertex_elements(
    struct v9x_hsw_batch *batch,
    const struct v9x_hsw_vertex_element *elements, v9x_u8 count);

/* Sequential, one instance ("[DevHSW]: Instance Count = 1"). */
v9x_status v9x_intel_hsw_3dprimitive(struct v9x_hsw_batch *batch,
                                     v9x_u8 topology, v9x_u32 vertices,
                                     v9x_u32 start);

/* flags: V9X_HSW_PC_*. With V9X_HSW_PC_WRITE_IMMEDIATE, address (dword
 * aligned) receives data. */
v9x_status v9x_intel_hsw_pipe_control(struct v9x_hsw_batch *batch,
                                      v9x_u32 flags, v9x_u32 address,
                                      v9x_u32 data);

/* RENDER_SURFACE_STATE for a linear 2D surface, one mip, channel selects
 * set to identity (SCS_ZERO is 0, so an all-zero DW7 samples as zero and
 * writes nothing). pitch in bytes. */
v9x_status v9x_intel_hsw_surface_2d(v9x_u32 out[8], v9x_u16 format,
                                    v9x_u32 base, v9x_u16 width,
                                    v9x_u16 height, v9x_u32 pitch);

struct v9x_hsw_sampler {
    v9x_u8 mip_filter;  /* DW0 21:20 */
    v9x_u8 mag_filter;  /* DW0 19:17 */
    v9x_u8 min_filter;  /* DW0 16:14 */
    v9x_u16 min_lod;    /* DW1 31:20, U4.8 */
    v9x_u16 max_lod;    /* DW1 19:8, U4.8 */
    v9x_u32 border_color_pointer; /* DW2 31:5, 32-byte aligned */
    v9x_u32 dw3;        /* address modes and the rest, raw */
};

v9x_status v9x_intel_hsw_sampler(v9x_u32 out[4],
                                 const struct v9x_hsw_sampler *sampler);

struct v9x_hsw_blend {
    v9x_u8 enable;
    v9x_u8 color_function;  /* 13:11 */
    v9x_u8 src_factor;      /* 9:5 */
    v9x_u8 dst_factor;      /* 4:0 */
    v9x_u8 independent_alpha;
    v9x_u8 alpha_function;  /* 28:26 */
    v9x_u8 src_alpha_factor; /* 24:20 */
    v9x_u8 dst_alpha_factor; /* 19:15 */
    v9x_u8 write_disable;   /* DW1 27:24: A, R, G, B */
    v9x_u8 alpha_test;      /* DW1 16 */
    v9x_u8 alpha_function_test; /* DW1 15:13 */
};

v9x_status v9x_intel_hsw_blend(v9x_u32 out[2],
                               const struct v9x_hsw_blend *blend);

v9x_status v9x_intel_hsw_depth_stencil(v9x_u32 out[3], v9x_u8 depth_test,
                                       v9x_u8 depth_function,
                                       v9x_u8 depth_write);

/* COLOR_CALC_STATE with an alpha reference as UNORM8 and no blend
 * constant. */
v9x_status v9x_intel_hsw_cc_state(v9x_u32 out[6], v9x_u8 alpha_ref);

/* Walk dwords[0..count) as a batch: every command must be in the table
 * with its fixed length (or a valid variable length), the batch must end
 * with MI_BATCH_BUFFER_END, optionally followed by one MI_NOOP, and count
 * must be even (a whole number of qwords; see hsw_3d.c for why). */
v9x_status v9x_intel_hsw_3d_check(const v9x_u32 *dwords, v9x_u32 count,
                                  struct v9x_hsw_check *check);

#endif
