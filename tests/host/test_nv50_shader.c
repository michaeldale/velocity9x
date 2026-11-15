#include <stdio.h>

#include "velocity9x/nv50_shader.h"
#include "velocity9x/nv50_3d.h"

/*
 * The Tesla shader encoder (src\chipsets\nvidia\gt218\nv50_shader.c)
 * rebuilding xf86-video-nouveau's EXA programs (nv50_accel.c) word for
 * word from their own disassembly. The vertex program and PFP_S ran on
 * M-BXMFA8 (docs\decisions\2026-11-14-gt218-3d-first-triangle.md); PFP_C
 * and PFP_S_A8 add the short multiply and move.
 */

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define R(n)      V9X_NV50_SH_R(n)
#define A(o)      V9X_NV50_SH_A(o)
#define C0(o)     V9X_NV50_SH_C(0, o)
#define O(o)      V9X_NV50_SH_O(o)
#define V(o)      V9X_NV50_SH_V(o)
#define L         V9X_NV50_SH_LONG
#define S         V9X_NV50_SH_SHORT

static v9x_u32 code[96];

static void check_words(const struct v9x_nv50_sh *sh, const v9x_u32 *expect,
                        v9x_u32 count, const char *name)
{
    v9x_u32 index;

    CHECK(sh->overflow == 0ul);
    CHECK(sh->invalid == 0ul);
    CHECK(sh->count == count);
    for (index = 0ul; index < count && index < sh->count; ++index) {
        if (sh->words[index] != expect[index]) {
            printf("FAIL %s word %lu: %08lx, expected %08lx\n", name,
                   (unsigned long)index, (unsigned long)sh->words[index],
                   (unsigned long)expect[index]);
            ++failures;
        }
    }
}

static void test_exa_vp(void)
{
    struct v9x_nv50_sh sh;

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_mov(&sh, L, O(0x0), A(0x0));
    v9x_nv50_sh_mov(&sh, L, O(0x4), A(0x4));
    v9x_nv50_sh_fmul(&sh, L, R(0), A(0x8), C0(0x0));
    v9x_nv50_sh_fmul(&sh, L, R(1), A(0x8), C0(0xc));
    v9x_nv50_sh_fmul(&sh, L, R(2), A(0x8), C0(0x18));
    v9x_nv50_sh_fmad(&sh, L, R(0), A(0xc), C0(0x4), R(0));
    v9x_nv50_sh_fmad(&sh, L, R(1), A(0xc), C0(0x10), R(1));
    v9x_nv50_sh_fmad(&sh, L, R(2), A(0xc), C0(0x1c), R(2));
    v9x_nv50_sh_fadd(&sh, L, R(0), R(0), C0(0x8));
    v9x_nv50_sh_fadd(&sh, L, R(1), R(1), C0(0x14));
    v9x_nv50_sh_fadd(&sh, L, R(2), R(2), C0(0x20));
    v9x_nv50_sh_rcp(&sh, L, R(2), R(2));
    v9x_nv50_sh_fmul(&sh, L, R(0), R(0), R(2));
    v9x_nv50_sh_fmul(&sh, L, R(1), R(1), R(2));
    v9x_nv50_sh_fmul(&sh, L, O(0x8), R(0), C0(0x24));
    v9x_nv50_sh_fmul(&sh, L, O(0xc), R(1), C0(0x28));
    v9x_nv50_sh_fmul(&sh, L, R(0), A(0x10), C0(0x2c));
    v9x_nv50_sh_fmul(&sh, L, R(1), A(0x10), C0(0x38));
    v9x_nv50_sh_fmul(&sh, L, R(2), A(0x10), C0(0x44));
    v9x_nv50_sh_fmad(&sh, L, R(0), A(0x14), C0(0x30), R(0));
    v9x_nv50_sh_fmad(&sh, L, R(1), A(0x14), C0(0x3c), R(1));
    v9x_nv50_sh_fmad(&sh, L, R(2), A(0x14), C0(0x48), R(2));
    v9x_nv50_sh_fadd(&sh, L, R(0), R(0), C0(0x34));
    v9x_nv50_sh_fadd(&sh, L, R(1), R(1), C0(0x40));
    v9x_nv50_sh_fadd(&sh, L, R(2), R(2), C0(0x4c));
    v9x_nv50_sh_rcp(&sh, L, R(2), R(2));
    v9x_nv50_sh_fmul(&sh, L, R(0), R(0), R(2));
    v9x_nv50_sh_fmul(&sh, L, R(1), R(1), R(2));
    v9x_nv50_sh_fmul(&sh, L, O(0x10), R(0), C0(0x50));
    v9x_nv50_sh_fmul(&sh, L, O(0x14), R(1), C0(0x54));
    v9x_nv50_sh_exit(&sh);
    check_words(&sh, v9x_nv50_3d_exa_vp, V9X_NV50_3D_EXA_VP_WORDS, "vp");
}

