/*
 * Raw Deflate decoding, written from RFC 1951. See
 * include\velocity9x\inflate.h for what it is for and what it promises.
 *
 * No C runtime: these modules link into tools built -zl with no default
 * libraries, so copies are byte loops and nothing here calls out.
 */
#include "velocity9x/inflate.h"

/* RFC 1951 3.2.3: BTYPE values. */
#define V9X_INFLATE_BTYPE_STORED  ((v9x_u32)0ul)
#define V9X_INFLATE_BTYPE_FIXED   ((v9x_u32)1ul)
#define V9X_INFLATE_BTYPE_DYNAMIC ((v9x_u32)2ul)

/* RFC 1951 3.2.5: literal bytes, end of block, first length symbol, and the
 * number of length and distance symbols that may appear in data. */
#define V9X_INFLATE_END_OF_BLOCK    ((v9x_u16)256u)
#define V9X_INFLATE_FIRST_LENGTH    ((v9x_u16)257u)
#define V9X_INFLATE_LENGTH_CODES    ((v9x_u16)29u)
#define V9X_INFLATE_DISTANCE_CODES  ((v9x_u16)30u)

/* RFC 1951 3.2.7: dynamic header field ranges. HLIT encodes 257-288 but only
 * 257-286 is legal; HDIST encodes 1-32, all legal. */
#define V9X_INFLATE_HLIT_BASE  ((v9x_u16)257u)
#define V9X_INFLATE_HLIT_MAX   ((v9x_u16)286u)
#define V9X_INFLATE_HDIST_BASE ((v9x_u16)1u)
#define V9X_INFLATE_HCLEN_BASE ((v9x_u16)4u)

/* RFC 1951 3.2.7: code length alphabet symbols 16-18 (repeat previous, short
 * zero run, long zero run). */
#define V9X_INFLATE_CLEN_REPEAT     ((v9x_u16)16u)
#define V9X_INFLATE_CLEN_ZEROS      ((v9x_u16)17u)

/* RFC 1951 3.2.5, the length table: base length for symbols 257-285 and the
 * extra bits that follow each. */
static const v9x_u16 v9x_inflate_length_base[29] = {
    3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 13u, 15u, 17u, 19u, 23u, 27u, 31u,
    35u, 43u, 51u, 59u, 67u, 83u, 99u, 115u, 131u, 163u, 195u, 227u, 258u
};
static const v9x_u8 v9x_inflate_length_extra[29] = {
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 1u, 2u, 2u, 2u, 2u,
    3u, 3u, 3u, 3u, 4u, 4u, 4u, 4u, 5u, 5u, 5u, 5u, 0u
};

/* RFC 1951 3.2.5, the distance table for symbols 0-29. The largest, 24577
 * plus 13 extra bits, reaches exactly 32768. */
static const v9x_u16 v9x_inflate_distance_base[30] = {
    1u, 2u, 3u, 4u, 5u, 7u, 9u, 13u, 17u, 25u, 33u, 49u, 65u, 97u, 129u,
    193u, 257u, 385u, 513u, 769u, 1025u, 1537u, 2049u, 3073u, 4097u, 6145u,
    8193u, 12289u, 16385u, 24577u
};
static const v9x_u8 v9x_inflate_distance_extra[30] = {
    0u, 0u, 0u, 0u, 1u, 1u, 2u, 2u, 3u, 3u, 4u, 4u, 5u, 5u, 6u,
    6u, 7u, 7u, 8u, 8u, 9u, 9u, 10u, 10u, 11u, 11u, 12u, 12u, 13u, 13u
};

/* RFC 1951 3.2.7: the order the code length code lengths are transmitted. */
static const v9x_u8 v9x_inflate_clen_order[19] = {
    16u, 17u, 18u, 0u, 8u, 7u, 9u, 6u, 10u, 5u, 11u, 4u, 12u, 3u, 13u, 2u,
    14u, 1u, 15u
};

/*
 * Take count bits (0-16), least significant first (RFC 1951 3.1.1).
 *
 * A byte is fetched only while fewer bits are buffered than asked for, so
 * after any call fewer than 8 bits remain, and those are always the unread
 * high bits of the last byte fetched. The stored-block path relies on that:
 * discarding the buffer is exactly "skip to the next byte boundary".
 */
