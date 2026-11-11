/*
 * The Gen3 fragment-program encoder. See velocity9x\i9xx_fs.h.
 *
 * An arithmetic instruction is three dwords:
 *
 *   A0 = opcode | saturate | dest type, nr | write mask | src0 type, nr
 *   A1 = src0's four channels | src1 type, nr | src1's x and y
 *   A2 = src1's z and w | src2 type, nr | src2's four channels
 *
 * A channel is a three-bit selector with its negate bit above it, four
 * bits a channel, so a source's channels are written by one loop over
 * nibbles rather than twelve named shifts: src0's from A1 bit 16 up,
 * src1's from A1 bit 0 (x, y) and A2 bit 24 (z, w), src2's from A2 bit
 * 0. In each, x is the highest nibble. Those positions are both
 * sources' A1_SRC0_CHANNEL_X_SHIFT 28 down to A2_SRC2_CHANNEL_W_SHIFT 0,
 * and the tests check them against Mesa's numbers.
 *
 * The encoder keeps Mesa's account of texture indirections
 * (i915_fpc_emit.c): the count starts at one; a texture instruction
 * whose address is an R register written in the current phase, or whose
 * destination is oC or oD, starts a new one; every R written takes the
 * current phase. "oC and OD registers can only be used as the
 * destination of a texture instruction once per phase" (both sources)
 * then holds by construction.
 */
#include "velocity9x/i9xx_fs.h"

/* The source packing's per-channel nibble: selector bits 0-2, negate
 * bit 3. */
#define V9X_I9XX_FS_NIBBLE_SEL           7ul
#define V9X_I9XX_FS_NIBBLE_NEGATE        8ul

/* A1 and A2 nibble positions of each source's x (the others follow
 * downward, four bits apart). src1 spans the two words. */
#define V9X_I9XX_FS_A1_SRC0_X_NIBBLE     28u
#define V9X_I9XX_FS_A1_SRC1_X_NIBBLE     4u
#define V9X_I9XX_FS_A2_SRC1_Z_NIBBLE     28u
#define V9X_I9XX_FS_A2_SRC2_X_NIBBLE     12u

/* Write-mask and declaration-mask position, A0_DEST_CHANNEL_SHIFT. */
#define V9X_I9XX_FS_MASK_SHIFT           10u

#define V9X_I9XX_FS_INSN_DWORDS          3ul
#define V9X_I9XX_FS_NO_CONST             0xfffffffful

static v9x_u32 v9x_i9xx_fs_src_type(v9x_u32 src)
{
    return (src >> V9X_I9XX_FS_SRC_TYPE_SHIFT) & 7ul;
}

static v9x_u32 v9x_i9xx_fs_src_nr(v9x_u32 src)
{
    return src & V9X_I9XX_FS_SRC_NR_MASK;
}

static v9x_u32 v9x_i9xx_fs_nibble(v9x_u32 src, v9x_u32 channel)
{
    return (src >> (V9X_I9XX_FS_SRC_CHANNEL_SHIFT + 4u * channel)) & 0xful;
}

v9x_u32 v9x_i9xx_fs_swizzle(v9x_u32 src, v9x_u32 x, v9x_u32 y,
                            v9x_u32 z, v9x_u32 w)
{
    v9x_u32 picks[4];
    v9x_u32 result;
    v9x_u32 channel;

    picks[0] = x;
    picks[1] = y;
    picks[2] = z;
    picks[3] = w;
    result = src & ((1ul << V9X_I9XX_FS_SRC_CHANNEL_SHIFT) - 1ul);

    /* X to W take src's own channel, negate and all; ZERO and ONE are
     * literals and carry no negate. */
    for (channel = 0ul; channel < 4ul; ++channel) {
        v9x_u32 nibble;

        if (picks[channel] <= V9X_I9XX_FS_SEL_W) {
            nibble = v9x_i9xx_fs_nibble(src, picks[channel]);
        } else {
            nibble = picks[channel] & V9X_I9XX_FS_NIBBLE_SEL;
        }
        result |= nibble << (V9X_I9XX_FS_SRC_CHANNEL_SHIFT + 4u * channel);
    }
    return result;
}

