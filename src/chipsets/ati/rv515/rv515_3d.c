/*
 * RV515 3D: the RS690 builder's lists through the R500 fixup, and the
 * R500 rasterizer setup and fragment programs. See
 * include\velocity9x\rv515_3d.h for what differs from the RS690 and the
 * sources.
 */
#include "velocity9x/rv515_3d.h"
#include "velocity9x/r500_fp.h"

/* The R300-only register ranges the fixup drops (inclusive). */
#define V9X_RV515_R300_RS_IP_FIRST      0x4310ul
#define V9X_RV515_R300_RS_IP_LAST       0x431cul
#define V9X_RV515_R300_RS_INST_FIRST    0x4330ul
#define V9X_RV515_R300_RS_INST_LAST     0x433cul
#define V9X_RV515_R300_US_CODE_FIRST    0x4608ul
#define V9X_RV515_R300_US_CODE_LAST     0x462cul
#define V9X_RV515_R300_US_ALU_FIRST     0x46c0ul
#define V9X_RV515_R300_US_ALU_LAST      0x49fcul
#define V9X_RV515_R300_US_CONST_FIRST   0x4c00ul
#define V9X_RV515_R300_US_CONST_LAST    0x4dfcul

/* A scan-converter point's two 13-bit fields (rs690_3d.h). */
#define V9X_RV515_SC_FIELD_MASK         0x00001ffful

/* FG_ALPHA_FUNC's enable (R300_FG_ALPHA_FUNC_ENABLE). */
#define V9X_RV515_ALPHA_TEST_ENABLE     0x00000800ul

/* R500_ZERO_TIMES_ANYTHING_EQUALS_ZERO, as Mesa sets US_CONFIG. */
#define V9X_RV515_US_CONFIG_VALUE       0x00000002ul
/* US_CODE_RANGE size and US_CODE_ADDR end, both at 16. */
#define V9X_RV515_CODE_END_SHIFT        16
/* GA_US_VECTOR_INDEX: TYPE_INSTR (0) or TYPE_CONST (1 << 16), index 0. */
#define V9X_RV515_VECTOR_INSTR_0        0x00000000ul
#define V9X_RV515_VECTOR_CONST_0        0x00010000ul

/*
 * Rasterizer words (Mesa r300_reg.h R500_RS_*):
 *   R500_RS_IP_n: tex S/T/R/Q pointers at 0, 6, 12, 18 (K0 62 is the
 *   constant 0, K1 63 the constant 1), colour pointer at 24, format at 27
 *   (RGBA 0).
 *   R500_RS_INST_n: TEX_ID 3:0, TEX_CN_WRITE bit 4, TEX_ADDR at 5; COL_ID
 *   at 12, COL_CN_WRITE bit 16, COL_ADDR at 18.
 */
#define V9X_RV515_RS_IP_COLOUR0         0x00000000ul
#define V9X_RV515_RS_IP_TEX_COLOUR0     0x00ffe040ul
#define V9X_RV515_RS_IP_COLOUR1         0x01000000ul
#define V9X_RV515_RS_INST_COL_ID1       0x00001000ul
#define V9X_RV515_RS_INST_COL_WRITE     0x00010000ul
#define V9X_RV515_RS_INST_TEX_WRITE     0x00000010ul
#define V9X_RV515_RS_INST_COL_ADDR_SHIFT 18

/* The fog colour's channels, 00RRGGBBh. */
#define V9X_RV515_CHANNEL_MAX           255ul

/* Writes that come after the program words: RS (four), US setup (six),
 * the vector index, and the fog constant (index and four values). */
#define V9X_RV515_PROGRAM_STEPS         5u

void v9x_rv515_3d_begin(struct v9x_rv515_3d_list *list)
{
    list->count = 0ul;
    list->dropped = 0ul;
}

static void v9x_rv515_3d_add(struct v9x_rv515_3d_list *list, v9x_u32 offset,
                             v9x_u32 value)
{
    if (list->count >= V9X_RV515_3D_WRITES) {
        return;
    }
    list->offsets[list->count] = offset;
    list->values[list->count] = value;
    ++list->count;
}