static v9x_u16 v9x_inflate_bits(struct v9x_inflate_state *state,
                                v9x_u16 count,
                                v9x_u32 *value)
{
    while (state->bit_count < count) {
        if (state->input_position >= state->input_length) {
            return V9X_INFLATE_TRUNCATED;
        }
        state->bit_buffer |=
            (v9x_u32)state->input[state->input_position] << state->bit_count;
        ++state->input_position;
        state->bit_count = (v9x_u16)(state->bit_count + 8u);
    }

    *value = state->bit_buffer & (((v9x_u32)1ul << count) - 1ul);
    state->bit_buffer >>= count;
    state->bit_count = (v9x_u16)(state->bit_count - count);
    return V9X_INFLATE_OK;
}

/*
 * Turn a list of code lengths into a canonical code (RFC 1951 3.2.2).
 *
 * The Kraft sum is checked as it is built: starting from one unit of code
 * space, each length doubles what is left and each code of that length uses
 * one. Going negative is an over-subscribed set, which would make some bit
 * patterns ambiguous; ending positive is an incomplete set, which leaves bit
 * patterns that decode to nothing. Both are refused (RFC 1951 3.2.7 requires
 * the code lengths to describe a complete code), with the one exception the
 * RFC makes for distances: a single distance code of length one, or none at
 * all for a block of only literals. allow_single says this is that alphabet.
 */
static v9x_u16 v9x_inflate_build(struct v9x_inflate_code *code,
                                 const v9x_u8 *lengths,
                                 v9x_u16 symbols,
                                 v9x_u16 allow_single)
{
    v9x_u16 offset[V9X_INFLATE_MAX_BITS + 1];
    v9x_s32 left;
    v9x_u16 length;
    v9x_u16 symbol;
    v9x_u16 used;

    for (length = 0u; length <= V9X_INFLATE_MAX_BITS; ++length) {
        code->count[length] = 0u;
    }
    for (symbol = 0u; symbol < symbols; ++symbol) {
        if (lengths[symbol] > V9X_INFLATE_MAX_BITS) {
            return V9X_INFLATE_BAD_LENGTHS;
        }
        ++code->count[lengths[symbol]];
    }

    left = 1l;
    for (length = 1u; length <= V9X_INFLATE_MAX_BITS; ++length) {
        left = left * 2l - (v9x_s32)code->count[length];
        if (left < 0l) {
            return V9X_INFLATE_BAD_LENGTHS;
        }
    }

    used = (v9x_u16)(symbols - code->count[0]);
    if (left > 0l) {
        if (allow_single != V9X_TRUE) {
            return V9X_INFLATE_BAD_LENGTHS;
        }
        if (used > 1u || (used == 1u && code->count[1] != 1u)) {
            return V9X_INFLATE_BAD_LENGTHS;
        }
    }

    /* Symbols sorted by code length, then by value within a length, which is
     * the canonical code order of 3.2.2 step 3. */
    offset[1] = 0u;
    for (length = 1u; length < V9X_INFLATE_MAX_BITS; ++length) {
        offset[length + 1u] = (v9x_u16)(offset[length] + code->count[length]);
    }
    for (symbol = 0u; symbol < symbols; ++symbol) {
        length = lengths[symbol];
        if (length != 0u) {
            code->symbol[offset[length]] = symbol;
            ++offset[length];
        }
    }
    return V9X_INFLATE_OK;
}

/*
 * Read one symbol of a canonical code, one bit at a time.
 *
 * Huffman codes are packed most significant bit first (RFC 1951 3.1.1), so
 * the code grows left to right. At each length the codes of that length are
 * the consecutive values first .. first + count - 1 (3.2.2 step 2's
 * next_code), and the symbol is found by its offset into that run. A pattern
 * still unmatched after 15 bits is not in the code - possible only for the
 * incomplete single-distance-code case, or an empty distance code.
 */
static v9x_u16 v9x_inflate_decode(struct v9x_inflate_state *state,
                                  const struct v9x_inflate_code *code,
                                  v9x_u16 *symbol)
{
    v9x_s32 value;
    v9x_s32 first;
    v9x_s32 count;
    v9x_u16 index;
    v9x_u16 length;
    v9x_u16 status;
    v9x_u32 bit;

    value = 0l;
    first = 0l;
    index = 0u;
    for (length = 1u; length <= V9X_INFLATE_MAX_BITS; ++length) {
        status = v9x_inflate_bits(state, 1u, &bit);
        if (status != V9X_INFLATE_OK) {
            return status;
        }
        value |= (v9x_s32)bit;
        count = (v9x_s32)code->count[length];
        if (value >= first && value - first < count) {
            *symbol = code->symbol[index + (v9x_u16)(value - first)];
            return V9X_INFLATE_OK;
        }
        index = (v9x_u16)(index + (v9x_u16)count);
        first = (first + count) * 2l;
        value *= 2l;
    }
    return V9X_INFLATE_BAD_CODE;
}

