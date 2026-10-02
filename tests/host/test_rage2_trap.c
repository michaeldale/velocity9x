#include <stdio.h>
#include <string.h>

#include "velocity9x/ati_rage2.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

static void make_target(struct v9x_r2_target *target)
{
    memset(target, 0, sizeof(*target));
    target->offset = 0x00200000ul;
    target->pitch_bytes = 128ul;        /* 64 pixels at 16 bpp */
    target->width = 64ul;
    target->height = 64ul;
    target->vram_bytes = 0x00400000ul;
    target->scissor_left = 8ul;
    target->scissor_top = 8ul;
    target->scissor_right = 55ul;
    target->scissor_bottom = 55ul;
}

static void make_trap(struct v9x_r2_flat_trap *trap)
{
    memset(trap, 0, sizeof(*trap));
    trap->x = 16ul;
    trap->y = 16ul;
    trap->length = 8ul;
    trap->trail_x = 32ul;
    trap->lead_err = -8l;
    trap->lead_inc = 0l;
    trap->lead_dec = -16l;
    trap->trail_err = -8l;
    trap->trail_inc = 0l;
    trap->trail_dec = -16l;
    trap->dst_cntl = V9X_M64_DST_X_DIR | V9X_M64_DST_Y_DIR |
                     V9X_R2_DST_Y_MAJOR | V9X_R2_TRAIL_X_DIR |
                     V9X_R2_TRAP_FILL_DIR;
}

