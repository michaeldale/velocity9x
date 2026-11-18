/*
 * An AtomBIOS command-table interpreter. velocity9x\atombios.h says what it
 * does, what it refuses and what checked it. The operand rules follow
 * Linux's atom.c: a source or destination is one of eight kinds (register,
 * parameter, workspace, FB scratch, data-table, immediate, PLL, MC) with an
 * alignment (a dword, one of three words, one of four bytes) in the
 * attribute byte; a destination's alignment is mapped through
 * v9x_atom_dst_to_src, and a partial write keeps the rest of the old value.
 */
#include "velocity9x/atombios.h"

#define V9X_ATOM_ARG_REG 0u
#define V9X_ATOM_ARG_PS  1u
#define V9X_ATOM_ARG_WS  2u
#define V9X_ATOM_ARG_FB  3u
#define V9X_ATOM_ARG_ID  4u
#define V9X_ATOM_ARG_IMM 5u
#define V9X_ATOM_ARG_PLL 6u
#define V9X_ATOM_ARG_MC  7u

#define V9X_ATOM_SRC_DWORD 0u

/* Workspace indices with a meaning of their own (atom.h ATOM_WS_*). */
#define V9X_ATOM_WS_QUOTIENT   0x40u
#define V9X_ATOM_WS_REMAINDER  0x41u
#define V9X_ATOM_WS_DATAPTR    0x42u
#define V9X_ATOM_WS_SHIFT      0x43u
#define V9X_ATOM_WS_OR_MASK    0x44u
#define V9X_ATOM_WS_AND_MASK   0x45u
#define V9X_ATOM_WS_FB_WINDOW  0x46u
#define V9X_ATOM_WS_ATTRIBUTES 0x47u
#define V9X_ATOM_WS_REGPTR     0x48u

/* A command table: its size, then at 4 its workspace dwords and at 5 its
 * parameter bytes (low seven bits), code from 6. */
#define V9X_ATOM_CT_WS_PTR    4u
#define V9X_ATOM_CT_PS_PTR    5u
#define V9X_ATOM_CT_PS_MASK   0x7fu
#define V9X_ATOM_CT_CODE_PTR  6u

#define V9X_ATOM_CASE_MAGIC   0x63u
#define V9X_ATOM_CASE_END     0x5a5au

static const v9x_u32 v9x_atom_arg_mask[8] = {
    0xfffffffful, 0x0000fffful, 0x00ffff00ul, 0xffff0000ul,
    0x000000fful, 0x0000ff00ul, 0x00ff0000ul, 0xff000000ul
};
static const v9x_u8 v9x_atom_arg_shift[8] = { 0u, 0u, 8u, 16u, 0u, 8u, 16u, 24u };
static const v9x_u8 v9x_atom_dst_to_src[8][4] = {
    { 0u, 0u, 0u, 0u }, { 1u, 2u, 3u, 0u }, { 1u, 2u, 3u, 0u },
    { 1u, 2u, 3u, 0u }, { 4u, 5u, 6u, 7u }, { 4u, 5u, 6u, 7u },
    { 4u, 5u, 6u, 7u }, { 4u, 5u, 6u, 7u }
};
static const v9x_u8 v9x_atom_def_dst[8] = { 0u, 0u, 1u, 2u, 0u, 1u, 2u, 3u };

/* One table's run: its base, where its parameters start in the caller's
 * array, its workspace in the shared pool, and the compare flags. */
struct v9x_atom_frame {
    v9x_u32 start;
    v9x_u32 *ps;
    v9x_u32 ps_dwords;
    v9x_u32 ps_shift;
    v9x_u32 ps_bytes;
    v9x_u32 *ws;
    v9x_u32 ws_dwords;
    int cs_equal;
    int cs_above;
    v9x_status status;
};

v9x_u32 v9x_atom_u8(const struct v9x_atom *atom, v9x_u32 offset)
{
    if (atom == 0 || offset >= atom->rom_bytes) {
        return 0ul;
    }
    return (v9x_u32)atom->rom[offset];
}

