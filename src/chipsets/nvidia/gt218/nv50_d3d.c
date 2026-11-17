/*
 * Direct3D draws on the GT218's Tesla 3D engine; see
 * include\velocity9x\nv50_d3d.h. The methods are Mesa's nv50_3d.xml.h
 * offsets and the state Mesa's nv50_state.c writes for the same pipe
 * state; the programs come from the host-tested encoder (nv50_shader.c).
 * Pure arithmetic, no I/O.
 */
#include "velocity9x/nv50_d3d.h"
#include "velocity9x/nv50_shader.h"

/* Methods this file adds to nv50_3d.h's (Mesa nv50_3d.xml.h). */
#define V9X_8597_VTX_ATTR_2F_X8      0x03c0ul /* 380h + 8 * 8 */
#define V9X_8597_VTX_ATTR_4F_X0      0x0500ul
#define V9X_8597_VTX_ATTR_4F_X1      0x0510ul
#define V9X_8597_VTX_ATTR_4F_X2      0x0520ul
#define V9X_8597_ALPHA_TEST_ENABLE   0x12ecul
#define V9X_8597_ALPHA_TEST_REF      0x1310ul
#define V9X_8597_BLEND_EQUATION_RGB  0x1340ul
#define V9X_8597_BLEND_FUNC_DST_ALPHA 0x1358ul
#define V9X_8597_CULL_FACE_ENABLE    0x1918ul
#define V9X_8597_COLOR_MASK0         0x1a00ul
#define V9X_8597_BLEND_FUNC_ADD      0x8006ul

/*
 * The varyings: position x y z w in results 0-3, diffuse 4-7, specular
 * 8-11, the coordinates 12 and 13; the result map sends each to the
 * interpolant of the same number, and 0x40 (unused) fills the last word.
 * The fragment side reads them EXA's way (FP_INTERPOLANT_CTRL offset 4,
 * position w alone interpolated, as PFP_C and phase 3's Gouraud program
 * read it): v[0x00] is the position's w term, diffuse from v[0x04],
 * specular from v[0x14], the coordinates at v[0x24]. The first /3d:d3d
 * run read from v[0x0c] and v[0x10], and its texture sampled texel (0, 0)
 * everywhere. Unit 1's coordinates (attribute 9, step 5 of
 * nvidia-gt218-ps11.md) follow as results 14 and 15, v[0x2c]; a one-unit
 * draw does not send attribute 9 and its programs never read them.
 */
#define V9X_NV50_D3D_RESULTS         16ul
#define V9X_NV50_D3D_MAP0            0x03020100ul
#define V9X_NV50_D3D_MAP1            0x07060504ul
#define V9X_NV50_D3D_MAP2            0x0b0a0908ul
#define V9X_NV50_D3D_MAP3            0x0f0e0d0cul
/* UMASK W, twelve non-flat interpolants, offset 4, twelve in all. */
#define V9X_NV50_D3D_INTERPOLANTS    0x080c040cul
/* Attributes 0, 1 and 2 whole; 8's and 9's x and y. */
#define V9X_NV50_D3D_ATTR_EN0        0x00000ffful
#define V9X_NV50_D3D_ATTR_EN1        0x00000033ul
#define V9X_8597_VTX_ATTR_2F_X9      0x03c8ul /* 380h + 9 * 8 */
#define V9X_8597_CODE_CB_FLUSH       0x1440ul
#define V9X_8597_TSC_FLUSH           0x1330ul
#define V9X_8597_FP_CONTROL          0x19a8ul
#define V9X_8597_FP_USES_KIL         0x00100000ul
/* The shader interface (nv50_d3d.h): attributes 0-6 whole; UMASK W, 24
 * non-flat interpolants from map entry 4, 24 in all. */
#define V9X_NV50_D3D_SHADER_ATTR_EN0 0x0ffffffful
#define V9X_NV50_D3D_SHADER_INTERPOLANTS 0x08180418ul
#define V9X_8597_VTX_ATTR_4F(n)      (0x0500ul + 0x10ul * (n))

#define V9X_NV50_D3D_V_W             0x00ul
#define V9X_NV50_D3D_V_DIFFUSE       0x04ul
#define V9X_NV50_D3D_V_SPECULAR      0x14ul
#define V9X_NV50_D3D_V_FOG           0x20ul /* specular alpha */
#define V9X_NV50_D3D_V_COORDS        0x24ul
#define V9X_NV50_D3D_V_COORDS1       0x2cul

/* Fragment registers: the colour in $r0-$r3 (the result), w in $r4, a
 * temporary in $r5, the texel in $r8-$r11, specular in $r12-$r14, the
 * fog factor in $r15. */
#define V9X_NV50_D3D_R_W             4ul
#define V9X_NV50_D3D_R_TEMP          5ul
#define V9X_NV50_D3D_R_TEXEL         8ul
#define V9X_NV50_D3D_R_SPECULAR      12ul
#define V9X_NV50_D3D_R_FOG           15ul
/* A two-unit program's second texel, $r16-$r19. */
#define V9X_NV50_D3D_R_TEXEL1        16ul

/* The fog colour at c0[0..2], its negation at c0[4..6] (byte offsets).
 * The constants are uploaded to buffer 2, FP CB_DEF, which the setup
 * binds to the fragment program's slot 0 (SET_PROGRAM_CB 31h | 2 << 12,
 * Mesa's binding too). The first /3d:d3d run read c2[], an unbound slot,
 * and the fog draw trapped in the MP. */
