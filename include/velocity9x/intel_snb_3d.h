/*
 * Sandy Bridge (Gen6) command and state encoding for the render engine.
 *
 * Step 2 of docs\plans\intel-sandybridge-direct3d.md. Pure encoding, no
 * I/O, beside the Gen7 builders in intel_hsw_3d.h rather than inside them:
 * nearly every 3DSTATE the Gen7 workload emits changes length or layout on
 * Gen6, and two opcodes name different commands on the two generations
 * (7805h is Gen6's 3DSTATE_URB and Gen7's 3DSTATE_DEPTH_BUFFER; 780Eh is
 * 4 dwords here and 2 there). Batches are the Gen7 path's struct
 * v9x_hsw_batch, and the MI builders are shared.
 *
 * Sources: Mesa 24.0.9 genxml gen6.xml (and the gen5/gen4 files it
 * imports) for every opcode, length and field position; the Sandy Bridge
 * PRM (Vol 2 Part 1, 3D pipeline) where genxml carries no rule; crocus
 * (crocus_state.c, crocus_pipe_control.c) for the order and workarounds.
 * Nothing here has run on hardware.
 */
#ifndef VELOCITY9X_INTEL_SNB_3D_H
#define VELOCITY9X_INTEL_SNB_3D_H

#include "velocity9x/status.h"
#include "velocity9x/types.h"
#include "velocity9x/intel_hsw_3d.h"

/* Commands, by index into the table in snb_3d.c. */
#define V9X_SNB_CMD_PIPELINE_SELECT        ((v9x_u16)0u)
#define V9X_SNB_CMD_STATE_BASE_ADDRESS     ((v9x_u16)1u)
#define V9X_SNB_CMD_URB                    ((v9x_u16)2u)
#define V9X_SNB_CMD_VS                     ((v9x_u16)3u)
#define V9X_SNB_CMD_GS                     ((v9x_u16)4u)
#define V9X_SNB_CMD_CLIP                   ((v9x_u16)5u)
#define V9X_SNB_CMD_SF                     ((v9x_u16)6u)
#define V9X_SNB_CMD_WM                     ((v9x_u16)7u)
#define V9X_SNB_CMD_CONSTANT_VS            ((v9x_u16)8u)
#define V9X_SNB_CMD_CONSTANT_GS            ((v9x_u16)9u)
#define V9X_SNB_CMD_CONSTANT_PS            ((v9x_u16)10u)
#define V9X_SNB_CMD_BINDING_TABLE_PTRS     ((v9x_u16)11u)
#define V9X_SNB_CMD_SAMPLER_PTRS           ((v9x_u16)12u)
#define V9X_SNB_CMD_VIEWPORT_PTRS          ((v9x_u16)13u)
#define V9X_SNB_CMD_CC_PTRS                ((v9x_u16)14u)
#define V9X_SNB_CMD_SCISSOR_PTRS           ((v9x_u16)15u)
#define V9X_SNB_CMD_SAMPLE_MASK            ((v9x_u16)16u)
#define V9X_SNB_CMD_VERTEX_BUFFERS         ((v9x_u16)17u)
#define V9X_SNB_CMD_VERTEX_ELEMENTS        ((v9x_u16)18u)
#define V9X_SNB_CMD_DRAWING_RECTANGLE      ((v9x_u16)19u)
#define V9X_SNB_CMD_DEPTH_BUFFER           ((v9x_u16)20u)
#define V9X_SNB_CMD_MULTISAMPLE            ((v9x_u16)21u)
#define V9X_SNB_CMD_STENCIL_BUFFER         ((v9x_u16)22u)
#define V9X_SNB_CMD_HIER_DEPTH_BUFFER      ((v9x_u16)23u)
#define V9X_SNB_CMD_CLEAR_PARAMS           ((v9x_u16)24u)
#define V9X_SNB_CMD_3DPRIMITIVE            ((v9x_u16)25u)
#define V9X_SNB_CMD_PIPE_CONTROL           ((v9x_u16)26u)
#define V9X_SNB_CMD_COUNT                  ((v9x_u16)27u)

/*
 * DW0 for a command: GFXPIPE type 3, the table's subtype, opcode and
 * sub-opcode, and the dword length (total - 2) for any multi-dword
 * command. dw0_bits are command-specific DW0 fields, which on Gen6 live
 * in 15:8 (the pointer commands' change bits, CONSTANT_*'s buffer-valid
 * bits, CLEAR_PARAMS's valid bit, 3DPRIMITIVE's topology and access
 * type); anything outside 15:8 is INVALID_ARGUMENT. Fixed-length
 * commands refuse any other total.
 */
