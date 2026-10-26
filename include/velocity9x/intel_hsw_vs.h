/*
 * Haswell (Gen7.5) vertex kernels generated from a T&L key
 * (docs\plans\hardware-tnl.md, step 4; include\velocity9x\r3d_tnl.h).
 *
 * One SIMD4x2 kernel per key: transform, eye position, normal, the lights
 * the key names in either API's model, the material sources, specular, fog
 * and the texture matrices, unrolled for that key so the kernel has no flow
 * control. Pure encoding, as hsw_eu.c is: the words go wherever the caller
 * points, and tests\host\test_intel_hsw_vs.c runs them in a host EU model
 * against the reference evaluator (r3d_tnl_ref.c).
 *
 * The register map, fixed except for where the inputs start:
 *
 *   g0          the thread payload header
 *   g1..gC      push constants: the vec4s of V9X_R3D_TNL_CONSTANTS the
 *               kernel reads, two to a GRF, in the program's map order;
 *               C = constant_grfs
 *   g(C+1)..    the VF's inputs, one vec4 each: object position, colour 0,
 *               colour 1, texture coordinates 0 and 1, then the normal
 *               when the key has one
 *   after them  temporaries
 *   g112        the URB write header
 *   g113        the VUE header slot, zero
 *   g114        the clip-space position
 *   g115..      the attributes, in the order SBE reads them:
 *                 one-sided: colour 0, colour 1, texture 0, texture 1
 *                 two-sided: colour 0, back colour 0, colour 1, back
 *                            colour 1, texture 0, texture 1
 *
 * Colours leave saturated. Colour 1's alpha carries the fog factor when the
 * key fogs, as Direct3D's specular alpha does and the pixel kernels read it
 * (hsw_eu.c); otherwise it is the unlit colour's alpha, or zero when lit.
 *
 * Every instruction used is from the subset intel_hsw_eu.h encodes, with
 * MATH in Align16 and source swizzles on it, which Gen7 allows (Mesa's
 * vec4 backend relies on it; Gen6 did not). None of this has executed on
 * hardware until the step 4 gate says so.
 */
#ifndef VELOCITY9X_INTEL_HSW_VS_H
#define VELOCITY9X_INTEL_HSW_VS_H

#include "velocity9x/status.h"
#include "velocity9x/types.h"
#include "velocity9x/r3d_tnl.h"

/* VF inputs without and with a normal. */
#define V9X_HSW_VSK_INPUTS        ((v9x_u8)5u)
#define V9X_HSW_VSK_INPUTS_NORMAL ((v9x_u8)6u)
/* VUE data GRFs after the URB header: VUE header, position, attributes. */
#define V9X_HSW_VSK_OUTPUTS          ((v9x_u8)6u)
#define V9X_HSW_VSK_OUTPUTS_TWO_SIDED ((v9x_u8)8u)
/* The most push-constant GRFs a program reads: the whole block. */
#define V9X_HSW_VSK_CONSTANT_GRFS_MAX ((v9x_u8)(V9X_R3D_TNL_VEC4S / 2u))

struct v9x_hsw_vs_program {
    v9x_u32 dwords;          /* kernel words written */
    v9x_u8 constant_grfs;    /* GRFs of push constants, at least 1 */
    v9x_u8 inputs;           /* V9X_HSW_VSK_INPUTS or _INPUTS_NORMAL */
    v9x_u8 outputs;          /* V9X_HSW_VSK_OUTPUTS or _TWO_SIDED */
    v9x_u8 constant_count;   /* vec4s in constant_map */
    /* Pushed vec4 n is the block's vec4 constant_map[n]; an odd count
     * leaves the last GRF's high half unread. */
    v9x_u8 constant_map[V9X_R3D_TNL_VEC4S];
};

/*
 * Build the kernel for a key into out (capacity in dwords). out may be
 * null to measure: the program is then filled and nothing is written.
 * Each instruction is encoded in a local and stored with four plain
 * writes, so out may be uncached memory. INVALID_ARGUMENT for a key that
 * fails v9x_r3d_tnl_key_check; INSUFFICIENT_MEMORY when the kernel does
 * not fit capacity.
 */
v9x_status v9x_intel_hsw_vs_kernel_build(const V9X_R3D_TNL_KEY *key,
                                         v9x_u32 *out, v9x_u32 capacity,
                                         struct v9x_hsw_vs_program *program);

/* The push constants a program reads, constant_grfs * 8 dwords, from a
 * block. The unread half of a last odd GRF is zero. */
void v9x_intel_hsw_vs_constants(const struct v9x_hsw_vs_program *program,
                                const V9X_R3D_TNL_CONSTANTS *block,
                                v9x_u32 *out);

#endif