static int v9x_rv515_r300_only(v9x_u32 offset)
{
    return (offset >= V9X_RV515_R300_RS_IP_FIRST &&
            offset <= V9X_RV515_R300_RS_IP_LAST) ||
           (offset >= V9X_RV515_R300_RS_INST_FIRST &&
            offset <= V9X_RV515_R300_RS_INST_LAST) ||
           (offset >= V9X_RV515_R300_US_CODE_FIRST &&
            offset <= V9X_RV515_R300_US_CODE_LAST) ||
           (offset >= V9X_RV515_R300_US_ALU_FIRST &&
            offset <= V9X_RV515_R300_US_ALU_LAST) ||
           (offset >= V9X_RV515_R300_US_CONST_FIRST &&
            offset <= V9X_RV515_R300_US_CONST_LAST);
}

/* A scan-converter point without the R300's guard offset. */
static v9x_u32 v9x_rv515_sc_point(v9x_u32 value)
{
    v9x_u32 x = value & V9X_RV515_SC_FIELD_MASK;
    v9x_u32 y = (value >> V9X_RS690_SC_Y_SHIFT) & V9X_RV515_SC_FIELD_MASK;
    v9x_u32 rest = value &
                   ~((V9X_RV515_SC_FIELD_MASK << V9X_RS690_SC_Y_SHIFT) |
                     V9X_RV515_SC_FIELD_MASK);

    x = x >= V9X_RS690_SC_OFFSET ? x - V9X_RS690_SC_OFFSET : 0ul;
    y = y >= V9X_RS690_SC_OFFSET ? y - V9X_RS690_SC_OFFSET : 0ul;
    return rest | x | (y << V9X_RS690_SC_Y_SHIFT);
}

void v9x_rv515_3d_fixup(const struct v9x_rs690_3d_list *in,
                        struct v9x_rv515_3d_list *out)
{
    v9x_u32 index;

    for (index = 0ul; index < in->count; ++index) {
        v9x_u32 offset = in->offsets[index];
        v9x_u32 value = in->values[index];

        if (v9x_rv515_r300_only(offset)) {
            ++out->dropped;
            continue;
        }
        if (offset == V9X_RS690_SC_CLIP_0_A ||
            offset == V9X_RS690_SC_CLIP_0_B ||
            offset == V9X_RS690_SC_SCISSOR0 ||
            offset == V9X_RS690_SC_SCISSOR1) {
            value = v9x_rv515_sc_point(value);
        }
        if (offset == V9X_RS690_FG_ALPHA_FUNC &&
            (value & V9X_RV515_ALPHA_TEST_ENABLE) != 0ul) {
            value |= V9X_RV515_FG_ALPHA_FUNC_8BIT;
        }
        v9x_rv515_3d_add(out, offset, value);
    }
}

v9x_status v9x_rv515_append(const struct v9x_rs690_3d_list *in,
                            struct v9x_rv515_3d_list *list)
{
    if (in == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_rv515_3d_fixup(in, list);
    return list->count < V9X_RV515_3D_WRITES ? V9X_STATUS_OK :
           V9X_STATUS_INSUFFICIENT_MEMORY;
}

v9x_status v9x_rv515_build_3d_init(v9x_u32 pipe_select,
                                   v9x_u32 rb2d_dstcache_mode,
                                   struct v9x_rv515_3d_list *list)
{
    struct v9x_rs690_3d_list init;
    v9x_status status;