/*
 * A stored block (RFC 1951 3.2.4): skip to a byte boundary, LEN and NLEN as
 * little-endian 16-bit values, NLEN the one's complement of LEN, then LEN
 * bytes copied as they are.
 */
static v9x_u16 v9x_inflate_stored(struct v9x_inflate_state *state)
{
    const v9x_u8 *input;
    v9x_u32 position;
    v9x_u32 length;
    v9x_u32 complement;
    v9x_u32 i;

    /* Fewer than 8 bits are buffered, all of them padding; see
     * v9x_inflate_bits(). */
    state->bit_buffer = 0ul;
    state->bit_count = 0u;

    input = state->input;
    position = state->input_position;
    if (state->input_length - position < 4ul) {
        return V9X_INFLATE_TRUNCATED;
    }
    length = (v9x_u32)input[position] | ((v9x_u32)input[position + 1ul] << 8);
    complement = (v9x_u32)input[position + 2ul] |
                 ((v9x_u32)input[position + 3ul] << 8);
    position += 4ul;
    if (length != (~complement & 0xFFFFul)) {
        return V9X_INFLATE_BAD_STORED;
    }
    if (state->input_length - position < length) {
        return V9X_INFLATE_TRUNCATED;
    }
    if (state->output_capacity - state->output_position < length) {
        return V9X_INFLATE_OUTPUT_FULL;
    }

    for (i = 0ul; i < length; ++i) {
        state->output[state->output_position + i] = input[position + i];
    }
    state->output_position += length;
    state->input_position = position + length;
    return V9X_INFLATE_OK;
}

/*
 * The data of a Huffman block (RFC 1951 3.2.5), up to and including its
 * end-of-block symbol, with the block's two codes already built.
 *
 * Every pass reads at least one bit, so a stream cannot keep this looping
 * past the end of its input; every byte written is checked against the
 * capacity first, and every back-reference against the bytes already
 * written. The copy is byte by byte because a match may overlap its own
 * output (distance less than length), which is how runs are encoded.
 */
static v9x_u16 v9x_inflate_codes(struct v9x_inflate_state *state)
{
    v9x_u16 symbol;
    v9x_u16 status;
    v9x_u32 extra;
    v9x_u32 length;
    v9x_u32 distance;
    v9x_u32 source;
    v9x_u32 i;

    for (;;) {
        status = v9x_inflate_decode(state, &state->litlen, &symbol);
        if (status != V9X_INFLATE_OK) {
            return status;
        }

        if (symbol < V9X_INFLATE_END_OF_BLOCK) {
            if (state->output_position >= state->output_capacity) {
                return V9X_INFLATE_OUTPUT_FULL;
            }
            state->output[state->output_position] = (v9x_u8)symbol;
            ++state->output_position;
            continue;
        }
        if (symbol == V9X_INFLATE_END_OF_BLOCK) {
            return V9X_INFLATE_OK;
        }

        /* 286 and 287 take part in the fixed code but never in data. */
        symbol = (v9x_u16)(symbol - V9X_INFLATE_FIRST_LENGTH);
        if (symbol >= V9X_INFLATE_LENGTH_CODES) {
            return V9X_INFLATE_BAD_CODE;
        }
        status = v9x_inflate_bits(state, v9x_inflate_length_extra[symbol],
                                  &extra);
        if (status != V9X_INFLATE_OK) {
            return status;
        }
        length = (v9x_u32)v9x_inflate_length_base[symbol] + extra;

        status = v9x_inflate_decode(state, &state->distance, &symbol);
        if (status != V9X_INFLATE_OK) {
            return status;
        }
        /* Likewise distance symbols 30 and 31. */
        if (symbol >= V9X_INFLATE_DISTANCE_CODES) {
            return V9X_INFLATE_BAD_DISTANCE;
        }
        status = v9x_inflate_bits(state, v9x_inflate_distance_extra[symbol],
                                  &extra);
        if (status != V9X_INFLATE_OK) {
            return status;
        }
        distance = (v9x_u32)v9x_inflate_distance_base[symbol] + extra;

        /* A distance reaching before the first byte of this stream's output.
         * The output buffer is the whole window - there is no preset
         * dictionary in a zip member - so nothing lies before it. */
        if (distance > state->output_position) {
            return V9X_INFLATE_BAD_DISTANCE;
        }
        if (state->output_capacity - state->output_position < length) {
            return V9X_INFLATE_OUTPUT_FULL;
        }

        source = state->output_position - distance;
        for (i = 0ul; i < length; ++i) {
            state->output[state->output_position + i] =
                state->output[source + i];
        }
        state->output_position += length;
    }
}

