#ifndef VELOCITY9X_RAGE2_REFERENCE_H
#define VELOCITY9X_RAGE2_REFERENCE_H

#include "velocity9x/ati_rage2.h"

/* Whether the triangle covers pixel (x, y): centre sampling, top-left
 * ties. See rage2_reference.c. */
int v9x_r2_ref_covers(const struct v9x_r2_vertex *v, v9x_s32 x, v9x_s32 y);

#endif
