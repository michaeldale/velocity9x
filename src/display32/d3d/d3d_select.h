/*
 * Which Direct3D engine serves this chip: the one decision both
 * v9x_d3d_engine() (every call) and v9x_d3d_publish_engine() (DriverInit)
 * make, as a pure function so tests\host\test_d3d_select.c can hold it to
 * every engine type.
 *
 * Fail closed. An engine type with no implementation in this binary selects
 * nothing: no caps are published for it and every entry point declines. The
 * earlier publish-time default to the ViRGE is gone; it existed because
 * engine_type was unreadable at DriverInit, and dd16.c's
 * v9x_dd_stamp_engine_caps now stamps it before DriverInit for every chip
 * that has an engine descriptor.
 */
#ifndef VELOCITY9X_D3D_SELECT_H
#define VELOCITY9X_D3D_SELECT_H

#include "velocity9x/types.h"

#define V9X_D3D_SELECT_NONE     0ul
#define V9X_D3D_SELECT_SOFTWARE 1ul
#define V9X_D3D_SELECT_VIRGE    2ul
#define V9X_D3D_SELECT_GEN3     3ul

/*
 * valid is non-zero when the descriptor carries V9X_DD_ENGINE_VALID. The
 * software rasterizer is chosen by capability, before validity, because it
 * serves chips whose descriptor names no engine at all. A hardware engine
 * needs a valid descriptor naming its type.
 */
v9x_u32 v9x_d3d_select_engine(int valid, v9x_u32 engine_type,
                              v9x_u32 engine_caps);

#endif