v9x_u32 v9x_i9xx_fs_negate(v9x_u32 src, v9x_u32 mask)
{
    v9x_u32 channel;

    for (channel = 0ul; channel < 4ul; ++channel) {
        if ((mask & (1ul << channel)) != 0ul) {
            src ^= V9X_I9XX_FS_NIBBLE_NEGATE <<
                   (V9X_I9XX_FS_SRC_CHANNEL_SHIFT + 4u * channel);
        }
    }
    return src;
}

static void v9x_i9xx_fs_fail(struct v9x_i9xx_fs *fs, v9x_status status)
{
    if (fs->failed == V9X_STATUS_OK) {
        fs->failed = status;
    }
}

void v9x_i9xx_fs_begin(struct v9x_i9xx_fs *fs, v9x_u32 *stream,
                       v9x_u32 capacity)
{
    v9x_u32 index;

    fs->stream = stream;
    fs->capacity = capacity;
    fs->at = 1ul;            /* stream[0] is the header */
    fs->alu = 0ul;
    fs->tex = 0ul;
    fs->dcl = 0ul;
    fs->phases = 1ul;
    for (index = 0ul; index < V9X_I9XX_FS_R_COUNT; ++index) {
        fs->r_phase[index] = 0ul;
    }
    for (index = 0ul; index < V9X_I9XX_FS_U_COUNT; ++index) {
        fs->u_phase[index] = 0ul;
    }
    fs->r_written = 0ul;
    fs->u_written = 0ul;
    fs->t_declared = 0ul;
    fs->s_declared = 0ul;
    fs->body = V9X_FALSE;
    fs->failed = V9X_STATUS_OK;

    if (stream == 0 || capacity < 1ul) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_INVALID_ARGUMENT);
    }
}

/* Room for one more instruction, or the failure recorded. */
static v9x_u16 v9x_i9xx_fs_room(struct v9x_i9xx_fs *fs)
{
    if (fs->failed != V9X_STATUS_OK) {
        return V9X_FALSE;
    }
    if (fs->capacity - fs->at < V9X_I9XX_FS_INSN_DWORDS) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_INSUFFICIENT_MEMORY);
        return V9X_FALSE;
    }
    return V9X_TRUE;
}

static void v9x_i9xx_fs_put(struct v9x_i9xx_fs *fs, v9x_u32 d0,
                            v9x_u32 d1, v9x_u32 d2)
{
    fs->stream[fs->at++] = d0;
    fs->stream[fs->at++] = d1;
    fs->stream[fs->at++] = d2;
}

static void v9x_i9xx_fs_dcl(struct v9x_i9xx_fs *fs, v9x_u32 type,
                            v9x_u32 nr, v9x_u32 flags)
{
    if (fs->body) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_INVALID_ARGUMENT);
        return;
    }
    if (fs->dcl >= V9X_I9XX_FS_MAX_DCL) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_UNSUPPORTED);
        return;
    }
    if (!v9x_i9xx_fs_room(fs)) {
        return;
    }
    v9x_i9xx_fs_put(fs, V9X_I9XX_FS_D0_DCL |
                        (type << V9X_I9XX_FS_TYPE_SHIFT) |
                        (nr << V9X_I9XX_FS_NR_SHIFT) | flags,
                    0ul, 0ul);
    ++fs->dcl;
}