#define V9X_NV50_D3D_FP_SLOT         0ul
#define V9X_NV50_D3D_C_FOG           0x00ul
#define V9X_NV50_D3D_C_FOG_NEG       0x10ul

/* TIC word 0 fields (g80_texture.xml.h, g80_defs.xml.h): UNORM in all
 * four types, then the sources of X Y Z W. */
#define V9X_NV50_TIC0_UNORM          0x00012480ul
#define V9X_NV50_TIC0_X_SHIFT        18u
#define V9X_NV50_TIC0_Y_SHIFT        21u
#define V9X_NV50_TIC0_Z_SHIFT        24u
#define V9X_NV50_TIC0_W_SHIFT        27u
#define V9X_NV50_TIC_SOURCE_R        2ul
#define V9X_NV50_TIC_SOURCE_G        3ul
#define V9X_NV50_TIC_SOURCE_B        4ul
#define V9X_NV50_TIC_SOURCE_A        5ul
#define V9X_NV50_TIC_SOURCE_ONE      7ul /* ONE_FLOAT */
#define V9X_NV50_TIC_SIZES_A8B8G8R8  0x08ul
#define V9X_NV50_TIC_SIZES_A4B4G4R4  0x12ul
#define V9X_NV50_TIC_SIZES_A1B5G5R5  0x14ul
#define V9X_NV50_TIC_SIZES_B5G6R5    0x15ul
/* Pitch layout, normalized coordinates, 2D, as phase 3's texture ran. */
#define V9X_NV50_TIC2_PITCH_2D       0xd005d000ul
#define V9X_NV50_TIC5_DEPTH_ONE      0x00010000ul
/* TSC word 0: EXA's 24000h beside the three address modes; word 1: the
 * filters with no mip filter (G80_TSC_1_*). */
#define V9X_NV50_TSC0_BASE           0x00024000ul
#define V9X_NV50_TSC1_MAG_NEAREST    0x01ul
#define V9X_NV50_TSC1_MAG_LINEAR     0x02ul
#define V9X_NV50_TSC1_MIN_NEAREST    0x10ul
#define V9X_NV50_TSC1_MIN_LINEAR     0x20ul
#define V9X_NV50_TSC1_MIP_NONE       0x40ul

#define V9X_NV50_D3D_COLOR_MAX       255.0f

static v9x_u32 v9x_nv50_d3d_bits(float value)
{
    union {
        float f;
        v9x_u32 u;
    } converted;

    converted.f = value;
    return converted.u;
}

static float v9x_nv50_d3d_unorm(v9x_u32 byte)
{
    return (float)(long)(byte & 0xfful) / V9X_NV50_D3D_COLOR_MAX;
}

static void v9x_nv50_d3d_one(struct v9x_nv50_push *push, v9x_u32 method,
                             v9x_u32 value)
{
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, method, 1ul, &value);
}

static void v9x_nv50_d3d_two(struct v9x_nv50_push *push, v9x_u32 method,
                             v9x_u32 first, v9x_u32 second)
{
    v9x_u32 data[2];

    data[0] = first;
    data[1] = second;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, method, 2ul, data);
}

v9x_u32 v9x_nv50_d3d_fp_variant(v9x_u32 op, v9x_u32 specular, v9x_u32 fog)
{
    return op * 4ul + (specular != 0ul ? 2ul : 0ul) + (fog != 0ul ? 1ul : 0ul);
}

v9x_u32 v9x_nv50_d3d_fp_start(v9x_u32 variant)
{
    if (variant >= V9X_NV50_D3D_FP_SCRATCH_VARIANTS) {
        return V9X_NV50_D3D_FP_RGB_FIRST +
               (variant - V9X_NV50_D3D_FP_SCRATCH_VARIANTS) *
                   V9X_NV50_D3D_FP_STRIDE;
    }
    return V9X_NV50_D3D_FP_FIRST + variant * V9X_NV50_D3D_FP_STRIDE;
}

v9x_u32 v9x_nv50_d3d_ps_start(v9x_u32 slot)
{
    return V9X_NV50_D3D_PS_FIRST + slot * V9X_NV50_D3D_PS_STRIDE;
}

/* The shader vertex program: attributes 0-6 to results 0-27, /3d:ps11's
 * program. */
v9x_u32 v9x_nv50_d3d_shader_vp(v9x_u32 *words, v9x_u32 max)
{
    struct v9x_nv50_sh sh;
    v9x_u32 index;

    v9x_nv50_sh_init(&sh, words, max);
    for (index = 0ul; index < V9X_NV50_D3D_SHADER_RESULTS; ++index) {
        v9x_nv50_sh_mov(&sh, V9X_NV50_SH_LONG, V9X_NV50_SH_O(index * 4ul),
                        V9X_NV50_SH_A(index * 4ul));
    }
    v9x_nv50_sh_exit(&sh);
    if (sh.invalid != 0ul || sh.overflow != 0ul) {
        return 0ul;
    }
    return sh.count;
}