v9x_u32 v9x_atom_u16(const struct v9x_atom *atom, v9x_u32 offset)
{
    return v9x_atom_u8(atom, offset) | (v9x_atom_u8(atom, offset + 1ul) << 8);
}

v9x_u32 v9x_atom_u32(const struct v9x_atom *atom, v9x_u32 offset)
{
    return v9x_atom_u16(atom, offset) |
           (v9x_atom_u16(atom, offset + 2ul) << 16);
}

v9x_status v9x_atom_init(struct v9x_atom *atom, const v9x_u8 *rom,
                         v9x_u32 rom_bytes, const struct v9x_atom_io *io)
{
    v9x_u32 header;
    v9x_u32 index;

    if (atom == 0 || rom == 0 || io == 0 || io->read == 0 ||
        io->write == 0 || io->delay_us == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < (v9x_u32)sizeof(*atom); ++index) {
        ((v9x_u8 *)atom)[index] = 0u;
    }
    atom->rom = rom;
    atom->rom_bytes = rom_bytes;
    atom->io = io;
    atom->step_limit = V9X_ATOM_STEP_LIMIT;
    if (rom_bytes < 0x100ul || rom[0] != 0x55u || rom[1] != 0xaau) {
        return V9X_STATUS_UNSUPPORTED;
    }
    header = v9x_atom_u16(atom, V9X_ATOM_ROM_HEADER_POINTER);
    if (header == 0ul || header + 0x22ul > rom_bytes ||
        v9x_atom_u32(atom, header + 4ul) != V9X_ATOM_ROM_SIGNATURE) {
        return V9X_STATUS_UNSUPPORTED;
    }
    atom->command_list = v9x_atom_u16(atom, header + 0x1eul);
    atom->data_list = v9x_atom_u16(atom, header + 0x20ul);
    if (atom->command_list == 0ul || atom->data_list == 0ul ||
        atom->command_list >= rom_bytes || atom->data_list >= rom_bytes) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}

/* A master list's entry, 0 when past the list or the image. */
static v9x_u32 v9x_atom_list_entry(const struct v9x_atom *atom, v9x_u32 list,
                                   v9x_u32 index)
{
    v9x_u32 entries = (v9x_atom_u16(atom, list) - 4ul) / 2ul;
    v9x_u32 offset;

    if (v9x_atom_u16(atom, list) < 4ul || index >= entries) {
        return 0ul;
    }
    offset = v9x_atom_u16(atom, list + 4ul + index * 2ul);
    return offset < atom->rom_bytes ? offset : 0ul;
}

v9x_u32 v9x_atom_data_table(const struct v9x_atom *atom, v9x_u32 index)
{
    if (atom == 0) {
        return 0ul;
    }
    return v9x_atom_list_entry(atom, atom->data_list, index);
}

/* --- operands ------------------------------------------------------------ */

static void v9x_atom_fail(struct v9x_atom_frame *frame, v9x_status status)
{
    if (frame->status == V9X_STATUS_OK) {
        frame->status = status;
    }
}

static v9x_u32 v9x_atom_fetch8(struct v9x_atom *atom, v9x_u32 *at)
{
    v9x_u32 value = v9x_atom_u8(atom, *at);

    *at += 1ul;
    return value;
}

static v9x_u32 v9x_atom_fetch16(struct v9x_atom *atom, v9x_u32 *at)
{
    v9x_u32 value = v9x_atom_u16(atom, *at);

    *at += 2ul;
    return value;
}

static v9x_u32 v9x_atom_fetch32(struct v9x_atom *atom, v9x_u32 *at)
{
    v9x_u32 value = v9x_atom_u32(atom, *at);

    *at += 4ul;
    return value;
}

/* A register's byte offset: the dword index plus the register block. */
static v9x_u32 v9x_atom_reg_offset(struct v9x_atom *atom, v9x_u32 index)
{
    return ((index + atom->reg_block) & 0x3ffffffful) * 4ul;
}

