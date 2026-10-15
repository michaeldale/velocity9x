/*
 * ATI3DCIF vertices and primitives to render-interface triangles
 * (docs\plans\ati-3dcif-wrapper.md, Phase 2). See cif_vertex.h for the
 * conventions and where each comes from.
 *
 * No C runtime in ATI3DCIF.DLL: a float-to-integer cast lowers to
 * Watcom's __CHP helper, which the DLL cannot link, so the one conversion
 * here goes through a local fistp as glide_vertex.c's does.
 */
#include "cif_vertex.h"

#ifdef __WATCOMC__
static long v9x_cif_to_long(double value);
#pragma aux v9x_cif_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_cif_to_long(double value)
{
    return (long)value;     /* truncates; the caller corrects to nearest */
}
#endif

/* Vertices a primitive takes in a list, and the triangles it makes. */
#define V9X_CIF_TRI_VERTICES  3ul
#define V9X_CIF_QUAD_VERTICES 4ul
#define V9X_CIF_QUAD_TRIANGLES 2ul
#define V9X_CIF_LINE_VERTICES 2ul
#define V9X_CIF_LINE_TRIANGLES 2ul

/* A colour channel: 0..255, clamped, halves rounded up. The x87 store
 * rounds to nearest-even; the correction makes both compilers agree. */
static v9x_u32 v9x_cif_channel(float value)
{
    long whole;
    double difference;

    if (!(value > 0.0f)) {
        return 0ul;
    }
    if (value >= 255.0f) {
        return 255ul;
    }
    whole = v9x_cif_to_long((double)value);
    difference = (double)value - (double)whole;
    if (difference >= 0.5) {
        ++whole;
    } else if (difference < -0.5) {
        --whole;
    }
    return whole < 0l ? 0ul : (whole > 255l ? 255ul : (v9x_u32)whole);
}

void v9x_cif_vtcf_convert(const V9X_CIF_VERTEX_SETUP *setup, const float *in,
                          V9X_R3D_ABI_VERTEX *out)
{
    float z = in[V9X_C3D_VTCF_Z] / V9X_CIF_Z_MAX;
    float w = in[V9X_C3D_VTCF_W];
    v9x_u32 alpha = v9x_cif_channel(in[V9X_C3D_VTCF_A]);

    out->sx = in[V9X_C3D_VTCF_X] + setup->origin_x;
    out->sy = in[V9X_C3D_VTCF_Y] + setup->origin_y;

    /* Clamped both ways: Cubes sends a negative z with Z off, and a z past
     * the buffer's range would wrap on the chip. */
    if (!(z > 0.0f)) {
        z = 0.0f;
    } else if (z > 1.0f) {
        z = 1.0f;
    }
    out->sz = z;

    /* s and t are premultiplied by w. A w of 0 (or below) has no inverse:
     * draw it as w = 1, so the coordinates stay finite. */
    if (!(w > 0.0f)) {
        w = 1.0f;
    }
    out->rhw = w;
    out->tu = in[V9X_C3D_VTCF_S] / w;
    out->tv = in[V9X_C3D_VTCF_T] / w;

    if (setup->shade == V9X_C3D_ESH_SOLID) {
        out->color = setup->solid_argb;
    } else {
        out->color = (alpha << 24) |
                     (v9x_cif_channel(in[V9X_C3D_VTCF_R]) << 16) |
                     (v9x_cif_channel(in[V9X_C3D_VTCF_G]) << 8) |
                     v9x_cif_channel(in[V9X_C3D_VTCF_B]);
    }

    if (setup->white) {
        out->color |= 0x00fffffful;
    }

    /* Fog and alpha blending share the Rage's alpha interpolator, so with
     * fog on the vertex alpha is the fog factor and the colour is opaque. */
    if (setup->fog) {
        out->specular = alpha << 24;
        out->color |= 0xff000000ul;
    } else {
        out->specular = 0xff000000ul;
    }
}