v9x_u32 v9x_nv50_d3d_vp(v9x_u32 *words, v9x_u32 max)
{
    struct v9x_nv50_sh sh;
    v9x_u32 index;

    v9x_nv50_sh_init(&sh, words, max);
    for (index = 0ul; index < V9X_NV50_D3D_RESULTS; ++index) {
        v9x_nv50_sh_mov(&sh, V9X_NV50_SH_LONG, V9X_NV50_SH_O(index * 4ul),
                        V9X_NV50_SH_A(index * 4ul));
    }
    v9x_nv50_sh_exit(&sh);
    if (sh.invalid != 0ul || sh.overflow != 0ul) {
        return 0ul;
    }
    return sh.count;
}

/*
 * Every program's end: specular's colour added, and fog as (colour - fog)
 * * f + fog with f the specular alpha, which the core leaves there for
 * vertex and table fog alike (d3d_fog.h); then a long move to end on if
 * the last instruction is short, and the exit. Returns the register count,
 * at least used, or 0 when the program is unusable.
 */
static v9x_u32 v9x_nv50_d3d_fp_tail(struct v9x_nv50_sh *sh, v9x_u32 specular,
                                    v9x_u32 fog, v9x_u32 used)
{
    v9x_u32 w = V9X_NV50_SH_R(V9X_NV50_D3D_R_W);
    v9x_u32 index;

    if (specular != 0ul) {
        for (index = 0ul; index < 3ul; ++index) {
            v9x_u32 lit = V9X_NV50_SH_R(V9X_NV50_D3D_R_SPECULAR + index);

            v9x_nv50_sh_interp(
                sh, lit,
                V9X_NV50_SH_V(V9X_NV50_D3D_V_SPECULAR + index * 4ul), w);
            v9x_nv50_sh_fadd(sh, V9X_NV50_SH_SHORT, V9X_NV50_SH_R(index),
                             V9X_NV50_SH_R(index), lit);
        }
        if (used < V9X_NV50_D3D_R_SPECULAR + 3ul) {
            used = V9X_NV50_D3D_R_SPECULAR + 3ul;
        }
    }

    if (fog != 0ul) {
        v9x_u32 factor = V9X_NV50_SH_R(V9X_NV50_D3D_R_FOG);
        v9x_u32 temp = V9X_NV50_SH_R(V9X_NV50_D3D_R_TEMP);

        v9x_nv50_sh_interp(sh, factor, V9X_NV50_SH_V(V9X_NV50_D3D_V_FOG), w);
        for (index = 0ul; index < 3ul; ++index) {
            v9x_nv50_sh_fadd(sh, V9X_NV50_SH_LONG, temp,
                             V9X_NV50_SH_R(index),
                             V9X_NV50_SH_C(V9X_NV50_D3D_FP_SLOT,
                                           V9X_NV50_D3D_C_FOG_NEG +
                                               index * 4ul));
            v9x_nv50_sh_fmad(sh, V9X_NV50_SH_LONG, V9X_NV50_SH_R(index),
                             temp, factor,
                             V9X_NV50_SH_C(V9X_NV50_D3D_FP_SLOT,
                                           V9X_NV50_D3D_C_FOG + index * 4ul));
        }
        if (used < V9X_NV50_D3D_R_FOG + 1ul) {
            used = V9X_NV50_D3D_R_FOG + 1ul;
        }
    }

    if (sh->last_size != V9X_NV50_SH_LONG) {
        v9x_nv50_sh_mov(sh, V9X_NV50_SH_LONG, V9X_NV50_SH_R(3),
                        V9X_NV50_SH_R(3));
    }
    v9x_nv50_sh_exit(sh);
    if (sh->invalid != 0ul || sh->overflow != 0ul) {
        return 0ul;
    }
    return used;
}

/*
 * A variant: w from the interpolated 1 / w; diffuse, then the texture
 * blend (REPLACE takes the texel, MODULATE multiplies, all four
 * channels: a texture without alpha samples alpha 1, so the vertex's
 * stays), specular's colour added, and fog as (colour - fog) * f + fog
 * with f the specular alpha, which the core leaves there for vertex and
 * table fog alike (d3d_fog.h). The encoder has no negation, hence the
 * negated fog colour constant. The program ends on a long instruction;
 * a variant whose last is short gets a long move to end on.
 */