void v9x_i9xx_fs_dcl_t(struct v9x_i9xx_fs *fs, v9x_u32 nr, v9x_u32 mask)
{
    if (fs->failed != V9X_STATUS_OK) {
        return;
    }

    /* The erratum both sources record: a T declaration may be x, xy,
     * xyz, w or xyzw, and no other mask. */
    if (nr >= V9X_I9XX_FS_T_COUNT ||
        (mask != V9X_I9XX_FS_MASK_X &&
         mask != (V9X_I9XX_FS_MASK_X | V9X_I9XX_FS_MASK_Y) &&
         mask != V9X_I9XX_FS_MASK_XYZ && mask != V9X_I9XX_FS_MASK_W &&
         mask != V9X_I9XX_FS_MASK_XYZW)) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_INVALID_ARGUMENT);
        return;
    }
    if ((fs->t_declared & (1ul << nr)) != 0ul) {
        return;
    }
    v9x_i9xx_fs_dcl(fs, V9X_I9XX_FS_REG_TYPE_T, nr,
                    mask << V9X_I9XX_FS_MASK_SHIFT);
    if (fs->failed == V9X_STATUS_OK) {
        fs->t_declared |= 1ul << nr;
    }
}

void v9x_i9xx_fs_dcl_s(struct v9x_i9xx_fs *fs, v9x_u32 nr,
                       v9x_u32 sample_type)
{
    if (fs->failed != V9X_STATUS_OK) {
        return;
    }
    if (nr >= V9X_I9XX_FS_S_COUNT ||
        (sample_type != V9X_I9XX_FS_D0_SAMPLE_2D &&
         sample_type != V9X_I9XX_FS_D0_SAMPLE_CUBE &&
         sample_type != V9X_I9XX_FS_D0_SAMPLE_VOLUME)) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_INVALID_ARGUMENT);
        return;
    }
    if ((fs->s_declared & (1ul << nr)) != 0ul) {
        return;
    }

    /* A sampler declaration carries no channel mask: see
     * i9xx_fragprog.c's sampling program. */
    v9x_i9xx_fs_dcl(fs, V9X_I9XX_FS_REG_TYPE_S, nr, sample_type);
    if (fs->failed == V9X_STATUS_OK) {
        fs->s_declared |= 1ul << nr;
    }
}

/* May `type`, `nr` be read now? T declared; R written; U written in
 * this phase; any constant. */
static v9x_u16 v9x_i9xx_fs_readable(const struct v9x_i9xx_fs *fs,
                                    v9x_u32 type, v9x_u32 nr)
{
    if (type == V9X_I9XX_FS_REG_TYPE_T) {
        return (v9x_u16)(nr < V9X_I9XX_FS_T_COUNT &&
                         (fs->t_declared & (1ul << nr)) != 0ul);
    }
    if (type == V9X_I9XX_FS_REG_TYPE_R) {
        return (v9x_u16)(nr < V9X_I9XX_FS_R_COUNT &&
                         (fs->r_written & (1ul << nr)) != 0ul);
    }
    if (type == V9X_I9XX_FS_REG_TYPE_U) {
        return (v9x_u16)(nr < V9X_I9XX_FS_U_COUNT &&
                         (fs->u_written & (1ul << nr)) != 0ul &&
                         fs->u_phase[nr] == fs->phases);
    }
    if (type == V9X_I9XX_FS_REG_TYPE_CONST) {
        return (v9x_u16)(nr < V9X_I9XX_FS_CONST_COUNT);
    }
    return V9X_FALSE;
}

/* A destination: R, U, oC or oD. Records the write. */
static v9x_u16 v9x_i9xx_fs_writable(const struct v9x_i9xx_fs *fs,
                                    v9x_u32 type, v9x_u32 nr)
{
    (void)fs;
    if (type == V9X_I9XX_FS_REG_TYPE_R) {
        return (v9x_u16)(nr < V9X_I9XX_FS_R_COUNT);
    }
    if (type == V9X_I9XX_FS_REG_TYPE_U) {
        return (v9x_u16)(nr < V9X_I9XX_FS_U_COUNT);
    }
    if (type == V9X_I9XX_FS_REG_TYPE_OC || type == V9X_I9XX_FS_REG_TYPE_OD) {
        return (v9x_u16)(nr == 0ul);
    }
    return V9X_FALSE;
}