    if (list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_rs690_build_3d_init(pipe_select, rb2d_dstcache_mode, &init);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    v9x_rv515_3d_begin(list);
    v9x_rv515_3d_fixup(&init, list);
    v9x_rv515_3d_add(list, V9X_RV515_VAP_INDEX_OFFSET, 0ul);
    return V9X_STATUS_OK;
}

/* What the program has to do, gathered from whichever builder asked. */
struct v9x_rv515_shader {
    v9x_u32 textured;
    v9x_u32 combine;
    v9x_u32 texture_alpha;
    v9x_u32 specular;
    v9x_u32 fog;
    v9x_u32 fog_color;
};

/* An ALU step with every address temp 0, A = slot 0, B = one, C = zero,
 * dest `result`, writing the result temp's RGB. */
static void v9x_rv515_step(struct v9x_r500_alu *alu, v9x_u32 result)
{
    v9x_r500_alu_init(alu);
    alu->dest = result;
    alu->flags = V9X_R500_ALU_WRITE_RGB;
}

/* Operand `op` of the RGB unit reads slot `slot` with `swizzle`. */
static void v9x_rv515_rgb(struct v9x_r500_alu *alu, v9x_u32 op,
                          v9x_u32 slot, v9x_u32 swizzle)
{
    alu->rgb_slot[op] = slot;
    alu->rgb_swizzle[op] = swizzle;
}

/*
 * The fragment program for the shader description, in the RS690 TL
 * shader's temp layout (rs690_3d.c v9x_rs690_add_shader): the texel in
 * temp 0 and the diffuse in 1 when textured, the diffuse in 0 otherwise;
 * the specular colour in the next temp when specular or fog needs it; the
 * result temp after that. The steps are the RS690's:
 *
 *   combine   REPLACE texel; MODULATE texel * diffuse; ADD texel +
 *             diffuse; DECALALPHA (texel - diffuse) unclamped, then
 *             * texel.a + diffuse; untextured the diffuse. Specular folds
 *             into the first step as its C where C is free.
 *   specular  + specular.rgb, as its own step after ADD or DECALALPHA.
 *   fog       - fog colour (constant 0), unclamped; then * f + fog, f the
 *             specular colour's alpha.
 *
 * Step 0 also makes the fragment's alpha; the last step writes the output
 * and moves that alpha out when it is not step 0.
 *
 * As on the R300, an RGB operand's A swizzle (AAA) reads the alpha unit's
 * source slot, not the RGB unit's: a step that wants a temp's alpha as an
 * RGB operand names that temp in the alpha address word too. Measured:
 * with fog's f read through RGB slot 2 and alpha slot 2 left on temp 0
 * (the diffuse, alpha FFh), f came out 1 and the fog vanished (V9XR53D
 * TlFogFull, M-5CAPM3 boot 44); Mesa's pair compiler routes W reads the
 * same way.
 */
static void v9x_rv515_build_program(const struct v9x_rv515_shader *shader,
                                    struct v9x_r500_fp *fp)
{
    struct v9x_r500_alu steps[V9X_RV515_PROGRAM_STEPS];
    v9x_u32 count = 0ul;
    v9x_u32 textured = shader->textured != 0ul ? 1ul : 0ul;
    v9x_u32 combine = textured != 0ul ? shader->combine :
                      V9X_RS690_TEXCOMBINE_REPLACE;
    v9x_u32 diffuse = textured;
    v9x_u32 side = textured + 1ul;
    v9x_u32 side_used = shader->specular != 0ul || shader->fog != 0ul;
    v9x_u32 result = side_used ? side + 1ul : textured + 1ul;
    v9x_u32 alpha = V9X_RS690_TEXALPHA_TEXEL;
    v9x_u32 specular_folded = 0ul;
    v9x_u32 index;
    struct v9x_r500_alu *alu;

    v9x_r500_fp_begin(fp);
    if (textured != 0ul) {
        v9x_r500_fp_tex(fp, V9X_R500_TEX_LD, 0ul, 0ul, 0ul);
        alpha = shader->texture_alpha;
        if (alpha == V9X_RS690_TEXALPHA_SAME) {
            alpha = combine == V9X_RS690_TEXCOMBINE_REPLACE ?
                    V9X_RS690_TEXALPHA_TEXEL :
                    combine == V9X_RS690_TEXCOMBINE_MODULATE ?
                    V9X_RS690_TEXALPHA_MODULATE :
                    V9X_RS690_TEXALPHA_DIFFUSE;
        }
    }