v9x_u32 v9x_nv50_d3d_fp(v9x_u32 variant, v9x_u32 *words, v9x_u32 max,
                        v9x_u32 *registers)
{
    struct v9x_nv50_sh sh;
    v9x_u32 op;
    v9x_u32 specular;
    v9x_u32 fog;
    v9x_u32 index;
    v9x_u32 used;
    v9x_u32 w = V9X_NV50_SH_R(V9X_NV50_D3D_R_W);

    if (variant >= V9X_NV50_D3D_FP_VARIANTS) {
        return 0ul;
    }
    op = variant / 4ul;
    specular = (variant & 2ul) != 0ul ? 1ul : 0ul;
    fog = (variant & 1ul) != 0ul ? 1ul : 0ul;
    used = V9X_NV50_D3D_R_W + 1ul;

    v9x_nv50_sh_init(&sh, words, max);
    v9x_nv50_sh_interp(&sh, w, V9X_NV50_SH_V(V9X_NV50_D3D_V_W),
                       V9X_NV50_SH_NONE);
    v9x_nv50_sh_rcp(&sh, V9X_NV50_SH_SHORT, w, w);
    for (index = 0ul; index < 4ul; ++index) {
        v9x_nv50_sh_interp(&sh, V9X_NV50_SH_R(index),
                           V9X_NV50_SH_V(V9X_NV50_D3D_V_DIFFUSE + index * 4ul),
                           w);
    }

    if (op != V9X_NV50_D3D_OP_NONE) {
        v9x_nv50_sh_interp(&sh, V9X_NV50_SH_R(V9X_NV50_D3D_R_TEXEL),
                           V9X_NV50_SH_V(V9X_NV50_D3D_V_COORDS), w);
        v9x_nv50_sh_interp(&sh, V9X_NV50_SH_R(V9X_NV50_D3D_R_TEXEL + 1ul),
                           V9X_NV50_SH_V(V9X_NV50_D3D_V_COORDS + 4ul), w);
        v9x_nv50_sh_tex(&sh, V9X_NV50_SH_R(V9X_NV50_D3D_R_TEXEL), 0ul, 0ul,
                        0xful, 0ul);
        for (index = 0ul; index < 4ul; ++index) {
            v9x_u32 texel = V9X_NV50_SH_R(V9X_NV50_D3D_R_TEXEL + index);

            /* MODULATE_RGB leaves the alpha the texel's: DX5 MODULATE
             * on a texture with alpha. */
            if (op == V9X_NV50_D3D_OP_REPLACE ||
                (op == V9X_NV50_D3D_OP_MODULATE_RGB && index == 3ul)) {
                v9x_nv50_sh_mov(&sh, V9X_NV50_SH_SHORT, V9X_NV50_SH_R(index),
                                texel);
            } else {
                v9x_nv50_sh_fmul(&sh, V9X_NV50_SH_SHORT, V9X_NV50_SH_R(index),
                                 V9X_NV50_SH_R(index), texel);
            }
        }
        used = V9X_NV50_D3D_R_TEXEL + 4ul;
    }

    used = v9x_nv50_d3d_fp_tail(&sh, specular, fog, used);
    if (used == 0ul) {
        return 0ul;
    }
    *registers = used;
    return sh.count;
}

/* One unit's combine into $r0-$r3 from the texel at texel onwards
 * (v9x_nv50_d3d_fp2): the colour first, which DECALALPHA weights by the
 * texel's own alpha, then the alpha. ADD saturates, as Direct3D clamps
 * every stage's result. */
static int v9x_nv50_d3d_fp2_unit(struct v9x_nv50_sh *sh, v9x_u32 color,
                                 v9x_u32 alpha, v9x_u32 texel)
{
    v9x_u32 temp = V9X_NV50_SH_R(V9X_NV50_D3D_R_TEMP);
    v9x_u32 texel_alpha = V9X_NV50_SH_R(texel + 3ul);
    v9x_u32 index;

    for (index = 0ul; index < 3ul; ++index) {
        v9x_u32 out = V9X_NV50_SH_R(index);
        v9x_u32 t = V9X_NV50_SH_R(texel + index);

        switch (color) {
        case V9X_NV50_D3D_COLOR_REPLACE:
            v9x_nv50_sh_mov(sh, V9X_NV50_SH_SHORT, out, t);
            break;
        case V9X_NV50_D3D_COLOR_MODULATE:
            v9x_nv50_sh_fmul(sh, V9X_NV50_SH_SHORT, out, out, t);
            break;
        case V9X_NV50_D3D_COLOR_ADD:
            v9x_nv50_sh_fadd(sh, V9X_NV50_SH_LONG, V9X_NV50_SH_SAT(out), out,
                             t);
            break;
        case V9X_NV50_D3D_COLOR_DECAL:
            /* Cin + At (Ct - Cin). */
            v9x_nv50_sh_fadd(sh, V9X_NV50_SH_LONG, temp, t,
                             V9X_NV50_SH_NEG(out));
            v9x_nv50_sh_fmad(sh, V9X_NV50_SH_LONG, out, temp, texel_alpha,
                             out);
            break;
        default:
            return 0;
        }
    }
    switch (alpha) {
    case V9X_NV50_D3D_ALPHA_FRAGMENT:
        break;
    case V9X_NV50_D3D_ALPHA_REPLACE:
        v9x_nv50_sh_mov(sh, V9X_NV50_SH_SHORT, V9X_NV50_SH_R(3), texel_alpha);
        break;
    case V9X_NV50_D3D_ALPHA_MODULATE:
        v9x_nv50_sh_fmul(sh, V9X_NV50_SH_SHORT, V9X_NV50_SH_R(3),
                         V9X_NV50_SH_R(3), texel_alpha);
        break;
    default:
        return 0;
    }
    return 1;
}

v9x_u32 v9x_nv50_d3d_combine_key(const struct v9x_nv50_d3d_combine *c)
{
    v9x_u32 key = 0ul;
    v9x_u32 unit;

    for (unit = 0ul; unit < 2ul; ++unit) {
        if (c->color[unit] >= V9X_NV50_D3D_COLOR_OPS ||
            c->alpha[unit] >= V9X_NV50_D3D_ALPHA_OPS) {
            return V9X_NV50_D3D_COMBINE_NONE;
        }
        key = (key * V9X_NV50_D3D_COLOR_OPS + c->color[unit]) *
                  V9X_NV50_D3D_ALPHA_OPS +
              c->alpha[unit];
    }
    key = key * 2ul + (c->specular != 0ul ? 1ul : 0ul);
    return key * 2ul + (c->fog != 0ul ? 1ul : 0ul);
}