v9x_status v9x_intel_snb_3d_header(v9x_u16 command, v9x_u32 total_dwords,
                                   v9x_u32 dw0_bits, v9x_u32 *header);
v9x_status v9x_intel_snb_3d_emit(struct v9x_hsw_batch *batch,
                                 v9x_u16 command, v9x_u32 dw0_bits,
                                 const v9x_u32 *payload, v9x_u32 count);

/*
 * PIPE_CONTROL (gen6.xml): DW1 the flags, which are Gen7's
 * V9X_HSW_PC_* bit for bit except that Gen6 has no DC flush (bit 5) and no
 * pipe-control flush (bit 7), and the GGTT bit, which is DW2 bit 2 here
 * (Destination Address Type), not DW1 bit 24. So the flags take
 * V9X_HSW_PC_GGTT and the builder moves it. DW2 the address 31:3, DW3
 * the immediate data. Refused: DC flush, a CS stall with none of the
 * companion bits the PRM requires, a depth stall with an RT or depth
 * flush (crocus, pre-Haswell), and an address not qword-aligned.
 */
#define V9X_SNB_PC_GGTT_DW2 ((v9x_u32)0x00000004ul)
v9x_status v9x_intel_snb_pipe_control(struct v9x_hsw_batch *batch,
                                      v9x_u32 flags, v9x_u32 address,
                                      v9x_u32 data);
/*
 * The post-sync non-zero flush Gen6 requires before any PIPE_CONTROL with a
 * render-target flush and before any depth stall (crocus
 * gen6_emit_post_sync_nonzero_flush, quoting the SNB PRM): CS stall plus
 * stall at scoreboard, then a qword write of 0 to scratch_address in the
 * GGTT with no other bit.
 */
v9x_status v9x_intel_snb_post_sync_nonzero(struct v9x_hsw_batch *batch,
                                           v9x_u32 scratch_address);

/*
 * 3DSTATE_URB: the whole URB to the VS, none to the GS (there is none).
 * vs_entries a multiple of 4 in GT2's 24-256; vs_rows the entry size in
 * 128-byte rows, 1-5 (crocus asserts at most 5), stored as rows - 1.
 */
#define V9X_SNB_URB_VS_MIN_ENTRIES ((v9x_u16)24u)
#define V9X_SNB_URB_VS_MAX_ENTRIES ((v9x_u16)256u)
#define V9X_SNB_URB_MAX_ROWS       ((v9x_u8)5u)
v9x_status v9x_intel_snb_urb(struct v9x_hsw_batch *batch,
                             v9x_u16 vs_entries, v9x_u8 vs_rows);

/* 3DSTATE_VS off: 6 dwords, Enable (DW5 bit 0) clear. */
v9x_status v9x_intel_snb_vs_disabled(struct v9x_hsw_batch *batch);
/* 3DSTATE_GS off as crocus emits it with no GS program: Dispatch GRF
 * Start 1, Rendering and Statistics enabled, Enable (DW6 bit 15) clear. */
v9x_status v9x_intel_snb_gs_disabled(struct v9x_hsw_batch *batch);

/* 3DSTATE_CLIP for pre-transformed vertices: DW2 Clip Enable 31, Clip
 * Mode 15:13 ACCEPT_ALL, Perspective Divide Disable 9, the Gen7 path's
 * word, at the same positions on Gen6. DW1 (no winding or cull on Gen6)
 * and DW3 0. */
v9x_status v9x_intel_snb_clip_screen(struct v9x_hsw_batch *batch);

/*
 * 3DSTATE_SF, 20 dwords, carrying what Gen7 splits into SF and SBE:
 *   DW1 Number of SF Output Attributes 27:22, Vertex URB Entry Read
 *       Length 15:11 and Offset 9:4 (Gen7 SBE DW1, bit for bit);
 *   DW2 Front Winding 0 (Gen7 SF DW1 bit 0);
 *   DW3 Cull Mode 30:29 (Gen7 SF DW2);
 *   DW17 Constant Interpolation Enable, the flat mask (Gen7 SBE DW11).
 * Cull as the Gen7 builder: CULL_NONE, or the winding Direct3D discards
 * made the back face (V9X_HSW_DRAW_CULL_*). Provoking vertices 0, no
 * swizzles, no depth offset, as on Gen7. Gen6 has no depth-format field.
 */
