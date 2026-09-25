/*
 * Screen-space clipping and the triangle-list builder, moved out of
 * d3d_core.c in Phase 1a of the OpenGL plan with the arithmetic unchanged.
 * Two things were parameters of the D3D context there and are arguments
 * here: the guard band, which is the executing engine's, and the target
 * size. The batch sink and the cull decision are callbacks, because where a
 * batch goes and what "culled" means are the front end's.
 *
 * No DDHAL or OS header: this file compiles on the host and
 * tests\host\test_r3d_clip.c holds it to what d3d_core.c did.
 */
#include "r3d.h"

/*
 * Float to long, rounding to nearest under the default control word.
 *
 * The HAL links with no runtime, so a plain cast would pull in Watcom's
 * __CHP helper and fail the link; d3d_zfixed.c carries the same four
 * instructions for the same reason. The host build under another compiler
 * takes the C below, which rounds half to even as fistp does, so the byte
 * lerp gives the same answer on both.
 */
#ifdef __WATCOMC__
static long v9x_r3d_float_to_long(float value);
#pragma aux v9x_r3d_float_to_long = \
    "sub esp,4" \
    "fistp dword ptr [esp]" \
    "pop eax" \
    parm [8087] value [eax] modify exact [eax];
#else
static long v9x_r3d_float_to_long(float value)
{
    long whole = (long)value;
    float fraction = value - (float)whole;

    if (fraction > 0.5f || (fraction == 0.5f && (whole & 1l) != 0l)) {
        ++whole;
    } else if (fraction < -0.5f || (fraction == -0.5f && (whole & 1l) != 0l)) {
        --whole;
    }
    return whole;
}
#endif

static v9x_u8 v9x_r3d_lerp_byte(v9x_u8 first, v9x_u8 second, float amount)
{
    return (v9x_u8)v9x_r3d_float_to_long((float)first +
        ((float)second - (float)first) * amount);
}

static v9x_u32 v9x_r3d_lerp_color(v9x_u32 first, v9x_u32 second, float amount)
{
    return ((v9x_u32)v9x_r3d_lerp_byte((v9x_u8)(first >> 24),
                                       (v9x_u8)(second >> 24), amount) << 24) |
           ((v9x_u32)v9x_r3d_lerp_byte((v9x_u8)(first >> 16),
                                       (v9x_u8)(second >> 16), amount) << 16) |
           ((v9x_u32)v9x_r3d_lerp_byte((v9x_u8)(first >> 8),
                                       (v9x_u8)(second >> 8), amount) << 8) |
           (v9x_u32)v9x_r3d_lerp_byte((v9x_u8)first, (v9x_u8)second, amount);
}

static void v9x_r3d_lerp_vertex(V9X_R3D_VERTEX *result,
                                const V9X_R3D_VERTEX *first,
                                const V9X_R3D_VERTEX *second,
                                float amount)
{
    result->sx = first->sx + (second->sx - first->sx) * amount;
    result->sy = first->sy + (second->sy - first->sy) * amount;
    result->sz = first->sz + (second->sz - first->sz) * amount;
    result->rhw = first->rhw + (second->rhw - first->rhw) * amount;
    result->color = v9x_r3d_lerp_color(first->color, second->color, amount);
    result->specular = v9x_r3d_lerp_color(first->specular,
                                          second->specular, amount);
    /*
     * Texture coordinates are not linear in screen space unless both ends
     * have the same rhw. rhw is, and so is tu * rhw, so the coordinate at the
     * cut is their quotient. Blending tu itself put every clipped vertex of a
     * receding polygon too far along the texture - on 3DMark 99's tunnel,
     * whose walls are clipped at every screen edge, that squeezed the whole
     * texture into the visible part of the wall and drove the mip level to
     * the bottom of the chain. Colour, depth and rhw stay linear: the engines
     * interpolate them that way.
     *
     * Equal rhw keeps the plain blend, so an affine triangle is cut exactly
     * as before, bit for bit.
     */
    if (first->rhw != second->rhw && first->rhw > 0.0f &&
        second->rhw > 0.0f && result->rhw > 0.0f) {
        float first_u = first->tu * first->rhw;
        float first_v = first->tv * first->rhw;
        float inverse = 1.0f / result->rhw;

        result->tu = (first_u +
                      (second->tu * second->rhw - first_u) * amount) * inverse;
        result->tv = (first_v +
                      (second->tv * second->rhw - first_v) * amount) * inverse;
    } else {
        result->tu = first->tu + (second->tu - first->tu) * amount;
        result->tv = first->tv + (second->tv - first->tv) * amount;
    }
}

