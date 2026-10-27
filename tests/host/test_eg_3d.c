#include <stdio.h>
#include <string.h>

#include "velocity9x/eg_3d.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

#define BUFFER_DWORDS 1024u
static v9x_u32 buffer[BUFFER_DWORDS];

/* A type-3 header's opcode and body length. */
static v9x_u32 op_of(v9x_u32 header)
{
    return (header >> 8) & 0xfful;
}

static v9x_u32 body_of(v9x_u32 header)
{
    return ((header >> 16) & 0x3ffful) + 1ul;
}

/*
 * Walks a stream packet by packet: every dword is a type-3 header whose
 * body lies inside the stream, or a type-2 pad. A miscounted packet throws
 * the walk off its headers and fails here, which is the check the Phase 3
 * stray write called for.
 */
static int walks(const v9x_u32 *dwords, v9x_u32 count)
{
    v9x_u32 at = 0ul;

    while (at < count) {
        if (dwords[at] == V9X_EG_PACKET2) {
            ++at;
            continue;
        }
        if ((dwords[at] >> 30) != 3ul) {
            return 0;
        }
        at += 1ul + body_of(dwords[at]);
    }
    return at == count;
}

/* The index of the first SET_*_REG packet writing reg, or count. */
static v9x_u32 find_reg(const v9x_u32 *dwords, v9x_u32 count, v9x_u32 reg)
{
    v9x_u32 at = 0ul;
    v9x_u32 start;

    while (at < count) {
        if (dwords[at] == V9X_EG_PACKET2) {
            ++at;
            continue;
        }
        start = 0xfffffffful;
        if (op_of(dwords[at]) == V9X_EG_P3_SET_CONTEXT_REG) {
            start = V9X_EG_SET_CONTEXT_START;
        } else if (op_of(dwords[at]) == V9X_EG_P3_SET_CONFIG_REG) {
            start = V9X_EG_SET_CONFIG_START;
        }
        if (start != 0xfffffffful &&
            reg >= start + dwords[at + 1ul] * 4ul &&
            reg < start + (dwords[at + 1ul] + body_of(dwords[at]) - 1ul) *
                          4ul) {
            return at;
        }
        at += 1ul + body_of(dwords[at]);
    }
    return count;
}

/* The value a stream last gives reg, or 0xdeadbeef if none. */
static v9x_u32 reg_value(const v9x_u32 *dwords, v9x_u32 count, v9x_u32 reg)
{
    v9x_u32 at = find_reg(dwords, count, reg);
    v9x_u32 start;

    if (at == count) {
        return 0xdeadbeeful;
    }
    start = op_of(dwords[at]) == V9X_EG_P3_SET_CONTEXT_REG ?
            V9X_EG_SET_CONTEXT_START : V9X_EG_SET_CONFIG_START;
    return dwords[at + 2ul + (reg - start) / 4ul - dwords[at + 1ul]];
}

