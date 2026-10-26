/*
 * The reference evaluator for include\velocity9x\r3d_tnl.h: one vertex
 * through a key and a constant block, in plain C with the C library's
 * float maths.
 *
 * It is the oracle the engine programs are checked against - in the host
 * tests here, and on the guest by comparing what a kernel wrote with what
 * this computes for the same input. It is not linked into the HAL or the
 * ICD (docs\plans\hardware-tnl.md, "Parked": a CPU back end would be this
 * file, wired in). Where the two APIs' equations differ it follows the
 * specification note, section 4, and the key's model.
 *
 * Every colour leaves clamped to [0, 1], and the fog factor too (1 is no
 * fog). The output's colour 0 is Direct3D's diffuse and OpenGL's primary;
 * colour 1 is Direct3D's specular and, for OpenGL, is zero when lit (1.1
 * adds the specular term into the primary). A lit colour 1's alpha is zero:
 * Direct3D carries fog there, and the engine packs the fog factor into it.
 */
#ifndef VELOCITY9X_R3D_TNL_REF_H
#define VELOCITY9X_R3D_TNL_REF_H

#include "velocity9x/r3d_tnl.h"

/* One vertex as the VF would deliver it: absent attributes are ignored. */
typedef struct v9x_r3d_tnl_input {
    float position[4];
    float normal[4];
    float color[2][4];
    float texture[V9X_R3D_TNL_MAX_TEXTURES][4];
} V9X_R3D_TNL_INPUT;

typedef struct v9x_r3d_tnl_output {
    float clip[4];
    float color[2][4];      /* front */
    float back_color[2][4]; /* two-sided only; otherwise the front's */
    float fog;
    float texture[V9X_R3D_TNL_MAX_TEXTURES][4];
} V9X_R3D_TNL_OUTPUT;

/* V9X_STATUS_INVALID_ARGUMENT for a null pointer or a key that fails
 * v9x_r3d_tnl_key_check; the output is then untouched. */
v9x_status v9x_r3d_tnl_ref_vertex(const V9X_R3D_TNL_KEY *key,
                                  const V9X_R3D_TNL_CONSTANTS *constants,
                                  const V9X_R3D_TNL_INPUT *input,
                                  V9X_R3D_TNL_OUTPUT *output);

#endif