v9x_u32 v9x_nv50_d3d_fp2_start(v9x_u32 slot)
{
    return V9X_NV50_D3D_FP2_FIRST + slot * V9X_NV50_D3D_FP2_STRIDE;
}

/*
 * Two units: w, diffuse, both texels (unit 0 from v[0x24], unit 1 from
 * v[0x2c] on texture and sampler 1, which BIND_TIC2's second entry
 * already names), the two combines, then specular and fog as a variant.
 */
v9x_u32 v9x_nv50_d3d_fp2(const struct v9x_nv50_d3d_combine *c,
                         v9x_u32 *words, v9x_u32 max, v9x_u32 *registers)
{
    struct v9x_nv50_sh sh;
    v9x_u32 index;
    v9x_u32 used;
    v9x_u32 w = V9X_NV50_SH_R(V9X_NV50_D3D_R_W);

    if (v9x_nv50_d3d_combine_key(c) == V9X_NV50_D3D_COMBINE_NONE) {
        return 0ul;
    }
    v9x_nv50_sh_init(&sh, words, max);
    v9x_nv50_sh_interp(&sh, w, V9X_NV50_SH_V(V9X_NV50_D3D_V_W),
                       V9X_NV50_SH_NONE);
    v9x_nv50_sh_rcp(&sh, V9X_NV50_SH_SHORT, w, w);
    for (index = 0ul; index < 4ul; ++index) {
        v9x_nv50_sh_interp(&sh, V9X_NV50_SH_R(index),
                           V9X_NV50_SH_V(V9X_NV50_D3D_V_DIFFUSE + index * 4ul),
                           w);
    }
    for (index = 0ul; index < 2ul; ++index) {
        v9x_nv50_sh_interp(&sh, V9X_NV50_SH_R(V9X_NV50_D3D_R_TEXEL + index),
                           V9X_NV50_SH_V(V9X_NV50_D3D_V_COORDS + index * 4ul),
                           w);
        v9x_nv50_sh_interp(&sh, V9X_NV50_SH_R(V9X_NV50_D3D_R_TEXEL1 + index),
                           V9X_NV50_SH_V(V9X_NV50_D3D_V_COORDS1 + index * 4ul),
                           w);
    }
    v9x_nv50_sh_tex(&sh, V9X_NV50_SH_R(V9X_NV50_D3D_R_TEXEL), 0ul, 0ul, 0xful,
                    0ul);
    v9x_nv50_sh_tex(&sh, V9X_NV50_SH_R(V9X_NV50_D3D_R_TEXEL1), 1ul, 1ul,
                    0xful, 0ul);
    if (!v9x_nv50_d3d_fp2_unit(&sh, c->color[0], c->alpha[0],
                               V9X_NV50_D3D_R_TEXEL) ||
        !v9x_nv50_d3d_fp2_unit(&sh, c->color[1], c->alpha[1],
                               V9X_NV50_D3D_R_TEXEL1)) {
        return 0ul;
    }
    used = v9x_nv50_d3d_fp_tail(&sh, c->specular, c->fog,
                                V9X_NV50_D3D_R_TEXEL1 + 4ul);
    if (used == 0ul) {
        return 0ul;
    }
    *registers = used;
    return sh.count;
}

int v9x_nv50_d3d_texture(v9x_u32 format, v9x_u32 address, v9x_u32 pitch,
                         v9x_u32 width, v9x_u32 height, v9x_u32 wrap,
                         v9x_u32 mag_linear, v9x_u32 min_linear,
                         v9x_u32 *tic, v9x_u32 *tsc)
{
    v9x_u32 sizes;
    v9x_u32 alpha = V9X_NV50_TIC_SOURCE_A;
    v9x_u32 index;

    /* Mesa's nv50_formats.c rows for B8G8R8A8, B5G6R5 (alpha one),
     * B5G5R5A1 and B4G4R4A4 UNORM: each reads blue from the memory
     * component the size names R, so X takes B and Z takes R. B8G8R8X8
     * is Mesa's F3 row: the 8888 sizes with W from the constant one, so
     * the unused top byte never reaches alpha. */
    switch (format) {
    case V9X_NV50_D3D_TEX_B8G8R8A8:
        sizes = V9X_NV50_TIC_SIZES_A8B8G8R8;
        break;
    case V9X_NV50_D3D_TEX_B8G8R8X8:
        sizes = V9X_NV50_TIC_SIZES_A8B8G8R8;
        alpha = V9X_NV50_TIC_SOURCE_ONE;
        break;
    case V9X_NV50_D3D_TEX_B5G6R5:
        sizes = V9X_NV50_TIC_SIZES_B5G6R5;
        alpha = V9X_NV50_TIC_SOURCE_ONE;
        break;
    case V9X_NV50_D3D_TEX_B5G5R5A1:
        sizes = V9X_NV50_TIC_SIZES_A1B5G5R5;
        break;
    case V9X_NV50_D3D_TEX_B4G4R4A4:
        sizes = V9X_NV50_TIC_SIZES_A4B4G4R4;
        break;
    default:
        return 0;
    }
    tic[0] = sizes | V9X_NV50_TIC0_UNORM |
             (V9X_NV50_TIC_SOURCE_B << V9X_NV50_TIC0_X_SHIFT) |
             (V9X_NV50_TIC_SOURCE_G << V9X_NV50_TIC0_Y_SHIFT) |
             (V9X_NV50_TIC_SOURCE_R << V9X_NV50_TIC0_Z_SHIFT) |
             (alpha << V9X_NV50_TIC0_W_SHIFT);
    tic[1] = address;
    tic[2] = V9X_NV50_TIC2_PITCH_2D;
    tic[3] = pitch;
    tic[4] = width;
    tic[5] = V9X_NV50_TIC5_DEPTH_ONE | height;
    tic[6] = 0ul;
    tic[7] = 0ul;

    for (index = 0ul; index < V9X_NV50_3D_TSC_WORDS; ++index) {
        tsc[index] = 0ul;
    }
    tsc[0] = V9X_NV50_TSC0_BASE | wrap | (wrap << 3) | (wrap << 6);
    tsc[1] = (mag_linear != 0ul ? V9X_NV50_TSC1_MAG_LINEAR
                                : V9X_NV50_TSC1_MAG_NEAREST) |
             (min_linear != 0ul ? V9X_NV50_TSC1_MIN_LINEAR
                                : V9X_NV50_TSC1_MIN_NEAREST) |
             V9X_NV50_TSC1_MIP_NONE;
    return 1;
}

