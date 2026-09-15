/*
 * The fragment program: declare the interpolated diffuse colour, move it to
 * the output colour. Two instructions, six dwords, plus the program header.
 *
 * It takes no arguments because it is a constant, and it is a constant because
 * Phase 6 forbids a shader compiler. Every field placement comes from
 * docs\decisions\2026-09-14-intel-gen3-3d-packet-audit.md section 7, where the
 * encoding is double-sourced by two independently written emitters - xf86's
 * macros expanding fields inline, and Mesa's lowering from its own UREG form
 * through helpers with entirely different shift constants.
 *
 * The dwords this produces are recorded in the audit as DERIVED and
 * UNVALIDATED. This unit and its host test are what validation means: the
 * audit wrote down what the field placements imply, and the test asserts that
 * this builder produces exactly that. Neither has been near the hardware.
 */
#include "velocity9x/intel_gen3_3d.h"

/* One header dword plus two three-dword instructions. */
#define V9X_I9XX_FRAGPROG_BODY_DWORDS  6ul
#define V9X_I9XX_FRAGPROG_DWORDS       (V9X_I9XX_FRAGPROG_BODY_DWORDS + 1ul)

v9x_u32 v9x_i9xx_fragment_program_extent(void)
{
    return V9X_I9XX_FRAGPROG_DWORDS;
}

v9x_status v9x_i9xx_build_fragment_program(
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 ||
        capacity < V9X_I9XX_FRAGPROG_DWORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /*
     * Header. The length field is (payload dwords - 1), the same convention as
     * every other packet in this stream, derived independently in both trees.
     */
    stream[at++] = V9X_I9XX_3DSTATE_PIXEL_SHADER |
                   (V9X_I9XX_FRAGPROG_BODY_DWORDS - 1ul);

    /*
     * dcl T8 (diffuse), all four channels.
     *
     * The channel mask is not free choice: the source records an erratum that
     * only (x), (xy), (xyz), (w) and (xyzw) are legal for a T declaration, and
     * that (xz), (xw) and (xzw) are forbidden for diffuse or specular. All
     * four is one of the legal five.
     */
    stream[at++] = V9X_I9XX_FS_D0_DCL |
                   (V9X_I9XX_FS_REG_TYPE_T << V9X_I9XX_FS_TYPE_SHIFT) |
                   (V9X_I9XX_FS_T_DIFFUSE << V9X_I9XX_FS_NR_SHIFT) |
                   V9X_I9XX_FS_CHANNEL_ALL;
    stream[at++] = 0ul;   /* D1, must be zero */
    stream[at++] = 0ul;   /* D2, must be zero */

    /* mov oC, T8.xyzw */
    stream[at++] = V9X_I9XX_FS_A0_MOV |
                   (V9X_I9XX_FS_REG_TYPE_OC << V9X_I9XX_FS_TYPE_SHIFT) |
                   (0ul << V9X_I9XX_FS_NR_SHIFT) |
                   V9X_I9XX_FS_CHANNEL_ALL |
                   (V9X_I9XX_FS_REG_TYPE_T <<
                        V9X_I9XX_FS_A0_SRC0_TYPE_SHIFT) |
                   (V9X_I9XX_FS_T_DIFFUSE <<
                        V9X_I9XX_FS_A0_SRC0_NR_SHIFT);
    stream[at++] = V9X_I9XX_FS_A1_SWIZZLE_XYZW;
    stream[at++] = 0ul;   /* A2: no src1, no src2 */

    *written = at;
    return V9X_STATUS_OK;
}