/* The fixed codes of RFC 1951 3.2.6. Rebuilt for every fixed block: 320
 * small iterations, and it keeps the state free of a "tables valid" flag. */
static v9x_u16 v9x_inflate_fixed(struct v9x_inflate_state *state)
{
    v9x_u16 symbol;
    v9x_u16 status;

    for (symbol = 0u; symbol < V9X_INFLATE_LITLEN_SYMBOLS; ++symbol) {
        if (symbol < 144u) {
            state->lengths[symbol] = 8u;
        } else if (symbol < 256u) {
            state->lengths[symbol] = 9u;
        } else if (symbol < 280u) {
            state->lengths[symbol] = 7u;
        } else {
            state->lengths[symbol] = 8u;
        }
    }
    status = v9x_inflate_build(&state->litlen, state->lengths,
                               V9X_INFLATE_LITLEN_SYMBOLS, V9X_FALSE);
    if (status != V9X_INFLATE_OK) {
        return status;
    }

    for (symbol = 0u; symbol < V9X_INFLATE_DIST_SYMBOLS; ++symbol) {
        state->lengths[symbol] = 5u;
    }
    status = v9x_inflate_build(&state->distance, state->lengths,
                               V9X_INFLATE_DIST_SYMBOLS, V9X_FALSE);
    if (status != V9X_INFLATE_OK) {
        return status;
    }
    return v9x_inflate_codes(state);
}

/*
 * A dynamic block's header (RFC 1951 3.2.7), then its data.
 *
 * The code length code is decoded into state->distance, which is free until
 * the real distance code is built from the lengths it produced.
 */
static v9x_u16 v9x_inflate_dynamic(struct v9x_inflate_state *state)
{
    v9x_u8 clen_lengths[V9X_INFLATE_CLEN_SYMBOLS];
    v9x_u32 value;
    v9x_u16 hlit;
    v9x_u16 hdist;
    v9x_u16 hclen;
    v9x_u16 total;
    v9x_u16 index;
    v9x_u16 symbol;
    v9x_u16 status;
    v9x_u8 repeat_value;
    v9x_u32 repeat;

    status = v9x_inflate_bits(state, 5u, &value);
    if (status != V9X_INFLATE_OK) {
        return status;
    }
    hlit = (v9x_u16)(value + V9X_INFLATE_HLIT_BASE);
    status = v9x_inflate_bits(state, 5u, &value);
    if (status != V9X_INFLATE_OK) {
        return status;
    }
    hdist = (v9x_u16)(value + V9X_INFLATE_HDIST_BASE);
    status = v9x_inflate_bits(state, 4u, &value);
    if (status != V9X_INFLATE_OK) {
        return status;
    }
    hclen = (v9x_u16)(value + V9X_INFLATE_HCLEN_BASE);
    if (hlit > V9X_INFLATE_HLIT_MAX) {
        return V9X_INFLATE_BAD_BLOCK;
    }

    for (index = 0u; index < V9X_INFLATE_CLEN_SYMBOLS; ++index) {
        clen_lengths[index] = 0u;
    }
    for (index = 0u; index < hclen; ++index) {
        status = v9x_inflate_bits(state, 3u, &value);
        if (status != V9X_INFLATE_OK) {
            return status;
        }
        clen_lengths[v9x_inflate_clen_order[index]] = (v9x_u8)value;
    }
    status = v9x_inflate_build(&state->distance, clen_lengths,
                               V9X_INFLATE_CLEN_SYMBOLS, V9X_FALSE);
    if (status != V9X_INFLATE_OK) {
        return status;
    }

    /* The literal/length and distance lengths are one sequence: a repeat may
     * run from the end of one into the start of the other, but not past the
     * end of both. */
    total = (v9x_u16)(hlit + hdist);
    index = 0u;
    while (index < total) {
        status = v9x_inflate_decode(state, &state->distance, &symbol);
        if (status != V9X_INFLATE_OK) {
            return status;
        }
        if (symbol < V9X_INFLATE_CLEN_REPEAT) {
            state->lengths[index] = (v9x_u8)symbol;
            ++index;
            continue;
        }

        if (symbol == V9X_INFLATE_CLEN_REPEAT) {
            /* "Copy the previous code length 3 - 6 times": there must be a
             * previous one. */
            if (index == 0u) {
                return V9X_INFLATE_BAD_LENGTHS;
            }
            repeat_value = state->lengths[index - 1u];
            status = v9x_inflate_bits(state, 2u, &repeat);
            repeat += 3ul;
        } else if (symbol == V9X_INFLATE_CLEN_ZEROS) {
            repeat_value = 0u;
            status = v9x_inflate_bits(state, 3u, &repeat);
            repeat += 3ul;
        } else {
            repeat_value = 0u;
            status = v9x_inflate_bits(state, 7u, &repeat);
            repeat += 11ul;
        }
        if (status != V9X_INFLATE_OK) {
            return status;
        }
        if (repeat > (v9x_u32)(total - index)) {
            return V9X_INFLATE_BAD_LENGTHS;
        }
        while (repeat != 0ul) {
            state->lengths[index] = repeat_value;
            ++index;
            --repeat;
        }
    }

    /* A block with no way to end is malformed whatever else it holds. */
    if (state->lengths[V9X_INFLATE_END_OF_BLOCK] == 0u) {
        return V9X_INFLATE_BAD_LENGTHS;
    }
    status = v9x_inflate_build(&state->litlen, state->lengths, hlit,
                               V9X_FALSE);
    if (status != V9X_INFLATE_OK) {
        return status;
    }
    status = v9x_inflate_build(&state->distance, state->lengths + hlit, hdist,
                               V9X_TRUE);
    if (status != V9X_INFLATE_OK) {
        return status;
    }
    return v9x_inflate_codes(state);
}