    /* Step 0: slots 0, 1, 2 = temp 0, the diffuse, the specular. */
    alu = &steps[count++];
    v9x_rv515_step(alu, result);
    if (textured != 0ul) {
        alu->rgb_addr[1] = diffuse;
        alu->alpha_addr[1] = diffuse;
    }
    if (combine == V9X_RS690_TEXCOMBINE_MODULATE) {
        v9x_rv515_rgb(alu, V9X_R500_OP_B, 1ul, V9X_R500_RGB_IDENTITY);
        specular_folded = 1ul;
    } else if (combine == V9X_RS690_TEXCOMBINE_ADD) {
        v9x_rv515_rgb(alu, V9X_R500_OP_C, 1ul, V9X_R500_RGB_IDENTITY);
    } else if (combine == V9X_RS690_TEXCOMBINE_DECALALPHA) {
        v9x_rv515_rgb(alu, V9X_R500_OP_C, 1ul, V9X_R500_RGB_IDENTITY);
        alu->rgb_mod[V9X_R500_OP_C] = V9X_R500_MOD_NEG;
    } else {
        specular_folded = 1ul;
    }
    if (specular_folded != 0ul && shader->specular != 0ul) {
        alu->rgb_addr[2] = side;
        v9x_rv515_rgb(alu, V9X_R500_OP_C, 2ul, V9X_R500_RGB_IDENTITY);
    }
    if (combine != V9X_RS690_TEXCOMBINE_DECALALPHA) {
        alu->flags |= V9X_R500_ALU_CLAMP;
    }
    /* The fragment's alpha: texel (or the diffuse when untextured, both
     * slot 0), the diffuse's, or their product. */
    alu->flags |= V9X_R500_ALU_WRITE_ALPHA;
    if (alpha == V9X_RS690_TEXALPHA_DIFFUSE) {
        alu->alpha_slot[V9X_R500_OP_A] = 1ul;
    } else if (alpha == V9X_RS690_TEXALPHA_MODULATE) {
        alu->alpha_slot[V9X_R500_OP_B] = 1ul;
        alu->alpha_swizzle[V9X_R500_OP_B] = V9X_R500_SWZ_A;
    }
    if (textured != 0ul) {
        alu->flags |= V9X_R500_ALU_TEX_WAIT;
    }

    /* DECALALPHA's second half: result * texel.a + diffuse. The AAA
     * operand reads alpha slot 2, so it names the texel's temp too. */
    if (combine == V9X_RS690_TEXCOMBINE_DECALALPHA) {
        alu = &steps[count++];
        v9x_rv515_step(alu, result);
        alu->rgb_addr[0] = result;
        alu->rgb_addr[1] = diffuse;
        alu->rgb_addr[2] = 0ul;
        alu->alpha_addr[2] = 0ul;
        v9x_rv515_rgb(alu, V9X_R500_OP_B, 2ul, V9X_R500_RGB_AAA);
        v9x_rv515_rgb(alu, V9X_R500_OP_C, 1ul, V9X_R500_RGB_IDENTITY);
        alu->flags |= V9X_R500_ALU_CLAMP;
    }
    if (shader->specular != 0ul && specular_folded == 0ul) {
        alu = &steps[count++];
        v9x_rv515_step(alu, result);
        alu->rgb_addr[0] = result;
        alu->rgb_addr[2] = side;
        v9x_rv515_rgb(alu, V9X_R500_OP_C, 2ul, V9X_R500_RGB_IDENTITY);
        alu->flags |= V9X_R500_ALU_CLAMP;
    }
    if (shader->fog != 0ul) {
        /* result - fog, unclamped. */
        alu = &steps[count++];
        v9x_rv515_step(alu, result);
        alu->rgb_addr[0] = V9X_R500_ADDR_CONST;
        alu->rgb_addr[1] = result;
        v9x_rv515_rgb(alu, V9X_R500_OP_A, 1ul, V9X_R500_RGB_IDENTITY);
        v9x_rv515_rgb(alu, V9X_R500_OP_C, 0ul, V9X_R500_RGB_IDENTITY);
        alu->rgb_mod[V9X_R500_OP_C] = V9X_R500_MOD_NEG;
        /* then * f + fog. f is the specular temp's alpha, read through
         * alpha slot 2 (see the AAA note above). */
        alu = &steps[count++];
        v9x_rv515_step(alu, result);
        alu->rgb_addr[0] = V9X_R500_ADDR_CONST;
        alu->rgb_addr[1] = result;
        alu->rgb_addr[2] = side;
        alu->alpha_addr[2] = side;
        v9x_rv515_rgb(alu, V9X_R500_OP_A, 1ul, V9X_R500_RGB_IDENTITY);
        v9x_rv515_rgb(alu, V9X_R500_OP_B, 2ul, V9X_R500_RGB_AAA);
        v9x_rv515_rgb(alu, V9X_R500_OP_C, 0ul, V9X_R500_RGB_IDENTITY);
        alu->flags |= V9X_R500_ALU_CLAMP;
    }