v9x_status v9x_intel_snb_sf(struct v9x_hsw_batch *batch, v9x_u8 outputs,
                            v9x_u8 read_offset, v9x_u8 read_length,
                            v9x_u8 cull, v9x_u32 constant_mask);

/*
 * 3DSTATE_WM, 9 dwords, carrying what Gen7 splits into WM and PS, for one
 * SIMD8 kernel:
 *   DW1 Kernel Start Pointer 0, 31:6;
 *   DW2 Sampler Count 29:27 (in fours), Binding Table Entry Count 25:18;
 *   DW4 Dispatch GRF Start Register for Constant/Setup Data 0, 22:16;
 *   DW5 Maximum Number of Threads 31:25 (count - 1; GT2 80), Pixel Shader
 *       Kills Pixel 22, Thread Dispatch Enable 19, 8 Pixel Dispatch 0;
 *   DW6 Number of SF Output Attributes 25:20 (must match 3DSTATE_SF's),
 *       Barycentric Interpolation Mode 15:10 (Gen7's encoding).
 */
#define V9X_SNB_WM_MAX_THREADS ((v9x_u32)80ul)
v9x_status v9x_intel_snb_wm(struct v9x_hsw_batch *batch, v9x_u32 kernel,
                            v9x_u8 binding_entries, v9x_u8 samplers,
                            v9x_u8 attributes, v9x_u8 grf_start,
                            v9x_u8 barycentric, v9x_u8 kills);

/*
 * 3DSTATE_CONSTANT_PS: 5 dwords; DW0 Buffer 0 Valid (bit 12) when
 * registers is non-zero; DW1 buffer 0's pointer 31:5 (an offset from the
 * dynamic state base) with Read Length - 1 in 4:0, in 256-bit registers.
 * registers 0 emits all-invalid, which is how crocus disables it.
 */
v9x_status v9x_intel_snb_constant_ps(struct v9x_hsw_batch *batch,
                                     v9x_u32 pointer, v9x_u8 registers);

/* The combined pointer commands, PS only: change bit 12 and DW3. */
v9x_status v9x_intel_snb_binding_table_ptrs_ps(struct v9x_hsw_batch *batch,
                                               v9x_u32 offset);
v9x_status v9x_intel_snb_sampler_ptrs_ps(struct v9x_hsw_batch *batch,
                                         v9x_u32 offset);
/* CLIP (bit 10, DW1), SF (bit 11, DW2) and CC (bit 12, DW3) viewports,
 * all three changed. 32-byte aligned offsets. */
v9x_status v9x_intel_snb_viewport_ptrs(struct v9x_hsw_batch *batch,
                                       v9x_u32 clip, v9x_u32 sf,
                                       v9x_u32 cc);
/* BLEND_STATE (DW1), DEPTH_STENCIL_STATE (DW2) and COLOR_CALC_STATE
 * (DW3), each 64-byte aligned with its change/valid bit 0 set. */
v9x_status v9x_intel_snb_cc_ptrs(struct v9x_hsw_batch *batch,
                                 v9x_u32 blend, v9x_u32 depth_stencil,
                                 v9x_u32 color_calc);

/*
 * 3DSTATE_DEPTH_BUFFER (7905h): DW1 Surface Type 31:29, Tiled 27, Tile
 * Walk 26 (Y-major, which the depth buffer requires), Surface Format
 * 20:18, Pitch - 1 16:0; separate stencil and HiZ both off, which the SNB
 * PRM requires to agree; DW2 base; DW3 Height - 1 31:19, Width - 1 18:6.
 * No write-enable bits on Gen6: DEPTH_STENCIL_STATE alone gates writes.
 * format V9X_HSW_DEPTH_* (the encodings are the same). base 0 with
 * width 0 emits SURFTYPE_NULL and D32_FLOAT, as for no Z buffer.
 */
v9x_status v9x_intel_snb_depth_buffer(struct v9x_hsw_batch *batch,
                                      v9x_u8 format, v9x_u32 base,
                                      v9x_u32 pitch, v9x_u16 width,
                                      v9x_u16 height);
/* 3DSTATE_CLEAR_PARAMS (7910h, 2 dwords): no clear value valid. */
v9x_status v9x_intel_snb_clear_params(struct v9x_hsw_batch *batch);
/* 3DSTATE_MULTISAMPLE (790Dh, 3 dwords): one sample, centre. */
v9x_status v9x_intel_snb_multisample(struct v9x_hsw_batch *batch);

