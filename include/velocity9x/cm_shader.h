/*
 * AMD Cayman-generation (Trinity, HD 7660D) shader words, and the shaders
 * the 3D bring-up uses.
 *
 * Pure policy, no I/O. Cayman keeps Evergreen's field layouts for the
 * words the two share (ALU slots, texture fetch, export), which this
 * module takes from eg_shader.c, and differs in its control flow: there is
 * no END_OF_PROGRAM bit, a CF_END instruction (20h) ends the program, and
 * there is no vertex-fetch clause, so vertex fetches run in a texture
 * clause (Mesa r600_isa.c: "VTX" has no Cayman opcode, "CF_END" only a
 * Cayman one; r600_asm.c cm_bytecode_add_cf_end). The encoder is held to
 * Linux 3.9's hand-assembled cayman_vs and cayman_ps
 * (cayman_blit_shaders.c, build\external\evergreen\linux-radeon\v3.9\) by
 * the host test, which rebuilds them.
 *
 * Shaders are arrays of little-endian dwords, uploaded at a 256-byte
 * boundary. Control-flow addresses count 8-byte units.
 */
#ifndef VELOCITY9X_CM_SHADER_H
#define VELOCITY9X_CM_SHADER_H

#include "velocity9x/status.h"

/* CF_END, the only way a Cayman program ends. */
#define V9X_CM_CF_END           0x20ul

/* The CF_END slot: two words. */
v9x_u32 v9x_cm_cf_end(v9x_u32 *out);

/* A vertex fetch from buffer 0 indexed by R0.x into dst_gpr, offset bytes
 * into the vertex, as data_format with its four channels selected. Linux's
 * Cayman blit puts a mega-fetch count of 1 in word 0 of every fetch (Mesa
 * leaves the field 0 on Cayman) and no MEGA_FETCH bit in word 2; this
 * does the same. */
v9x_u32 v9x_cm_vtx_fetch(v9x_u32 dst_gpr, v9x_u32 offset,
                         v9x_u32 data_format, v9x_u32 sel_x, v9x_u32 sel_y,
                         v9x_u32 sel_z, v9x_u32 sel_w, v9x_u32 *out);

/* The shaders. Each writes up to V9X_CM_SHADER_DWORDS_MAX dwords and
 * returns the count. */
#define V9X_CM_SHADER_DWORDS_MAX 128u

/* Linux 3.9's Cayman blit: VS fetches position (R1) and texture coordinate
 * (R0) as two floats each, 16-byte stride; PS interpolates and samples. */
v9x_u32 v9x_cm_shader_blit_vs(v9x_u32 *out);
v9x_u32 v9x_cm_shader_blit_ps(v9x_u32 *out);

/*
 * An Evergreen shader from eg_shader.c, rewritten for Cayman: its control
 * flow up to the end-of-program bit, that bit dropped, vertex-fetch
 * clauses made texture clauses and CF_END appended; then its clauses laid
 * out again in their order after the longer CF list, ALU clauses on an
 * 8-byte and fetch clauses on a 16-byte boundary, each CF address fixed;
 * vertex fetches in Cayman's form (v9x_cm_vtx_fetch's word 0 and 2). The
 * ALU and texture words are copied as they are: the shaders this is for
 * use at most four ALU slots in a group, one per channel, and no
 * transcendental, so none needs the T slot Cayman lacks, and their
 * opcodes are Cayman's too (Mesa r600_isa.c). Linux 3.9's Cayman blit is
 * this of Evergreen's blit, word for word.
 *
 * Returns the dwords written, at most V9X_CM_SHADER_DWORDS_MAX, or 0 for
 * a shader it cannot read: no end of program in the first 16 CF slots, a
 * CF instruction other than ALU, TC, VC and the exports, or a clause past
 * eg_count.
 */
v9x_u32 v9x_cm_shader_from_eg(const v9x_u32 *eg, v9x_u32 eg_count,
                              v9x_u32 *out);

#endif