/* Reads a source operand of kind attr & 7 at *at. `saved` gets the whole
 * value before masking (the immediate's own value for an immediate). */
static v9x_u32 v9x_atom_get_src_int(struct v9x_atom *atom,
                                    struct v9x_atom_frame *frame,
                                    v9x_u32 attr, v9x_u32 *at,
                                    v9x_u32 *saved)
{
    v9x_u32 arg = attr & 7ul;
    v9x_u32 align = (attr >> 3) & 7ul;
    v9x_u32 index;
    v9x_u32 value = 0ul;

    switch (arg) {
    case V9X_ATOM_ARG_REG:
        index = v9x_atom_fetch16(atom, at);
        if (atom->io_mode != 0ul) {
            v9x_atom_fail(frame, V9X_STATUS_UNSUPPORTED);
            break;
        }
        value = atom->io->read(atom->io->context,
                               v9x_atom_reg_offset(atom, index));
        break;
    case V9X_ATOM_ARG_PS:
        index = v9x_atom_fetch8(atom, at) + frame->ps_shift;
        if (index >= frame->ps_dwords) {
            v9x_atom_fail(frame, V9X_STATUS_INVALID_ARGUMENT);
            break;
        }
        value = frame->ps[index];
        break;
    case V9X_ATOM_ARG_WS:
        index = v9x_atom_fetch8(atom, at);
        switch (index) {
        case V9X_ATOM_WS_QUOTIENT:   value = atom->divmul[0]; break;
        case V9X_ATOM_WS_REMAINDER:  value = atom->divmul[1]; break;
        case V9X_ATOM_WS_DATAPTR:    value = atom->data_block; break;
        case V9X_ATOM_WS_SHIFT:      value = atom->shift; break;
        case V9X_ATOM_WS_OR_MASK:
            value = (atom->shift < 32ul) ? (1ul << atom->shift) : 0ul;
            break;
        case V9X_ATOM_WS_AND_MASK:
            value = (atom->shift < 32ul) ? ~(1ul << atom->shift) :
                                           0xfffffffful;
            break;
        case V9X_ATOM_WS_FB_WINDOW:  value = atom->fb_base; break;
        case V9X_ATOM_WS_ATTRIBUTES: value = atom->io_attr; break;
        case V9X_ATOM_WS_REGPTR:     value = atom->reg_block; break;
        default:
            if (index >= frame->ws_dwords) {
                v9x_atom_fail(frame, V9X_STATUS_INVALID_ARGUMENT);
                break;
            }
            value = frame->ws[index];
            break;
        }
        break;
    case V9X_ATOM_ARG_FB:
        index = v9x_atom_fetch8(atom, at) + atom->fb_base / 4ul;
        if (index >= V9X_ATOM_SCRATCH_DWORDS) {
            v9x_atom_fail(frame, V9X_STATUS_UNSUPPORTED);
            break;
        }
        value = atom->scratch[index];
        break;
    case V9X_ATOM_ARG_ID:
        index = v9x_atom_fetch16(atom, at);
        if (index + atom->data_block + 4ul > atom->rom_bytes) {
            v9x_atom_fail(frame, V9X_STATUS_INVALID_ARGUMENT);
            break;
        }
        value = v9x_atom_u32(atom, index + atom->data_block);
        break;
    case V9X_ATOM_ARG_IMM:
        if (align == V9X_ATOM_SRC_DWORD) {
            value = v9x_atom_fetch32(atom, at);
        } else if (align <= 3ul) {
            value = v9x_atom_fetch16(atom, at);
        } else {
            value = v9x_atom_fetch8(atom, at);
        }
        if (saved != 0) {
            *saved = value;
        }
        return value;
    default:
        /* PLL and MC: indirect spaces this interpreter does not reach. */
        (void)v9x_atom_fetch8(atom, at);
        v9x_atom_fail(frame, V9X_STATUS_UNSUPPORTED);
        break;
    }
    if (saved != 0) {
        *saved = value;
    }
    return (value & v9x_atom_arg_mask[align]) >> v9x_atom_arg_shift[align];
}