/* 3DPRIMITIVE, 6 dwords: topology DW0 14:10, Vertex Access Type DW0 15
 * (random = indexed); DW1 vertex count, DW2 start, DW3 instance count 1. */
v9x_status v9x_intel_snb_3dprimitive(struct v9x_hsw_batch *batch,
                                     v9x_u8 topology, v9x_u32 count,
                                     v9x_u32 start, v9x_u8 indexed);

/*
 * RENDER_SURFACE_STATE, 6 dwords: DW0 Surface Type 31:29, Format 26:18;
 * DW1 base; DW2 Height - 1 31:19, Width - 1 18:6, one mip; DW3 Pitch - 1
 * 19:3, linear (Tiled bit 1 clear). DW4, DW5 0. format V9X_HSW_FMT_*
 * (the SURFACE_FORMAT codes are shared).
 */
#define V9X_SNB_SURFACE_DWORDS 6u
v9x_status v9x_intel_snb_surface_2d(v9x_u32 out[6], v9x_u16 format,
                                    v9x_u32 base, v9x_u16 width,
                                    v9x_u16 height, v9x_u32 pitch);

/* SF_VIEWPORT, 8 dwords: m00 m11 m22 m30 m31 m32 as floats' bits, then 2
 * reserved; CLIP_VIEWPORT, 4 dwords: x min, x max, y min, y max guard
 * band, as floats' bits. */
void v9x_intel_snb_sf_viewport(v9x_u32 out[8], v9x_u32 m00, v9x_u32 m11,
                               v9x_u32 m22, v9x_u32 m30, v9x_u32 m31,
                               v9x_u32 m32);
void v9x_intel_snb_clip_viewport(v9x_u32 out[4], v9x_u32 x_min,
                                 v9x_u32 x_max, v9x_u32 y_min,
                                 v9x_u32 y_max);

/*
 * VERTEX_BUFFER_STATE in a one-buffer 3DSTATE_VERTEX_BUFFERS: DW0 index
 * 31:26, pitch 11:0, and nothing at bit 14 (Gen7's Address Modify Enable;
 * reserved here); Buffer Access Type 20 = 0, vertex data. DW1 start, DW2
 * the inclusive end, DW3 step rate 0. Vertex elements and the index buffer
 * are Gen7's encodings (gen6.xml), so v9x_intel_hsw_vertex_elements and
 * v9x_intel_hsw_index_buffer serve both.
 */
v9x_status v9x_intel_snb_vertex_buffer(struct v9x_hsw_batch *batch,
                                       v9x_u8 index, v9x_u16 pitch,
                                       v9x_u32 start, v9x_u32 end);

/*
 * SAMPLER_STATE from the Gen7 description (struct v9x_hsw_sampler), so the
 * HAL's texture mapping serves both:
 *   DW0 filters at Gen7's bits (mip 21:20, mag 19:17, min 16:14), Min and
 *       Mag State Not Equal 27 when they differ (crocus sets it on Gen6);
 *   DW1 Min LOD 31:22 and Max LOD 21:12 in U4.6, the Gen7 U4.8 values
 *       shifted down and the maximum clamped to 13 (crocus); the
 *       address modes, which Gen7 keeps in DW3 8:0, at DW1 8:0;
 *   DW2 the border colour pointer, a 12-dword SAMPLER_BORDER_COLOR_STATE
 *       (gen5.xml) 32-byte aligned from the dynamic state base;
 *   DW3 Gen7's DW3 less its address modes: the rounding enables 18:13 are
 *       at the same bits.
 */
#define V9X_SNB_BORDER_COLOR_DWORDS 12u
v9x_status v9x_intel_snb_sampler(v9x_u32 out[4],
                                 const struct v9x_hsw_sampler *sampler);

/*
 * The initialising batch's state image (Phase C's role on Gen6): every
 * pointer the batch sends points at valid state and nothing is drawn.
 * Offsets from the state base, which is every base but the indirect one,
 * each meeting its pointer's alignment. The Gen7 image's SF_CLIP_VIEWPORT
 * slot holds Gen6's separate SF and CLIP viewports, and the NULL surface
 * is 6 dwords; the rest are where Gen7's are, so draw state the HAL
 * writes at Gen7's shaded offsets lands clear of them.
 */
