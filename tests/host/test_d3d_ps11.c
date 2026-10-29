/*
 * The PS 1.0/1.1 parser (include\velocity9x\ps11.h). Token layouts are
 * d3d8types.h's, as docs\specifications\ddi8-on-win9x.md section 6 tables
 * them. The shaders here are written for the tests: they use the
 * instruction patterns docs\decisions\
 * 2026-10-29-haswell-ddi8-step3-what-nature-asks.md found, not its text.
 */
#include <stdio.h>
#include <string.h>
#include "velocity9x/ps11.h"

static unsigned int failures;
#define CHECK(e) do { if (!(e)) { ++failures; \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
} } while (0)

/* Token builders. */
#define VER(major, minor) (0xffff0000ul | ((major) << 8) | (minor))
#define END               0x0000fffful
#define COISSUE           0x40000000ul
#define COMMENT(n)        (0x0000fffeul | ((v9x_u32)(n) << 16))
#define DST(file, reg, mask, mod, shift) \
    (0x80000000ul | ((v9x_u32)(file) << 28) | ((v9x_u32)(shift) << 24) | \
     ((v9x_u32)(mod) << 20) | ((v9x_u32)(mask) << 16) | (v9x_u32)(reg))
#define SRC(file, reg, swizzle, mod) \
    (0x80000000ul | ((v9x_u32)(file) << 28) | ((v9x_u32)(mod) << 24) | \
     ((v9x_u32)(swizzle) << 16) | (v9x_u32)(reg))
#define R V9X_PS11_FILE_TEMP
#define V V9X_PS11_FILE_INPUT
#define C V9X_PS11_FILE_CONST
#define T V9X_PS11_FILE_TEXTURE
#define XYZW 0xe4u
#define AAAA 0xffu
#define BBBB 0xaau
#define ALL  0xfu
#define RGB  0x7u
#define A    0x8u

/* Opcodes, d3d8types.h. */
#define OP_NOP          0ul
#define OP_MOV          1ul
#define OP_ADD          2ul
#define OP_MAD          4ul
#define OP_MUL          5ul
#define OP_DP3          8ul
#define OP_CND          80ul
#define OP_DEF          81ul
#define OP_TEXCOORD     64ul
#define OP_TEXKILL      65ul
#define OP_TEX          66ul
#define OP_TEXBEM       67ul
#define OP_TEXREG2AR    69ul
#define OP_TEXM3X3PAD   73ul
#define OP_TEXM3X3VSPEC 77ul

static v9x_u32 words[96];
static v9x_u32 used;

static void put(v9x_u32 word)
{
    words[used++] = word;
}

static v9x_u32 parse(V9X_PS11_PROGRAM *p, v9x_u32 *where)
{
    return v9x_ps11_parse(words, used, p, where);
}

