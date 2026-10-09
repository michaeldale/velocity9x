/*
 * Glide 3 vertex layouts, texture-info translation and primitive
 * expansion (glide3_layout.h). Pure: the host tests compile it, and
 * GLIDE3X.DLL builds it under the DLL's options.
 */
#include "glide3_layout.h"

/* GR_LOD_LOG2_256 and the aspect extremes (glide.h). */
#define V9X_GLIDE3_LOD_LOG2_MAX    8l
#define V9X_GLIDE3_ASPECT_LOG2_MAX 3l

/* TMU 0's oow in a GrVertex, after sow and tow; glide_vertex.h does not
 * name it because the engine never reads it. */
#define V9X_GLIDE3_GRVERTEX_TMU0_OOW 11u

/* A colour channel or alpha the layout leaves out. */
#define V9X_GLIDE3_CHANNEL_FULL 255.0f

void v9x_glide3_layout_init(V9X_GLIDE3_LAYOUT *layout)
{
    layout->xy = 0ul;
    layout->z = 0ul;
    layout->w = 0ul;
    layout->q = 0ul;
    layout->a = 0ul;
    layout->rgb = 0ul;
    layout->pargb = 0ul;
    layout->st0 = 0ul;
    layout->q0 = 0ul;
}

v9x_u16 v9x_glide3_layout_set(V9X_GLIDE3_LAYOUT *layout, v9x_u32 param,
                              v9x_u32 offset, v9x_u32 mode)
{
    v9x_u32 *slot;
    v9x_u32 value = mode != V9X_GLIDE3_PARAM_DISABLE ? offset + 1ul : 0ul;

    switch (param) {
    case V9X_GLIDE3_PARAM_XY:    slot = &layout->xy; break;
    case V9X_GLIDE3_PARAM_Z:     slot = &layout->z; break;
    case V9X_GLIDE3_PARAM_W:     slot = &layout->w; break;
    case V9X_GLIDE3_PARAM_Q:     slot = &layout->q; break;
    case V9X_GLIDE3_PARAM_A:     slot = &layout->a; break;
    case V9X_GLIDE3_PARAM_RGB:   slot = &layout->rgb; break;
    case V9X_GLIDE3_PARAM_PARGB: slot = &layout->pargb; break;
    case V9X_GLIDE3_PARAM_ST0:   slot = &layout->st0; break;
    case V9X_GLIDE3_PARAM_Q0:    slot = &layout->q0; break;
    case V9X_GLIDE3_PARAM_FOG_EXT:
    case V9X_GLIDE3_PARAM_ST1:
    case V9X_GLIDE3_PARAM_ST2:
    case V9X_GLIDE3_PARAM_Q1:
    case V9X_GLIDE3_PARAM_Q2:
        return V9X_TRUE;
    default:
        return V9X_FALSE;
    }
    *slot = value;
    return V9X_TRUE;
}

static float v9x_glide3_float_at(const v9x_u8 *vertex, v9x_u32 slot,
                                 v9x_u32 index)
{
    const float *field = (const float *)(vertex + (slot - 1ul));

    return field[index];
}

void v9x_glide3_vertex_read(const V9X_GLIDE3_LAYOUT *layout,
                            const v9x_u8 *vertex, float *out)
{
    unsigned int i;
    float oow = 1.0f;

    for (i = 0u; i < V9X_GLIDE_VERTEX_FLOATS; ++i) {
        out[i] = 0.0f;
    }
    if (layout->xy != 0ul) {
        out[V9X_GLIDE_VERTEX_X] = v9x_glide3_float_at(vertex, layout->xy, 0u);
        out[V9X_GLIDE_VERTEX_Y] = v9x_glide3_float_at(vertex, layout->xy, 1u);
    }
    if (layout->z != 0ul) {
        out[V9X_GLIDE_VERTEX_OOZ] = v9x_glide3_float_at(vertex, layout->z, 0u);
    }

    /* Q is 1/w as Glide 2's oow is. Without it, W gives it; Diablo II
     * sends only Q0, the texture's q, which then stands for both. */
    if (layout->q != 0ul) {
        oow = v9x_glide3_float_at(vertex, layout->q, 0u);
    } else if (layout->w != 0ul) {
        float w = v9x_glide3_float_at(vertex, layout->w, 0u);

        oow = w != 0.0f ? 1.0f / w : 1.0f;
    } else if (layout->q0 != 0ul) {
        oow = v9x_glide3_float_at(vertex, layout->q0, 0u);
    }
    out[V9X_GLIDE_VERTEX_OOW] = oow;
    out[V9X_GLIDE3_GRVERTEX_TMU0_OOW] = layout->q0 != 0ul ?
        v9x_glide3_float_at(vertex, layout->q0, 0u) : oow;

    out[V9X_GLIDE_VERTEX_R] = V9X_GLIDE3_CHANNEL_FULL;
    out[V9X_GLIDE_VERTEX_G] = V9X_GLIDE3_CHANNEL_FULL;
    out[V9X_GLIDE_VERTEX_B] = V9X_GLIDE3_CHANNEL_FULL;
    out[V9X_GLIDE_VERTEX_A] = V9X_GLIDE3_CHANNEL_FULL;
    if (layout->pargb != 0ul) {
        v9x_u32 argb = *(const v9x_u32 *)(vertex + (layout->pargb - 1ul));

        out[V9X_GLIDE_VERTEX_A] = (float)(argb >> 24);
        out[V9X_GLIDE_VERTEX_R] = (float)((argb >> 16) & 0xFFul);
        out[V9X_GLIDE_VERTEX_G] = (float)((argb >> 8) & 0xFFul);
        out[V9X_GLIDE_VERTEX_B] = (float)(argb & 0xFFul);
    }
    if (layout->rgb != 0ul) {
        out[V9X_GLIDE_VERTEX_R] = v9x_glide3_float_at(vertex, layout->rgb, 0u);
        out[V9X_GLIDE_VERTEX_G] = v9x_glide3_float_at(vertex, layout->rgb, 1u);
        out[V9X_GLIDE_VERTEX_B] = v9x_glide3_float_at(vertex, layout->rgb, 2u);
    }
    if (layout->a != 0ul) {
        out[V9X_GLIDE_VERTEX_A] = v9x_glide3_float_at(vertex, layout->a, 0u);
    }
    if (layout->st0 != 0ul) {
        out[V9X_GLIDE_VERTEX_SOW] = v9x_glide3_float_at(vertex, layout->st0, 0u);
        out[V9X_GLIDE_VERTEX_TOW] = v9x_glide3_float_at(vertex, layout->st0, 1u);
    }
}