static v9x_u32 v9x_atom_get_src(struct v9x_atom *atom,
                                struct v9x_atom_frame *frame, v9x_u32 attr,
                                v9x_u32 *at)
{
    return v9x_atom_get_src_int(atom, frame, attr, at, 0);
}

/* A destination read: its own kind, the alignment mapped from the
 * attribute's destination fields. */
static v9x_u32 v9x_atom_get_dst(struct v9x_atom *atom,
                                struct v9x_atom_frame *frame, v9x_u32 arg,
                                v9x_u32 attr, v9x_u32 *at, v9x_u32 *saved)
{
    return v9x_atom_get_src_int(
        atom, frame,
        arg | ((v9x_u32)v9x_atom_dst_to_src[(attr >> 3) & 7ul]
                                           [(attr >> 6) & 3ul] << 3),
        at, saved);
}

static void v9x_atom_skip_dst(struct v9x_atom *atom, v9x_u32 arg,
                              v9x_u32 *at)
{
    (void)atom;
    *at += (arg == V9X_ATOM_ARG_REG || arg == V9X_ATOM_ARG_ID) ? 2ul : 1ul;
}

/* An immediate of the attribute's alignment, unmasked (MASK's mask). */
static v9x_u32 v9x_atom_get_src_direct(struct v9x_atom *atom, v9x_u32 align,
                                       v9x_u32 *at)
{
    if (align == V9X_ATOM_SRC_DWORD) {
        return v9x_atom_fetch32(atom, at);
    }
    if (align <= 3ul) {
        return v9x_atom_fetch16(atom, at);
    }
    return v9x_atom_fetch8(atom, at);
}

/* Writes value into the destination at *at, merged into `saved` (the old
 * whole value) outside the destination's field. */
static void v9x_atom_put_dst(struct v9x_atom *atom,
                             struct v9x_atom_frame *frame, v9x_u32 arg,
                             v9x_u32 attr, v9x_u32 *at, v9x_u32 value,
                             v9x_u32 saved)
{
    v9x_u32 align = v9x_atom_dst_to_src[(attr >> 3) & 7ul][(attr >> 6) & 3ul];
    v9x_u32 index;

    value = (value << v9x_atom_arg_shift[align]) & v9x_atom_arg_mask[align];
    value |= saved & ~v9x_atom_arg_mask[align];
    value &= 0xfffffffful;
    switch (arg) {
    case V9X_ATOM_ARG_REG:
        index = v9x_atom_fetch16(atom, at);
        if (atom->io_mode != 0ul) {
            v9x_atom_fail(frame, V9X_STATUS_UNSUPPORTED);
            return;
        }
        atom->io->write(atom->io->context, v9x_atom_reg_offset(atom, index),
                        value);
        return;
    case V9X_ATOM_ARG_PS:
        index = v9x_atom_fetch8(atom, at) + frame->ps_shift;
        if (index >= frame->ps_dwords) {
            v9x_atom_fail(frame, V9X_STATUS_INVALID_ARGUMENT);
            return;
        }
        frame->ps[index] = value;
        return;
    case V9X_ATOM_ARG_WS:
        index = v9x_atom_fetch8(atom, at);
        switch (index) {
        case V9X_ATOM_WS_QUOTIENT:   atom->divmul[0] = value; return;
        case V9X_ATOM_WS_REMAINDER:  atom->divmul[1] = value; return;
        case V9X_ATOM_WS_DATAPTR:    atom->data_block = value; return;
        case V9X_ATOM_WS_SHIFT:      atom->shift = value; return;
        case V9X_ATOM_WS_OR_MASK:
        case V9X_ATOM_WS_AND_MASK:   return;
        case V9X_ATOM_WS_FB_WINDOW:  atom->fb_base = value; return;
        case V9X_ATOM_WS_ATTRIBUTES: atom->io_attr = value; return;
        case V9X_ATOM_WS_REGPTR:     atom->reg_block = value; return;
        default:
            if (index >= frame->ws_dwords) {
                v9x_atom_fail(frame, V9X_STATUS_INVALID_ARGUMENT);
                return;
            }
            frame->ws[index] = value;
            return;
        }
    case V9X_ATOM_ARG_FB:
        index = v9x_atom_fetch8(atom, at) + atom->fb_base / 4ul;
        if (index >= V9X_ATOM_SCRATCH_DWORDS) {
            v9x_atom_fail(frame, V9X_STATUS_UNSUPPORTED);
            return;
        }
        atom->scratch[index] = value;
        return;
    default:
        (void)v9x_atom_fetch8(atom, at);
        v9x_atom_fail(frame, V9X_STATUS_UNSUPPORTED);
        return;
    }
}

