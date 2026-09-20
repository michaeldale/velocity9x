/*
 * Direct3D depth comparison functions, as the Gen3 S6 field encodes them.
 *
 * The engine emitted COMPAREFUNC_LESS and nothing else, and skipped the
 * depth test outright for any other function. intel98 measured what that
 * costs: 3DMark99 asks for D3DCMP_LESSEQUAL, so 192,069 of 224,838 draws -
 * eighty-five per cent of the run - went to the ring with no depth test at
 * all, and the photograph of the netbook shows one correctly textured
 * object against an otherwise empty scene. Without a depth test geometry
 * paints in submission order and whatever is drawn late covers what came
 * before.
 *
 * The hardware was never the limit. S6 bits 18:16 hold a three-bit function
 * and the part implements all eight; the driver simply named one.
 *
 * The table is transcribed from Mesa 20.3.5,
 * src/gallium/drivers/i915/i915_reg.h and i915_state_inlines.h, which is
 * the same source intel_gen3_3d.h already cites for COMPAREFUNC_LESS being
 * 2. It lives here rather than in the engine because it is a pure mapping
 * over eight values, and the project's rule is that such things are tested
 * on the host rather than reasoned about - an off-by-one in a depth
 * comparison does not crash, it renders the wrong picture on a machine that
 * has to be walked to.
 */
#ifndef VELOCITY9X_I9XX_DEPTH_H
#define VELOCITY9X_I9XX_DEPTH_H

#include "velocity9x/types.h"

/*
 * COMPAREFUNC_*, from i915_reg.h. ALWAYS is zero, which is why a caller
 * cannot use zero to mean "no answer" and why the function below reports
 * failure separately.
 */
#define V9X_I9XX_COMPAREFUNC_ALWAYS_V    ((v9x_u32)0ul)
#define V9X_I9XX_COMPAREFUNC_NEVER_V     ((v9x_u32)1ul)
#define V9X_I9XX_COMPAREFUNC_LESS_V      ((v9x_u32)2ul)
#define V9X_I9XX_COMPAREFUNC_EQUAL_V     ((v9x_u32)3ul)
#define V9X_I9XX_COMPAREFUNC_LEQUAL_V    ((v9x_u32)4ul)
#define V9X_I9XX_COMPAREFUNC_GREATER_V   ((v9x_u32)5ul)
#define V9X_I9XX_COMPAREFUNC_NOTEQUAL_V  ((v9x_u32)6ul)
#define V9X_I9XX_COMPAREFUNC_GEQUAL_V    ((v9x_u32)7ul)

/*
 * The S6 function for a D3DCMP_* value.
 *
 * Returns V9X_TRUE and writes the encoding, or V9X_FALSE for a value
 * outside D3DCMP's range and leaves the output alone. A function this does
 * not know must NOT fall back to ALWAYS: that draws every fragment and is a
 * worse picture than declining the depth test, which at least leaves the
 * caller free to decide. Mesa's own default arm returns ALWAYS because its
 * input is a validated enumeration; a HAL's input is whatever an
 * application put in a render state.
 */
v9x_u16 v9x_i9xx_depth_func(v9x_u32 d3d_compare, v9x_u32 *encoded_out);

#endif /* VELOCITY9X_I9XX_DEPTH_H */