/*
 * Whether a screen coordinate is finite, from its bits: an all-ones exponent
 * is NaN or infinity.
 *
 * No float comparison can be trusted to refuse a NaN under Open Watcom. It
 * branches on `fcomp; fnstsw; sahf` without testing PF, so an unordered
 * result reads as below and equal at once, and whether a range test passes a
 * NaN depends on which way round each comparison happened to be compiled
 * (docs\decisions\2026-09-25-nan-detection-in-the-z-conversion.md). An
 * infinity is refused here too; the range tests would have refused it anyway.
 */
#define V9X_R3D_FLOAT_EXPONENT_MASK 0x7f800000ul

static int v9x_r3d_coordinate_finite(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } stored;

    stored.value = value;
    return (stored.bits & V9X_R3D_FLOAT_EXPONENT_MASK) !=
           V9X_R3D_FLOAT_EXPONENT_MASK ? 1 : 0;
}

int v9x_r3d_clip_triangle(const V9X_R3D_VERTEX *triangle,
                          float guard_limit,
                          float width,
                          float height,
                          V9X_R3D_VERTEX *result)
{
    V9X_R3D_VERTEX buffers[2][V9X_R3D_CLIP_MAX_VERTICES];
    V9X_R3D_VERTEX *input = buffers[0];
    V9X_R3D_VERTEX *output = buffers[1];
    v9x_u32 count = 3ul;
    v9x_u32 edge;
    v9x_u32 index;

    /* The guard band belongs to the engine, not to the clipper: a vertex
     * outside it overflows that engine's fixed-point coordinate conversion,
     * so it is refused here rather than wrapped there. A non-finite one is
     * refused from its bits before the range test sees it. */
    for (index = 0ul; index < 3ul; ++index) {
        if (!v9x_r3d_coordinate_finite(triangle[index].sx) ||
            !v9x_r3d_coordinate_finite(triangle[index].sy)) {
            return -1;
        }
        if (!(triangle[index].sx >= -guard_limit &&
              triangle[index].sx < guard_limit &&
              triangle[index].sy >= -guard_limit &&
              triangle[index].sy < guard_limit)) {
            return -1;
        }
        input[index] = triangle[index];
    }
    for (edge = 0ul; edge < 4ul && count != 0ul; ++edge) {
        V9X_R3D_VERTEX previous = input[count - 1ul];
        int previous_inside;
        v9x_u32 output_count = 0ul;
        /*
         * The right and bottom edges are the viewport's, width and height,
         * not the last pixel's. A full-screen quad is 0..640 in Direct3D's
         * convention, and cut at 639 its spans end one column early on an
         * engine that fills [ceil(x1), ceil(x2)) - a black line down the
         * right and along the bottom of every full-screen plane. The pixel
         * past the edge is the hardware clip rectangle's to discard: S3's
         * own driver sets cmdHWCLIP_EN with CLIP_L_R at width - 1
         * (98DDK s3v\D3DRENDR.C:64, 399-427) and so does this one, and the
         * CPU rasterizer clamps to extent - 1 on its own.
         */
        float boundary = (edge == 0ul || edge == 2ul) ? 0.0f :
            (edge == 1ul ? width : height);

        if (edge < 2ul) {
            previous_inside = edge == 0ul ? previous.sx >= boundary
                                          : previous.sx <= boundary;
        } else {
            previous_inside = edge == 2ul ? previous.sy >= boundary
                                          : previous.sy <= boundary;
        }
        for (index = 0ul; index < count; ++index) {
            V9X_R3D_VERTEX current = input[index];
            int current_inside;

            if (edge < 2ul) {
                current_inside = edge == 0ul ? current.sx >= boundary
                                             : current.sx <= boundary;
            } else {
                current_inside = edge == 2ul ? current.sy >= boundary
                                             : current.sy <= boundary;
            }
            if (current_inside != previous_inside) {
                float denominator = edge < 2ul
                    ? current.sx - previous.sx : current.sy - previous.sy;
                float numerator = edge < 2ul
                    ? boundary - previous.sx : boundary - previous.sy;

                if (denominator != 0.0f &&
                    output_count < V9X_R3D_CLIP_MAX_VERTICES) {
                    v9x_r3d_lerp_vertex(&output[output_count], &previous,
                                        &current, numerator / denominator);
                    if (edge < 2ul) {
                        output[output_count].sx = boundary;
                    } else {
                        output[output_count].sy = boundary;
                    }
                    ++output_count;
                }
            }
            if (current_inside && output_count < V9X_R3D_CLIP_MAX_VERTICES) {
                output[output_count++] = current;
            }
            previous = current;
            previous_inside = current_inside;
        }
        count = output_count;
        {
            V9X_R3D_VERTEX *swap = input;
            input = output;
            output = swap;
        }
    }
    for (index = 0ul; index < count; ++index) {
        result[index] = input[index];
    }
    return (int)count;
}