static void test_packets(void)
{
    struct v9x_eg_pm4 stream;
    v9x_u32 values[8];
    v9x_u32 index;

    for (index = 0ul; index < 8ul; ++index) {
        values[index] = index;
    }
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    /* Eight registers: a count of 8 in the header, offset then values. */
    v9x_eg_pm4_set_context(&stream, 0x28040ul, values, 8ul);
    CHECK(stream.count == 10ul);
    CHECK(buffer[0] == 0xc0086900ul && buffer[1] == 0x10ul);
    CHECK(buffer[9] == 7ul);
    /* The probe's single config write. */
    v9x_eg_pm4_set_config1(&stream, V9X_EG_SCRATCH_REG0, 0x3d3d3d3dul);
    CHECK(buffer[10] == 0xc0016800ul && buffer[11] == 0x140ul &&
          buffer[12] == 0x3d3d3d3dul);
    CHECK(walks(buffer, stream.count));
    CHECK(stream.overflow == 0ul);
    /* Out of the packet's range, misaligned, or empty: nothing written. */
    v9x_eg_pm4_set_context(&stream, V9X_EG_SCRATCH_REG0, values, 1ul);
    CHECK(stream.overflow == 1ul && stream.count == 13ul);
    stream.overflow = 0ul;
    v9x_eg_pm4_set_config(&stream, 0x8002ul, values, 1ul);
    CHECK(stream.overflow == 1ul && stream.count == 13ul);
    stream.overflow = 0ul;
    v9x_eg_pm4_set_context(&stream, 0x28000ul, values, 0ul);
    CHECK(stream.overflow == 1ul && stream.count == 13ul);
    /* Padding to the CP's 16-dword fetch. */
    stream.overflow = 0ul;
    v9x_eg_pm4_pad(&stream);
    CHECK(stream.count == 16ul && buffer[15] == V9X_EG_PACKET2);
    /* Capacity: past it, overflow and no write. */
    v9x_eg_pm4_begin(&stream, buffer, 2ul);
    v9x_eg_pm4_set_config1(&stream, V9X_EG_SCRATCH_REG0, 1ul);
    CHECK(stream.overflow == 1ul && stream.count == 2ul);
    /* A full surface sync and one of 300 bytes at 1000h. */
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_eg_pm4_surface_sync(&stream, V9X_EG_SYNC_CB_ACTION, 0xfffffffful, 0ul);
    v9x_eg_pm4_surface_sync(&stream, V9X_EG_SYNC_TC_ACTION, 300ul, 0x1000ul);
    CHECK(buffer[0] == 0xc0034300ul && buffer[2] == 0xfffffffful &&
          buffer[3] == 0ul && buffer[4] == 10ul);
    CHECK(buffer[7] == 2ul && buffer[8] == 0x10ul);
}

static void test_cp_streams(void)
{
    struct v9x_eg_pm4 stream;

    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_eg_3d_me_initialize(&stream);
    CHECK(stream.count == V9X_EG_ME_INITIALIZE_DWORDS);
    CHECK(buffer[0] == 0xc0054400ul && buffer[3] == 3ul &&
          buffer[4] == 0x10000ul);

    /* The preamble as V9XEG3D sent it: the default context (227 dwords)
     * between BEGIN and END_CLEAR_STATE, then 18 more. */
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_eg_3d_preamble(&stream);
    CHECK(stream.overflow == 0ul && walks(buffer, stream.count));
    CHECK(buffer[0] == 0xc0004a00ul && buffer[1] == 0x20000000ul);
    CHECK(buffer[2] == 0xc0016900ul && buffer[3] == 0x23bul);
    CHECK(buffer[229] == 0xc0004a00ul && buffer[230] == 0x30000000ul);
    CHECK(buffer[231] == 0xc0001200ul);
    CHECK(buffer[233] == 0xc0026f00ul && buffer[237] == 0xc0036f00ul &&
          buffer[238] == 0xbc4ul);
    CHECK(stream.count == 246ul);
    CHECK(buffer[242] == 0xc0026900ul && buffer[243] == 0x316ul &&
          buffer[244] == 0xeul && buffer[245] == 0x10ul);

    /* set_default_state: 35 dwords of packets, then the 227. */
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_eg_3d_default_state(&stream);
    CHECK(stream.overflow == 0ul && walks(buffer, stream.count));
    CHECK(stream.count == 35ul + 227ul);
    CHECK(buffer[0] == 0xc0001200ul);
    CHECK(buffer[2] == 0xc0016800ul && buffer[3] == 0x363ul);
    CHECK(buffer[5] == 0xc0016800ul && buffer[6] == 0x38bul &&
          buffer[7] == 0x10001000ul);
    CHECK(buffer[8] == 0xc00b6800ul && buffer[9] == 0x300ul &&
          buffer[10] == 0xe4000002ul && buffer[11] == 0x402e005dul);
    CHECK(buffer[21] == 0xc0012800ul && buffer[22] == 0x80000000ul);
    CHECK(buffer[24] == 0xc0026f00ul);
    CHECK(buffer[28] == 0xc0036e00ul && buffer[30] == 0x12ul);
    CHECK(buffer[33] == 0xc0001800ul && buffer[34] == 1ul);
    CHECK(buffer[35] == 0xc0016900ul && buffer[36] == 0x23bul);
}