    /* The last step writes the output. Alone, it keeps step 0's alpha;
     * otherwise its alpha unit moves the result temp's alpha out. */
    alu = &steps[count - 1ul];
    alu->flags = (alu->flags & (V9X_R500_ALU_CLAMP |
                                V9X_R500_ALU_TEX_WAIT)) |
                 V9X_R500_ALU_OUT | V9X_R500_ALU_CLAMP;
    if (count == 1ul) {
        /* Writing only the output: no temp is named, as the programs
         * measured on the card had it. */
        alu->dest = 0ul;
    } else {
        alu->alpha_addr[0] = result;
        alu->alpha_addr[1] = 0ul;
        alu->alpha_slot[V9X_R500_OP_A] = 0ul;
        alu->alpha_swizzle[V9X_R500_OP_A] = V9X_R500_SWZ_A;
        alu->alpha_slot[V9X_R500_OP_B] = 0ul;
        alu->alpha_swizzle[V9X_R500_OP_B] = V9X_R500_SWZ_ONE;
    }
    for (index = 0ul; index < count; ++index) {
        v9x_r500_fp_alu(fp, &steps[index]);
    }
    (void)v9x_r500_fp_end(fp);
}

/* The rasterizer setup and the program's registers for the shader. */
static v9x_status v9x_rv515_add_program(const struct v9x_rv515_shader *shader,
                                        struct v9x_rv515_3d_list *list)
{
    struct v9x_r500_fp fp;
    v9x_u32 textured = shader->textured != 0ul ? 1ul : 0ul;
    v9x_u32 words;
    v9x_u32 index;

    v9x_rv515_build_program(shader, &fp);
    if (fp.status != V9X_STATUS_OK) {
        return fp.status;
    }

    /* Texture coordinates into temp 0 and colour 0 after them, or colour
     * 0 into temp 0; colour 1 into the specular temp when used. RS_COUNT
     * and RS_INST_COUNT are the RS690 builder's, kept by the fixup. */
    v9x_rv515_3d_add(list, V9X_RV515_RS_IP_0,
                     textured != 0ul ? V9X_RV515_RS_IP_TEX_COLOUR0 :
                                       V9X_RV515_RS_IP_COLOUR0);
    v9x_rv515_3d_add(list, V9X_RV515_RS_INST_0,
                     (textured != 0ul ? V9X_RV515_RS_INST_TEX_WRITE : 0ul) |
                     V9X_RV515_RS_INST_COL_WRITE |
                     (textured << V9X_RV515_RS_INST_COL_ADDR_SHIFT));
    if (shader->specular != 0ul || shader->fog != 0ul) {
        v9x_rv515_3d_add(list, V9X_RV515_RS_IP_0 + 4ul,
                         V9X_RV515_RS_IP_COLOUR1);
        v9x_rv515_3d_add(list, V9X_RV515_RS_INST_0 + 4ul,
                         V9X_RV515_RS_INST_COL_ID1 |
                         V9X_RV515_RS_INST_COL_WRITE |
                         ((textured + 1ul) <<
                          V9X_RV515_RS_INST_COL_ADDR_SHIFT));
    }

    v9x_rv515_3d_add(list, V9X_RV515_US_CONFIG, V9X_RV515_US_CONFIG_VALUE);
    v9x_rv515_3d_add(list, V9X_RV515_US_PIXSIZE, fp.max_temp);
    v9x_rv515_3d_add(list, V9X_RV515_US_FC_CTRL, 0ul);
    v9x_rv515_3d_add(list, V9X_RV515_US_CODE_RANGE,
                     (fp.count - 1ul) << V9X_RV515_CODE_END_SHIFT);
    v9x_rv515_3d_add(list, V9X_RV515_US_CODE_OFFSET, 0ul);
    v9x_rv515_3d_add(list, V9X_RV515_US_CODE_ADDR,
                     (fp.count - 1ul) << V9X_RV515_CODE_END_SHIFT);
    v9x_rv515_3d_add(list, V9X_RV515_GA_US_VECTOR_INDEX,
                     V9X_RV515_VECTOR_INSTR_0);
    words = fp.count * V9X_R500_FP_WORDS_PER_INST;
    for (index = 0ul; index < words; ++index) {
        v9x_rv515_3d_add(list, V9X_RV515_GA_US_VECTOR_DATA, fp.words[index]);
    }