static void test_flat_state(void)
{
    struct v9x_r2_target target;
    v9x_u32 offsets[V9X_R2_FLAT_STATE_DWORDS];
    v9x_u32 values[V9X_R2_FLAT_STATE_DWORDS];
    v9x_u32 written = 9ul;

    make_target(&target);
    CHECK(v9x_r2_build_flat_state(&target, 0xf800ul, offsets, values,
                                  V9X_R2_FLAT_STATE_DWORDS - 1ul,
                                  &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    CHECK(v9x_r2_build_flat_state(&target, 0xf800ul, offsets, values,
                                  V9X_R2_FLAT_STATE_DWORDS, &written) ==
          V9X_STATUS_OK);
    CHECK(written == V9X_R2_FLAT_STATE_DWORDS);
    /* The 3D pipe off first: flat colour comes from DP_FRGD_CLR. */
    CHECK(offsets[0] == V9X_M64_SCALE_3D_CNTL && values[0] == 0ul);
    CHECK(offsets[1] == V9X_M64_Z_CNTL && values[1] == 0ul);
    CHECK(offsets[4] == V9X_M64_DP_MIX && values[4] == 0x00070003ul);
    CHECK(offsets[5] == V9X_M64_DP_SRC && values[5] == 0x00000100ul);
    CHECK(offsets[6] == V9X_M64_DP_FRGD_CLR && values[6] == 0xf800ul);
    CHECK(offsets[8] == V9X_M64_DST_OFF_PITCH &&
          values[8] == ((8ul << 22) | (0x00200000ul >> 3)));
    CHECK(offsets[9] == V9X_M64_SC_LEFT_RIGHT &&
          values[9] == ((55ul << 16) | 8ul));
    CHECK(offsets[10] == V9X_M64_SC_TOP_BOTTOM &&
          values[10] == ((55ul << 16) | 8ul));

    /* A scissor outside the target is refused, not clamped. */
    target.scissor_right = 64ul;
    CHECK(v9x_r2_build_flat_state(&target, 0ul, offsets, values,
                                  V9X_R2_FLAT_STATE_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    make_target(&target);
    target.offset = 0x003ff000ul;   /* the target would run past VRAM */
    CHECK(v9x_r2_build_flat_state(&target, 0ul, offsets, values,
                                  V9X_R2_FLAT_STATE_DWORDS, &written) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
}

static void test_trap_encoding(void)
{
    struct v9x_r2_target target;
    struct v9x_r2_flat_trap trap;
    v9x_u32 offsets[V9X_R2_TRAP_DWORDS];
    v9x_u32 values[V9X_R2_TRAP_DWORDS];
    v9x_u32 written = 9ul;

    make_target(&target);
    make_trap(&trap);
    CHECK(v9x_r2_build_trap(&target, &trap, offsets, values,
                            V9X_R2_TRAP_DWORDS, &written) == V9X_STATUS_OK);
    CHECK(written == V9X_R2_TRAP_DWORDS);
    CHECK(offsets[0] == V9X_M64_DST_CNTL &&
          values[0] == (0x3ul | 0x4ul | 0x2000ul | 0x4000ul));
    CHECK(offsets[1] == V9X_M64_DST_Y_X &&
          values[1] == ((16ul << 16) | 16ul));
    /* 18-bit two's complement terms. */
    CHECK(offsets[2] == V9X_M64_DST_BRES_ERR && values[2] == 0x3fff8ul);
    CHECK(offsets[3] == V9X_M64_DST_BRES_INC && values[3] == 0ul);
    CHECK(offsets[4] == V9X_M64_DST_BRES_DEC && values[4] == 0x3fff0ul);
    CHECK(offsets[5] == V9X_R2_TRAIL_BRES_ERR && values[5] == 0x3fff8ul);
    CHECK(offsets[6] == V9X_R2_TRAIL_BRES_INC && values[6] == 0ul);
    CHECK(offsets[7] == V9X_R2_TRAIL_BRES_DEC && values[7] == 0x3fff0ul);
    /* The trigger is last: load TRAIL_X (bit 31), draw (bit 15),
     * TRAIL_X in 28:16, leading-edge length in 14:0 (RRG p.4-46). */
    CHECK(offsets[8] == V9X_R2_DST_BRES_LNTH);
    CHECK(values[8] == (0x80000000ul | 0x8000ul | (32ul << 16) | 8ul));
}

static void test_trap_bounds(void)
{
    struct v9x_r2_target target;
    struct v9x_r2_flat_trap trap;
    v9x_u32 offsets[V9X_R2_TRAP_DWORDS];
    v9x_u32 values[V9X_R2_TRAP_DWORDS];
    v9x_u32 written;

    make_target(&target);
    make_trap(&trap);
    trap.trail_x = 65ul;               /* past the target's border */
    CHECK(v9x_r2_build_trap(&target, &trap, offsets, values,
                            V9X_R2_TRAP_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    make_trap(&trap);
    trap.y = 60ul;                     /* rows would run past the target */
    CHECK(v9x_r2_build_trap(&target, &trap, offsets, values,
                            V9X_R2_TRAP_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    make_trap(&trap);
    trap.length = 0ul;
    CHECK(v9x_r2_build_trap(&target, &trap, offsets, values,
                            V9X_R2_TRAP_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    make_trap(&trap);
    trap.length = V9X_R2_TRAP_LENGTH_MAX + 1ul;
    CHECK(v9x_r2_build_trap(&target, &trap, offsets, values,
                            V9X_R2_TRAP_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    make_trap(&trap);
    trap.lead_dec = -131073l;          /* below the signed 18-bit range */
    CHECK(v9x_r2_build_trap(&target, &trap, offsets, values,
                            V9X_R2_TRAP_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    make_trap(&trap);
    trap.trail_inc = 131072l;          /* above it */
    CHECK(v9x_r2_build_trap(&target, &trap, offsets, values,
                            V9X_R2_TRAP_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    make_trap(&trap);
    trap.dst_cntl |= 0x00000040ul;     /* DST_POLYGON_EN: not a trap bit */
    CHECK(v9x_r2_build_trap(&target, &trap, offsets, values,
                            V9X_R2_TRAP_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    make_trap(&trap);
    CHECK(v9x_r2_build_trap(&target, &trap, offsets, values,
                            V9X_R2_TRAP_DWORDS - 1ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
}

static void test_shade_state(void)
{
    struct v9x_r2_target target;
    struct v9x_r2_shade shade;
    v9x_u32 offsets[V9X_R2_SHADE_STATE_DWORDS];
    v9x_u32 values[V9X_R2_SHADE_STATE_DWORDS];
    v9x_u32 written = 3ul;
    v9x_u32 index;

    make_target(&target);
    memset(&shade, 0, sizeof(shade));
    shade.start[0] = 255l << 16;          /* red 255.0 */
    shade.x_inc[1] = -(8l << 16);         /* green -8.0 per pixel */
    shade.y_inc[2] = 0x00008000l;         /* blue +0.5 per row */
    CHECK(v9x_r2_build_shade_state(&target, &shade, offsets, values,
                                   V9X_R2_SHADE_STATE_DWORDS - 1ul,
                                   &written) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    CHECK(v9x_r2_build_shade_state(&target, &shade, offsets, values,
                                   V9X_R2_SHADE_STATE_DWORDS, &written) ==
          V9X_STATUS_OK);
    CHECK(written == V9X_R2_SHADE_STATE_DWORDS);
    /* SCALE_3D_CNTL with a shading function before any accumulator. */
    CHECK(offsets[0] == V9X_M64_SCALE_3D_CNTL && values[0] == 0xc0ul);
    for (index = 1ul; index < written; ++index) {
        CHECK(offsets[index] != V9X_M64_SCALE_3D_CNTL);
    }
    CHECK(offsets[5] == V9X_M64_DP_SRC && values[5] == 0x00000500ul);
    /* Values in the measured S.8.12 field, bits 24:4. */
    CHECK(offsets[11] == V9X_R2_RED_X_INC && values[11] == 0ul);
    CHECK(offsets[13] == V9X_R2_RED_START && values[13] == 0x00ff0000ul);
    CHECK(offsets[14] == V9X_R2_GREEN_X_INC && values[14] == 0x01f80000ul);
    CHECK(offsets[18] == V9X_R2_BLUE_Y_INC && values[18] == 0x00008000ul);
    /* A start outside 0..255 wraps into the field: the accumulators are
     * modular (measured), so a negative start is encoded, not refused. */
    shade.start[0] = -(8l << 16);
    CHECK(v9x_r2_build_shade_state(&target, &shade, offsets, values,
                                   V9X_R2_SHADE_STATE_DWORDS, &written) ==
          V9X_STATUS_OK);
    CHECK(values[13] == 0x01f80000ul);
}

static void test_z_state(void)
{
    struct v9x_r2_target target;
    struct v9x_r2_depth depth;
    v9x_u32 offsets[V9X_R2_Z_STATE_DWORDS];
    v9x_u32 values[V9X_R2_Z_STATE_DWORDS];
    v9x_u32 written = 3ul;

    make_target(&target);
    memset(&depth, 0, sizeof(depth));
    depth.offset = 0x00210000ul;
    depth.z_cntl = V9X_R2_Z_EN | (1ul << V9X_R2_Z_TEST_SHIFT) |
                   V9X_R2_Z_WRITE;
    depth.start = 0x1234l << 16;
    depth.x_inc = -(0x10l << 16);
    depth.y_inc = 0x8000l;
    CHECK(v9x_r2_build_z_state(&target, &depth, offsets, values,
                               V9X_R2_Z_STATE_DWORDS - 1ul, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(written == 0ul);
    CHECK(v9x_r2_build_z_state(&target, &depth, offsets, values,
                               V9X_R2_Z_STATE_DWORDS, &written) ==
          V9X_STATUS_OK);
    CHECK(written == V9X_R2_Z_STATE_DWORDS);
    /* Same pitch as the colour target, its own offset. */
    CHECK(offsets[0] == V9X_M64_Z_OFF_PITCH &&
          values[0] == ((8ul << 22) | (0x00210000ul >> 3)));
    CHECK(offsets[1] == V9X_M64_Z_CNTL && values[1] == 0x111ul);
    /* S.16.12 in bits 28:0: 16.16 shifted down four. */
    /* -16.0 is -65536 in S.16.12: 0x20000000 - 0x10000. */
    CHECK(offsets[2] == V9X_R2_Z_X_INC && values[2] == 0x1fff0000ul);
    CHECK(offsets[3] == V9X_R2_Z_Y_INC && values[3] == 0x00000800ul);
    CHECK(offsets[4] == V9X_R2_Z_START && values[4] == 0x01234000ul);
    /* A Z surface that would run past VRAM, or an unimplemented
     * Z_CNTL bit, is refused. */
    depth.offset = 0x003ff000ul;
    CHECK(v9x_r2_build_z_state(&target, &depth, offsets, values,
                               V9X_R2_Z_STATE_DWORDS, &written) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
    depth.offset = 0x00210000ul;
    depth.z_cntl |= 0x00000200ul;
    CHECK(v9x_r2_build_z_state(&target, &depth, offsets, values,
                               V9X_R2_Z_STATE_DWORDS, &written) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_rage2_trap_tests(void)
{
    test_z_state();
    test_shade_state();
    test_flat_state();
    test_trap_encoding();
    test_trap_bounds();
    return failures;
}