v9x_u16 v9x_inflate_with_state(struct v9x_inflate_state *state,
                               const v9x_u8 *input,
                               v9x_u32 input_length,
                               v9x_u8 *output,
                               v9x_u32 output_capacity,
                               v9x_u32 *output_length)
{
    v9x_u32 final_block;
    v9x_u32 type;
    v9x_u16 status;

    if (output_length == 0) {
        return V9X_INFLATE_BAD_ARGUMENT;
    }
    *output_length = 0ul;
    if (state == 0 || (input == 0 && input_length != 0ul) ||
        (output == 0 && output_capacity != 0ul)) {
        return V9X_INFLATE_BAD_ARGUMENT;
    }

    state->input = input;
    state->input_length = input_length;
    state->input_position = 0ul;
    state->bit_buffer = 0ul;
    state->bit_count = 0u;
    state->output = output;
    state->output_capacity = output_capacity;
    state->output_position = 0ul;

    /* RFC 1951 3.2.3: blocks until one with BFINAL set. Each block header
     * costs three bits of input, so this ends with the input at the latest. */
    do {
        status = v9x_inflate_bits(state, 1u, &final_block);
        if (status == V9X_INFLATE_OK) {
            status = v9x_inflate_bits(state, 2u, &type);
        }
        if (status == V9X_INFLATE_OK) {
            if (type == V9X_INFLATE_BTYPE_STORED) {
                status = v9x_inflate_stored(state);
            } else if (type == V9X_INFLATE_BTYPE_FIXED) {
                status = v9x_inflate_fixed(state);
            } else if (type == V9X_INFLATE_BTYPE_DYNAMIC) {
                status = v9x_inflate_dynamic(state);
            } else {
                status = V9X_INFLATE_BAD_BLOCK;
            }
        }
        *output_length = state->output_position;
        if (status != V9X_INFLATE_OK) {
            return status;
        }
    } while (final_block == 0ul);

    return V9X_INFLATE_OK;
}

v9x_u16 v9x_inflate(const v9x_u8 *input,
                    v9x_u32 input_length,
                    v9x_u8 *output,
                    v9x_u32 output_capacity,
                    v9x_u32 *output_length)
{
    struct v9x_inflate_state state;

    return v9x_inflate_with_state(&state, input, input_length, output,
                                  output_capacity, output_length);
}
