#include <stdio.h>
#include <string.h>

#include "velocity9x/cm_shader.h"
#include "velocity9x/eg_shader.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* Linux v3.9 cayman_blit_shaders.c, the little-endian words: the blit
 * Linux ran on every Cayman-generation part before its 3.10 rework. */
static const v9x_u32 linux_cayman_vs[16] = {
    0x00000004ul, 0x80400400ul, 0x0000a03cul, 0x95000688ul,
    0x00004000ul, 0x15000688ul, 0x00000000ul, 0x88000000ul,
    0x04000000ul, 0x67961001ul, 0x00000000ul, 0x00000000ul,
    0x04000000ul, 0x67961000ul, 0x00000008ul, 0x00000000ul
};

static const v9x_u32 linux_cayman_ps[20] = {
    0x00000004ul, 0xa00c0000ul, 0x00000008ul, 0x80400000ul,
    0x00000000ul, 0x95000688ul, 0x00000000ul, 0x88000000ul,
    0x00380400ul, 0x00146b10ul, 0x00380000ul, 0x20146b10ul,
    0x00380400ul, 0x40146b00ul, 0x80380000ul, 0x60146b00ul,
    0x00000010ul, 0x000d1000ul, 0xb0800000ul, 0x00000000ul
};

static v9x_u32 shader[V9X_CM_SHADER_DWORDS_MAX];

static void reset(void)
{
    memset(shader, 0xcc, sizeof(shader));
}

static void test_blit(void)
{
    v9x_u32 count;
    v9x_u32 index;

    reset();
    count = v9x_cm_shader_blit_vs(shader);
    CHECK(count == 16ul);
    for (index = 0ul; index < 16ul; ++index) {
        CHECK(shader[index] == linux_cayman_vs[index]);
    }
    reset();
    count = v9x_cm_shader_blit_ps(shader);
    CHECK(count == 20ul);
    for (index = 0ul; index < 20ul; ++index) {
        CHECK(shader[index] == linux_cayman_ps[index]);
    }
}

static void test_cf_end(void)
{
    reset();
    CHECK(v9x_cm_cf_end(shader) == 2ul);
    /* Barrier set, CF_INST 20h, nothing else. */
    CHECK(shader[0] == 0ul);
    CHECK(shader[1] == 0x88000000ul);
    CHECK(shader[2] == 0xccccccccul);
}

static v9x_u32 eg_shader[V9X_EG_SHADER_DWORDS_MAX];

/* Evergreen's blit through the relocator is Linux's Cayman blit: the VS
 * keeps its fetch clause at dword 8, the PS moves its ALU clause from
 * dword 6 to 8 and keeps its texture clause at 16. */
static void test_from_eg_blit(void)
{
    v9x_u32 count;
    v9x_u32 index;

    reset();
    count = v9x_cm_shader_from_eg(eg_shader, v9x_eg_shader_blit_vs(eg_shader),
                                  shader);
    CHECK(count == 16ul);
    for (index = 0ul; index < 16ul; ++index) {
        CHECK(shader[index] == linux_cayman_vs[index]);
    }
    reset();
    count = v9x_cm_shader_from_eg(eg_shader, v9x_eg_shader_blit_ps(eg_shader),
                                  shader);
    CHECK(count == 20ul);
    for (index = 0ul; index < 20ul; ++index) {
        CHECK(shader[index] == linux_cayman_ps[index]);
    }
}

/* A relocated shader's control flow: no end-of-program bit, no VC, CF_END
 * last; every clause's words equal the Evergreen clause's (fetches apart
 * from the two fields Cayman drops); fetch clauses 16-byte aligned. */