static void test_exa_fp_s(void)
{
    struct v9x_nv50_sh sh;

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_interp(&sh, R(0), V(0x0), V9X_NV50_SH_NONE);
    v9x_nv50_sh_rcp(&sh, S, R(1), R(0));
    v9x_nv50_sh_interp(&sh, R(0), V(0x4), R(1));
    v9x_nv50_sh_interp(&sh, R(1), V(0x8), R(1));
    v9x_nv50_sh_tex(&sh, R(0), 0ul, 0ul, 0xful, 1ul);
    v9x_nv50_sh_exit(&sh);
    check_words(&sh, v9x_nv50_3d_exa_fp_s, V9X_NV50_3D_EXA_FP_S_WORDS,
                "fp_s");
}

/* PFP_C: two textures multiplied, short multiplies, a long one to end. */
static void test_exa_fp_c(void)
{
    static const v9x_u32 expect[] = {
        0x80000000ul, 0x90000004ul, 0x82030210ul, 0x82040214ul,
        0x82010200ul, 0x82020204ul, 0xf6400001ul, 0x0000c784ul,
        0xf0400211ul, 0x00008784ul, 0xc0040000ul, 0xc0040204ul,
        0xc0040409ul, 0x00000780ul, 0xc004060dul, 0x00000781ul
    };
    struct v9x_nv50_sh sh;

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_interp(&sh, R(0), V(0x0), V9X_NV50_SH_NONE);
    v9x_nv50_sh_rcp(&sh, S, R(1), R(0));
    v9x_nv50_sh_interp(&sh, R(4), V(0xc), R(1));
    v9x_nv50_sh_interp(&sh, R(5), V(0x10), R(1));
    v9x_nv50_sh_interp(&sh, R(0), V(0x4), R(1));
    v9x_nv50_sh_interp(&sh, R(1), V(0x8), R(1));
    v9x_nv50_sh_tex(&sh, R(0), 0ul, 0ul, 0xful, 1ul);
    /* "texauto live #:#:#:$r4 $t1": only the fourth component, into
     * $r4, its coordinates in $r4:$r5. */
    v9x_nv50_sh_tex(&sh, R(4), 1ul, 0ul, 0x8ul, 1ul);
    v9x_nv50_sh_fmul(&sh, S, R(0), R(0), R(4));
    v9x_nv50_sh_fmul(&sh, S, R(1), R(1), R(4));
    v9x_nv50_sh_fmul(&sh, L, R(2), R(2), R(4));
    v9x_nv50_sh_fmul(&sh, L, R(3), R(3), R(4));
    v9x_nv50_sh_exit(&sh);
    check_words(&sh, expect, sizeof(expect) / sizeof(expect[0]), "fp_c");
}

