#ifndef VELOCITY9X_RAGE2_REFERENCE_H
#define VELOCITY9X_RAGE2_REFERENCE_H

#include "velocity9x/ati_rage2.h"

/* Whether the triangle covers pixel (x, y): centre sampling, top-left
 * ties. See rage2_reference.c. */
int v9x_r2_ref_covers(const struct v9x_r2_vertex *v, v9x_s32 x, v9x_s32 y);

/* The engine's measured edge walk for one trapezoid: each row's span is
 * [lead[row], trail[row]). Both arrays hold trap->length entries. */
void v9x_r2_ref_walk(const struct v9x_r2_flat_trap *trap, v9x_s32 *lead,
                     v9x_s32 *trail);

/* Every pixel the trapezoid draws, with its S and T as the engine walks
 * them (v9x_r2_st_*): fn(context, x, y, s, t). 0 if the trapezoid is
 * taller than the walk's buffers (2048 rows). */
typedef void (*v9x_r2_ref_texel_fn)(void *context, v9x_s32 x, v9x_s32 y,
                                    v9x_u32 s, v9x_u32 t);
int v9x_r2_ref_walk_st(const struct v9x_r2_flat_trap *trap,
                       const struct v9x_r2_st *st, v9x_r2_ref_texel_fn fn,
                       void *context);

#endif
