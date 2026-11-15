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

/*
 * Modifiers carried in an operand's bits 18:16. NEG and ABS on a source,
 * SAT (clamp to [0, 1]) on a destination. Only the instructions that
 * encode them take them (fadd, fmul and fmad negate and saturate; fmin,
 * fmax and set negate and take the absolute value); anywhere else a
 * modifier makes the program invalid rather than silently dropped.
 */
#define V9X_NV50_SH_MOD_NEG          0x00010000ul
#define V9X_NV50_SH_MOD_ABS          0x00020000ul
#define V9X_NV50_SH_MOD_SAT          0x00040000ul
#define V9X_NV50_SH_MODS             0x00070000ul
#define V9X_NV50_SH_NEG(operand) ((v9x_u32)(operand) | V9X_NV50_SH_MOD_NEG)
#define V9X_NV50_SH_ABS(operand) ((v9x_u32)(operand) | V9X_NV50_SH_MOD_ABS)
#define V9X_NV50_SH_SAT(operand) ((v9x_u32)(operand) | V9X_NV50_SH_MOD_SAT)

/*
 * Condition codes (Mesa's emitCondCode, envydis' setf and predicate
 * tables): ordered float comparisons for set, and the tests a predicated
 * instruction makes of a flags register.
 */
#define V9X_NV50_SH_CC_NEVER         0x0ul
#define V9X_NV50_SH_CC_LT            0x1ul
#define V9X_NV50_SH_CC_EQ            0x2ul
#define V9X_NV50_SH_CC_LE            0x3ul
#define V9X_NV50_SH_CC_GT            0x4ul
#define V9X_NV50_SH_CC_NE            0x5ul
#define V9X_NV50_SH_CC_GE            0x6ul
#define V9X_NV50_SH_CC_ALWAYS        0xful

/* The four flags registers, $c0-$c3. */
#define V9X_NV50_SH_FLAGS_MAX        4ul

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
    /* v9x_nv50_sh_predicate's condition and flags register for the next
     * instruction, which must be long; pred_set while one is pending. */
    v9x_u32 pred_set;
    v9x_u32 pred_cc;
    v9x_u32 pred_reg;
    /* The last instruction is a flow instruction (kil), which has no
     * exit bit. */
    v9x_u32 last_flow;
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
/*
 * The same fetch with argc coordinates (1 to 4) in dst onwards: three for
 * a 3D texture, or three with cube set for a cube map (a direction, any
 * length).
 */
void v9x_nv50_sh_tex_n(struct v9x_nv50_sh *sh, v9x_u32 dst, v9x_u32 tex,
                       v9x_u32 samp, v9x_u32 mask, v9x_u32 argc,
                       v9x_u32 cube);
/* dst = min(a, b) or max(a, b), f32, long; a a register or input, b may
 * be a constant; NEG and ABS on either. */
void v9x_nv50_sh_fmin(struct v9x_nv50_sh *sh, v9x_u32 dst, v9x_u32 a,
                      v9x_u32 b);
void v9x_nv50_sh_fmax(struct v9x_nv50_sh *sh, v9x_u32 dst, v9x_u32 a,
                      v9x_u32 b);
/*
 * set.f32, long: compares a with b by cc (V9X_NV50_SH_CC_*) and writes
 * 0xffffffff when it holds, else 0, to dst, or to nothing when dst is
 * V9X_NV50_SH_NONE. flags, a flags register number or V9X_NV50_SH_NONE,
 * also takes the result's flags: a later instruction predicated on it
 * with CC_NE runs where the comparison held, CC_EQ where it did not
 * (Mesa's pattern for a predicate, nv50_ir_lowering_nv50.cpp).
 */
void v9x_nv50_sh_set(struct v9x_nv50_sh *sh, v9x_u32 dst, v9x_u32 cc,
                     v9x_u32 a, v9x_u32 b, v9x_u32 flags);
/* The next instruction, which must be long, runs only where flags
 * register reg passes cc. A select is a pair of predicated moves. */
void v9x_nv50_sh_predicate(struct v9x_nv50_sh *sh, v9x_u32 cc, v9x_u32 reg);
/* Discard the fragment (Mesa's emitFlow for OP_DISCARD), predicated or
 * not. A program that kills needs FP_CONTROL's USES_KIL. */
void v9x_nv50_sh_kil(struct v9x_nv50_sh *sh);
/* End the program at the last instruction, which must be long and not a
 * kil. */
void v9x_nv50_sh_exit(struct v9x_nv50_sh *sh);

#endif