/* PFP_S_A8: the short register moves and a long one to end. */
static void test_exa_fp_s_a8(void)
{
    static const v9x_u32 expect[] = {
        0x80000000ul, 0x90000004ul, 0x82010200ul, 0x82020204ul,
        0xf0400001ul, 0x00008784ul, 0x10008004ul, 0x10008008ul,
        0x1000000dul, 0x0403c781ul
    };
    struct v9x_nv50_sh sh;

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_interp(&sh, R(0), V(0x0), V9X_NV50_SH_NONE);
    v9x_nv50_sh_rcp(&sh, S, R(1), R(0));
    v9x_nv50_sh_interp(&sh, R(0), V(0x4), R(1));
    v9x_nv50_sh_interp(&sh, R(1), V(0x8), R(1));
    v9x_nv50_sh_tex(&sh, R(0), 0ul, 0ul, 0x8ul, 1ul);
    v9x_nv50_sh_mov(&sh, S, R(1), R(0));
    v9x_nv50_sh_mov(&sh, S, R(2), R(0));
    v9x_nv50_sh_mov(&sh, L, R(3), R(0));
    v9x_nv50_sh_exit(&sh);
    check_words(&sh, expect, sizeof(expect) / sizeof(expect[0]), "fp_s_a8");
}

/*
 * Short instructions come in pairs: a long one starts on a 64-bit word.
 * EXA's programs keep the pairs by hand (PFP_C's two short multiplies
 * before its long ones). A long instruction after an odd count of short
 * ones gets a short `mov $r0, $r0` before it, which changes nothing. The
 * first Direct3D fog program on M-BXMFA8 put its long fadd at byte 54h
 * and the MP trapped it as INVALID_OPCODE.
 */
static void test_pairing(void)
{
    static const v9x_u32 expect[] = {
        0x80000000ul,                   /* interp $r0 v[0x0] */
        0x10008000ul,                   /* mov $r0 $r0, the pad */
        0xc0020001ul, 0x00000780ul,     /* fmul $r0 $r0 $r2, long */
        0x80000000ul, 0x90000004ul,     /* a pair: no pad */
        0xc0020001ul, 0x00000781ul      /* the same long, ending */
    };
    struct v9x_nv50_sh sh;

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_interp(&sh, R(0), V(0x0), V9X_NV50_SH_NONE);
    v9x_nv50_sh_fmul(&sh, L, R(0), R(0), R(2));
    v9x_nv50_sh_interp(&sh, R(0), V(0x0), V9X_NV50_SH_NONE);
    v9x_nv50_sh_rcp(&sh, S, R(1), R(0));
    v9x_nv50_sh_fmul(&sh, L, R(0), R(0), R(2));
    v9x_nv50_sh_exit(&sh);
    check_words(&sh, expect, sizeof(expect) / sizeof(expect[0]), "pairing");

    /* A pad that does not fit is an overflow, not a misaligned long. */
    v9x_nv50_sh_init(&sh, code, 3ul);
    v9x_nv50_sh_interp(&sh, R(0), V(0x0), V9X_NV50_SH_NONE);
    v9x_nv50_sh_fmul(&sh, L, R(0), R(0), R(2));
    CHECK(sh.overflow != 0ul);
}

/* What cannot be encoded is refused, not emitted wrong. */
static void test_refusals(void)
{
    struct v9x_nv50_sh sh;

    /* A short instruction cannot write an output or read a constant
     * in place of a register pair it lacks; exit needs a long one. */
    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_fmul(&sh, S, O(0x0), R(0), R(1));
    CHECK(sh.invalid != 0ul);

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_rcp(&sh, S, R(1), R(0));
    v9x_nv50_sh_exit(&sh);
    CHECK(sh.invalid != 0ul);

    /* Two inputs in one instruction: no source-file mode has it. */
    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_fmul(&sh, L, R(0), A(0x0), A(0x4));
    CHECK(sh.invalid != 0ul);

    /* Overflow. */
    v9x_nv50_sh_init(&sh, code, 1ul);
    v9x_nv50_sh_fmul(&sh, L, R(0), R(0), R(1));
    CHECK(sh.overflow != 0ul);
}

/*
 * The pixel shader 1.1 instructions (step 1 of docs\plans\
 * nvidia-gt218-ps11.md). Each expected word is worked out from Mesa's
 * CodeEmitterNV50 for the same instruction (nv50_ir_emit_nv50.cpp) and
 * read back against envydis' g80.c tables, cited per case: the field
 * positions there are bit numbers in the 64-bit instruction, so 3Ah is
 * word 1 bit 26.
 */