v9x_u16 v9x_glide3_lod_to_glide2(v9x_u32 lod_log2, v9x_u32 *lod)
{
    v9x_s32 value = (v9x_s32)lod_log2;

    if (value < 0l || value > V9X_GLIDE3_LOD_LOG2_MAX) {
        return V9X_FALSE;
    }
    *lod = (v9x_u32)(V9X_GLIDE3_LOD_LOG2_MAX - value);
    return V9X_TRUE;
}

v9x_u16 v9x_glide3_aspect_to_glide2(v9x_u32 aspect_log2, v9x_u32 *aspect)
{
    v9x_s32 value = (v9x_s32)aspect_log2;

    if (value < -V9X_GLIDE3_ASPECT_LOG2_MAX || value > V9X_GLIDE3_ASPECT_LOG2_MAX) {
        return V9X_FALSE;
    }
    *aspect = (v9x_u32)(V9X_GLIDE3_ASPECT_LOG2_MAX - value);
    return V9X_TRUE;
}

v9x_u16 v9x_glide3_texinfo_to_glide2(const v9x_u32 *glide3, v9x_u32 *glide2)
{
    if (!v9x_glide3_lod_to_glide2(glide3[0], &glide2[0]) ||
        !v9x_glide3_lod_to_glide2(glide3[1], &glide2[1]) ||
        !v9x_glide3_aspect_to_glide2(glide3[2], &glide2[2])) {
        return V9X_FALSE;
    }
    glide2[3] = glide3[3];
    glide2[4] = glide3[4];
    return V9X_TRUE;
}

v9x_u32 v9x_glide3_triangle_count(v9x_u32 mode, v9x_u32 count)
{
    switch (mode) {
    case V9X_GLIDE3_POLYGON:
    case V9X_GLIDE3_TRIANGLE_FAN:
    case V9X_GLIDE3_TRIANGLE_STRIP:
        return count >= 3ul ? count - 2ul : 0ul;
    case V9X_GLIDE3_TRIANGLES:
        return count / 3ul;
    default:
        return 0ul;
    }
}

void v9x_glide3_triangle_at(v9x_u32 mode, v9x_u32 n, v9x_u32 *indices)
{
    if (mode == V9X_GLIDE3_TRIANGLES) {
        indices[0] = n * 3ul;
        indices[1] = n * 3ul + 1ul;
        indices[2] = n * 3ul + 2ul;
        return;
    }
    if (mode == V9X_GLIDE3_TRIANGLE_STRIP) {
        indices[0] = (n & 1ul) ? n + 1ul : n;
        indices[1] = (n & 1ul) ? n : n + 1ul;
        indices[2] = n + 2ul;
        return;
    }
    indices[0] = 0ul;
    indices[1] = n + 1ul;
    indices[2] = n + 2ul;
}

v9x_u32 v9x_glide3_line_count(v9x_u32 mode, v9x_u32 count)
{
    if (mode == V9X_GLIDE3_LINES) {
        return count / 2ul;
    }
    if (mode == V9X_GLIDE3_LINE_STRIP) {
        return count >= 2ul ? count - 1ul : 0ul;
    }
    return 0ul;
}

void v9x_glide3_line_at(v9x_u32 mode, v9x_u32 n, v9x_u32 *indices)
{
    if (mode == V9X_GLIDE3_LINES) {
        indices[0] = n * 2ul;
        indices[1] = n * 2ul + 1ul;
        return;
    }
    indices[0] = n;
    indices[1] = n + 1ul;
}