static void v9x_i9xx_fs_wrote(struct v9x_i9xx_fs *fs, v9x_u32 type,
                              v9x_u32 nr)
{
    if (type == V9X_I9XX_FS_REG_TYPE_R) {
        fs->r_written |= 1ul << nr;
        fs->r_phase[nr] = fs->phases;
    } else if (type == V9X_I9XX_FS_REG_TYPE_U) {
        fs->u_written |= 1ul << nr;
        fs->u_phase[nr] = fs->phases;
    }
}

/* How many sources each arithmetic opcode reads, 0 for one this encoder
 * does not emit. */
static v9x_u32 v9x_i9xx_fs_operands(v9x_u32 opcode)
{
    if (opcode == V9X_I9XX_FS_A0_MAD || opcode == V9X_I9XX_FS_A0_DP2ADD ||
        opcode == V9X_I9XX_FS_A0_CMP) {
        return 3ul;
    }
    if (opcode == V9X_I9XX_FS_A0_ADD || opcode == V9X_I9XX_FS_A0_MUL ||
        opcode == V9X_I9XX_FS_A0_DP3 || opcode == V9X_I9XX_FS_A0_DP4 ||
        opcode == V9X_I9XX_FS_A0_MIN || opcode == V9X_I9XX_FS_A0_MAX ||
        opcode == V9X_I9XX_FS_A0_SGE || opcode == V9X_I9XX_FS_A0_SLT) {
        return 2ul;
    }
    if (opcode == V9X_I9XX_FS_A0_MOV || opcode == V9X_I9XX_FS_A0_FRC ||
        opcode == V9X_I9XX_FS_A0_RCP || opcode == V9X_I9XX_FS_A0_RSQ ||
        opcode == V9X_I9XX_FS_A0_EXP || opcode == V9X_I9XX_FS_A0_LOG ||
        opcode == V9X_I9XX_FS_A0_FLR) {
        return 1ul;
    }
    return 0ul;
}

/* One channel, negate included, in all four. */
static v9x_u16 v9x_i9xx_fs_replicated(v9x_u32 src)
{
    v9x_u32 first = v9x_i9xx_fs_nibble(src, 0ul);

    return (v9x_u16)(v9x_i9xx_fs_nibble(src, 1ul) == first &&
                     v9x_i9xx_fs_nibble(src, 2ul) == first &&
                     v9x_i9xx_fs_nibble(src, 3ul) == first);
}

/* A source's four nibbles packed x highest, as A1 holds src0 and A2
 * src2. */
static v9x_u32 v9x_i9xx_fs_channels(v9x_u32 src)
{
    return (v9x_i9xx_fs_nibble(src, 0ul) << 12) |
           (v9x_i9xx_fs_nibble(src, 1ul) << 8) |
           (v9x_i9xx_fs_nibble(src, 2ul) << 4) |
           v9x_i9xx_fs_nibble(src, 3ul);
}

