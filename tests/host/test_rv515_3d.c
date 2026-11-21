#include <stdio.h>
#include <string.h>

#include "velocity9x/rv515_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* M-5CAPM3's frame buffer: 256 MiB at GPU address D0000000
 * (docs\decisions\2026-11-21-rv515-register-probe.md). */
#define RV515_FB_BASE  0xd0000000ul
#define RV515_FB_BYTES 0x10000000ul

/* The programs measured on the card (V9XR53D, M-5CAPM3 boot 44), which are
 * also the golden words test_r500_fp.c generated from Mesa's defines:
 * colour (first draw), and LD + OUT for REPLACE and MODULATE. */
static const v9x_u32 colour_out[6] = {
    0x001f8101ul, 0ul, 0ul, 0x00db0220ul, 0x00c0c000ul, 0x20490000ul
};
static const v9x_u32 tex_ld[6] = {
    0x00187807ul, 0x06400000ul, 0xe4000400ul, 0ul, 0ul, 0ul
};
static const v9x_u32 replace_out[6] = {
    0x001f8105ul, 0x00000400ul, 0x00000400ul, 0x00db0220ul, 0x00c0c000ul,
    0x20490000ul
};
static const v9x_u32 modulate_out[6] = {
    0x001f8105ul, 0x00000400ul, 0x00000400ul, 0x00442220ul, 0x0068c000ul,
    0x20490000ul
};

/* R500_INST_LAST. */
#define INST_LAST 0x00000100ul

static void base_target(struct v9x_rs690_3d_target *target)
{
    memset(target, 0, sizeof(*target));
    target->fb_gpu_base = RV515_FB_BASE;
    target->fb_bytes = RV515_FB_BYTES;
    target->offset = 0x01000000ul;
    target->pitch_bytes = 128ul;
    target->bytes_per_pixel = 2ul;
    target->width = 64ul;
    target->height = 32ul;
}

static void base_texture(struct v9x_rs690_3d_texture *texture)
{
    memset(texture, 0, sizeof(*texture));
    texture->offset = 0x01010000ul;
    texture->pitch_bytes = 64ul;
    texture->format = V9X_RS690_TEXFMT_RGB565;
    texture->width = 32ul;
    texture->height = 16ul;
    texture->filter = V9X_RS690_TEXFILTER_NEAREST;
    texture->address = V9X_RS690_TEXADDR_CLAMP;
    texture->combine = V9X_RS690_TEXCOMBINE_REPLACE;
}

static void base_state(struct v9x_rs690_3d_state *state)
{
    memset(state, 0, sizeof(*state));
    base_target(&state->target);
    base_texture(&state->texture);
    state->depth_offset = 0x01020000ul;
    state->depth_pitch_bytes = 128ul;
    state->depth_func = V9X_RS690_ZFUNC_LESS;
    state->depth_write = 1ul;
    state->blend_src = V9X_RS690_BLEND_SRC_ALPHA;
    state->blend_dst = V9X_RS690_BLEND_INV_SRC_ALPHA;
    state->alpha_func = V9X_RS690_AFUNC_GREATER;
    state->alpha_ref = 128ul;
}

/* Non-zero when no write lands on an address the R500 must not be given
 * the R300's value for. */
static int no_r300_only(const struct v9x_rv515_3d_list *list)
{
    v9x_u32 index;

    for (index = 0ul; index < list->count; ++index) {
        v9x_u32 offset = list->offsets[index];

        if ((offset >= 0x4310ul && offset <= 0x431cul) ||
            (offset >= 0x4330ul && offset <= 0x433cul) ||
            (offset >= 0x4608ul && offset <= 0x462cul &&
             offset != V9X_RV515_US_FC_CTRL) ||
            (offset >= 0x46c0ul && offset <= 0x49fcul) ||
            (offset >= 0x4c00ul && offset <= 0x4dfcul)) {
            return 0;
        }
    }
    return 1;
}

static v9x_u32 last_value(const struct v9x_rv515_3d_list *list,
                          v9x_u32 offset)
{
    v9x_u32 index;
    v9x_u32 value = 0xdeadbeeful;

    for (index = 0ul; index < list->count; ++index) {
        if (list->offsets[index] == offset) {
            value = list->values[index];
        }
    }
    return value;
}

/* The program words: every GA_US_VECTOR_DATA write after the first
 * instruction-type GA_US_VECTOR_INDEX, until the next index write. */
static v9x_u32 program_words(const struct v9x_rv515_3d_list *list,
                             v9x_u32 *words, v9x_u32 capacity)
{
    v9x_u32 index;
    v9x_u32 count = 0ul;
    int in_program = 0;

    for (index = 0ul; index < list->count; ++index) {
        if (list->offsets[index] == V9X_RV515_GA_US_VECTOR_INDEX) {
            if (in_program) {
                break;
            }
            in_program = list->values[index] == 0ul;
        } else if (in_program &&
                   list->offsets[index] == V9X_RV515_GA_US_VECTOR_DATA &&
                   count < capacity) {
            words[count++] = list->values[index];
        }
    }
    return count;
}

