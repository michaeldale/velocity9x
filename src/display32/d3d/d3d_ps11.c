/*
 * The PS 1.0/1.1 parser; include\velocity9x\ps11.h says what it accepts.
 * Every rule is ddi8-on-win9x.md section 6's; held to
 * tests\host\test_d3d_ps11.c.
 */
#include "velocity9x/ps11.h"

/* Version, end and comment tokens, d3d8types.h. */
#define V9X_PS11_VERSION_1_0  0xffff0100ul
#define V9X_PS11_VERSION_1_1  0xffff0101ul
#define V9X_PS11_TOKEN_END    0x0000fffful
#define V9X_PS11_TOKEN_COMMENT 0xfffeul
#define V9X_PS11_COISSUE      0x40000000ul

/* d3d8types.h opcodes. */
#define V9X_PS11_D3D_NOP          0ul
#define V9X_PS11_D3D_MOV          1ul
#define V9X_PS11_D3D_ADD          2ul
#define V9X_PS11_D3D_SUB          3ul
#define V9X_PS11_D3D_MAD          4ul
#define V9X_PS11_D3D_MUL          5ul
#define V9X_PS11_D3D_DP3          8ul
#define V9X_PS11_D3D_LRP          18ul
#define V9X_PS11_D3D_TEXCOORD     64ul
#define V9X_PS11_D3D_TEXKILL      65ul
#define V9X_PS11_D3D_TEX          66ul
#define V9X_PS11_D3D_TEXBEM       67ul
#define V9X_PS11_D3D_TEXBEML      68ul
#define V9X_PS11_D3D_TEXM3X3PAD   73ul
#define V9X_PS11_D3D_TEXM3X3TEX   74ul
#define V9X_PS11_D3D_TEXM3X3SPEC  76ul
#define V9X_PS11_D3D_TEXM3X3VSPEC 77ul
#define V9X_PS11_D3D_CND          80ul
#define V9X_PS11_D3D_DEF          81ul

/* Swizzles 1.1 allows: identity, alpha replicate, blue replicate (into
 * alpha only). */
#define V9X_PS11_SWIZZLE_XYZW 0xe4u
#define V9X_PS11_SWIZZLE_AAAA 0xffu
#define V9X_PS11_SWIZZLE_BBBB 0xaau

/* Write masks 1.1 allows. */
#define V9X_PS11_MASK_ALL 0xfu
#define V9X_PS11_MASK_RGB 0x7u
#define V9X_PS11_MASK_A   0x8u

/* The shift field's values: 1 _x2, 2 _x4, 15 _d2. */
#define V9X_PS11_SHIFT_D2 15ul

/* An opcode's operation and source count, or 0 for one not accepted. */
static int v9x_ps11_op(v9x_u32 d3d, v9x_u8 *op, v9x_u8 *sources)
{
    switch (d3d) {
    case V9X_PS11_D3D_NOP:  *op = V9X_PS11_NOP;  *sources = 0u; return 1;
    case V9X_PS11_D3D_MOV:  *op = V9X_PS11_MOV;  *sources = 1u; return 1;
    case V9X_PS11_D3D_ADD:  *op = V9X_PS11_ADD;  *sources = 2u; return 1;
    case V9X_PS11_D3D_SUB:  *op = V9X_PS11_SUB;  *sources = 2u; return 1;
    case V9X_PS11_D3D_MUL:  *op = V9X_PS11_MUL;  *sources = 2u; return 1;
    case V9X_PS11_D3D_MAD:  *op = V9X_PS11_MAD;  *sources = 3u; return 1;
    case V9X_PS11_D3D_LRP:  *op = V9X_PS11_LRP;  *sources = 3u; return 1;
    case V9X_PS11_D3D_DP3:  *op = V9X_PS11_DP3;  *sources = 2u; return 1;
    case V9X_PS11_D3D_CND:  *op = V9X_PS11_CND;  *sources = 3u; return 1;
    case V9X_PS11_D3D_TEX:      *op = V9X_PS11_TEX;      *sources = 0u;
        return 1;
    case V9X_PS11_D3D_TEXCOORD: *op = V9X_PS11_TEXCOORD; *sources = 0u;
        return 1;
    case V9X_PS11_D3D_TEXKILL:  *op = V9X_PS11_TEXKILL;  *sources = 0u;
        return 1;
    case V9X_PS11_D3D_TEXBEM:   *op = V9X_PS11_TEXBEM;   *sources = 1u;
        return 1;
    case V9X_PS11_D3D_TEXBEML:  *op = V9X_PS11_TEXBEML;  *sources = 1u;
        return 1;
    case V9X_PS11_D3D_TEXM3X3PAD:   *op = V9X_PS11_TEXM3X3PAD;
        *sources = 1u; return 1;
    case V9X_PS11_D3D_TEXM3X3TEX:   *op = V9X_PS11_TEXM3X3TEX;
        *sources = 1u; return 1;
    case V9X_PS11_D3D_TEXM3X3SPEC:  *op = V9X_PS11_TEXM3X3SPEC;
        *sources = 2u; return 1;
    case V9X_PS11_D3D_TEXM3X3VSPEC: *op = V9X_PS11_TEXM3X3VSPEC;
        *sources = 1u; return 1;
    default:
        return 0;
    }
}

