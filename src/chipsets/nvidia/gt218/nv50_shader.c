/*
 * The Tesla shader instruction encoder. include\velocity9x\nv50_shader.h
 * declares it. Every encoding follows Mesa's CodeEmitterNV50
 * (nv50_ir_emit_nv50.cpp) for the same instruction and size; the comment
 * on each names the function it follows. Pure arithmetic, no I/O.
 */
#include "velocity9x/nv50_shader.h"

/* Word 0 bit 0: a long instruction. Word 1 bits 0 and 1: exit, join. */
#define V9X_NV50_SH_LONG_BIT         0x00000001ul
#define V9X_NV50_SH_EXIT_BIT         0x00000001ul
/* emitFlagsRd with no predicate: condition "always". */
#define V9X_NV50_SH_FLAGS_ALWAYS     0x00000780ul
/* setDst: an output, in word 1. */
#define V9X_NV50_SH_DST_OUTPUT       0x00000008ul
/* setSrcFileBits. */
#define V9X_NV50_SH_SRC_INPUT_LONG   0x00200000ul
#define V9X_NV50_SH_SRC_INPUT_SHORT  0x01000000ul
#define V9X_NV50_SH_SRC_CONST        0x00800000ul
#define V9X_NV50_SH_SRC_CONST_ALT    0x01000000ul
#define V9X_NV50_SH_SRC_BUFFER_SHIFT 22u
/* Opcodes in word 0 bits 31:28 (emitMOV, emitFMUL, emitFMAD, emitFADD,
 * emitSFnOp, emitINTERP, emitTEX). */
#define V9X_NV50_SH_OP_MOV           0x10000000ul
#define V9X_NV50_SH_OP_FADD          0xb0000000ul
#define V9X_NV50_SH_OP_FMUL          0xc0000000ul
#define V9X_NV50_SH_OP_FMAD          0xe0000000ul
#define V9X_NV50_SH_OP_SFN           0x90000000ul
#define V9X_NV50_SH_OP_INTERP        0x80000000ul
#define V9X_NV50_SH_OP_TEX           0xf0000000ul
/* emitMOV: a 32-bit move (short in word 0, long in word 1), all four
 * lanes. */
#define V9X_NV50_SH_MOV32_SHORT      0x00008000ul
#define V9X_NV50_SH_MOV32_LONG       0x04000000ul
#define V9X_NV50_SH_MOV_LANES        (0xful << 14)
/* emitINTERP: perspective (PINTERP). */
#define V9X_NV50_SH_INTERP_PERSP     0x02000000ul
/* emitTEX: two coordinates (argc - 1 = 1), liveOnly. */
#define V9X_NV50_SH_TEX_ARGC_2D      (1ul << 22)
#define V9X_NV50_SH_TEX_LIVE         0x00000004ul

/* The three encodings setSrcFileBits distinguishes. */
#define V9X_NV50_SH_ENC_SHORT        0u
#define V9X_NV50_SH_ENC_LONG         1u
#define V9X_NV50_SH_ENC_LONG_ALT     2u

void v9x_nv50_sh_init(struct v9x_nv50_sh *sh, v9x_u32 *words, v9x_u32 max)
{
    sh->words = words;
    sh->max = max;
    sh->count = 0ul;
    sh->last = 0ul;
    sh->last_size = 0ul;
    sh->overflow = 0ul;
    sh->invalid = 0ul;
}

static v9x_u32 v9x_nv50_sh_file(v9x_u32 operand)
{
    return operand >> V9X_NV50_SH_FILE_SHIFT;
}

static v9x_u32 v9x_nv50_sh_buffer(v9x_u32 operand)
{
    return (operand >> V9X_NV50_SH_BUFFER_SHIFT) & 0xful;
}

/* setSrc's id: a register's number, or a 32-bit slot's byte offset / 4. */
static v9x_u32 v9x_nv50_sh_id(v9x_u32 operand)
{
    v9x_u32 value = operand & 0xfffful;

    if (v9x_nv50_sh_file(operand) == V9X_NV50_SH_FILE_R) {
        return value;
    }
    return value >> 2;
}

static void v9x_nv50_sh_emit(struct v9x_nv50_sh *sh, v9x_u32 word0,
                             v9x_u32 word1, v9x_u32 size)
{
    if (sh->invalid != 0ul) {
        return;
    }
    if (sh->count + size / 4ul > sh->max) {
        sh->overflow = 1ul;
        return;
    }
    sh->last = sh->count;
    sh->last_size = size;
    sh->words[sh->count++] = word0;
    if (size == V9X_NV50_SH_LONG) {
        sh->words[sh->count++] = word1;
    }
}