static int same(const v9x_u32 *actual, const v9x_u32 *expected)
{
    return memcmp(actual, expected, 6u * sizeof(v9x_u32)) == 0;
}

static void test_fixup(void)
{
    struct v9x_rs690_3d_list in;
    struct v9x_rv515_3d_list out;

    in.count = 0ul;
#define ADD(o, v) do { in.offsets[in.count] = (o); \
    in.values[in.count] = (v); ++in.count; } while (0)
    ADD(0x4310ul, 1ul);                   /* R300 RS_IP_0 */
    ADD(0x4334ul, 2ul);                   /* R300 RS_INST_1 */
    ADD(0x4620ul, 3ul);                   /* R300 US_TEX_INST_0 */
    ADD(0x48c0ul, 4ul);                   /* R300 US_ALU_RGB_INST_0 */
    ADD(0x4c00ul, 5ul);                   /* R300 US_ALU_CONST_R_0 */
    ADD(V9X_RS690_SC_SCISSOR1,
        (1440ul + 63ul) | ((1440ul + 31ul) << V9X_RS690_SC_Y_SHIFT));
    ADD(V9X_RS690_FG_ALPHA_FUNC, 0x00000c80ul);  /* GREATER 128, on */
    ADD(V9X_RS690_FG_ALPHA_FUNC, 0ul);           /* off */
    ADD(0x4300ul, 0x00040080ul);          /* RS_COUNT: shared, kept */
#undef ADD
    v9x_rv515_3d_begin(&out);
    v9x_rv515_3d_fixup(&in, &out);
    CHECK(out.dropped == 5ul);
    CHECK(out.count == 4ul);
    CHECK(out.offsets[0] == V9X_RS690_SC_SCISSOR1);
    CHECK(out.values[0] == (63ul | (31ul << V9X_RS690_SC_Y_SHIFT)));
    CHECK(out.values[1] == (0x00000c80ul | V9X_RV515_FG_ALPHA_FUNC_8BIT));
    CHECK(out.values[2] == 0ul);
    CHECK(out.offsets[3] == 0x4300ul && out.values[3] == 0x00040080ul);
}

static void test_init(void)
{
    struct v9x_rv515_3d_list list;

    CHECK(v9x_rv515_build_3d_init(0x000001e4ul, 0ul, &list) ==
          V9X_STATUS_OK);
    CHECK(list.count > 0ul);
    CHECK(no_r300_only(&list));
    CHECK(list.offsets[list.count - 1ul] == V9X_RV515_VAP_INDEX_OFFSET);
    CHECK(list.values[list.count - 1ul] == 0ul);
    /* The init's one guard-offset write, SC_CLIP_0_A at (0, 0). */
    CHECK(last_value(&list, V9X_RS690_SC_CLIP_0_A) == 0ul);
}

/* The colour and textured builders give the programs the card ran. */
static void test_measured_programs(void)
{
    struct v9x_rs690_3d_target target;
    struct v9x_rs690_3d_texture texture;
    struct v9x_rv515_3d_list list;
    v9x_u32 words[30];

    base_target(&target);
    CHECK(v9x_rv515_build_3d_target(&target, &list) == V9X_STATUS_OK);
    CHECK(no_r300_only(&list));
    CHECK(program_words(&list, words, 30ul) == 6ul);
    CHECK(same(words, colour_out));
    CHECK(last_value(&list, V9X_RV515_RS_IP_0) == 0ul);
    CHECK(last_value(&list, V9X_RV515_RS_INST_0) == 0x00010000ul);
    CHECK(last_value(&list, V9X_RV515_US_CODE_ADDR) == 0ul);
    CHECK(last_value(&list, V9X_RS690_SC_SCISSOR1) ==
          (63ul | (31ul << V9X_RS690_SC_Y_SHIFT)));

    base_texture(&texture);
    CHECK(v9x_rv515_build_3d_texture(RV515_FB_BASE, RV515_FB_BYTES, &texture,
                                     &list) == V9X_STATUS_OK);
    CHECK(no_r300_only(&list));
    CHECK(program_words(&list, words, 30ul) == 12ul);
    CHECK(same(&words[0], tex_ld));
    CHECK(same(&words[6], replace_out));
    CHECK(last_value(&list, V9X_RV515_RS_IP_0) == 0x00ffe040ul);
    CHECK(last_value(&list, V9X_RV515_RS_INST_0) == 0x00050010ul);
    CHECK(last_value(&list, V9X_RV515_US_PIXSIZE) == 1ul);
    CHECK(last_value(&list, V9X_RV515_US_CODE_RANGE) == 0x00010000ul);

    texture.combine = V9X_RS690_TEXCOMBINE_MODULATE;
    CHECK(v9x_rv515_build_3d_texture(RV515_FB_BASE, RV515_FB_BYTES, &texture,
                                     &list) == V9X_STATUS_OK);
    CHECK(program_words(&list, words, 30ul) == 12ul);
    CHECK(same(&words[6], modulate_out));
}