#define NEG(x)    V9X_NV50_SH_NEG(x)
#define ABS(x)    V9X_NV50_SH_ABS(x)
#define SAT(x)    V9X_NV50_SH_SAT(x)

static void test_ps11_arithmetic(void)
{
    struct v9x_nv50_sh sh;
    static const v9x_u32 fadd[] = {
        /* add sat f32 $r2, neg $r1, neg $r3: emitFADD long, o0sat 3Dh,
         * m1neg 3Ah, m2neg 3Bh, the second source in slot 2. */
        0xb0000209ul, 0x2c00c780ul,
        /* add sat f32 $r0, neg $r1, $r2; add f32 $r3, $r4, neg $r5:
         * short, sm1sat 8, sm2neg 0Fh, sm3neg 16h. */
        0xb0028300ul, 0xb045080cul,
    };
    static const v9x_u32 fmul[] = {
        /* mul sat f32 $r4, neg $r1, c0[0x10]: emitFMUL long, the
         * product's negation in 3Bh, s36satg200 34h, rcr. */
        0xc0840211ul, 0x08100780ul,
        /* mul f32 $r1, neg $r2, neg $r3 (the signs cancel); mul sat f32
         * $r5, $r6, neg $r7: short. */
        0xc0030404ul, 0xc0078d14ul,
    };
    static const v9x_u32 fmad[] = {
        /* add sat f32 $r0, neg (mul $r1, c0[0x8]), neg $r2: emitFMAD
         * long, neg_mul 3Ah, neg_add 3Bh, sat 3Dh. */
        0xe0820201ul, 0x2c008780ul,
    };
    static const v9x_u32 minmax[] = {
        /* max f32 $r3, abs $r1, neg c0[0x4]: emitMINMAX, s36abs 34h,
         * m2neg 3Bh. */
        0xb081020dul, 0x88100780ul,
        /* min f32 $r2, neg $r0, abs $r5: m1neg 3Ah, s35abs 33h. */
        0xb0050009ul, 0xa4080780ul,
    };
    static const v9x_u32 set[] = {
        /* set gt f32 $c1 (no register), $r1, c0[0x14]: emitSET, the
         * condition at 2Eh (setf), the bit bucket, emitFlagsWr. */
        0xb08503fdul, 0x600107d8ul,
        /* set lt f32 $r6, $r2, $r3. */
        0xb0030419ul, 0x60004780ul,
        /* $c1 ne mov b32 $r0, $r7: emitFlagsRd, the condition at 27h,
         * the register at 2Ch (envydis tabp). */
        0x10000e01ul, 0x0403d280ul,
        /* $c0 ne discard; discard: emitFlow 0, tabc. */
        0x00000003ul, 0x00000280ul,
        0x00000003ul, 0x00000780ul,
        /* mov b32 $r3, $r3 to end on, with exit. */
        0x1000060dul, 0x0403c781ul,
    };
    static const v9x_u32 tex[] = {
        /* texauto cube all $r8-$r11, t1, s1, three coordinates: emitTEX,
         * argc - 1 at 16h, the cube bit 1Bh. */
        0xfe820221ul, 0x0000c780ul,
        /* texauto 3D all $r12-$r15, t2, s2. */
        0xf6840431ul, 0x0000c780ul,
    };

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_fadd(&sh, L, SAT(R(2)), NEG(R(1)), NEG(R(3)));
    v9x_nv50_sh_fadd(&sh, S, SAT(R(0)), NEG(R(1)), R(2));
    v9x_nv50_sh_fadd(&sh, S, R(3), R(4), NEG(R(5)));
    check_words(&sh, fadd, 4ul, "ps11 fadd");

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_fmul(&sh, L, SAT(R(4)), NEG(R(1)), C0(0x10));
    v9x_nv50_sh_fmul(&sh, S, R(1), NEG(R(2)), NEG(R(3)));
    v9x_nv50_sh_fmul(&sh, S, SAT(R(5)), R(6), NEG(R(7)));
    check_words(&sh, fmul, 4ul, "ps11 fmul");

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_fmad(&sh, L, SAT(R(0)), NEG(R(1)), C0(0x8), NEG(R(2)));
    check_words(&sh, fmad, 2ul, "ps11 fmad");

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_fmax(&sh, R(3), ABS(R(1)), NEG(C0(0x4)));
    v9x_nv50_sh_fmin(&sh, R(2), NEG(R(0)), ABS(R(5)));
    check_words(&sh, minmax, 4ul, "ps11 min max");

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_set(&sh, V9X_NV50_SH_NONE, V9X_NV50_SH_CC_GT, R(1),
                    C0(0x14), 1ul);
    v9x_nv50_sh_set(&sh, R(6), V9X_NV50_SH_CC_LT, R(2), R(3),
                    V9X_NV50_SH_NONE);
    v9x_nv50_sh_predicate(&sh, V9X_NV50_SH_CC_NE, 1ul);
    v9x_nv50_sh_mov(&sh, L, R(0), R(7));
    v9x_nv50_sh_predicate(&sh, V9X_NV50_SH_CC_NE, 0ul);
    v9x_nv50_sh_kil(&sh);
    v9x_nv50_sh_kil(&sh);
    v9x_nv50_sh_mov(&sh, L, R(3), R(3));
    v9x_nv50_sh_exit(&sh);
    check_words(&sh, set, 12ul, "ps11 set select kill");

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_tex_n(&sh, R(8), 1ul, 1ul, 0xful, 3ul, 1ul);
    v9x_nv50_sh_tex_n(&sh, R(12), 2ul, 2ul, 0xful, 3ul, 0ul);
    check_words(&sh, tex, 4ul, "ps11 tex");
}