/* --- opcodes ------------------------------------------------------------- */

#define V9X_ATOM_OP_AND 0u
#define V9X_ATOM_OP_OR  1u
#define V9X_ATOM_OP_ADD 2u
#define V9X_ATOM_OP_SUB 3u
#define V9X_ATOM_OP_XOR 4u

/* dst = dst op src, written back. */
static void v9x_atom_binop(struct v9x_atom *atom, struct v9x_atom_frame *frame,
                           v9x_u32 arg, v9x_u32 *at, v9x_u32 op)
{
    v9x_u32 attr = v9x_atom_fetch8(atom, at);
    v9x_u32 dptr = *at;
    v9x_u32 saved = 0ul;
    v9x_u32 dst = v9x_atom_get_dst(atom, frame, arg, attr, at, &saved);
    v9x_u32 src = v9x_atom_get_src(atom, frame, attr, at);

    switch (op) {
    case V9X_ATOM_OP_AND: dst &= src; break;
    case V9X_ATOM_OP_OR:  dst |= src; break;
    case V9X_ATOM_OP_ADD: dst += src; break;
    case V9X_ATOM_OP_SUB: dst -= src; break;
    default:              dst ^= src; break;
    }
    v9x_atom_put_dst(atom, frame, arg, attr, &dptr, dst & 0xfffffffful,
                     saved);
}

static v9x_status v9x_atom_run(struct v9x_atom *atom, v9x_u32 index,
                               v9x_u32 *params, v9x_u32 param_dwords,
                               v9x_u32 ps_shift);