static void check_relocated(const v9x_u32 *eg, v9x_u32 eg_count)
{
    v9x_u32 count;
    v9x_u32 slot;
    v9x_u32 w1;
    v9x_u32 cf_inst;
    v9x_u32 eg_slot = 0ul;
    v9x_u32 eg_w1;
    v9x_u32 addr;
    v9x_u32 eg_addr;
    v9x_u32 dwords;
    v9x_u32 index;
    v9x_u32 ended = 0ul;

    reset();
    count = v9x_cm_shader_from_eg(eg, eg_count, shader);
    CHECK(count != 0ul && count <= V9X_CM_SHADER_DWORDS_MAX);
    for (slot = 0ul; slot < 16ul && count != 0ul; ++slot) {
        w1 = shader[slot * 2ul + 1ul];
        CHECK((w1 & 0x00200000ul) == 0ul);
        if ((w1 & 0x20000000ul) == 0ul &&
            ((w1 >> 22) & 0xfful) == V9X_CM_CF_END) {
            ended = 1ul;
            break;
        }
        eg_w1 = eg[eg_slot * 2ul + 1ul];
        ++eg_slot;
        addr = shader[slot * 2ul] * 2ul;
        eg_addr = eg[(eg_slot - 1ul) * 2ul] * 2ul;
        if ((w1 & 0x20000000ul) != 0ul) {
            /* ALU: count is slots, two dwords each. */
            dwords = (((w1 >> 18) & 0x7ful) + 1ul) * 2ul;
            for (index = 0ul; index < dwords; ++index) {
                CHECK(shader[addr + index] == eg[eg_addr + index]);
            }
            continue;
        }
        cf_inst = (w1 >> 22) & 0xfful;
        CHECK(cf_inst != V9X_EG_CF_VC);
        if (cf_inst == V9X_EG_CF_TC) {
            CHECK((addr & 3ul) == 0ul);
            dwords = (((w1 >> 10) & 0x3ful) + 1ul) * 4ul;
            for (index = 0ul; index < dwords; ++index) {
                if (((eg_w1 >> 22) & 0xfful) == V9X_EG_CF_VC &&
                    (index & 3ul) == 0ul) {
                    CHECK((shader[addr + index] & 0x03fffffful) ==
                          (eg[eg_addr + index] & 0x03fffffful));
                } else if (((eg_w1 >> 22) & 0xfful) == V9X_EG_CF_VC &&
                           (index & 3ul) == 2ul) {
                    CHECK(shader[addr + index] ==
                          (eg[eg_addr + index] & ~0x00080000ul));
                } else {
                    CHECK(shader[addr + index] == eg[eg_addr + index]);
                }
            }
        }
    }
    CHECK(ended == 1ul);
}

static void test_from_eg_d3d(void)
{
    struct v9x_eg_d3d_ps ps;
    v9x_u32 count;
    v9x_u32 op;

    for (op = 0ul; op < 2ul; ++op) {
        count = v9x_eg_shader_d3d_vs(op, eg_shader);
        check_relocated(eg_shader, count);
        count = v9x_eg_shader_d3d_vs_lit(op, eg_shader);
        check_relocated(eg_shader, count);
    }
    for (op = V9X_EG_D3D_OP_NONE; op <= V9X_EG_D3D_OP_MODULATE; ++op) {
        count = v9x_eg_shader_d3d_ps(op, eg_shader);
        check_relocated(eg_shader, count);
        count = v9x_eg_shader_d3d_ps_lit(op, 1ul, 1ul, 0x00ff8000ul,
                                         eg_shader);
        check_relocated(eg_shader, count);
    }
    /* The keyed PS, the longest: textured, specular, fog and the key. */
    ps.op = V9X_EG_D3D_OP_MODULATE;
    ps.specular = 1ul;
    ps.fog = 1ul;
    ps.fog_color = 0x00ff8000ul;
    ps.color_key = 1ul;
    ps.key[0] = 0ul;
    ps.key[1] = 0ul;
    ps.key[2] = 0ul;
    ps.key_tolerance = 0x3b000000ul;
    count = v9x_eg_shader_d3d_ps_built(&ps, eg_shader);
    check_relocated(eg_shader, count);
}

static void test_from_eg_refuses(void)
{
    v9x_u32 bad[8];
    v9x_u32 index;

    /* No end of program anywhere. */
    for (index = 0ul; index < 8ul; ++index) {
        bad[index] = 0ul;
    }
    CHECK(v9x_cm_shader_from_eg(bad, 8ul, shader) == 0ul);
    /* An ALU clause past the shader's end. */
    v9x_eg_cf_alu(40ul, 4ul, bad);
    v9x_eg_cf_export(V9X_EG_EXPORT_PIXEL, 0ul, 0ul, 1ul, 1ul, 1ul, bad + 2);
    CHECK(v9x_cm_shader_from_eg(bad, 8ul, shader) == 0ul);
}

unsigned int v9x_run_cm_shader_tests(void)
{
    failures = 0u;
    test_blit();
    test_cf_end();
    test_from_eg_blit();
    test_from_eg_d3d();
    test_from_eg_refuses();
    return failures;
}