static void test_gpu_init(void)
{
    const struct v9x_eg_reg_update *table = v9x_eg_cedar_gpu_init;
    v9x_u32 index;

    CHECK(table[0].offset == V9X_EG_GRBM_CNTL && table[0].set == 0xfful);
    CHECK(table[1].offset == 0x98f8ul && table[1].set == 0x02010001ul);
    CHECK(table[20].offset == V9X_EG_SQ_CONFIG &&
          table[20].keep == 0x00fffffeul && table[20].set == 0xe4000002ul);
    CHECK(table[V9X_EG_CEDAR_GPU_INIT_COUNT - 1u].offset == 0x8a14ul);
    /* SMX_DC_CTL0 clears NUMBER_OF_SETS (bits 9:1) before setting 4. */
    CHECK(table[11].offset == 0xa020ul && (table[11].keep & 0x3feul) == 0ul &&
          table[11].set == 8ul);
    /* No register is written twice. */
    for (index = 0ul; index < V9X_EG_CEDAR_GPU_INIT_COUNT; ++index) {
        v9x_u32 other;

        for (other = index + 1ul; other < V9X_EG_CEDAR_GPU_INIT_COUNT;
             ++other) {
            CHECK(table[index].offset != table[other].offset);
        }
    }
}

static void test_words(void)
{
    /* The probe's depth test: Z on, writes, LESS. */
    CHECK(v9x_eg_3d_depth_control(1ul, 1ul, V9X_EG_FUNC_LESS) == 0x16ul);
    CHECK(v9x_eg_3d_depth_control(1ul, 0ul, V9X_EG_FUNC_ALWAYS) == 0x72ul);
    CHECK(v9x_eg_3d_blend(0ul, V9X_EG_BLEND_SRC_ALPHA,
                          V9X_EG_BLEND_ONE_MINUS_SRC_ALPHA) == 0x00010001ul);
    CHECK(v9x_eg_3d_blend(1ul, V9X_EG_BLEND_SRC_ALPHA,
                          V9X_EG_BLEND_ONE_MINUS_SRC_ALPHA) == 0x45040504ul);
    CHECK(v9x_eg_3d_alpha_test(1ul, 4ul) == 0x0cul);
    CHECK(v9x_eg_3d_alpha_test(0ul, 7ul) == 0x07ul);
    /* Linux's sampler 0 is CLAMP_X and _Y at the last texel, point. */
    CHECK((v9x_eg_3d_sampler(0ul, 0ul, V9X_EG_TEX_CLAMP_LAST_TEXEL,
                             V9X_EG_TEX_CLAMP_LAST_TEXEL) & 0x3ful) == 0x12ul);
    CHECK(v9x_eg_3d_sampler(1ul, 1ul, V9X_EG_TEX_WRAP, V9X_EG_TEX_WRAP) ==
          ((2ul << 6) | (1ul << 9) | (1ul << 11)));
    /* Magnification and minification apart. */
    CHECK(v9x_eg_3d_sampler(1ul, 0ul, V9X_EG_TEX_MIRROR, V9X_EG_TEX_WRAP) ==
          (1ul | (2ul << 6) | (1ul << 9)));
}

static void fill_state(struct v9x_eg_3d_state *state)
{
    memset(state, 0, sizeof(*state));
    state->target_gpu = 0x00100000ul;
    state->target_pitch = 640ul;
    state->target_height = 480ul;
    state->target_format = V9X_EG_COLOR_5_6_5;
    state->scissor_right = 640ul;
    state->scissor_bottom = 480ul;
    state->has_depth = 1ul;
    state->depth_gpu = 0x00200000ul;
    state->depth_pitch = 640ul;
    state->depth_height = 480ul;
    state->depth_control = v9x_eg_3d_depth_control(1ul, 1ul, 3ul);
    state->blend_control = v9x_eg_3d_blend(0ul, 0ul, 0ul);
    state->perspective = 1ul;
    state->vs_gpu = 0x0f010000ul;
    state->vs_gprs = 3ul;
    state->ps_gpu = 0x0f010100ul;
    state->ps_gprs = 2ul;
    state->interpolants = 2ul;
    state->has_texture = 1ul;
    state->texture_levels = 1ul;
    state->texture_gpu = 0x00300000ul;
    state->texture_width = 60ul;
    state->texture_height = 30ul;
    state->texture_pitch = 64ul;
    state->texture_format = V9X_EG_COLOR_8_8_8_8;
    state->sampler_word0 = v9x_eg_3d_sampler(1ul, 1ul, 0ul, 0ul);
    state->vertex_gpu = 0x0f200000ul;
    state->vertex_stride = 32ul;
    state->vertex_bytes = 96ul;
}