/* The fog colour and its negation as buffer 2's first eight words. */
static void v9x_nv50_d3d_fog_constants(struct v9x_nv50_push *push,
                                       v9x_u32 color)
{
    v9x_u32 data[V9X_NV50_D3D_FP_CONSTANTS];
    v9x_u32 header_at;
    float channel;
    v9x_u32 index;

    for (index = 0ul; index < 3ul; ++index) {
        /* Red, green, blue from D3DCOLOR's bits 23:16, 15:8, 7:0. */
        channel = v9x_nv50_d3d_unorm(color >> (16u - 8u * (unsigned)index));
        data[index] = v9x_nv50_d3d_bits(channel);
        data[4ul + index] = v9x_nv50_d3d_bits(-channel);
    }
    data[3] = 0ul;
    data[7] = 0ul;

    v9x_nv50_d3d_one(push, V9X_8597_CB_ADDR,
                     (0ul << V9X_8597_CB_ADDR_ID_SHIFT) | V9X_8597_CB_FP);
    header_at = push->count;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_CB_DATA0,
                         V9X_NV50_D3D_FP_CONSTANTS, data);
    if (push->overflow == 0ul) {
        push->words[header_at] |= V9X_NV50_PUSH_NONINC;
    }
}

void v9x_nv50_push_d3d_state(struct v9x_nv50_push *push,
                             const struct v9x_nv50_d3d_state *state)
{
    v9x_u32 blend[5];

    v9x_nv50_push_3d_target(push, &state->target, state->target_format);
    /* The screen scissor bounds what a clear or a draw may write, and
     * EXA leaves it at 8192 (step 4's PROP SURF_WIDTH_OVERRUN). */
    v9x_nv50_d3d_two(push, V9X_8597_SCREEN_SCISSOR_HORIZ,
                     state->target.width << 16, state->target.height << 16);
    v9x_nv50_d3d_two(push, V9X_8597_SCISSOR_HORIZ0,
                     (state->scissor_right << 16) | state->scissor_left,
                     (state->scissor_bottom << 16) | state->scissor_top);
    v9x_nv50_d3d_one(push, V9X_8597_COLOR_MASK0, state->color_mask);

    if (state->has_zeta != 0ul) {
        v9x_nv50_push_3d_zeta(push, &state->zeta, state->zeta_format);
        v9x_nv50_d3d_one(push, V9X_8597_DEPTH_TEST_ENABLE,
                         state->depth_test);
        v9x_nv50_d3d_one(push, V9X_8597_DEPTH_WRITE_ENABLE,
                         state->depth_write);
        v9x_nv50_d3d_one(push, V9X_8597_DEPTH_TEST_FUNC, state->depth_func);
    } else {
        v9x_nv50_d3d_one(push, V9X_8597_ZETA_ENABLE, 0ul);
        v9x_nv50_d3d_one(push, V9X_8597_DEPTH_TEST_ENABLE, 0ul);
        v9x_nv50_d3d_one(push, V9X_8597_DEPTH_WRITE_ENABLE, 0ul);
    }

    /* Blending as Mesa's common function: one equation, add, with the
     * same factors for alpha. */
    v9x_nv50_d3d_one(push, V9X_8597_BLEND_ENABLE0, state->blend);
    if (state->blend != 0ul) {
        blend[0] = V9X_8597_BLEND_FUNC_ADD;
        blend[1] = state->blend_src;
        blend[2] = state->blend_dst;
        blend[3] = V9X_8597_BLEND_FUNC_ADD;
        blend[4] = state->blend_src;
        v9x_nv50_push_method(push, V9X_NV50_SUBC_3D,
                             V9X_8597_BLEND_EQUATION_RGB, 5ul, blend);
        v9x_nv50_d3d_one(push, V9X_8597_BLEND_FUNC_DST_ALPHA,
                         state->blend_dst);
    }
    v9x_nv50_d3d_one(push, V9X_8597_ALPHA_TEST_ENABLE, state->alpha_test);
    if (state->alpha_test != 0ul) {
        v9x_nv50_d3d_two(push, V9X_8597_ALPHA_TEST_REF, state->alpha_ref,
                         state->alpha_func);
    }
    /* The core culls before it hands triangles over. */
    v9x_nv50_d3d_one(push, V9X_8597_CULL_FACE_ENABLE, 0ul);

    v9x_nv50_d3d_one(push, V9X_8597_VP_START_ID,
                     state->shader != 0ul ? V9X_NV50_D3D_SHADER_VP_START
                                          : V9X_NV50_D3D_VP_START);
    /* A program written since the last draw: the code cache may hold what
     * was at its address before (Mesa flushes after every upload,
     * nv50_program.c). */
    if (state->code_flush != 0ul) {
        v9x_nv50_d3d_one(push, V9X_8597_CODE_CB_FLUSH, 0ul);
    }
    v9x_nv50_d3d_one(push, V9X_8597_FP_START_ID,
                     state->two_units != 0ul || state->shader != 0ul
                         ? state->fp_start
                         : v9x_nv50_d3d_fp_start(state->fp_variant));
    /* Mesa writes FP_CONTROL with each program; USES_KIL only for one
     * that discards, so a draw after a shader that killed clears it. */
    v9x_nv50_d3d_one(push, V9X_8597_FP_CONTROL,
                     state->shader != 0ul && state->kills != 0ul
                         ? V9X_8597_FP_USES_KIL : 0ul);
    if (state->shader != 0ul) {
        /* /3d:ps11's interface: attributes 0-6 whole, 28 results mapped
         * to the interpolant of the same number, 24 read by the program
         * after the position (V9XN5CH /3d:ps11, W9QI0DAD boot 21). */
        v9x_u32 map[V9X_NV50_D3D_SHADER_RESULTS / 4ul];
        v9x_u32 index;

        v9x_nv50_d3d_two(push, V9X_8597_VP_ATTR_EN0,
                         V9X_NV50_D3D_SHADER_ATTR_EN0, 0ul);
        v9x_nv50_d3d_one(push, V9X_8597_VP_REG_ALLOC_RESULT,
                         V9X_NV50_D3D_SHADER_RESULTS);
        v9x_nv50_d3d_one(push, V9X_8597_VP_RESULT_MAP_SIZE,
                         V9X_NV50_D3D_SHADER_RESULTS);
        for (index = 0ul; index < V9X_NV50_D3D_SHADER_RESULTS / 4ul;
             ++index) {
            v9x_u32 first = index * 4ul;

            map[index] = first | ((first + 1ul) << 8) |
                         ((first + 2ul) << 16) | ((first + 3ul) << 24);
        }
        v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VP_RESULT_MAP0,
                             V9X_NV50_D3D_SHADER_RESULTS / 4ul, map);
        v9x_nv50_d3d_two(push, V9X_8597_FP_INTERPOLANT_CTRL,
                         V9X_NV50_D3D_SHADER_INTERPOLANTS,
                         state->fp_registers);
        v9x_nv50_d3d_one(push, V9X_8597_TIC_FLUSH, 0ul);
        v9x_nv50_d3d_one(push, V9X_8597_TSC_FLUSH, 0ul);
        for (index = 0ul; index < V9X_NV50_D3D_SHADER_UNITS; ++index) {
            v9x_nv50_d3d_one(push, V9X_8597_BIND_TIC2,
                             (index << 9) | (index << 1) | 1ul);
        }
        return;
    }
    v9x_nv50_d3d_two(push, V9X_8597_VP_ATTR_EN0, V9X_NV50_D3D_ATTR_EN0,
                     V9X_NV50_D3D_ATTR_EN1);
    v9x_nv50_d3d_one(push, V9X_8597_VP_REG_ALLOC_RESULT,
                     V9X_NV50_D3D_RESULTS);
    v9x_nv50_d3d_one(push, V9X_8597_VP_RESULT_MAP_SIZE, V9X_NV50_D3D_RESULTS);
    {
        v9x_u32 map[4];

        map[0] = V9X_NV50_D3D_MAP0;
        map[1] = V9X_NV50_D3D_MAP1;
        map[2] = V9X_NV50_D3D_MAP2;
        map[3] = V9X_NV50_D3D_MAP3;
        v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VP_RESULT_MAP0,
                             4ul, map);
    }
    v9x_nv50_d3d_two(push, V9X_8597_FP_INTERPOLANT_CTRL,
                     V9X_NV50_D3D_INTERPOLANTS, state->fp_registers);

    if (state->textured != 0ul) {
        v9x_nv50_d3d_one(push, V9X_8597_TIC_FLUSH, 0ul);
        v9x_nv50_d3d_one(push, V9X_8597_BIND_TIC2, V9X_8597_BIND_TIC_FIRST);
        v9x_nv50_d3d_one(push, V9X_8597_BIND_TIC2, V9X_8597_BIND_TIC_UNIT0);
    }
    if (state->two_units != 0ul ? state->fog != 0ul
                                : (state->fp_variant & 1ul) != 0ul) {
        v9x_nv50_d3d_fog_constants(push, state->fog_color);
    }
}