/* setDst for a long instruction: a register, or an output (word 1 bit 3). */
static int v9x_nv50_sh_dst(v9x_u32 dst, v9x_u32 *word0, v9x_u32 *word1)
{
    v9x_u32 file = v9x_nv50_sh_file(dst);

    if (file == V9X_NV50_SH_FILE_O) {
        *word1 |= V9X_NV50_SH_DST_OUTPUT;
    } else if (file != V9X_NV50_SH_FILE_R) {
        return 0;
    }
    *word0 |= v9x_nv50_sh_id(dst) << 2;
    return 1;
}

/*
 * setSrcFileBits: the source files as a mode, two bits per source in
 * operation order (register 0, input 1, constant 2), and the bits each
 * encodable mode sets. Zero when the hardware has no such mode.
 */
static int v9x_nv50_sh_files(const v9x_u32 *src, v9x_u32 count,
                             unsigned enc, v9x_u32 *word0, v9x_u32 *word1)
{
    v9x_u32 mode = 0ul;
    v9x_u32 index;
    v9x_u32 file;

    for (index = 0ul; index < count; ++index) {
        file = v9x_nv50_sh_file(src[index]);
        if (file == V9X_NV50_SH_FILE_A) {
            mode |= 1ul << (index * 2ul);
        } else if (file == V9X_NV50_SH_FILE_C) {
            mode |= 2ul << (index * 2ul);
        } else if (file != V9X_NV50_SH_FILE_R) {
            return 0;
        }
    }
    switch (mode) {
    case 0x00ul:                /* rrr */
        return 1;
    case 0x01ul:                /* arr */
        if (enc == V9X_NV50_SH_ENC_SHORT) {
            *word0 |= V9X_NV50_SH_SRC_INPUT_SHORT;
        } else {
            *word1 |= V9X_NV50_SH_SRC_INPUT_LONG;
        }
        return 1;
    case 0x08ul:                /* rcr */
    case 0x09ul:                /* acr */
        if (enc == V9X_NV50_SH_ENC_SHORT && v9x_nv50_sh_buffer(src[1]) != 0ul) {
            return 0;           /* the buffer would be in word 1 */
        }
        *word0 |= enc == V9X_NV50_SH_ENC_LONG_ALT ? V9X_NV50_SH_SRC_CONST_ALT
                                                  : V9X_NV50_SH_SRC_CONST;
        *word1 |= v9x_nv50_sh_buffer(src[1]) << V9X_NV50_SH_SRC_BUFFER_SHIFT;
        if (mode == 0x09ul) {
            if (enc == V9X_NV50_SH_ENC_SHORT) {
                return 0;
            }
            *word1 |= V9X_NV50_SH_SRC_INPUT_LONG;
        }
        return 1;
    case 0x20ul:                /* rrc */
    case 0x21ul:                /* arc */
        if (enc == V9X_NV50_SH_ENC_SHORT) {
            return 0;
        }
        *word0 |= V9X_NV50_SH_SRC_CONST_ALT;
        *word1 |= v9x_nv50_sh_buffer(src[2]) << V9X_NV50_SH_SRC_BUFFER_SHIFT;
        if (mode == 0x21ul) {
            *word1 |= V9X_NV50_SH_SRC_INPUT_LONG;
        }
        return 1;
    default:
        return 0;
    }
}

/* setSrc: slot 0 at word 0 bit 9, slot 1 at word 0 bit 16, slot 2 at
 * word 1 bit 14. */
static void v9x_nv50_sh_slot(v9x_u32 operand, unsigned slot, v9x_u32 *word0,
                             v9x_u32 *word1)
{
    v9x_u32 id = v9x_nv50_sh_id(operand);

    if (slot == 0u) {
        *word0 |= id << 9;
    } else if (slot == 1u) {
        *word0 |= id << 16;
    } else {
        *word1 |= id << 14;
    }
}

/* emitForm_MAD: long, sources in slots 0, 1, 2. */
static void v9x_nv50_sh_form_mad(struct v9x_nv50_sh *sh, v9x_u32 word0,
                                 v9x_u32 word1, v9x_u32 dst,
                                 const v9x_u32 *src, v9x_u32 count)
{
    v9x_u32 index;

    word0 |= V9X_NV50_SH_LONG_BIT;
    word1 |= V9X_NV50_SH_FLAGS_ALWAYS;
    if (!v9x_nv50_sh_dst(dst, &word0, &word1) ||
        !v9x_nv50_sh_files(src, count, V9X_NV50_SH_ENC_LONG, &word0,
                           &word1)) {
        sh->invalid = 1ul;
        return;
    }
    for (index = 0ul; index < count; ++index) {
        v9x_nv50_sh_slot(src[index], (unsigned)index, &word0, &word1);
    }
    v9x_nv50_sh_emit(sh, word0, word1, V9X_NV50_SH_LONG);
}