static void accepted(void)
{
    V9X_PS11_PROGRAM p;
    v9x_u32 where;

    /* tex t0; mov r0, t0 (1.0). */
    used = 0ul;
    put(VER(1ul, 0ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(END);
    CHECK(parse(&p, &where) == V9X_PS11_OK);
    CHECK(p.version == 0x0100ul && p.count == 2ul);
    CHECK(p.inst[0].op == V9X_PS11_TEX && p.inst[0].dst_reg == 0u);
    CHECK(p.inst[1].op == V9X_PS11_MOV && p.inst[1].sources == 1u &&
          p.inst[1].src[0].file == T && p.inst[1].src[0].swizzle == XYZW &&
          p.inst[1].dst_mask == ALL);
    CHECK(p.samples == 1ul && p.coords == 1ul && p.has_kill == 0ul);

    /* tex t0; texkill t1; mov r0, t0, with a comment between. */
    used = 0ul;
    put(VER(1ul, 0ul));
    put(COMMENT(2)); put(0x12345678ul); put(0x9abcdef0ul);
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXKILL); put(DST(T, 1, ALL, 0, 0));
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(END);
    CHECK(parse(&p, &where) == V9X_PS11_OK);
    CHECK(p.count == 3ul && p.inst[1].op == V9X_PS11_TEXKILL);
    CHECK(p.has_kill == 1ul && p.samples == 1ul && p.coords == 3ul);

    /* tex t0; texm3x3pad t1, t0; texm3x3pad t2, t0; texm3x3vspec t3, t0;
     * mov r0.rgb, t3; mov r0.a, v0.a */
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXM3X3PAD); put(DST(T, 1, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_TEXM3X3PAD); put(DST(T, 2, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_TEXM3X3VSPEC); put(DST(T, 3, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_MOV); put(DST(R, 0, RGB, 0, 0)); put(SRC(T, 3, XYZW, 0));
    put(OP_MOV); put(DST(R, 0, A, 0, 0)); put(SRC(V, 0, AAAA, 0));
    put(END);
    CHECK(parse(&p, &where) == V9X_PS11_OK);
    CHECK(p.count == 6ul && p.inst[3].op == V9X_PS11_TEXM3X3VSPEC);
    CHECK(p.samples == 0x9ul && p.coords == 0xful && p.inputs == 1ul);

    /* tex t0; texbem t1, t0; tex t2; tex t3; mul r0.rgb, t1, c0;
     * + mul r0.a, t2, t3 */
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_TEX); put(DST(T, 0, ALL, 0, 0));
    put(OP_TEXBEM); put(DST(T, 1, ALL, 0, 0)); put(SRC(T, 0, XYZW, 0));
    put(OP_TEX); put(DST(T, 2, ALL, 0, 0));
    put(OP_TEX); put(DST(T, 3, ALL, 0, 0));
    put(OP_MUL); put(DST(R, 0, RGB, 0, 0)); put(SRC(T, 1, XYZW, 0));
    put(SRC(C, 0, XYZW, 0));
    put(OP_MUL | COISSUE); put(DST(R, 0, A, 0, 0)); put(SRC(T, 2, XYZW, 0));
    put(SRC(T, 3, XYZW, 0));
    put(END);
    CHECK(parse(&p, &where) == V9X_PS11_OK);
    CHECK(p.count == 6ul && p.inst[5].coissue == 1u &&
          p.inst[4].coissue == 0u);
    CHECK(p.samples == 0xful && p.coords == 0xful);

    /* def c1, then modifiers, shifts and saturation. */
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_DEF); put(DST(C, 1, ALL, 0, 0));
    put(0x3f000000ul); put(0xbf800000ul); put(0x3f800000ul); put(0ul);
    put(OP_MAD); put(DST(R, 1, ALL, 1, 1)); put(SRC(C, 1, XYZW, 4));
    put(SRC(V, 1, XYZW, 6)); put(SRC(R, 0, AAAA, 1));
    put(OP_DP3); put(DST(R, 0, RGB, 0, 15)); put(SRC(R, 1, XYZW, 2));
    put(SRC(C, 1, XYZW, 3));
    put(OP_CND); put(DST(R, 0, ALL, 0, 0)); put(SRC(R, 0, AAAA, 0));
    put(SRC(R, 1, XYZW, 0)); put(SRC(V, 0, XYZW, 5));
    put(END);
    CHECK(parse(&p, &where) == V9X_PS11_OK);
    CHECK(p.def_mask == 2ul && p.def[1][0] == 0x3f000000ul &&
          p.def[1][1] == 0xbf800000ul);
    CHECK(p.count == 3ul);
    CHECK(p.inst[0].op == V9X_PS11_MAD && p.inst[0].saturate == 1u &&
          p.inst[0].shift == 1l && p.inst[0].src[0].mod == 4u &&
          p.inst[0].src[1].mod == 6u && p.inst[0].src[2].swizzle == AAAA);
    CHECK(p.inst[1].op == V9X_PS11_DP3 && p.inst[1].shift == -1l);
    CHECK(p.inst[2].op == V9X_PS11_CND && p.inst[2].src[2].mod == 5u);
    CHECK(p.inputs == 3ul);

    /* nop takes no parameters. */
    used = 0ul;
    put(VER(1ul, 1ul));
    put(OP_NOP);
    put(OP_MOV); put(DST(R, 0, ALL, 0, 0)); put(SRC(V, 0, XYZW, 0));
    put(END);
    CHECK(parse(&p, &where) == V9X_PS11_OK && p.count == 2ul);
}

/* One refusal: the shader body between the version and the end. */
static v9x_u32 refused(v9x_u32 version, const v9x_u32 *body, v9x_u32 n,
                       int end)
{
    V9X_PS11_PROGRAM p;
    v9x_u32 where;
    v9x_u32 i;

    used = 0ul;
    put(version);
    for (i = 0ul; i < n; ++i) {
        put(body[i]);
    }
    if (end) {
        put(END);
    }
    return parse(&p, &where);
}

static void refusals(void)
{
    static const v9x_u32 movv0[] = {
        OP_MOV, DST(R, 0, ALL, 0, 0), SRC(V, 0, XYZW, 0)
    };
    static const v9x_u32 reg2ar[] = {
        OP_TEX, DST(T, 0, ALL, 0, 0),
        OP_TEXREG2AR, DST(T, 1, ALL, 0, 0), SRC(T, 0, XYZW, 0)
    };
    static const v9x_u32 late_tex[] = {
        OP_MOV, DST(R, 0, ALL, 0, 0), SRC(V, 0, XYZW, 0),
        OP_TEX, DST(T, 0, ALL, 0, 0)
    };
    static const v9x_u32 r2[] = {
        OP_MOV, DST(R, 2, ALL, 0, 0), SRC(V, 0, XYZW, 0)
    };
    static const v9x_u32 c8[] = {
        OP_MOV, DST(R, 0, ALL, 0, 0), SRC(C, 8, XYZW, 0)
    };
    static const v9x_u32 mask_xy[] = {
        OP_MOV, DST(R, 0, 0x3u, 0, 0), SRC(V, 0, XYZW, 0)
    };
    static const v9x_u32 src_x2[] = {
        OP_MOV, DST(R, 0, ALL, 0, 0), SRC(V, 0, XYZW, 7)
    };
    static const v9x_u32 shift_x8[] = {
        OP_MOV, DST(R, 0, ALL, 0, 3), SRC(V, 0, XYZW, 0)
    };
    static const v9x_u32 partial[] = {
        OP_MOV, DST(R, 0, ALL, 2, 0), SRC(V, 0, XYZW, 0)
    };
    static const v9x_u32 swizzle_b[] = {
        OP_MOV, DST(R, 0, RGB, 0, 0), SRC(V, 0, BBBB, 0)
    };
    static const v9x_u32 swizzle_xxyy[] = {
        OP_MOV, DST(R, 0, ALL, 0, 0), SRC(V, 0, 0x50u, 0)
    };
    static const v9x_u32 cnd_v0[] = {
        OP_CND, DST(R, 0, ALL, 0, 0), SRC(V, 0, AAAA, 0),
        SRC(V, 0, XYZW, 0), SRC(V, 1, XYZW, 0)
    };
    static const v9x_u32 bem_order[] = {
        OP_TEX, DST(T, 1, ALL, 0, 0),
        OP_TEXBEM, DST(T, 0, ALL, 0, 0), SRC(T, 1, XYZW, 0)
    };
    static const v9x_u32 lone_pad[] = {
        OP_TEX, DST(T, 0, ALL, 0, 0),
        OP_TEXM3X3PAD, DST(T, 1, ALL, 0, 0), SRC(T, 0, XYZW, 0),
        OP_MOV, DST(R, 0, ALL, 0, 0), SRC(T, 0, XYZW, 0)
    };
    static const v9x_u32 pair_rgb[] = {
        OP_MOV, DST(R, 0, RGB, 0, 0), SRC(V, 0, XYZW, 0),
        OP_MOV | COISSUE, DST(R, 1, RGB, 0, 0), SRC(V, 1, XYZW, 0)
    };
    static const v9x_u32 dp3_alpha[] = {
        OP_DP3, DST(R, 0, A, 0, 0), SRC(V, 0, XYZW, 0), SRC(V, 1, XYZW, 0)
    };
    static const v9x_u32 truncated[] = {
        OP_MUL, DST(R, 0, ALL, 0, 0), SRC(V, 0, XYZW, 0)
    };
    v9x_u32 nine[27];
    v9x_u32 i;

    CHECK(refused(VER(1ul, 1ul), movv0, 3ul, 1) == V9X_PS11_OK);
    CHECK(refused(VER(1ul, 2ul), movv0, 3ul, 1) == V9X_PS11_VERSION);
    CHECK(refused(0xfffe0101ul, movv0, 3ul, 1) == V9X_PS11_VERSION);
    CHECK(refused(VER(1ul, 1ul), movv0, 3ul, 0) == V9X_PS11_MALFORMED);
    CHECK(refused(VER(1ul, 1ul), truncated, 3ul, 0) == V9X_PS11_MALFORMED);
    CHECK(refused(VER(1ul, 1ul), reg2ar, 5ul, 1) == V9X_PS11_UNSUPPORTED);
    CHECK(refused(VER(1ul, 1ul), late_tex, 5ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), r2, 3ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), c8, 3ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), mask_xy, 3ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), src_x2, 3ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), shift_x8, 3ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), partial, 3ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), swizzle_b, 3ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), swizzle_xxyy, 3ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), cnd_v0, 5ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), bem_order, 5ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), lone_pad, 8ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), pair_rgb, 6ul, 1) == V9X_PS11_INVALID);
    CHECK(refused(VER(1ul, 1ul), dp3_alpha, 4ul, 1) == V9X_PS11_INVALID);

    /* Nine arithmetic instructions are one too many. */
    for (i = 0ul; i < 9ul; ++i) {
        nine[i * 3ul] = OP_MOV;
        nine[i * 3ul + 1ul] = DST(R, 0, ALL, 0, 0);
        nine[i * 3ul + 2ul] = SRC(V, 0, XYZW, 0);
    }
    CHECK(refused(VER(1ul, 1ul), nine, 24ul, 1) == V9X_PS11_OK);
    CHECK(refused(VER(1ul, 1ul), nine, 27ul, 1) == V9X_PS11_INVALID);
}

unsigned int v9x_run_d3d_ps11_tests(void)
{
    failures = 0u;
    accepted();
    refusals();
    return failures;
}