static void test_state(void)
{
    struct v9x_eg_pm4 stream;
    struct v9x_eg_3d_state state;
    v9x_u32 count;
    v9x_u32 at;

    fill_state(&state);
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    count = stream.count;
    CHECK(walks(buffer, count));
    /* The shaders, GPRs and the PS's one colour export. */
    CHECK(reg_value(buffer, count, 0x2885cul) == 0x0f0100ul);
    CHECK(reg_value(buffer, count, 0x28860ul) == 3ul);
    CHECK(reg_value(buffer, count, 0x28840ul) == 0x0f0101ul);
    CHECK(reg_value(buffer, count, 0x28844ul) == 2ul);
    CHECK(reg_value(buffer, count, 0x2884cul) == 2ul);
    /* Two interpolants, perspective. */
    CHECK(reg_value(buffer, count, 0x2861cul) == 0x100ul);
    CHECK(reg_value(buffer, count, 0x28648ul) == 0x101ul);
    CHECK(reg_value(buffer, count, 0x286c4ul) == 2ul);
    CHECK(reg_value(buffer, count, 0x286ccul) == 0x10000002ul);
    CHECK(reg_value(buffer, count, 0x286e0ul) == 1ul);
    /* W0_FMT clear: measured perspective-correct (d3dpersp, run b1). */
    CHECK(reg_value(buffer, count, 0x28818ul) == 0x300ul);
    CHECK(reg_value(buffer, count, 0x28c08ul) == 4ul);
    /* The colour buffer: base, pitch 640 / 8 - 1, slices, linear general
     * COLOR_5_6_5 with SOURCE_FORMAT 1, NON_DISP_TILING_ORDER, 640x480. */
    at = find_reg(buffer, count, 0x28c60ul);
    CHECK(at != count && buffer[at] == 0xc00f6900ul && buffer[at + 1ul] ==
          0x318ul);
    CHECK(buffer[at + 2ul] == 0x1000ul && buffer[at + 3ul] == 79ul &&
          buffer[at + 4ul] == 4799ul && buffer[at + 6ul] == 0x01000020ul &&
          buffer[at + 7ul] == 0x10ul &&
          buffer[at + 8ul] == (639ul | (479ul << 16)));
    /* Z16 1D tiled at 2000h (256-byte units), 80x60 tiles. */
    at = find_reg(buffer, count, 0x28040ul);
    CHECK(at != count && buffer[at] == 0xc0086900ul);
    CHECK(buffer[at + 2ul] == 0x21ul && buffer[at + 4ul] == 0x2000ul &&
          buffer[at + 6ul] == 0x2000ul &&
          buffer[at + 8ul] == (79ul | (59ul << 11)) &&
          buffer[at + 9ul] == 4799ul);
    CHECK(reg_value(buffer, count, 0x28800ul) == 0x36ul);
    /* The scissors, window offset disabled. */
    CHECK(reg_value(buffer, count, 0x28240ul) == 0x80000000ul);
    CHECK(reg_value(buffer, count, 0x28244ul) == (640ul | (480ul << 16)));
    /* The texture: pitch 64 / 8 - 1 and width 59 in word 0, height 29 and
     * linear general in word 1, FMT_8_8_8_8 valid in word 7. */
    for (at = 0ul; at < count; ++at) {
        if (buffer[at] == 0xc0086d00ul && buffer[at + 1ul] == 0ul) {
            break;
        }
    }
    CHECK(at < count);
    CHECK(buffer[at + 2ul] == (1ul | (7ul << 6) | (59ul << 18)));
    CHECK(buffer[at + 3ul] == 29ul && buffer[at + 4ul] == 0x3000ul &&
          buffer[at + 9ul] == 0x8000001aul);
    /* The vertex buffer, resource 580h, 96 bytes at stride 32. */
    for (at = 0ul; at < count; ++at) {
        if (buffer[at] == 0xc0086d00ul && buffer[at + 1ul] == 0x580ul) {
            break;
        }
    }
    CHECK(at < count && buffer[at + 2ul] == 0x0f200000ul &&
          buffer[at + 3ul] == 95ul && buffer[at + 4ul] == 0x2000ul &&
          buffer[at + 9ul] == 0xc0000000ul);

    /* Without depth or texture: no DB block, DB_DEPTH_CONTROL 0, no
     * texture resource; one interpolant, linear. */
    fill_state(&state);
    state.has_depth = 0ul;
    state.has_texture = 0ul;
    state.interpolants = 1ul;
    state.perspective = 0ul;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    count = stream.count;
    CHECK(walks(buffer, count));
    CHECK(find_reg(buffer, count, 0x28040ul) == count);
    CHECK(reg_value(buffer, count, 0x28800ul) == 0ul);
    CHECK(find_reg(buffer, count, 0x28648ul) == count);
    CHECK(reg_value(buffer, count, 0x286ccul) == 0x20000001ul);
    CHECK(reg_value(buffer, count, 0x286e0ul) == 0x10000ul);
    CHECK(reg_value(buffer, count, 0x28818ul) == 0x300ul);

    /* The CB's channel mask: all four with write_alpha, B, G and R alone
     * without, so an XRGB8888 target's X byte keeps what it held (V9XD32
     * at 32 bpp read FF13579B for 0013579B, boot 15). */
    fill_state(&state);
    state.target_format = V9X_EG_COLOR_8_8_8_8;
    state.write_alpha = 0ul;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(reg_value(buffer, stream.count, 0x28238ul) == 7ul);
    state.write_alpha = 1ul;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(reg_value(buffer, stream.count, 0x28238ul) == 0xful);

    /* An OpenGL write mask: a masked channel's bit is cleared, x, y and z
     * being B, G and R. */
    state.write_alpha = 0ul;
    state.masked_channels = V9X_EG_CHANNEL_RED | V9X_EG_CHANNEL_BLUE;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(reg_value(buffer, stream.count, 0x28238ul) == 2ul);
    state.masked_channels = 0ul;

    /* DB_SHADER_CONTROL: the default state's 210h, and for a PS that
     * kills, KILL_ENABLE with Z late, after the shader (evergreend.h). */
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(reg_value(buffer, stream.count, 0x2880cul) == 0x210ul);
    state.kill = 1ul;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(reg_value(buffer, stream.count, 0x2880cul) == 0x240ul);
    state.kill = 0ul;

    /* GPU address 0 is memory like any other: on M-BXMFA8 the heap starts
     * there, and after a flip the back buffer is the surface at offset 0
     * (V9XD32 at 32 bpp, boot 14). Depth and texture are switched by their
     * flags, not by a zero address. */
    fill_state(&state);
    state.target_gpu = 0ul;
    state.depth_gpu = 0ul;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    count = stream.count;
    at = find_reg(buffer, count, 0x28c60ul);
    CHECK(at != count && buffer[at + 2ul] == 0ul);
    at = find_reg(buffer, count, 0x28040ul);
    CHECK(at != count && buffer[at + 2ul] == 0x21ul &&
          buffer[at + 4ul] == 0ul);

    /* Refusals write nothing. */
    fill_state(&state);
    state.target_gpu = 0x00100080ul;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT && stream.count == 0ul);
    fill_state(&state);
    state.target_pitch = 644ul;
    CHECK(v9x_eg_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    fill_state(&state);
    state.depth_height = 476ul;
    CHECK(v9x_eg_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    fill_state(&state);
    state.scissor_right = 641ul;
    CHECK(v9x_eg_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    fill_state(&state);
    state.texture_format = 0x05ul;
    CHECK(v9x_eg_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* A texture row is a whole number of 64 bytes, measured on M-BXMFA8
     * (V9XEG3D /d3d1555, boot 20): 32, 48, 80 and 96 bytes read odd rows
     * wrong; 64, 128 and 192 were exact in 1555 and in 8888. So 16
     * texels of 1555 are refused and 32 taken; 16 of 8888 taken. */
    fill_state(&state);
    state.texture_format = V9X_EG_COLOR_1_5_5_5;
    state.texture_width = 16ul;
    state.texture_pitch = 16ul;
    CHECK(v9x_eg_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    state.texture_pitch = 32ul;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    state.texture_pitch = 24ul;
    state.texture_format = V9X_EG_COLOR_8_8_8_8;
    CHECK(v9x_eg_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    state.texture_pitch = 16ul;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    CHECK(v9x_eg_3d_texture_pitch(V9X_EG_COLOR_5_6_5, 16ul) == 32ul);
    CHECK(v9x_eg_3d_texture_pitch(V9X_EG_COLOR_8_8_8_8, 1ul) == 16ul);
    CHECK(v9x_eg_3d_texture_pitch(V9X_EG_COLOR_4_4_4_4, 33ul) == 64ul);

    /* A 565 texture has no alpha: its W reads 1 (DST_SEL_W 5), not the
     * missing fourth component, which read 0 and blended a texture away
     * under SRCALPHA (V9XDDP's untagged texture, boot 21). Formats with
     * alpha keep W. */
    fill_state(&state);
    state.texture_format = V9X_EG_COLOR_5_6_5;
    state.texture_pitch = 64ul;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    for (at = 0ul; at < stream.count; ++at) {
        if (buffer[at] == 0xc0086d00ul && buffer[at + 1ul] == 0ul) {
            break;
        }
    }
    CHECK(at < stream.count &&
          buffer[at + 6ul] == ((0ul << 16) | (1ul << 19) | (2ul << 22) |
                               (5ul << 25)));
    fill_state(&state);
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    for (at = 0ul; at < stream.count; ++at) {
        if (buffer[at] == 0xc0086d00ul && buffer[at + 1ul] == 0ul) {
            break;
        }
    }
    CHECK(at < stream.count && ((buffer[at + 6ul] >> 25) & 7ul) == 3ul);

    /* 1555 and 4444 are taken, with their format in word 7. */
    fill_state(&state);
    state.texture_format = V9X_EG_COLOR_1_5_5_5;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    for (at = 0ul; at < stream.count; ++at) {
        if (buffer[at] == 0xc0086d00ul && buffer[at + 1ul] == 0ul) {
            break;
        }
    }
    CHECK(at < stream.count && buffer[at + 9ul] == 0x8000000aul);
    fill_state(&state);
    state.texture_format = V9X_EG_COLOR_4_4_4_4;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    /* Too small a buffer: overflow reported, not an overrun. */
    fill_state(&state);
    v9x_eg_pm4_begin(&stream, buffer, 40ul);
    CHECK(v9x_eg_3d_build_state(&state, &stream) ==
          V9X_STATUS_INSUFFICIENT_MEMORY && stream.count <= 40ul);
}

static void test_mip(void)
{
    struct v9x_eg_pm4 stream;
    struct v9x_eg_3d_state state;
    v9x_u32 offsets[V9X_EG_MIP_LEVELS_MAX];
    v9x_u32 pitches[V9X_EG_MIP_LEVELS_MAX];
    v9x_u32 total = 0ul;
    v9x_u32 at;

    /* The chain V9XEG3D /d3dmip measured on M-BXMFA8 (boot 23): 32x32
     * 8888, every level at 64 texels a row, back to back. */
    CHECK(v9x_eg_3d_mip_layout(V9X_EG_COLOR_8_8_8_8, 32ul, 32ul, 6ul,
                               offsets, pitches, &total) == 1);
    CHECK(offsets[0] == 0ul && offsets[1] == 0x2000ul &&
          offsets[2] == 0x3000ul && offsets[3] == 0x3800ul &&
          offsets[4] == 0x3c00ul && offsets[5] == 0x3e00ul);
    CHECK(pitches[0] == 64ul && pitches[5] == 64ul && total == 0x3f00ul);
    /* 16-bit texels: MAX(64, 256 / 2) = 128 a row; a 256-wide level is
     * its own width. A non-square chain runs to 1x1. */
    CHECK(v9x_eg_3d_mip_layout(V9X_EG_COLOR_1_5_5_5, 256ul, 16ul, 9ul,
                               offsets, pitches, &total) == 1);
    CHECK(pitches[0] == 256ul && pitches[1] == 128ul && pitches[8] == 128ul);
    CHECK(offsets[1] == 256ul * 2ul * 16ul &&
          offsets[2] == offsets[1] + 128ul * 2ul * 8ul &&
          offsets[5] == offsets[4] + 256ul * 1ul);
    /* Too many levels for the edges, or an unsampled format: refused. */
    CHECK(v9x_eg_3d_mip_layout(V9X_EG_COLOR_8_8_8_8, 32ul, 32ul, 7ul,
                               offsets, pitches, &total) == 0);
    CHECK(v9x_eg_3d_mip_layout(0x05ul, 32ul, 32ul, 2ul, offsets, pitches,
                               &total) == 0);

    /* A chained texture: linear aligned, MIP_ADDRESS, LAST_LEVEL, the mip
     * filter in the sampler and MAX_LOD 4.8. */
    fill_state(&state);
    state.texture_width = 32ul;
    state.texture_height = 32ul;
    state.texture_pitch = 64ul;
    state.texture_levels = 6ul;
    state.texture_mip_gpu = 0x00302000ul;
    state.mip_filter = V9X_EG_MIP_LINEAR;
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    for (at = 0ul; at < stream.count; ++at) {
        if (buffer[at] == 0xc0086d00ul && buffer[at + 1ul] == 0ul) {
            break;
        }
    }
    CHECK(at < stream.count);
    CHECK((buffer[at + 3ul] >> 28) == V9X_EG_ARRAY_LINEAR_ALIGNED);
    CHECK(buffer[at + 5ul] == 0x3020ul && buffer[at + 7ul] == 5ul);
    CHECK(buffer[at + 10ul] == 0xc0036e00ul);
    CHECK((buffer[at + 12ul] >> 15 & 3ul) == V9X_EG_MIP_LINEAR &&
          buffer[at + 13ul] == (5ul * 256ul) << 12);
    /* A chain whose pitch is not the layout's is refused. */
    state.texture_pitch = 32ul;
    CHECK(v9x_eg_3d_build_state(&state, &stream) ==
          V9X_STATUS_INVALID_ARGUMENT);
    /* One level: linear general, no MIP_ADDRESS, no LOD range. */
    fill_state(&state);
    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    CHECK(v9x_eg_3d_build_state(&state, &stream) == V9X_STATUS_OK);
    for (at = 0ul; at < stream.count; ++at) {
        if (buffer[at] == 0xc0086d00ul && buffer[at + 1ul] == 0ul) {
            break;
        }
    }
    CHECK((buffer[at + 3ul] >> 28) == V9X_EG_ARRAY_LINEAR_GENERAL &&
          buffer[at + 7ul] == 0ul && buffer[at + 13ul] == 0ul);
}

static void test_draw(void)
{
    struct v9x_eg_pm4 stream;

    v9x_eg_pm4_begin(&stream, buffer, BUFFER_DWORDS);
    v9x_eg_3d_draw(&stream, 6ul);
    v9x_eg_3d_flush(&stream);
    v9x_eg_3d_fence(&stream, 0x12345678ul);
    CHECK(walks(buffer, stream.count));
    CHECK(buffer[0] == 0xc0016800ul && buffer[1] == 0x256ul &&
          buffer[2] == 4ul);
    CHECK(buffer[3] == 0xc0002a00ul && buffer[5] == 0xc0002f00ul &&
          buffer[7] == 0xc0012d00ul && buffer[8] == 6ul && buffer[9] == 2ul);
    CHECK(buffer[10] == 0xc0034300ul && buffer[11] == 0x06804040ul);
    CHECK(buffer[15] == 0xc0016800ul && buffer[16] == 0x140ul &&
          buffer[17] == 0x12345678ul);
}

unsigned int v9x_run_eg_3d_tests(void)
{
    failures = 0u;
    test_packets();
    test_cp_streams();
    test_gpu_init();
    test_words();
    test_state();
    test_mip();
    test_draw();
    return failures;
}