/* One opcode at *at. Returns 0 at the end of the table. */
static int v9x_atom_step(struct v9x_atom *atom, struct v9x_atom_frame *frame,
                         v9x_u32 op, v9x_u32 *at)
{
    v9x_u32 arg;
    v9x_u32 attr;
    v9x_u32 dptr;
    v9x_u32 saved;
    v9x_u32 dst;
    v9x_u32 src;
    v9x_u32 value;
    v9x_u32 target;
    int take;

    if (op >= 1ul && op <= 6ul) {
        /* MOVE: a partial destination keeps the rest of its old value. */
        arg = op - 1ul;
        attr = v9x_atom_fetch8(atom, at);
        dptr = *at;
        saved = 0ul;
        if (((attr >> 3) & 7ul) != V9X_ATOM_SRC_DWORD) {
            (void)v9x_atom_get_dst(atom, frame, arg, attr, at, &saved);
        } else {
            v9x_atom_skip_dst(atom, arg, at);
        }
        src = v9x_atom_get_src(atom, frame, attr, at);
        v9x_atom_put_dst(atom, frame, arg, attr, &dptr, src, saved);
    } else if (op >= 7ul && op <= 12ul) {
        v9x_atom_binop(atom, frame, op - 7ul, at, V9X_ATOM_OP_AND);
    } else if (op >= 13ul && op <= 18ul) {
        v9x_atom_binop(atom, frame, op - 13ul, at, V9X_ATOM_OP_OR);
    } else if (op >= 19ul && op <= 30ul) {
        /* SHIFT_LEFT / SHIFT_RIGHT by an immediate byte, in the
         * destination's default alignment. */
        arg = (op - 19ul) % 6ul;
        attr = v9x_atom_fetch8(atom, at);
        dptr = *at;
        attr &= 0x38ul;
        attr |= (v9x_u32)v9x_atom_def_dst[attr >> 3] << 6;
        saved = 0ul;
        dst = v9x_atom_get_dst(atom, frame, arg, attr, at, &saved);
        value = v9x_atom_fetch8(atom, at);
        if (value >= 32ul) {
            dst = 0ul;
        } else {
            dst = op < 25ul ? (dst << value) : (dst >> value);
        }
        v9x_atom_put_dst(atom, frame, arg, attr, &dptr, dst & 0xfffffffful,
                         saved);
    } else if (op >= 31ul && op <= 42ul) {
        /* MUL into the quotient; DIV into quotient and remainder. */
        arg = (op - 31ul) % 6ul;
        attr = v9x_atom_fetch8(atom, at);
        dst = v9x_atom_get_dst(atom, frame, arg, attr, at, 0);
        src = v9x_atom_get_src(atom, frame, attr, at);
        if (op <= 36ul) {
            atom->divmul[0] = (dst * src) & 0xfffffffful;
        } else if (src != 0ul) {
            atom->divmul[0] = dst / src;
            atom->divmul[1] = dst % src;
        } else {
            atom->divmul[0] = 0ul;
            atom->divmul[1] = 0ul;
        }
    } else if (op >= 43ul && op <= 48ul) {
        v9x_atom_binop(atom, frame, op - 43ul, at, V9X_ATOM_OP_ADD);
    } else if (op >= 49ul && op <= 54ul) {
        v9x_atom_binop(atom, frame, op - 49ul, at, V9X_ATOM_OP_SUB);
    } else if (op == 55ul) {
        /* SET_ATI_PORT: 0 is memory-mapped registers, anything else the
         * indirect I/O programs, which are not supported. */
        atom->io_mode = v9x_atom_fetch16(atom, at);
    } else if (op == 56ul || op == 57ul) {
        /* SET_PCI_PORT, SET_SYSIO_PORT. */
        *at += 1ul;
        atom->io_mode = op == 56ul ? 0x100ul : 0x200ul;
    } else if (op == 58ul) {
        atom->reg_block = v9x_atom_fetch16(atom, at);
    } else if (op == 59ul) {
        attr = v9x_atom_fetch8(atom, at);
        atom->fb_base = v9x_atom_get_src(atom, frame, attr, at);
    } else if (op >= 60ul && op <= 65ul) {
        arg = op - 60ul;
        attr = v9x_atom_fetch8(atom, at);
        dst = v9x_atom_get_dst(atom, frame, arg, attr, at, 0);
        src = v9x_atom_get_src(atom, frame, attr, at);
        frame->cs_equal = dst == src;
        frame->cs_above = dst > src;
    } else if (op == 66ul) {
        /* SWITCH: cases of the source's alignment, each with a target. */
        attr = v9x_atom_fetch8(atom, at);
        src = v9x_atom_get_src(atom, frame, attr, at);
        while (v9x_atom_u16(atom, *at) != V9X_ATOM_CASE_END) {
            if (v9x_atom_u8(atom, *at) != V9X_ATOM_CASE_MAGIC ||
                *at >= atom->rom_bytes) {
                v9x_atom_fail(frame, V9X_STATUS_INVALID_ARGUMENT);
                return 0;
            }
            *at += 1ul;
            value = v9x_atom_get_src(atom, frame,
                                     (attr & 0x38ul) | V9X_ATOM_ARG_IMM, at);
            target = v9x_atom_fetch16(atom, at);
            if (value == src) {
                *at = frame->start + target;
                return 1;
            }
        }
        *at += 2ul;
    } else if (op >= 67ul && op <= 73ul) {
        target = v9x_atom_fetch16(atom, at);
        switch (op) {
        case 67ul: take = 1; break;
        case 68ul: take = frame->cs_equal; break;
        case 69ul: take = !(frame->cs_above || frame->cs_equal); break;
        case 70ul: take = frame->cs_above; break;
        case 71ul: take = !frame->cs_above; break;
        case 72ul: take = frame->cs_above || frame->cs_equal; break;
        default:   take = !frame->cs_equal; break;
        }
        if (take) {
            *at = frame->start + target;
        }
    } else if (op >= 74ul && op <= 79ul) {
        arg = op - 74ul;
        attr = v9x_atom_fetch8(atom, at);
        dst = v9x_atom_get_dst(atom, frame, arg, attr, at, 0);
        src = v9x_atom_get_src(atom, frame, attr, at);
        frame->cs_equal = (dst & src) == 0ul;
    } else if (op == 80ul || op == 81ul) {
        value = v9x_atom_fetch8(atom, at);
        atom->io->delay_us(atom->io->context,
                           op == 80ul ? value * 1000ul : value);
    } else if (op == 82ul) {
        value = v9x_atom_fetch8(atom, at);
        if (frame->status == V9X_STATUS_OK) {
            frame->status = v9x_atom_run(atom, value, frame->ps,
                                         frame->ps_dwords,
                                         frame->ps_shift +
                                             frame->ps_bytes / 4ul);
        }
    } else if (op >= 84ul && op <= 89ul) {
        /* CLEAR: the destination's field to 0. */
        arg = op - 84ul;
        attr = v9x_atom_fetch8(atom, at);
        dptr = *at;
        attr &= 0x38ul;
        attr |= (v9x_u32)v9x_atom_def_dst[attr >> 3] << 6;
        saved = 0ul;
        (void)v9x_atom_get_dst(atom, frame, arg, attr, at, &saved);
        v9x_atom_put_dst(atom, frame, arg, attr, &dptr, 0ul, saved);
    } else if (op == 90ul) {
        /* NOP */
    } else if (op == 91ul) {
        return 0;   /* EOT */
    } else if (op >= 92ul && op <= 97ul) {
        /* MASK: dst = (dst & immediate mask) | src. */
        arg = op - 92ul;
        attr = v9x_atom_fetch8(atom, at);
        dptr = *at;
        saved = 0ul;
        dst = v9x_atom_get_dst(atom, frame, arg, attr, at, &saved);
        value = v9x_atom_get_src_direct(atom, (attr >> 3) & 7ul, at);
        src = v9x_atom_get_src(atom, frame, attr, at);
        v9x_atom_put_dst(atom, frame, arg, attr, &dptr, (dst & value) | src,
                         saved);
    } else if (op == 98ul) {
        *at += 1ul;     /* POST_CARD */
    } else if (op == 99ul) {
        /* BEEP */
    } else if (op == 102ul) {
        value = v9x_atom_fetch8(atom, at);
        if (value == 0ul) {
            atom->data_block = 0ul;
        } else if (value == 255ul) {
            atom->data_block = frame->start;
        } else {
            atom->data_block = v9x_atom_list_entry(atom, atom->data_list,
                                                   value);
        }
    } else if (op >= 103ul && op <= 108ul) {
        v9x_atom_binop(atom, frame, op - 103ul, at, V9X_ATOM_OP_XOR);
    } else if (op >= 109ul && op <= 120ul) {
        /* SHL / SHR by a source operand, on the whole value, then the
         * destination's field taken out of it. */
        arg = (op - 109ul) % 6ul;
        attr = v9x_atom_fetch8(atom, at);
        dptr = *at;
        value = v9x_atom_dst_to_src[(attr >> 3) & 7ul][(attr >> 6) & 3ul];
        saved = 0ul;
        (void)v9x_atom_get_dst(atom, frame, arg, attr, at, &saved);
        dst = saved;
        src = v9x_atom_get_src(atom, frame, attr, at);
        if (src >= 32ul) {
            dst = 0ul;
        } else {
            dst = op < 115ul ? (dst << src) : (dst >> src);
        }
        dst = (dst & v9x_atom_arg_mask[value]) >> v9x_atom_arg_shift[value];
        v9x_atom_put_dst(atom, frame, arg, attr, &dptr, dst & 0xfffffffful,
                         saved);
    } else if (op == 121ul) {
        *at += 1ul;     /* DEBUG */
    } else if (op == 122ul) {
        /* PROCESS_DS: skip the data block that follows. */
        value = v9x_atom_u16(atom, *at);
        *at += 4ul + value;
    } else {
        /* REPEAT, SAVEREG, RESTOREREG, 0, and anything newer. */
        atom->fault_opcode = op;
        atom->fault_offset = *at - 1ul;
        v9x_atom_fail(frame, V9X_STATUS_UNSUPPORTED);
        return 0;
    }
    return 1;
}

