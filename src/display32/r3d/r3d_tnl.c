/*
 * The T&L key's validity rule (include\velocity9x\r3d_tnl.h). Shared by
 * every front end that builds a key and every engine that compiles one.
 *
 * The rule is strict on purpose: engines cache programs by the key's bytes,
 * so a key with a stray bit past a count would compile the same program
 * under a second cache entry, or - worse - an engine that reads the stray
 * bit would build a different program for what the front end meant as the
 * same state. Refusing such a key at the boundary keeps "equal state, equal
 * bytes" true.
 */
#include "velocity9x/r3d_tnl.h"

#define V9X_R3D_TNL_SOURCES_MASK 0x000000FFul /* four terms, two bits each */

v9x_status v9x_r3d_tnl_key_check(const V9X_R3D_TNL_KEY *key)
{
    v9x_u32 used_types;
    v9x_u32 index;

    if (key == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (key->model != V9X_R3D_TNL_MODEL_D3D7 &&
        key->model != V9X_R3D_TNL_MODEL_GL11) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((key->flags & ~V9X_R3D_TNL_FLAGS_ALL) != 0ul ||
        (key->inputs & ~V9X_R3D_TNL_IN_ALL) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Direct3D has no two-sided lighting (spec note, section 4). */
    if ((key->flags & V9X_R3D_TNL_TWO_SIDED) != 0ul &&
        key->model != V9X_R3D_TNL_MODEL_GL11) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    if (key->texture_count > (v9x_u32)V9X_R3D_TNL_MAX_TEXTURES ||
        (key->texture_matrix >> key->texture_count) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Each light named by the count has a defined type; nothing past it. */
    if (key->light_count > (v9x_u32)V9X_R3D_TNL_MAX_LIGHTS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    used_types = key->light_count == 0ul
        ? 0ul
        : (key->light_types & (0xFFFFFFFFul >> (32u - 2u * key->light_count)));
    if (used_types != key->light_types) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < key->light_count; ++index) {
        if (V9X_R3D_TNL_LIGHT_TYPE(key->light_types, index) == 0ul) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }

    /* Four terms, each the material or one of the two colours. */
    if ((key->material_sources & ~V9X_R3D_TNL_SOURCES_MASK) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < 4ul; ++index) {
        if (V9X_R3D_TNL_SOURCE(key->material_sources, index) >
            V9X_R3D_TNL_SOURCE_COLOR1) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }

    if (key->fog_mode > V9X_R3D_TNL_FOG_EXP2 ||
        key->fog_distance > V9X_R3D_TNL_FOG_RANGE) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0ul; index < 6ul; ++index) {
        if (key->reserved[index] != 0ul) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    return V9X_STATUS_OK;
}