void v9x_nv50_push_d3d_constants(struct v9x_nv50_push *push,
                                 const v9x_u32 *words, v9x_u32 count)
{
    v9x_u32 header_at;

    v9x_nv50_d3d_one(push, V9X_8597_CB_ADDR,
                     (0ul << V9X_8597_CB_ADDR_ID_SHIFT) | V9X_8597_CB_FP);
    header_at = push->count;
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_CB_DATA0, count,
                         words);
    if (push->overflow == 0ul) {
        push->words[header_at] |= V9X_NV50_PUSH_NONINC;
    }
}

void v9x_nv50_push_d3d_wide(struct v9x_nv50_push *push, const float *vertex,
                            float centre)
{
    v9x_u32 data[4];
    v9x_u32 attribute;
    v9x_u32 c;
    float weight;

    for (attribute = 1ul; attribute < 7ul; ++attribute) {
        for (c = 0ul; c < 4ul; ++c) {
            data[c] = v9x_nv50_d3d_bits(vertex[attribute * 4ul + c]);
        }
        v9x_nv50_push_method(push, V9X_NV50_SUBC_3D,
                             V9X_8597_VTX_ATTR_4F(attribute), 4ul, data);
    }
    /* The position last, as v9x_nv50_push_d3d_vertex sends it. */
    weight = 1.0f;
    if (vertex[3] > 0.0f) {
        weight = vertex[3];
    }
    data[0] = v9x_nv50_d3d_bits(vertex[0] + centre);
    data[1] = v9x_nv50_d3d_bits(vertex[1] + centre);
    data[2] = v9x_nv50_d3d_bits(vertex[2]);
    data[3] = v9x_nv50_d3d_bits(weight);
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VTX_ATTR_4F(0ul),
                         4ul, data);
}