static v9x_status v9x_atom_run(struct v9x_atom *atom, v9x_u32 index,
                               v9x_u32 *params, v9x_u32 param_dwords,
                               v9x_u32 ps_shift)
{
    struct v9x_atom_frame frame;
    v9x_u32 base = v9x_atom_list_entry(atom, atom->command_list, index);
    v9x_u32 size;
    v9x_u32 at;
    v9x_u32 op = 0ul;
    v9x_u32 ws_base = atom->ws_used;
    v9x_u32 i;

    if (base == 0ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    size = v9x_atom_u16(atom, base);
    if (size < V9X_ATOM_CT_CODE_PTR || base + size > atom->rom_bytes ||
        atom->depth >= V9X_ATOM_DEPTH_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    frame.start = base;
    frame.ps = params;
    frame.ps_dwords = param_dwords;
    frame.ps_shift = ps_shift;
    frame.ps_bytes = v9x_atom_u8(atom, base + V9X_ATOM_CT_PS_PTR) &
                     V9X_ATOM_CT_PS_MASK;
    frame.ws_dwords = v9x_atom_u8(atom, base + V9X_ATOM_CT_WS_PTR);
    if (ws_base + frame.ws_dwords > V9X_ATOM_WS_DWORDS) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    frame.ws = &atom->ws[ws_base];
    for (i = 0ul; i < frame.ws_dwords; ++i) {
        frame.ws[i] = 0ul;
    }
    frame.cs_equal = 0;
    frame.cs_above = 0;
    frame.status = V9X_STATUS_OK;
    atom->ws_used = ws_base + frame.ws_dwords;
    ++atom->depth;

    at = base + V9X_ATOM_CT_CODE_PTR;
    for (;;) {
        if (++atom->steps > atom->step_limit) {
            frame.status = V9X_STATUS_TIMEOUT;
            atom->fault_offset = at;
            break;
        }
        if (at < base || at >= base + size) {
            frame.status = V9X_STATUS_INVALID_ARGUMENT;
            atom->fault_offset = at;
            break;
        }
        op = v9x_atom_fetch8(atom, &at);
        if (!v9x_atom_step(atom, &frame, op, &at) ||
            frame.status != V9X_STATUS_OK) {
            break;
        }
    }
    if (frame.status != V9X_STATUS_OK && atom->fault_offset == 0ul) {
        atom->fault_opcode = op;
        atom->fault_offset = at;
    }
    --atom->depth;
    atom->ws_used = ws_base;
    return frame.status;
}

v9x_status v9x_atom_execute(struct v9x_atom *atom, v9x_u32 index,
                            v9x_u32 *params, v9x_u32 param_dwords)
{
    if (atom == 0 || atom->rom == 0 || params == 0 || param_dwords == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /* A top-level run starts from atom.c's reset state. */
    atom->steps = 0ul;
    atom->depth = 0ul;
    atom->ws_used = 0ul;
    atom->io_mode = 0ul;
    atom->data_block = 0ul;
    atom->reg_block = 0ul;
    atom->fb_base = 0ul;
    atom->divmul[0] = 0ul;
    atom->divmul[1] = 0ul;
    atom->fault_opcode = 0ul;
    atom->fault_offset = 0ul;
    return v9x_atom_run(atom, index, params, param_dwords, 0ul);
}