/*
 * Whether all three vertices lie on the render target, edges included.
 *
 * A NaN coordinate answers no and goes to the clipper, whose guard-band test
 * refuses it, rather than answering yes and reaching the engine's fixed-point
 * conversion. That is decided from the bits: the range test below, compiled
 * by Open Watcom, passed a NaN on all four comparisons.
 */
int v9x_r3d_triangle_on_target(const V9X_R3D_VERTEX *triangle,
                               float width,
                               float height)
{
    v9x_u32 index;

    for (index = 0ul; index < 3ul; ++index) {
        if (!v9x_r3d_coordinate_finite(triangle[index].sx) ||
            !v9x_r3d_coordinate_finite(triangle[index].sy)) {
            return 0;
        }
        if (!(triangle[index].sx >= 0.0f && triangle[index].sx <= width &&
              triangle[index].sy >= 0.0f && triangle[index].sy <= height)) {
            return 0;
        }
    }
    return 1;
}

/*
 * A triangle list, clipped where the engine needs it, then drawn.
 *
 * The engine contract says the core hands over vertices already clipped, and
 * until 2026-09-23 only RenderPrimitive did. DrawOnePrimitive, DrawPrimitives
 * and DrawOneIndexedPrimitive passed the application's vertices straight
 * through, and the S3D emitter declines any triangle with a vertex off the
 * target, so every triangle that crossed the screen edge was dropped whole.
 * On the Trio3D/2X that was 3DMark 99's fill-rate test drawn entirely black -
 * its planes are full-screen quads ending exactly at 640.0 - the filtering
 * tunnel reduced to one or two walls, and 66,990 declined triangles in one
 * run (build\driver-results\3dmark99-640-20260923-run2).
 *
 * Triangles already on the target go through as runs, windows on the
 * caller's array, so the common case costs one bounds test per triangle and
 * no copy. Only a triangle that crosses an edge is cut, and its fan is drawn
 * on its own between the runs either side of it. A refused triangle - past
 * the guard band, or one the sink declines - is counted by the caller as a
 * refused batch and the rest of the list is still drawn.
 *
 * A culled triangle ends the run the same way a clipped one does, and is then
 * simply not drawn. That holds for an engine that clips for itself too, which
 * is why the clip_in_core test is inside the loop rather than a shortcut
 * around it.
 */
int v9x_r3d_draw_list(const V9X_R3D_LIST *list,
                      const V9X_R3D_VERTEX *vertices,
                      v9x_u32 triangle_count)
{
    V9X_R3D_VERTEX clipped[V9X_R3D_CLIP_MAX_VERTICES];
    V9X_R3D_VERTEX fan_list[V9X_R3D_MAX_FAN_TRIANGLES * 3u];
    v9x_u32 run_start = 0ul;
    v9x_u32 index;
    int ok = 1;

    for (index = 0ul; index < triangle_count; ++index) {
        const V9X_R3D_VERTEX *triangle = &vertices[index * 3ul];
        v9x_u32 fan_triangles = 0ul;
        int culled = list->culled(list->user, triangle);
        int clipped_count;
        int fan;

        if (!culled && (list->clip_in_core == 0ul ||
                        v9x_r3d_triangle_on_target(triangle, list->width,
                                                   list->height))) {
            continue;
        }
        if (index > run_start &&
            !list->batch(list->user, &vertices[run_start * 3ul],
                         index - run_start)) {
            ok = 0;
        }
        run_start = index + 1ul;
        if (culled) {
            continue;
        }

        clipped_count = v9x_r3d_clip_triangle(triangle, list->guard_limit,
                                              list->width, list->height,
                                              clipped);
        if (clipped_count < 0) {
            ok = 0;
            continue;
        }
        for (fan = 1; fan + 1 < clipped_count; ++fan) {
            fan_list[fan_triangles * 3ul] = clipped[0];
            fan_list[fan_triangles * 3ul + 1ul] = clipped[fan];
            fan_list[fan_triangles * 3ul + 2ul] = clipped[fan + 1];
            ++fan_triangles;
        }
        if (fan_triangles != 0ul &&
            !list->batch(list->user, fan_list, fan_triangles)) {
            ok = 0;
        }
    }
    if (run_start < triangle_count &&
        !list->batch(list->user, &vertices[run_start * 3ul],
                     triangle_count - run_start)) {
        ok = 0;
    }
    return ok;
}