/* emitForm_ADD: long, the second source in slot 2. */
static void v9x_nv50_sh_form_add(struct v9x_nv50_sh *sh, v9x_u32 word0,
                                 v9x_u32 dst, v9x_u32 a, v9x_u32 b)
{
    v9x_u32 word1 = V9X_NV50_SH_FLAGS_ALWAYS;
    v9x_u32 src[2];

    src[0] = a;
    src[1] = b;
    word0 |= V9X_NV50_SH_LONG_BIT;
    if (!v9x_nv50_sh_dst(dst, &word0, &word1) ||
        !v9x_nv50_sh_files(src, 2ul, V9X_NV50_SH_ENC_LONG_ALT, &word0,
                           &word1)) {
        sh->invalid = 1ul;
        return;
    }
    v9x_nv50_sh_slot(a, 0u, &word0, &word1);
    v9x_nv50_sh_slot(b, 2u, &word0, &word1);
    v9x_nv50_sh_emit(sh, word0, word1, V9X_NV50_SH_LONG);
}

/* emitForm_MUL: short, a register destination, sources in slots 0, 1. */
static void v9x_nv50_sh_form_mul(struct v9x_nv50_sh *sh, v9x_u32 word0,
                                 v9x_u32 dst, const v9x_u32 *src,
                                 v9x_u32 count)
{
    v9x_u32 word1 = 0ul;
    v9x_u32 index;

    if (v9x_nv50_sh_file(dst) != V9X_NV50_SH_FILE_R ||
        !v9x_nv50_sh_files(src, count, V9X_NV50_SH_ENC_SHORT, &word0,
                           &word1)) {
        sh->invalid = 1ul;
        return;
    }
    word0 |= v9x_nv50_sh_id(dst) << 2;
    for (index = 0ul; index < count; ++index) {
        v9x_nv50_sh_slot(src[index], (unsigned)index, &word0, &word1);
    }
    v9x_nv50_sh_emit(sh, word0, 0ul, V9X_NV50_SH_SHORT);
}

/* emitMOV, register or input source. A long move from an input sets the
 * input file bit in word 1 (setSrcFileBits' "arr"), as EXA's vertex
 * program does to copy its position; that program ran
 * (docs\decisions\2026-11-14-gt218-3d-first-triangle.md). */
void v9x_nv50_sh_mov(struct v9x_nv50_sh *sh, v9x_u32 size, v9x_u32 dst,
                     v9x_u32 src)
{
    v9x_u32 word0 = V9X_NV50_SH_OP_MOV;
    v9x_u32 word1;
    v9x_u32 file = v9x_nv50_sh_file(src);

    if (size == V9X_NV50_SH_SHORT) {
        if (v9x_nv50_sh_file(dst) != V9X_NV50_SH_FILE_R ||
            file != V9X_NV50_SH_FILE_R) {
            sh->invalid = 1ul;
            return;
        }
        word0 |= V9X_NV50_SH_MOV32_SHORT | (v9x_nv50_sh_id(dst) << 2) |
                 (v9x_nv50_sh_id(src) << 9);
        v9x_nv50_sh_emit(sh, word0, 0ul, V9X_NV50_SH_SHORT);
        return;
    }
    word0 |= V9X_NV50_SH_LONG_BIT;
    word1 = V9X_NV50_SH_MOV32_LONG | V9X_NV50_SH_MOV_LANES |
            V9X_NV50_SH_FLAGS_ALWAYS;
    if ((file != V9X_NV50_SH_FILE_R && file != V9X_NV50_SH_FILE_A) ||
        !v9x_nv50_sh_dst(dst, &word0, &word1)) {
        sh->invalid = 1ul;
        return;
    }
    if (file == V9X_NV50_SH_FILE_A) {
        word1 |= V9X_NV50_SH_SRC_INPUT_LONG;
    }
    word0 |= v9x_nv50_sh_id(src) << 9;
    v9x_nv50_sh_emit(sh, word0, word1, V9X_NV50_SH_LONG);
}

/* emitFMUL, round to nearest, no negation or saturation. */
void v9x_nv50_sh_fmul(struct v9x_nv50_sh *sh, v9x_u32 size, v9x_u32 dst,
                      v9x_u32 a, v9x_u32 b)
{
    v9x_u32 src[2];

    src[0] = a;
    src[1] = b;
    if (size == V9X_NV50_SH_SHORT) {
        v9x_nv50_sh_form_mul(sh, V9X_NV50_SH_OP_FMUL, dst, src, 2ul);
        return;
    }
    v9x_nv50_sh_form_mad(sh, V9X_NV50_SH_OP_FMUL, 0ul, dst, src, 2ul);
}