#define V9X_SNB_INIT_CC_VIEWPORT   ((v9x_u32)0x000ul) /* 2 dwords */
#define V9X_SNB_INIT_SF_VIEWPORT   ((v9x_u32)0x040ul) /* 8 */
#define V9X_SNB_INIT_CLIP_VIEWPORT ((v9x_u32)0x060ul) /* 4 */
#define V9X_SNB_INIT_SCISSOR       ((v9x_u32)0x080ul) /* 2 */
#define V9X_SNB_INIT_BLEND         ((v9x_u32)0x0c0ul) /* 2 */
#define V9X_SNB_INIT_DEPTH_STENCIL ((v9x_u32)0x100ul) /* 3 */
#define V9X_SNB_INIT_CC            ((v9x_u32)0x140ul) /* 6 */
#define V9X_SNB_INIT_SAMPLER       ((v9x_u32)0x180ul) /* 4 */
#define V9X_SNB_INIT_BINDING_TABLE ((v9x_u32)0x1c0ul) /* 1 entry */
#define V9X_SNB_INIT_NULL_SURFACE  ((v9x_u32)0x200ul) /* 6 */
#define V9X_SNB_INIT_STATE_BYTES   ((v9x_u32)0x220ul)

/* Writes the image: all zero but the CC viewport's maximum depth (1.0),
 * the NULL surface and the binding table entry naming it, as Gen7's. The
 * SF and CLIP viewports stay zero: these batches clip with ACCEPT_ALL and
 * the viewport transform off, so neither is read. */
v9x_status v9x_intel_snb_init_state(v9x_u32 *image, v9x_u32 dwords);

/*
 * The batches, in crocus's Gen6 order: the post-sync non-zero pair and a
 * flush before PIPELINE_SELECT, STATE_BASE_ADDRESS and a cache invalidate,
 * 3DSTATE_URB (256 VS entries of one 128-byte row: VS off, the VF writes
 * the VUE), CONSTANT_VS, VS off and the post-VS flush, CONSTANT_GS and GS
 * off, CLIP, SF, WM, CONSTANT_PS, the depth stall/flush/stall around the
 * depth buffer, HiZ, stencil and clear parameters, sample mask, drawing
 * rectangle, multisample, the viewport, scissor, CC, binding-table and
 * sampler pointers; for a draw the vertices and 3DPRIMITIVE; then an RT
 * flush, a scoreboard stall and the breadcrumb, MI_BATCH_BUFFER_END
 * padded to a qword. Every RT flush and depth stall follows the post-sync
 * non-zero pair, writing scratch_address (a qword in the GGTT nothing
 * else uses).
 *
 * The init batch zeroes CLIP, SF and WM as the Gen7 one does. The draw
 * batch takes the Gen7 path's struct v9x_hsw_draw, for Direct3D's TL
 * vertices only (tl_vertices set, attributes V9X_HSW_SHADE_ATTRIBUTES):
 * depth (with or without a clear primitive), cull, flat, scissor, a
 * second texture coordinate and 16-bit indices as a list or strip.
 * Anything else is INVALID_ARGUMENT.
 */
v9x_status v9x_intel_snb_init_workload(struct v9x_hsw_batch *batch,
                                       v9x_u32 state_base,
                                       v9x_u32 state_bytes,
                                       v9x_u32 scratch_address,
                                       v9x_u32 done_address,
                                       v9x_u32 done_value);
v9x_status v9x_intel_snb_draw_workload(struct v9x_hsw_batch *batch,
                                       v9x_u32 state_base,
                                       v9x_u32 state_bytes,
                                       const struct v9x_hsw_draw *draw,
                                       v9x_u32 scratch_address,
                                       v9x_u32 done_address,
                                       v9x_u32 done_value);

/*
 * Walks a finished batch and refuses what Gen6 would not take: a GFXPIPE
 * command outside the Gen6 table or of another length (so Gen7's 7-dword
 * 3DPRIMITIVE, 2-dword CC_STATE_POINTERS or 3DSTATE_SBE), an MI command
 * other than MI_NOOP and MI_BATCH_BUFFER_END, a PIPE_CONTROL with an RT
 * flush or depth stall not straight after the post-sync non-zero pair, a
 * 3DPRIMITIVE before the pipeline, bases, URB, stages and pointers have
 * all been sent, and a batch that does not end in MI_BATCH_BUFFER_END at
 * a qword boundary. OK, or UNSUPPORTED with *bad_at the dword index.
 */
v9x_status v9x_intel_snb_3d_check(const v9x_u32 *dw, v9x_u32 count,
                                  v9x_u32 *bad_at);

#endif
