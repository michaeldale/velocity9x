/*
 * The shader stream's constants packet and vertex run, step 5c of
 * docs\plans\gen3-ps11-pixel-shaders.md. The state block is in i9xx_3d.c,
 * beside the static emitters it shares with the other blocks. 32-bit side
 * only: the 16-bit driver never builds a shader stream, and this file is
 * not in its module list.
 */
#include "velocity9x/intel_gen3_3d.h"

/* Position, diffuse, specular: four floats and two packed colours. */
#define V9X_I9XX_SHADER_FIXED_DWORDS 6ul
/* The wide vertex's fields (r3d.h V9X_R3D_SHADER_FLOATS). */
#define V9X_I9XX_WIDE_DIFFUSE        4ul
#define V9X_I9XX_WIDE_SPECULAR       8ul
#define V9X_I9XX_WIDE_SETS           12ul

v9x_status v9x_i9xx_build_shader_constants(
    const v9x_u32 *values, v9x_u32 count,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 at = 0ul;
    v9x_u32 index;

    if (written != 0) { *written = 0ul; }
    if (values == 0 || stream == 0 || written == 0 || count == 0ul ||
        count > V9X_I9XX_SHADER_CONSTANTS_MAX) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (capacity < 2ul + count * 4ul) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    /* The constants packet's layout (the two-unit program's): length
     * (total - 2), the register mask, four floats a register in order. */
    stream[at++] = V9X_I9XX_3DSTATE_PS_CONSTANTS | (count * 4ul);
    stream[at++] = count == 32ul ? 0xfffffffful : (1ul << count) - 1ul;
    for (index = 0ul; index < count * 4ul; ++index) {
        stream[at++] = values[index];
    }
    *written = at;
    return V9X_STATUS_OK;
}

v9x_u32 v9x_i9xx_shader_run_dwords(v9x_u32 triangles, v9x_u32 sets)
{
    if (triangles == 0ul || triangles > V9X_I9XX_RUNTIME_MAX_TRIANGLES ||
        sets > V9X_I9XX_SHADER_STAGES) {
        return 0ul;
    }
    return 1ul + triangles * V9X_I9XX_VERTEX_COUNT *
                     (V9X_I9XX_SHADER_FIXED_DWORDS + 4ul * sets);
}

typedef union v9x_i9xx_shader_float {
    float f;
    v9x_u32 bits;
} V9X_I9XX_SHADER_FLOAT;

static v9x_u32 v9x_i9xx_shader_bits(float value)
{
    V9X_I9XX_SHADER_FLOAT u;

    u.f = value;
    return u.bits;
}

/* A colour channel as a byte: [0, 1] to 0-255, rounded; NaN as 0. */
static v9x_u32 v9x_i9xx_shader_byte(float value)
{
    if (!(value > 0.0f)) {
        return 0ul;
    }
    if (value >= 1.0f) {
        return 255ul;
    }
    return (v9x_u32)(value * 255.0f + 0.5f);
}

/* r g b a floats as the vertex's ARGB dword, the order the D3DTLVERTEX
 * colour the other runs copy already has. */
static v9x_u32 v9x_i9xx_shader_colour(const float *rgba)
{
    return (v9x_i9xx_shader_byte(rgba[3]) << 24) |
           (v9x_i9xx_shader_byte(rgba[0]) << 16) |
           (v9x_i9xx_shader_byte(rgba[1]) << 8) |
           v9x_i9xx_shader_byte(rgba[2]);
}

/*
 * The run. Position, then the two colours, then the sets in stage order:
 * the order S4 then S2 lay a vertex out, which is the two-unit run's
 * (position, colour, specular, set 0, set 1). The position checks are
 * i9xx_vertex.c's, on the same predicates; they are the memory-safety
 * argument for the path.
 */
v9x_status v9x_i9xx_build_shader_run(
    const float *wide, v9x_u32 triangles, v9x_u32 sets,
    v9x_u32 width, v9x_u32 height,
    v9x_u32 *stream, v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 run_dwords = v9x_i9xx_shader_run_dwords(triangles, sets);
    v9x_u32 band = v9x_i9xx_vertex_get_band();
    v9x_u32 band_bits = 0ul;
    v9x_u32 width_bits = 0ul;
    v9x_u32 height_bits = 0ul;
    v9x_u32 one_bits = 0ul;
    v9x_u32 at = 0ul;
    v9x_u32 vertex;
    v9x_u32 k;

    if (written != 0) { *written = 0ul; }
    if (stream == 0 || written == 0 || wide == 0 || width == 0ul ||
        height == 0ul || run_dwords == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (capacity < run_dwords) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    if (v9x_i9xx_float_from_int(width + band, &width_bits) !=
            V9X_I9XX_FLOAT_OK ||
        v9x_i9xx_float_from_int(height + band, &height_bits) !=
            V9X_I9XX_FLOAT_OK ||
        (band != 0ul &&
         v9x_i9xx_float_from_int(band, &band_bits) != V9X_I9XX_FLOAT_OK) ||
        v9x_i9xx_float_from_int(1ul, &one_bits) != V9X_I9XX_FLOAT_OK) {
        return V9X_STATUS_INVALID_STATE;
    }

    stream[at++] = V9X_I9XX_3DPRIMITIVE_INLINE | V9X_I9XX_PRIM3D_TRILIST |
                   (run_dwords - 2ul);
    for (vertex = 0ul; vertex < triangles * V9X_I9XX_VERTEX_COUNT; ++vertex) {
        const float *v = &wide[vertex * V9X_I9XX_WIDE_FLOATS];
        v9x_u32 x = v9x_i9xx_shader_bits(v[0]);
        v9x_u32 y = v9x_i9xx_shader_bits(v[1]);
        v9x_u32 z = v9x_i9xx_shader_bits(v[2]);
        v9x_u32 w = v9x_i9xx_shader_bits(v[3]);

        if (v9x_i9xx_float_in_span(x, width_bits, band_bits) == V9X_FALSE ||
            v9x_i9xx_float_in_span(y, height_bits, band_bits) ==
                V9X_FALSE ||
            v9x_i9xx_float_in_range(z, one_bits) == V9X_FALSE ||
            v9x_i9xx_float_positive_finite(w) == V9X_FALSE) {
            *written = 0ul;
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        for (k = 0ul; k < 4ul * sets; ++k) {
            if (v9x_i9xx_float_finite(v9x_i9xx_shader_bits(
                    v[V9X_I9XX_WIDE_SETS + k])) == V9X_FALSE) {
                *written = 0ul;
                return V9X_STATUS_INVALID_ARGUMENT;
            }
        }
        stream[at++] = x;
        stream[at++] = y;
        stream[at++] = z;
        stream[at++] = w;
        stream[at++] = v9x_i9xx_shader_colour(&v[V9X_I9XX_WIDE_DIFFUSE]);
        stream[at++] = v9x_i9xx_shader_colour(&v[V9X_I9XX_WIDE_SPECULAR]);
        for (k = 0ul; k < 4ul * sets; ++k) {
            stream[at++] = v9x_i9xx_shader_bits(v[V9X_I9XX_WIDE_SETS + k]);
        }
    }
    *written = at;
    return V9X_STATUS_OK;
}