void v9x_i9xx_fs_alu(struct v9x_i9xx_fs *fs, v9x_u32 opcode,
                     v9x_u32 dest_type, v9x_u32 dest_nr, v9x_u32 mask,
                     v9x_u32 saturate,
                     v9x_u32 src0, v9x_u32 src1, v9x_u32 src2)
{
    v9x_u32 operands = v9x_i9xx_fs_operands(opcode);
    v9x_u32 srcs[3];
    v9x_u32 const_nr = V9X_I9XX_FS_NO_CONST;
    v9x_u32 index;
    v9x_u32 a0;
    v9x_u32 a1;
    v9x_u32 a2;
    v9x_u32 src1_channels;

    if (fs->failed != V9X_STATUS_OK) {
        return;
    }
    srcs[0] = src0;
    srcs[1] = src1;
    srcs[2] = src2;

    if (operands == 0ul || !v9x_i9xx_fs_writable(fs, dest_type, dest_nr) ||
        mask == 0ul || mask > V9X_I9XX_FS_MASK_XYZW) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_INVALID_ARGUMENT);
        return;
    }

    for (index = 0ul; index < operands; ++index) {
        v9x_u32 type = v9x_i9xx_fs_src_type(srcs[index]);
        v9x_u32 nr = v9x_i9xx_fs_src_nr(srcs[index]);

        if (!v9x_i9xx_fs_readable(fs, type, nr)) {
            v9x_i9xx_fs_fail(fs, V9X_STATUS_INVALID_ARGUMENT);
            return;
        }

        /* One constant register an instruction, which several sources
         * may share (Mesa moves any other into a temporary first; the
         * translator does that here). */
        if (type == V9X_I9XX_FS_REG_TYPE_CONST) {
            if (const_nr != V9X_I9XX_FS_NO_CONST && const_nr != nr) {
                v9x_i9xx_fs_fail(fs, V9X_STATUS_UNSUPPORTED);
                return;
            }
            const_nr = nr;
        }
    }

    /* The scalar operations, and DP2ADD's addend: which channel the
     * hardware reads is not known, so the source must be one channel
     * in all four, and either reading gives the same answer. */
    if (((opcode == V9X_I9XX_FS_A0_RCP || opcode == V9X_I9XX_FS_A0_RSQ ||
          opcode == V9X_I9XX_FS_A0_EXP || opcode == V9X_I9XX_FS_A0_LOG) &&
         !v9x_i9xx_fs_replicated(src0)) ||
        (opcode == V9X_I9XX_FS_A0_DP2ADD && !v9x_i9xx_fs_replicated(src2))) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_INVALID_ARGUMENT);
        return;
    }

    if (fs->alu >= V9X_I9XX_FS_MAX_ALU) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_UNSUPPORTED);
        return;
    }
    if (!v9x_i9xx_fs_room(fs)) {
        return;
    }

    a0 = opcode |
         (saturate ? V9X_I9XX_FS_A0_SATURATE : 0ul) |
         (dest_type << V9X_I9XX_FS_TYPE_SHIFT) |
         (dest_nr << V9X_I9XX_FS_NR_SHIFT) |
         (mask << V9X_I9XX_FS_MASK_SHIFT) |
         (v9x_i9xx_fs_src_type(src0) << V9X_I9XX_FS_A0_SRC0_TYPE_SHIFT) |
         (v9x_i9xx_fs_src_nr(src0) << V9X_I9XX_FS_A0_SRC0_NR_SHIFT);
    a1 = v9x_i9xx_fs_channels(src0) << (V9X_I9XX_FS_A1_SRC0_X_NIBBLE - 12u);
    a2 = 0ul;

    /* src1's x and y in A1's low byte, z and w in A2's top byte. */
    if (operands >= 2ul) {
        src1_channels = v9x_i9xx_fs_channels(src1);
        a1 |= (v9x_i9xx_fs_src_type(src1) << V9X_I9XX_FS_A1_SRC1_TYPE_SHIFT) |
              (v9x_i9xx_fs_src_nr(src1) << V9X_I9XX_FS_A1_SRC1_NR_SHIFT) |
              ((src1_channels >> 8) << (V9X_I9XX_FS_A1_SRC1_X_NIBBLE - 4u));
        a2 |= (src1_channels & 0xfful) <<
              (V9X_I9XX_FS_A2_SRC1_Z_NIBBLE - 4u);
    }
    if (operands >= 3ul) {
        a2 |= (v9x_i9xx_fs_src_type(src2) << V9X_I9XX_FS_A2_SRC2_TYPE_SHIFT) |
              (v9x_i9xx_fs_src_nr(src2) << V9X_I9XX_FS_A2_SRC2_NR_SHIFT) |
              (v9x_i9xx_fs_channels(src2) <<
                   (V9X_I9XX_FS_A2_SRC2_X_NIBBLE - 12u));
    }

    v9x_i9xx_fs_put(fs, a0, a1, a2);
    v9x_i9xx_fs_wrote(fs, dest_type, dest_nr);
    ++fs->alu;
    fs->body = V9X_TRUE;
}

