/*
 * GCN 3 to GCN 2 shader translation for AMD Mullins (Radeon R3E, CIK).
 *
 * The bring-up and Direct3D shaders are written once, as GCN 3 words, by
 * src\chipsets\ati\vi\vi_shader.c. CIK runs the same programs with
 * different encodings for a few formats and opcodes, and every one of
 * those instructions is the same length on both, so the program can be
 * rewritten in place word by word, the way Trinity's shaders are relocated
 * from Evergreen's (cm_shader.c). What changes, from LLVM's AMDGPU
 * instruction tables (VOP1/VOP2/VOPC/VOP3/SOP/FLAT/MIMG/EXP Instructions.td,
 * the gfx6_gfx7 and vi opcodes) and its assembler tests' SI/CI and VI
 * encodings:
 *
 *   EXP     prefix 110001 (C4h) becomes 111110 (F8h); fields unchanged.
 *   VINTRP  prefix 110101 (D4h) becomes 110010 (C8h); fields unchanged.
 *   VOP2    opcodes renumbered (bits 30:25).
 *   VOPC    opcodes renumbered (bits 24:17).
 *   VOP3    opcode in 25:17 (9 bits) rather than 25:16, clamp at 11 rather
 *           than 15; abs, vdst and the second dword unchanged.
 *   SOP1    s_mov_b32, s_mov_b64 and s_wqm_b64 renumbered (bits 15:8).
 *   FLAT    flat_load_dwordx4 is 0Eh, not 17h (bits 24:18).
 *   VOP1, SOPP and MIMG's image_sample: unchanged.
 *
 * Only the opcodes the shaders use are mapped. Anything else, and any
 * other format, refuses the whole program, so a shader can never reach
 * the chip half translated.
 */
#ifndef VELOCITY9X_CIK_SHADER_H
#define VELOCITY9X_CIK_SHADER_H

#include "velocity9x/status.h"

/*
 * Rewrites count GCN 3 words at words into GCN 2, in place. Returns
 * V9X_STATUS_OK, or V9X_STATUS_UNSUPPORTED with *failed_at (if non-null)
 * the index of the first word it could not translate; the words are then
 * partly rewritten and must not be used. A literal constant's dword is
 * left alone.
 */
v9x_status v9x_cik_shader_from_vi(v9x_u32 *words, v9x_u32 count,
                                  v9x_u32 *failed_at);

#endif