/* A parameter token's register file (bits 30:28, and 12:11 above them). */
static v9x_u32 v9x_ps11_file(v9x_u32 token)
{
    return ((token >> 28) & 0x7ul) | ((token >> 8) & 0x18ul);
}

/* Whether reg is a register of file in 1.1. */
static int v9x_ps11_register(v9x_u32 file, v9x_u32 reg)
{
    switch (file) {
    case V9X_PS11_FILE_TEMP:    return reg < 2ul;
    case V9X_PS11_FILE_INPUT:   return reg < 2ul;
    case V9X_PS11_FILE_CONST:   return reg < 8ul;
    case V9X_PS11_FILE_TEXTURE: return reg < 4ul;
    default:                    return 0;
    }
}

v9x_u32 v9x_ps11_parse(const v9x_u32 *tokens, v9x_u32 dwords,
                       V9X_PS11_PROGRAM *program, v9x_u32 *where)
{
    v9x_u8 *bytes = (v9x_u8 *)program;
    v9x_u32 at = 1ul;
    v9x_u32 index;
    v9x_u32 texture_ops = 0ul;
    v9x_u32 arithmetic_ops = 0ul;
    int arithmetic_seen = 0;
    /* The texm3x3 sequence in progress: pads seen, their first stage and
     * their source. */
    v9x_u32 pads = 0ul;
    v9x_u32 pad_stage = 0ul;
    v9x_u32 pad_source = 0ul;

    for (index = 0ul; index < (v9x_u32)sizeof(*program); ++index) {
        bytes[index] = 0u;
    }
    *where = 0ul;
    if (dwords == 0ul) {
        return V9X_PS11_MALFORMED;
    }
    if (tokens[0] != V9X_PS11_VERSION_1_0 &&
        tokens[0] != V9X_PS11_VERSION_1_1) {
        return V9X_PS11_VERSION;
    }
    program->version = tokens[0] & 0xfffful;

    for (;;) {
        V9X_PS11_INST *inst;
        v9x_u32 token;
        v9x_u32 d3d;
        v9x_u32 dst;
        v9x_u32 s;

        *where = at;
        if (at >= dwords) {
            return V9X_PS11_MALFORMED;
        }
        token = tokens[at];
        if (token == V9X_PS11_TOKEN_END) {
            break;
        }
        d3d = token & 0xfffful;
        if (d3d == V9X_PS11_TOKEN_COMMENT) {
            v9x_u32 length = (token >> 16) & 0x7ffful;

            if (length > dwords - at - 1ul) {
                return V9X_PS11_MALFORMED;
            }
            at += 1ul + length;
            continue;
        }

        /* def c_k, four floats: the shader's own constant. */
        if (d3d == V9X_PS11_D3D_DEF) {
            v9x_u32 reg;

            if (dwords - at < 6ul) {
                return V9X_PS11_MALFORMED;
            }
            reg = tokens[at + 1ul] & 0x7fful;
            if (v9x_ps11_file(tokens[at + 1ul]) != V9X_PS11_FILE_CONST ||
                reg >= 8ul) {
                return V9X_PS11_INVALID;
            }
            for (index = 0ul; index < 4ul; ++index) {
                program->def[reg][index] = tokens[at + 2ul + index];
            }
            program->def_mask |= 1ul << reg;
            at += 6ul;
            continue;
        }

        if (program->count >= V9X_PS11_MAX_INSTRUCTIONS) {
            return V9X_PS11_INVALID;
        }
        inst = &program->inst[program->count];
        if (!v9x_ps11_op(d3d, &inst->op, &inst->sources)) {
            return V9X_PS11_UNSUPPORTED;
        }
        inst->coissue = (token & V9X_PS11_COISSUE) != 0ul ? 1u : 0u;
        if (inst->op == V9X_PS11_NOP) {
            ++program->count;
            ++at;
            continue;
        }
        if (dwords - at < 2ul + (v9x_u32)inst->sources) {
            return V9X_PS11_MALFORMED;
        }

        /* The destination: file, register, mask, _sat, shift. */
        dst = tokens[at + 1ul];
        inst->dst_file = (v9x_u8)v9x_ps11_file(dst);
        inst->dst_reg = (v9x_u8)(dst & 0x7fful);
        inst->dst_mask = (v9x_u8)((dst >> 16) & 0xful);
        inst->saturate = (v9x_u8)((dst >> 20) & 0xful);
        switch ((dst >> 24) & 0xful) {
        case 0ul:                inst->shift = 0l;  break;
        case 1ul:                inst->shift = 1l;  break;
        case 2ul:                inst->shift = 2l;  break;
        case V9X_PS11_SHIFT_D2:  inst->shift = -1l; break;
        default:
            return V9X_PS11_INVALID;
        }
        if (inst->saturate > 1u ||
            !v9x_ps11_register(inst->dst_file, inst->dst_reg) ||
            (inst->dst_file != V9X_PS11_FILE_TEMP &&
             inst->dst_file != V9X_PS11_FILE_TEXTURE)) {
            return V9X_PS11_INVALID;
        }

        /* The sources. */
        for (s = 0ul; s < inst->sources; ++s) {
            v9x_u32 src = tokens[at + 2ul + s];
            V9X_PS11_SRC *operand = &inst->src[s];

            operand->file = (v9x_u8)v9x_ps11_file(src);
            operand->reg = (v9x_u8)(src & 0x7fful);
            operand->swizzle = (v9x_u8)((src >> 16) & 0xfful);
            operand->mod = (v9x_u8)((src >> 24) & 0xful);
            if (!v9x_ps11_register(operand->file, operand->reg) ||
                operand->mod > V9X_PS11_SRC_COMP) {
                return V9X_PS11_INVALID;
            }
            if (operand->file == V9X_PS11_FILE_INPUT) {
                program->inputs |= 1ul << operand->reg;
            }
            if (operand->swizzle != V9X_PS11_SWIZZLE_XYZW &&
                operand->swizzle != V9X_PS11_SWIZZLE_AAAA &&
                !(operand->swizzle == V9X_PS11_SWIZZLE_BBBB &&
                  inst->dst_mask == V9X_PS11_MASK_A)) {
                return V9X_PS11_INVALID;
            }
        }

        if (V9X_PS11_IS_TEXTURE(inst->op)) {
            v9x_u32 stage = inst->dst_reg;

            /* Texture instructions: before any arithmetic, writing a
             * texture register whole, sourcing an earlier one. */
            if (arithmetic_seen || inst->coissue != 0u ||
                inst->dst_file != V9X_PS11_FILE_TEXTURE ||
                inst->dst_mask != V9X_PS11_MASK_ALL ||
                inst->saturate != 0u || inst->shift != 0l ||
                ++texture_ops > V9X_PS11_MAX_TEXTURE) {
                return V9X_PS11_INVALID;
            }
            if (inst->sources != 0u &&
                (inst->src[0].file != V9X_PS11_FILE_TEXTURE ||
                 inst->src[0].reg >= stage)) {
                return V9X_PS11_INVALID;
            }
            if (inst->op == V9X_PS11_TEXM3X3SPEC &&
                inst->src[1].file != V9X_PS11_FILE_CONST) {
                return V9X_PS11_INVALID;
            }
            switch (inst->op) {
            case V9X_PS11_TEX:
                program->samples |= 1ul << stage;
                program->coords |= 1ul << stage;
                break;
            case V9X_PS11_TEXCOORD:
                program->coords |= 1ul << stage;
                break;
            case V9X_PS11_TEXKILL:
                program->coords |= 1ul << stage;
                program->has_kill = 1ul;
                break;
            case V9X_PS11_TEXBEM:
            case V9X_PS11_TEXBEML:
                program->samples |= 1ul << stage;
                program->coords |= 1ul << stage;
                break;
            case V9X_PS11_TEXM3X3PAD:
                /* Two pads, on consecutive stages with one source, then
                 * the lookup on the third. */
                if (pads == 0ul) {
                    pad_stage = stage;
                    pad_source = inst->src[0].reg;
                } else if (pads != 1ul || stage != pad_stage + 1ul ||
                           inst->src[0].reg != pad_source) {
                    return V9X_PS11_INVALID;
                }
                ++pads;
                program->coords |= 1ul << stage;
                break;
            default:
                if (pads != 2ul || stage != pad_stage + 2ul ||
                    inst->src[0].reg != pad_source) {
                    return V9X_PS11_INVALID;
                }
                pads = 0ul;
                program->samples |= 1ul << stage;
                program->coords |= 1ul << stage;
                break;
            }
            if (inst->op != V9X_PS11_TEXM3X3PAD && pads != 0ul) {
                return V9X_PS11_INVALID;
            }
        } else {
            arithmetic_seen = 1;
            if (pads != 0ul) {
                return V9X_PS11_INVALID;
            }
            if (inst->dst_mask != V9X_PS11_MASK_ALL &&
                inst->dst_mask != V9X_PS11_MASK_RGB &&
                inst->dst_mask != V9X_PS11_MASK_A) {
                return V9X_PS11_INVALID;
            }
            if (inst->op == V9X_PS11_DP3 &&
                inst->dst_mask == V9X_PS11_MASK_A) {
                return V9X_PS11_INVALID;
            }
            /* cnd in 1.1 tests r0.a alone. */
            if (inst->op == V9X_PS11_CND &&
                (inst->src[0].file != V9X_PS11_FILE_TEMP ||
                 inst->src[0].reg != 0u ||
                 inst->src[0].swizzle != V9X_PS11_SWIZZLE_AAAA)) {
                return V9X_PS11_INVALID;
            }
            /* A co-issued pair is one .rgb and one .a, one slot. */
            if (inst->coissue != 0u) {
                const V9X_PS11_INST *first;

                if (program->count == 0ul) {
                    return V9X_PS11_INVALID;
                }
                first = &program->inst[program->count - 1ul];
                if (V9X_PS11_IS_TEXTURE(first->op) ||
                    first->op == V9X_PS11_NOP || first->coissue != 0u ||
                    (first->dst_mask | inst->dst_mask) != V9X_PS11_MASK_ALL ||
                    (first->dst_mask & inst->dst_mask) != 0u) {
                    return V9X_PS11_INVALID;
                }
            } else if (++arithmetic_ops > V9X_PS11_MAX_ARITHMETIC) {
                return V9X_PS11_INVALID;
            }
        }
        ++program->count;
        at += 2ul + inst->sources;
    }
    if (pads != 0ul) {
        return V9X_PS11_INVALID;
    }
    *where = at;
    return V9X_PS11_OK;
}
