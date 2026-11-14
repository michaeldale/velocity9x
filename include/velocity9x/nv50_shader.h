/*
 * A Tesla (NV50, the GT218's) shader instruction encoder: the few
 * instructions a fixed-function Direct3D pipeline needs, encoded as
 * Mesa's nv50 code emitter does (src\nouveau\codegen\
 * nv50_ir_emit_nv50.cpp, CodeEmitterNV50). Pure arithmetic, no I/O.
 * Phase 3 of docs\plans\nvidia-gt218-3d.md. Its host test rebuilds
 * xf86-video-nouveau's EXA programs word for word.
 *
 * An instruction is long (8 bytes, bit 0 of word 0 set) or short (4
 * bytes). Only a long instruction can write an output, read a constant
 * in its third source, or end the program.
 */
#ifndef VELOCITY9X_NV50_SHADER_H
#define VELOCITY9X_NV50_SHADER_H

#include "velocity9x/types.h"

/*
 * An operand, packed in a v9x_u32: the file in bits 31:28, a constant
 * buffer in 27:24, and the register number or byte offset in 15:0.
 *
 *   R  a general register, $r<n>
 *   A  a shader input, a[<byte offset>] (vertex attributes)
 *   C  a constant, c<buffer>[<byte offset>]
 *   O  a shader output, o[<byte offset>]
 *   V  a fragment varying, v[<byte offset>], for interp only
 */
#define V9X_NV50_SH_FILE_SHIFT       28u
#define V9X_NV50_SH_FILE_R           0ul
#define V9X_NV50_SH_FILE_A           1ul
#define V9X_NV50_SH_FILE_C           2ul
#define V9X_NV50_SH_FILE_O           3ul
#define V9X_NV50_SH_FILE_V           4ul
#define V9X_NV50_SH_BUFFER_SHIFT     24u
#define V9X_NV50_SH_R(n)  ((v9x_u32)(n))
#define V9X_NV50_SH_A(off) \
    ((V9X_NV50_SH_FILE_A << V9X_NV50_SH_FILE_SHIFT) | (v9x_u32)(off))
#define V9X_NV50_SH_C(buf, off) \
    ((V9X_NV50_SH_FILE_C << V9X_NV50_SH_FILE_SHIFT) | \
     ((v9x_u32)(buf) << V9X_NV50_SH_BUFFER_SHIFT) | (v9x_u32)(off))
#define V9X_NV50_SH_O(off) \
    ((V9X_NV50_SH_FILE_O << V9X_NV50_SH_FILE_SHIFT) | (v9x_u32)(off))
#define V9X_NV50_SH_V(off) \
    ((V9X_NV50_SH_FILE_V << V9X_NV50_SH_FILE_SHIFT) | (v9x_u32)(off))
/* interp's second source when there is none (linear, not perspective). */
#define V9X_NV50_SH_NONE  0xfffffffful

/* Instruction sizes. */
#define V9X_NV50_SH_SHORT            4ul
#define V9X_NV50_SH_LONG             8ul

struct v9x_nv50_sh {
    v9x_u32 *words;
    v9x_u32 max;
    v9x_u32 count;
    /* Where the last instruction starts, for v9x_nv50_sh_exit. */
    v9x_u32 last;
    v9x_u32 last_size;
    /* Set by a write past max, or by an operand combination the
     * hardware cannot encode; the program is then unusable. */
    v9x_u32 overflow;
    v9x_u32 invalid;
};

void v9x_nv50_sh_init(struct v9x_nv50_sh *sh, v9x_u32 *words, v9x_u32 max);

/* dst = src, 32 bits. Long: any register or output from a register.
 * Short: register to register. */
void v9x_nv50_sh_mov(struct v9x_nv50_sh *sh, v9x_u32 size, v9x_u32 dst,
                     v9x_u32 src);
/* dst = a * b, f32, round to nearest. */
void v9x_nv50_sh_fmul(struct v9x_nv50_sh *sh, v9x_u32 size, v9x_u32 dst,
                      v9x_u32 a, v9x_u32 b);
/* dst = a * b + c, f32. */
void v9x_nv50_sh_fmad(struct v9x_nv50_sh *sh, v9x_u32 size, v9x_u32 dst,
                      v9x_u32 a, v9x_u32 b, v9x_u32 c);
/* dst = a + b, f32, round to nearest. */
void v9x_nv50_sh_fadd(struct v9x_nv50_sh *sh, v9x_u32 size, v9x_u32 dst,
                      v9x_u32 a, v9x_u32 b);
/* dst = 1 / src, f32. */
void v9x_nv50_sh_rcp(struct v9x_nv50_sh *sh, v9x_u32 size, v9x_u32 dst,
                     v9x_u32 src);
/* dst = the varying at v[offset], short form; with persp a register
 * (1/w), the perspective-correct interpolation, else linear. */
void v9x_nv50_sh_interp(struct v9x_nv50_sh *sh, v9x_u32 dst,
                        v9x_u32 varying, v9x_u32 persp);
/*
 * A texture fetch, long: coordinates in registers dst, dst + 1 (2D),
 * results to the registers mask selects starting at dst; texture unit tex
 * and sampler samp; live adds Mesa's liveOnly (EXA's "texauto live").
 */
void v9x_nv50_sh_tex(struct v9x_nv50_sh *sh, v9x_u32 dst, v9x_u32 tex,
                     v9x_u32 samp, v9x_u32 mask, v9x_u32 live);
/* End the program at the last instruction, which must be long. */
void v9x_nv50_sh_exit(struct v9x_nv50_sh *sh);

#endif
