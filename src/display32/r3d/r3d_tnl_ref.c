/*
 * The reference T&L evaluator (r3d_tnl_ref.h). Host tests only, so it uses
 * the C library's float maths freely; nothing here is fast or meant to be.
 *
 * The order of work is the order an engine program follows, so a mismatch
 * on the guest can be located by comparing intermediate values:
 *   1. clip position: model_view_projection rows against the position;
 *   2. eye position: model_view rows (lighting and fog read it);
 *   3. texture coordinates, through their matrix when the key says so;
 *   4. fog factor;
 *   5. colours: passed through when unlit; otherwise the lighting sum per
 *      face, with N.L <= 0 giving neither diffuse nor specular (both APIs:
 *      Mesa's TL:173 and the spec note's Direct3D equations).
 */
#include <math.h>
#include <string.h>

#include "r3d_tnl_ref.h"

static float v9x_tnl_dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float v9x_tnl_dot4(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

/* Scale a 3-vector to unit length; a zero vector stays zero. */
static void v9x_tnl_normalize3(float *v)
{
    float length = (float)sqrt((double)v9x_tnl_dot3(v, v));

    if (length > 0.0f) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

static float v9x_tnl_clamp01(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static void v9x_tnl_clamp4(float *out, const float *in)
{
    unsigned int i;

    for (i = 0u; i < 4u; ++i) {
        out[i] = v9x_tnl_clamp01(in[i]);
    }
}

/* The colour a material term reads: the material, or a vertex colour (its
 * default when the vertex lacks it). */
static const float *v9x_tnl_term(const V9X_R3D_TNL_KEY *key,
                                 const float *material, const float *color0,
                                 const float *color1, unsigned int term)
{
    v9x_u32 source = V9X_R3D_TNL_SOURCE(key->material_sources, term);

    if (source == V9X_R3D_TNL_SOURCE_COLOR0) {
        return color0;
    }
    if (source == V9X_R3D_TNL_SOURCE_COLOR1) {
        return color1;
    }
    return material;
}

static float v9x_tnl_fog(const V9X_R3D_TNL_KEY *key,
                         const V9X_R3D_TNL_CONSTANTS *c, const float *eye)
{
    float distance;
    float scaled;

    if (key->fog_mode == V9X_R3D_TNL_FOG_NONE) {
        return 1.0f;
    }
    if (key->fog_distance == V9X_R3D_TNL_FOG_RANGE) {
        distance = (float)sqrt((double)v9x_tnl_dot3(eye, eye));
    } else {
        distance = eye[2] < 0.0f ? -eye[2] : eye[2];
    }
    if (key->fog_mode == V9X_R3D_TNL_FOG_LINEAR) {
        return v9x_tnl_clamp01((c->fog[1] - distance) * c->fog[3]);
    }
    scaled = c->fog[2] * distance;
    if (key->fog_mode == V9X_R3D_TNL_FOG_EXP2) {
        scaled *= scaled;
    }
    return v9x_tnl_clamp01((float)exp(-(double)scaled));
}

/*
 * The spot factor for a light toward which the unit vector L points. The
 * cone is measured on -L against the spot's axis (spec note, section 4).
 */
static float v9x_tnl_spot(const V9X_R3D_TNL_KEY *key,
                          const V9X_R3D_TNL_LIGHT *light, const float *l)
{
    float rho = -v9x_tnl_dot3(l, light->direction);

    if (key->model == V9X_R3D_TNL_MODEL_GL11) {
        if (rho < light->spot[0]) {
            return 0.0f;
        }
        return (float)pow((double)rho, (double)light->spot[1]);
    }
    if (rho > light->spot[0]) {
        return 1.0f;
    }
    if (rho <= light->spot[1]) {
        return 0.0f;
    }
    return (float)pow((double)((rho - light->spot[1]) * light->spot[3]),
                      (double)light->spot[2]);
}

/*
 * One face's lit colours. sum receives emissive + ambient + diffuse with
 * the diffuse term's alpha; specular receives the specular sum, alpha 0.
 */
static void v9x_tnl_light_face(const V9X_R3D_TNL_KEY *key,
                               const V9X_R3D_TNL_CONSTANTS *c,
                               const V9X_R3D_TNL_MATERIAL *material,
                               const float *normal, const float *eye,
                               const float *color0, const float *color1,
                               float *sum, float *specular)
{
    const float *diffuse_src;
    const float *specular_src;
    const float *ambient_src;
    const float *emissive_src;
    float view[3];
    unsigned int i;
    unsigned int k;

    diffuse_src = v9x_tnl_term(key, material->diffuse, color0, color1,
                               V9X_R3D_TNL_TERM_DIFFUSE);
    specular_src = v9x_tnl_term(key, material->specular, color0, color1,
                                V9X_R3D_TNL_TERM_SPECULAR);
    ambient_src = v9x_tnl_term(key, material->ambient, color0, color1,
                               V9X_R3D_TNL_TERM_AMBIENT);
    emissive_src = v9x_tnl_term(key, material->emissive, color0, color1,
                                V9X_R3D_TNL_TERM_EMISSIVE);

    /* The direction toward the viewer: from the vertex to the eye at the
     * origin, or the front end's constant for an infinite viewer. */
    if ((key->flags & V9X_R3D_TNL_LOCAL_VIEWER) != 0ul) {
        view[0] = -eye[0];
        view[1] = -eye[1];
        view[2] = -eye[2];
        v9x_tnl_normalize3(view);
    } else {
        view[0] = c->viewer[0];
        view[1] = c->viewer[1];
        view[2] = c->viewer[2];
    }

    for (k = 0u; k < 3u; ++k) {
        sum[k] = emissive_src[k] + ambient_src[k] * c->scene_ambient[k];
        specular[k] = 0.0f;
    }

    for (i = 0u; i < (unsigned int)key->light_count; ++i) {
        const V9X_R3D_TNL_LIGHT *light = &c->light[i];
        v9x_u32 type = V9X_R3D_TNL_LIGHT_TYPE(key->light_types, i);
        float l[3];
        float factor = 1.0f;
        float n_dot_l;

        if (type == V9X_R3D_TNL_LIGHT_DIRECTIONAL) {
            l[0] = light->position[0];
            l[1] = light->position[1];
            l[2] = light->position[2];
        } else {
            float distance;
            float denominator;

            l[0] = light->position[0] - eye[0];
            l[1] = light->position[1] - eye[1];
            l[2] = light->position[2] - eye[2];
            distance = (float)sqrt((double)v9x_tnl_dot3(l, l));
            if (distance > light->attenuation[3]) {
                continue;
            }
            v9x_tnl_normalize3(l);
            denominator = light->attenuation[0] +
                          light->attenuation[1] * distance +
                          light->attenuation[2] * distance * distance;
            /* Both APIs require a positive denominator; a front end that
             * passes zero gets no light rather than an infinity. */
            if (denominator <= 0.0f) {
                continue;
            }
            factor = 1.0f / denominator;
            if (type == V9X_R3D_TNL_LIGHT_SPOT) {
                factor *= v9x_tnl_spot(key, light, l);
            }
        }
        if (factor == 0.0f) {
            continue;
        }

        for (k = 0u; k < 3u; ++k) {
            sum[k] += factor * light->ambient[k] * ambient_src[k];
        }
        n_dot_l = v9x_tnl_dot3(normal, l);
        if (n_dot_l <= 0.0f) {
            continue;
        }
        for (k = 0u; k < 3u; ++k) {
            sum[k] += factor * n_dot_l * light->diffuse[k] * diffuse_src[k];
        }
        if ((key->flags & V9X_R3D_TNL_SPECULAR) != 0ul) {
            float h[3];
            float n_dot_h;
            float term;

            h[0] = l[0] + view[0];
            h[1] = l[1] + view[1];
            h[2] = l[2] + view[2];
            v9x_tnl_normalize3(h);
            n_dot_h = v9x_tnl_dot3(normal, h);
            if (n_dot_h > 0.0f) {
                term = factor * (float)pow((double)n_dot_h,
                                           (double)material->power[0]);
                for (k = 0u; k < 3u; ++k) {
                    specular[k] += term * light->specular[k] *
                                   specular_src[k];
                }
            }
        }
    }
    sum[3] = diffuse_src[3];
    specular[3] = 0.0f;
}

/* A face's two output colours from its sums, per the key's model. */
static void v9x_tnl_face_out(const V9X_R3D_TNL_KEY *key, float *sum,
                             const float *specular, float *out0, float *out1)
{
    static const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    unsigned int k;

    if (key->model == V9X_R3D_TNL_MODEL_GL11) {
        /* 1.1 adds specular into the primary, before texturing. */
        for (k = 0u; k < 3u; ++k) {
            sum[k] += specular[k];
        }
        v9x_tnl_clamp4(out0, sum);
        v9x_tnl_clamp4(out1, zero);
        return;
    }
    v9x_tnl_clamp4(out0, sum);
    v9x_tnl_clamp4(out1, specular);
}

v9x_status v9x_r3d_tnl_ref_vertex(const V9X_R3D_TNL_KEY *key,
                                  const V9X_R3D_TNL_CONSTANTS *constants,
                                  const V9X_R3D_TNL_INPUT *input,
                                  V9X_R3D_TNL_OUTPUT *output)
{
    const float *color0;
    const float *color1;
    float eye[4];
    float normal[3];
    float sum[4];
    float specular[4];
    unsigned int row;
    unsigned int unit;

    if (constants == 0 || input == 0 || output == 0 ||
        v9x_r3d_tnl_key_check(key) != V9X_STATUS_OK) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    memset(output, 0, sizeof(*output));

    for (row = 0u; row < 4u; ++row) {
        output->clip[row] = v9x_tnl_dot4(constants->model_view_projection[row],
                                         input->position);
        eye[row] = v9x_tnl_dot4(constants->model_view[row], input->position);
    }

    for (unit = 0u; unit < (unsigned int)key->texture_count; ++unit) {
        if ((key->texture_matrix & (1ul << unit)) != 0ul) {
            for (row = 0u; row < 4u; ++row) {
                output->texture[unit][row] =
                    v9x_tnl_dot4(constants->texture[unit][row],
                                 input->texture[unit]);
            }
        } else {
            memcpy(output->texture[unit], input->texture[unit],
                   sizeof(output->texture[unit]));
        }
    }

    output->fog = v9x_tnl_fog(key, constants, eye);

    color0 = (key->inputs & V9X_R3D_TNL_IN_COLOR0) != 0ul
        ? input->color[0] : constants->default_color[0];
    color1 = (key->inputs & V9X_R3D_TNL_IN_COLOR1) != 0ul
        ? input->color[1] : constants->default_color[1];

    if ((key->flags & V9X_R3D_TNL_LIGHTING) == 0ul) {
        v9x_tnl_clamp4(output->color[0], color0);
        v9x_tnl_clamp4(output->color[1], color1);
        memcpy(output->back_color, output->color, sizeof(output->color));
        return V9X_STATUS_OK;
    }

    /* The eye-space normal: the normal matrix's rows against the input. A
     * key without normals lights a zero normal, which gets ambient and
     * emissive only - what both APIs do with a normal of length zero. */
    for (row = 0u; row < 3u; ++row) {
        normal[row] = (key->inputs & V9X_R3D_TNL_IN_NORMAL) != 0ul
            ? v9x_tnl_dot3(constants->normal[row], input->normal)
            : 0.0f;
    }
    if ((key->flags & V9X_R3D_TNL_NORMALIZE) != 0ul) {
        v9x_tnl_normalize3(normal);
    }

    v9x_tnl_light_face(key, constants, &constants->material[0], normal, eye,
                       color0, color1, sum, specular);
    v9x_tnl_face_out(key, sum, specular, output->color[0], output->color[1]);

    if ((key->flags & V9X_R3D_TNL_TWO_SIDED) == 0ul) {
        memcpy(output->back_color, output->color, sizeof(output->color));
        return V9X_STATUS_OK;
    }
    normal[0] = -normal[0];
    normal[1] = -normal[1];
    normal[2] = -normal[2];
    v9x_tnl_light_face(key, constants, &constants->material[1], normal, eye,
                       color0, color1, sum, specular);
    v9x_tnl_face_out(key, sum, specular, output->back_color[0],
                     output->back_color[1]);
    return V9X_STATUS_OK;
}