/* The TL state's programs: instruction counts, LAST only on the last,
 * the specular rasterizer setup, the fog constant, the alpha-test bit. */
static void test_state_programs(void)
{
    struct v9x_rs690_3d_state state;
    struct v9x_rv515_3d_list list;
    v9x_u32 words[60];
    v9x_u32 count;
    v9x_u32 index;

    /* Untextured, nothing on: the colour program. */
    base_state(&state);
    CHECK(v9x_rv515_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(no_r300_only(&list));
    CHECK(program_words(&list, words, 60ul) == 6ul);
    CHECK(same(words, colour_out));

    /* Alpha test on: the 8-bit compare. */
    state.alpha_test_enable = 1ul;
    CHECK(v9x_rv515_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK((last_value(&list, V9X_RS690_FG_ALPHA_FUNC) &
           V9X_RV515_FG_ALPHA_FUNC_8BIT) != 0ul);
    state.alpha_test_enable = 0ul;

    /* Specular, untextured: folded into one instruction, colour 1 into
     * temp 1 through R500_RS_IP_1 / R500_RS_INST_1. */
    state.specular = 1ul;
    CHECK(v9x_rv515_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(program_words(&list, words, 60ul) == 6ul);
    CHECK(last_value(&list, V9X_RV515_RS_IP_0 + 4ul) == 0x01000000ul);
    CHECK(last_value(&list, V9X_RV515_RS_INST_0 + 4ul) == 0x00051000ul);
    /* RGB C = slot 2 RGB (temp 1): word 1's ADDR2 names temp 1. */
    CHECK(words[1] == (1ul << 20));

    /* Fog: two more instructions and constant 0 after the program. */
    state.fog = 1ul;
    state.fog_color = 0x00ff0000ul;
    CHECK(v9x_rv515_build_3d_state(&state, &list) == V9X_STATUS_OK);
    count = program_words(&list, words, 60ul);
    CHECK(count == 18ul);
    for (index = 0ul; index + 6ul < count; index += 6ul) {
        CHECK((words[index] & INST_LAST) == 0ul);
    }
    CHECK((words[count - 6ul] & INST_LAST) != 0ul);
    CHECK(last_value(&list, V9X_RV515_GA_US_VECTOR_INDEX) == 0x00010000ul);
    CHECK(last_value(&list, V9X_RV515_US_PIXSIZE) == 2ul);
    /* The last step's RGB B is slot 2 AAA (f): alpha slot 2 must name the
     * specular temp (1), as RGB slot 2 does; word 2 is ALPHA_ADDR. */
    CHECK(((words[count - 6ul + 2ul] >> 20) & 0x1fful) == 1ul);
    CHECK(((words[count - 6ul + 1ul] >> 20) & 0x1fful) == 1ul);

    /* Textured DECALALPHA with specular: LD, two combine steps and the
     * specular add. */
    base_state(&state);
    state.textured = 1ul;
    state.texture.combine = V9X_RS690_TEXCOMBINE_DECALALPHA;
    state.specular = 1ul;
    CHECK(v9x_rv515_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(no_r300_only(&list));
    CHECK(program_words(&list, words, 60ul) == 24ul);
    CHECK(same(&words[0], tex_ld));
    CHECK(last_value(&list, V9X_RV515_RS_INST_0 + 4ul) == 0x00091000ul);
}

/* Mip chains at the R500's 32-byte rows (V9X_RV515_TEX_ROW_ALIGN): a
 * 16-wide 16 bpp chain with a 32-byte base pitch, which the RS690 builder
 * alone refuses, is taken by both RV515 builders, from a caller that left
 * row_align 0. */
static void test_mip_rows(void)
{
    struct v9x_rs690_3d_state state;
    struct v9x_rs690_3d_texture texture;
    struct v9x_rs690_3d_list rs690;
    struct v9x_rv515_3d_list list;

    base_texture(&texture);
    texture.width = 16ul;
    texture.height = 16ul;
    texture.pitch_bytes = 32ul;
    texture.levels = 5ul;
    texture.mip_filter = V9X_RS690_MIPFILTER_NEAREST;
    CHECK(v9x_rs690_build_3d_texture(RV515_FB_BASE, RV515_FB_BYTES, &texture,
                                     &rs690) == V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_rv515_build_3d_texture(RV515_FB_BASE, RV515_FB_BYTES, &texture,
                                     &list) == V9X_STATUS_OK);

    base_state(&state);
    state.textured = 1ul;
    state.texture = texture;
    CHECK(v9x_rv515_build_3d_state(&state, &list) == V9X_STATUS_OK);
    CHECK(state.texture.row_align == 0ul);
}

unsigned int v9x_run_rv515_3d_tests(void)
{
    failures = 0u;
    test_fixup();
    test_init();
    test_measured_programs();
    test_state_programs();
    test_mip_rows();
    return failures;
}