void v9x_cif_line_widen(V9X_R3D_ABI_VERTEX *triangle, v9x_u32 second)
{
    float dx = triangle[2].sx - triangle[0].sx;
    float dy = triangle[2].sy - triangle[0].sy;
    float off_x = 0.0f;
    float off_y = 0.0f;

    if (dx < 0.0f) {
        dx = -dx;
    }
    if (dy < 0.0f) {
        dy = -dy;
    }
    if (dx >= dy) {
        off_y = 1.0f;
    } else {
        off_x = 1.0f;
    }

    /* The first triangle is a, a+, b+; the second a, b+, b. */
    triangle[1].sx += off_x;
    triangle[1].sy += off_y;
    if (second == 0ul) {
        triangle[2].sx += off_x;
        triangle[2].sy += off_y;
    }
}

v9x_u16 v9x_cif_vertices_opaque(const V9X_R3D_ABI_VERTEX *vertices,
                                v9x_u32 triangles)
{
    v9x_u32 index;

    for (index = 0ul; index < triangles * 3ul; ++index) {
        if ((vertices[index].color >> 24) != 0xfful) {
            return V9X_FALSE;
        }
    }
    return V9X_TRUE;
}

void v9x_cif_triangle_flat(V9X_R3D_ABI_VERTEX *triangle)
{
    triangle[0].color = triangle[2].color;
    triangle[1].color = triangle[2].color;
}

v9x_u32 v9x_cif_prim_triangles(v9x_u32 prim, v9x_u32 form, v9x_u32 count)
{
    if (prim == V9X_C3D_EPRIM_TRI) {
        if (form == V9X_CIF_FORM_LIST) {
            return count / V9X_CIF_TRI_VERTICES;
        }
        return count >= V9X_CIF_TRI_VERTICES ? count - 2ul : 0ul;
    }
    if (prim == V9X_C3D_EPRIM_QUAD && form == V9X_CIF_FORM_LIST) {
        return (count / V9X_CIF_QUAD_VERTICES) * V9X_CIF_QUAD_TRIANGLES;
    }

    /* Lines: a list takes vertex pairs, a strip is a polyline. Each line
     * is drawn as two triangles (v9x_cif_line_widen). */
    if (prim == V9X_C3D_EPRIM_LINE) {
        if (form == V9X_CIF_FORM_LIST) {
            return (count / V9X_CIF_LINE_VERTICES) * V9X_CIF_LINE_TRIANGLES;
        }
        return count >= V9X_CIF_LINE_VERTICES
                   ? (count - 1ul) * V9X_CIF_LINE_TRIANGLES
                   : 0ul;
    }
    return 0ul;
}

v9x_u32 v9x_cif_prim_vertex(v9x_u32 prim, v9x_u32 form, v9x_u32 triangle,
                            v9x_u32 corner)
{
    v9x_u32 first;

    if (prim == V9X_C3D_EPRIM_LINE) {
        /* a, a, b then a, b, b: the second end from corner 2 of the first
         * triangle and corners 1 and 2 of the second. */
        first = triangle / V9X_CIF_LINE_TRIANGLES;
        if (form == V9X_CIF_FORM_LIST) {
            first *= V9X_CIF_LINE_VERTICES;
        }
        if (corner == 2ul ||
            (corner == 1ul && (triangle % V9X_CIF_LINE_TRIANGLES) != 0ul)) {
            return first + 1ul;
        }
        return first;
    }
    if (prim == V9X_C3D_EPRIM_QUAD) {
        /* The fan 0-1-2, 0-2-3 of quadrangle triangle / 2. */
        first = (triangle / V9X_CIF_QUAD_TRIANGLES) * V9X_CIF_QUAD_VERTICES;
        if (corner == 0ul) {
            return first;
        }
        return first + corner + (triangle % V9X_CIF_QUAD_TRIANGLES);
    }
    if (form == V9X_CIF_FORM_LIST) {
        return triangle * V9X_CIF_TRI_VERTICES + corner;
    }

    /* A strip's odd triangles swap their first two corners, keeping the
     * first triangle's winding. */
    if ((triangle & 1ul) != 0ul && corner < 2ul) {
        return triangle + (1ul - corner);
    }
    return triangle + corner;
}