/* Modifiers where the instruction has no field for them, and the new
 * forms' own limits, make the program invalid. */
static void test_ps11_refusals(void)
{
    struct v9x_nv50_sh sh;

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_mov(&sh, L, R(0), NEG(R(1)));
    CHECK(sh.invalid != 0ul);

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_rcp(&sh, L, SAT(R(0)), R(1));
    CHECK(sh.invalid != 0ul);

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_fadd(&sh, L, R(0), ABS(R(1)), R(2));
    CHECK(sh.invalid != 0ul);

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_fmax(&sh, SAT(R(0)), R(1), R(2));
    CHECK(sh.invalid != 0ul);

    /* A short instruction has no condition field. */
    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_predicate(&sh, V9X_NV50_SH_CC_NE, 0ul);
    v9x_nv50_sh_fadd(&sh, S, R(0), R(1), R(2));
    CHECK(sh.invalid != 0ul);

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_set(&sh, V9X_NV50_SH_NONE, V9X_NV50_SH_CC_LT, R(0), R(1),
                    4ul);
    CHECK(sh.invalid != 0ul);

    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_tex_n(&sh, R(8), 0ul, 0ul, 0xful, 2ul, 1ul);
    CHECK(sh.invalid != 0ul);

    /* A short instruction's register fields are six bits (envydis
     * sdst_bf, ssrc_bf): $r64 needs the long form. */
    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_mov(&sh, S, R(64), R(1));
    CHECK(sh.invalid != 0ul);
    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_fadd(&sh, S, R(0), R(1), R(64));
    CHECK(sh.invalid != 0ul);
    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_mov(&sh, L, R(64), R(100));
    CHECK(sh.invalid == 0ul);

    /* A program cannot end on a discard. */
    v9x_nv50_sh_init(&sh, code, 96ul);
    v9x_nv50_sh_kil(&sh);
    v9x_nv50_sh_exit(&sh);
    CHECK(sh.invalid != 0ul);
}

unsigned int v9x_run_nv50_shader_tests(void)
{
    failures = 0u;
    test_ps11_arithmetic();
    test_ps11_refusals();
    test_exa_vp();
    test_exa_fp_s();
    test_exa_fp_c();
    test_exa_fp_s_a8();
    test_pairing();
    test_refusals();
    return failures;
}