/* emitFMAD, long only: the short form takes its third source from the
 * destination, which nothing here needs. */
void v9x_nv50_sh_fmad(struct v9x_nv50_sh *sh, v9x_u32 size, v9x_u32 dst,
                      v9x_u32 a, v9x_u32 b, v9x_u32 c)
{
    v9x_u32 src[3];

    if (size != V9X_NV50_SH_LONG) {
        sh->invalid = 1ul;
        return;
    }
    src[0] = a;
    src[1] = b;
    src[2] = c;
    v9x_nv50_sh_form_mad(sh, V9X_NV50_SH_OP_FMAD, 0ul, dst, src, 3ul);
}

/* emitFADD, round to nearest. */
void v9x_nv50_sh_fadd(struct v9x_nv50_sh *sh, v9x_u32 size, v9x_u32 dst,
                      v9x_u32 a, v9x_u32 b)
{
    v9x_u32 src[2];

    if (size == V9X_NV50_SH_SHORT) {
        src[0] = a;
        src[1] = b;
        v9x_nv50_sh_form_mul(sh, V9X_NV50_SH_OP_FADD, dst, src, 2ul);
        return;
    }
    v9x_nv50_sh_form_add(sh, V9X_NV50_SH_OP_FADD, dst, a, b);
}

/* emitSFnOp with subop 0, the reciprocal. */
void v9x_nv50_sh_rcp(struct v9x_nv50_sh *sh, v9x_u32 size, v9x_u32 dst,
                     v9x_u32 src)
{
    if (size == V9X_NV50_SH_SHORT) {
        v9x_nv50_sh_form_mul(sh, V9X_NV50_SH_OP_SFN, dst, &src, 1ul);
        return;
    }
    v9x_nv50_sh_form_mad(sh, V9X_NV50_SH_OP_SFN, 0ul, dst, &src, 1ul);
}

/* emitINTERP, short: linear, or perspective with persp's 1/w. */
void v9x_nv50_sh_interp(struct v9x_nv50_sh *sh, v9x_u32 dst,
                        v9x_u32 varying, v9x_u32 persp)
{
    v9x_u32 word0 = V9X_NV50_SH_OP_INTERP;

    if (v9x_nv50_sh_file(dst) != V9X_NV50_SH_FILE_R ||
        v9x_nv50_sh_file(varying) != V9X_NV50_SH_FILE_V) {
        sh->invalid = 1ul;
        return;
    }
    word0 |= v9x_nv50_sh_id(dst) << 2;
    word0 |= v9x_nv50_sh_id(varying) << 16;
    if (persp != V9X_NV50_SH_NONE) {
        if (v9x_nv50_sh_file(persp) != V9X_NV50_SH_FILE_R) {
            sh->invalid = 1ul;
            return;
        }
        word0 |= V9X_NV50_SH_INTERP_PERSP | (v9x_nv50_sh_id(persp) << 9);
    }
    v9x_nv50_sh_emit(sh, word0, 0ul, V9X_NV50_SH_SHORT);
}

/* emitTEX for a plain 2D fetch. */
void v9x_nv50_sh_tex(struct v9x_nv50_sh *sh, v9x_u32 dst, v9x_u32 tex,
                     v9x_u32 samp, v9x_u32 mask, v9x_u32 live)
{
    v9x_u32 word0 = V9X_NV50_SH_OP_TEX | V9X_NV50_SH_LONG_BIT |
                    V9X_NV50_SH_TEX_ARGC_2D;
    v9x_u32 word1 = V9X_NV50_SH_FLAGS_ALWAYS;

    if (v9x_nv50_sh_file(dst) != V9X_NV50_SH_FILE_R) {
        sh->invalid = 1ul;
        return;
    }
    word0 |= (tex & 0x7ful) << 9;
    word0 |= (samp & 0xful) << 17;
    word0 |= (mask & 0x3ul) << 25;
    word1 |= (mask & 0xcul) << 12;
    if (live != 0ul) {
        word1 |= V9X_NV50_SH_TEX_LIVE;
    }
    word0 |= v9x_nv50_sh_id(dst) << 2;
    v9x_nv50_sh_emit(sh, word0, word1, V9X_NV50_SH_LONG);
}

void v9x_nv50_sh_exit(struct v9x_nv50_sh *sh)
{
    if (sh->invalid != 0ul || sh->overflow != 0ul) {
        return;
    }
    if (sh->last_size != V9X_NV50_SH_LONG) {
        sh->invalid = 1ul;
        return;
    }
    sh->words[sh->last + 1ul] |= V9X_NV50_SH_EXIT_BIT;
}