void v9x_i9xx_fs_tex(struct v9x_i9xx_fs *fs, v9x_u32 opcode,
                     v9x_u32 dest_type, v9x_u32 dest_nr, v9x_u32 sampler,
                     v9x_u32 addr_type, v9x_u32 addr_nr)
{
    v9x_u32 phases;

    if (fs->failed != V9X_STATUS_OK) {
        return;
    }
    if ((opcode != V9X_I9XX_T0_TEXLD && opcode != V9X_I9XX_T0_TEXLDP &&
         opcode != V9X_I9XX_T0_TEXLDB && opcode != V9X_I9XX_T0_TEXKILL) ||
        !v9x_i9xx_fs_writable(fs, dest_type, dest_nr) ||
        (addr_type != V9X_I9XX_FS_REG_TYPE_T &&
         addr_type != V9X_I9XX_FS_REG_TYPE_R &&
         addr_type != V9X_I9XX_FS_REG_TYPE_U) ||
        !v9x_i9xx_fs_readable(fs, addr_type, addr_nr)) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_INVALID_ARGUMENT);
        return;
    }

    /* "This field ignored for TEXKILL" (both sources): no sampler to
     * declare, and it is encoded zero. */
    if (opcode == V9X_I9XX_T0_TEXKILL) {
        sampler = 0ul;
    } else if (sampler >= V9X_I9XX_FS_S_COUNT ||
               (fs->s_declared & (1ul << sampler)) == 0ul) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_INVALID_ARGUMENT);
        return;
    }

    phases = fs->phases;
    if (dest_type == V9X_I9XX_FS_REG_TYPE_OC ||
        dest_type == V9X_I9XX_FS_REG_TYPE_OD) {
        ++phases;
    }
    if (addr_type == V9X_I9XX_FS_REG_TYPE_R &&
        fs->r_phase[addr_nr] == phases) {
        ++phases;
    }
    if (phases > V9X_I9XX_FS_MAX_PHASES || fs->tex >= V9X_I9XX_FS_MAX_TEX) {
        v9x_i9xx_fs_fail(fs, V9X_STATUS_UNSUPPORTED);
        return;
    }
    if (!v9x_i9xx_fs_room(fs)) {
        return;
    }
    fs->phases = phases;

    v9x_i9xx_fs_put(fs, opcode |
                        (dest_type << V9X_I9XX_T0_DEST_TYPE_SHIFT) |
                        (dest_nr << V9X_I9XX_T0_DEST_NR_SHIFT) |
                        (sampler << V9X_I9XX_T0_SAMPLER_NR_SHIFT),
                    (addr_type << V9X_I9XX_T1_ADDR_TYPE_SHIFT) |
                    (addr_nr << V9X_I9XX_T1_ADDR_NR_SHIFT),
                    0ul);
    v9x_i9xx_fs_wrote(fs, dest_type, dest_nr);
    ++fs->tex;
    fs->body = V9X_TRUE;
}

v9x_status v9x_i9xx_fs_end(struct v9x_i9xx_fs *fs, v9x_u32 *written)
{
    if (written == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    *written = 0ul;
    if (fs->failed != V9X_STATUS_OK) {
        return fs->failed;
    }
    if (fs->at < 1ul + V9X_I9XX_FS_INSN_DWORDS) {
        return V9X_STATUS_INVALID_STATE;
    }

    /* Length is the payload less one, as every packet in this stream. */
    fs->stream[0] = V9X_I9XX_3DSTATE_PIXEL_SHADER | (fs->at - 2ul);
    *written = fs->at;
    return V9X_STATUS_OK;
}