    /* The fog colour as constant 0: R500 constants are IEEE singles
     * (Mesa r500_emit_fs_constants writes the floats as they are). */
    if (shader->fog != 0ul) {
        v9x_rv515_3d_add(list, V9X_RV515_GA_US_VECTOR_INDEX,
                         V9X_RV515_VECTOR_CONST_0);
        v9x_rv515_3d_add(list, V9X_RV515_GA_US_VECTOR_DATA,
                         v9x_rs690_float_ratio(
                             (shader->fog_color >> 16) & 0xfful,
                             V9X_RV515_CHANNEL_MAX));
        v9x_rv515_3d_add(list, V9X_RV515_GA_US_VECTOR_DATA,
                         v9x_rs690_float_ratio(
                             (shader->fog_color >> 8) & 0xfful,
                             V9X_RV515_CHANNEL_MAX));
        v9x_rv515_3d_add(list, V9X_RV515_GA_US_VECTOR_DATA,
                         v9x_rs690_float_ratio(shader->fog_color & 0xfful,
                                               V9X_RV515_CHANNEL_MAX));
        v9x_rv515_3d_add(list, V9X_RV515_GA_US_VECTOR_DATA, 0ul);
    }
    return list->count < V9X_RV515_3D_WRITES ? V9X_STATUS_OK :
           V9X_STATUS_INSUFFICIENT_MEMORY;
}

v9x_status v9x_rv515_build_3d_target(const struct v9x_rs690_3d_target *target,
                                     struct v9x_rv515_3d_list *list)
{
    struct v9x_rs690_3d_list rs690;
    struct v9x_rv515_shader shader;
    v9x_status status;

    if (target == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_rs690_build_3d_target(target, &rs690);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    v9x_rv515_3d_begin(list);
    v9x_rv515_3d_fixup(&rs690, list);
    shader.textured = 0ul;
    shader.combine = V9X_RS690_TEXCOMBINE_REPLACE;
    shader.texture_alpha = V9X_RS690_TEXALPHA_SAME;
    shader.specular = 0ul;
    shader.fog = 0ul;
    shader.fog_color = 0ul;
    return v9x_rv515_add_program(&shader, list);
}

v9x_status v9x_rv515_build_3d_texture(v9x_u32 fb_gpu_base, v9x_u32 fb_bytes,
                                      const struct v9x_rs690_3d_texture *texture,
                                      struct v9x_rv515_3d_list *list)
{
    struct v9x_rs690_3d_list rs690;
    struct v9x_rs690_3d_texture r500_texture;
    struct v9x_rv515_shader shader;
    v9x_status status;

    if (texture == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    r500_texture = *texture;
    r500_texture.row_align = V9X_RV515_TEX_ROW_ALIGN;
    status = v9x_rs690_build_3d_texture(fb_gpu_base, fb_bytes, &r500_texture,
                                        &rs690);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    v9x_rv515_3d_begin(list);
    v9x_rv515_3d_fixup(&rs690, list);
    shader.textured = 1ul;
    shader.combine = texture->combine;
    shader.texture_alpha = V9X_RS690_TEXALPHA_SAME;
    shader.specular = 0ul;
    shader.fog = 0ul;
    shader.fog_color = 0ul;
    return v9x_rv515_add_program(&shader, list);
}

v9x_status v9x_rv515_build_3d_state(const struct v9x_rs690_3d_state *state,
                                    struct v9x_rv515_3d_list *list)
{
    static struct v9x_rs690_3d_state r500_state;
    struct v9x_rs690_3d_list rs690;
    struct v9x_rv515_shader shader;
    v9x_status status;

    if (state == 0 || list == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    r500_state = *state;
    r500_state.texture.row_align = V9X_RV515_TEX_ROW_ALIGN;
    status = v9x_rs690_build_3d_state(&r500_state, &rs690);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    v9x_rv515_3d_begin(list);
    v9x_rv515_3d_fixup(&rs690, list);
    shader.textured = state->textured;
    shader.combine = state->texture.combine;
    shader.texture_alpha = state->texture_alpha;
    shader.specular = state->specular;
    shader.fog = state->fog;
    shader.fog_color = state->fog_color;
    return v9x_rv515_add_program(&shader, list);
}
