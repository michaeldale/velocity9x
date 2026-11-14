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

unsigned int v9x_run_nv50_shader_tests(void)
{
    failures = 0u;
    test_exa_vp();
    test_exa_fp_s();
    test_exa_fp_c();
    test_exa_fp_s_a8();
    test_refusals();
    return failures;
}