void v9x_nv50_push_d3d_uv1(struct v9x_nv50_push *push, float tu1, float tv1)
{
    v9x_u32 data[2];

    data[0] = v9x_nv50_d3d_bits(tu1);
    data[1] = v9x_nv50_d3d_bits(tv1);
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VTX_ATTR_2F_X9, 2ul,
                         data);
}

void v9x_nv50_push_d3d_vertex(struct v9x_nv50_push *push, float sx, float sy,
                              float sz, float rhw, v9x_u32 diffuse,
                              v9x_u32 specular, float tu, float tv,
                              float centre, v9x_u32 with_specular,
                              v9x_u32 with_texture)
{
    v9x_u32 data[4];
    float weight;

    if (with_texture != 0ul) {
        data[0] = v9x_nv50_d3d_bits(tu);
        data[1] = v9x_nv50_d3d_bits(tv);
        v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VTX_ATTR_2F_X8,
                             2ul, data);
    }
    if (with_specular != 0ul) {
        data[0] = v9x_nv50_d3d_bits(v9x_nv50_d3d_unorm(specular >> 16));
        data[1] = v9x_nv50_d3d_bits(v9x_nv50_d3d_unorm(specular >> 8));
        data[2] = v9x_nv50_d3d_bits(v9x_nv50_d3d_unorm(specular));
        data[3] = v9x_nv50_d3d_bits(v9x_nv50_d3d_unorm(specular >> 24));
        v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VTX_ATTR_4F_X2,
                             4ul, data);
    }
    data[0] = v9x_nv50_d3d_bits(v9x_nv50_d3d_unorm(diffuse >> 16));
    data[1] = v9x_nv50_d3d_bits(v9x_nv50_d3d_unorm(diffuse >> 8));
    data[2] = v9x_nv50_d3d_bits(v9x_nv50_d3d_unorm(diffuse));
    data[3] = v9x_nv50_d3d_bits(v9x_nv50_d3d_unorm(diffuse >> 24));
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VTX_ATTR_4F_X1, 4ul,
                         data);

    /* The position last: writing attribute 0 emits the vertex. x, y and z
     * go as they are: with the viewport transform off the engine does not
     * divide by w (/3d:d3d's first run sent sx w at w 2 and the rectangle
     * landed at twice its coordinates). The fourth word is rhw itself,
     * the weight the engine interpolates by: rectangle D sent 1 / rhw
     * there and its texels followed weights of w, not rhw (M-BXMFA8). A
     * rhw that is not positive (NaN included) is taken as 1, which draws
     * the vertex without perspective. */
    weight = 1.0f;
    if (rhw > 0.0f) {
        weight = rhw;
    }
    data[0] = v9x_nv50_d3d_bits(sx + centre);
    data[1] = v9x_nv50_d3d_bits(sy + centre);
    data[2] = v9x_nv50_d3d_bits(sz);
    data[3] = v9x_nv50_d3d_bits(weight);
    v9x_nv50_push_method(push, V9X_NV50_SUBC_3D, V9X_8597_VTX_ATTR_4F_X0, 4ul,
                         data);
}

void v9x_nv50_push_d3d_begin(struct v9x_nv50_push *push)
{
    v9x_nv50_d3d_one(push, V9X_8597_VERTEX_BEGIN_GL,
                     V9X_8597_PRIMITIVE_TRIANGLES);
}

void v9x_nv50_push_d3d_end(struct v9x_nv50_push *push)
{
    v9x_nv50_d3d_one(push, V9X_8597_VERTEX_END_GL, 0ul);
}
