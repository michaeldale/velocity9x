/*
 * Engine identity and capability values.
 *
 * These are the one vocabulary shared by all three execution contexts: the
 * 16-bit chip modules that fill the engine descriptor, the 16-bit driver that
 * clamps its DirectDraw caps from it, and the flat 32-bit HAL that selects its
 * engine ops from it. They are therefore in a header of their own, with no
 * includes and no dependency on <windows.h>, so that
 * include\velocity9x\hw16.h can stay free of the OS boundary and
 * include\velocity9x\win9x_ddraw_abi.h no longer has to reach into the
 * 16-bit hardware layer to find them.
 *
 * engine_type names the chip's drawing engine and selects a code path.
 * engine_caps says what that engine will do and is a mask, so a chip can
 * carry an engine with only part of the family's capability set.
 */
#ifndef VELOCITY9X_ENGINE_ABI_H
#define VELOCITY9X_ENGINE_ABI_H

#define V9X_DD_ENGINE_TYPE_NONE         0ul
#define V9X_DD_ENGINE_TYPE_S3_VIRGE_DX  1ul
#define V9X_DD_ENGINE_TYPE_S3_TRIO64    2ul

#define V9X_DD_ENGINE_CAP_SOLID_FILL    0x00000001ul
#define V9X_DD_ENGINE_CAP_SCREEN_COPY   0x00000002ul
#define V9X_DD_ENGINE_CAP_FLIP          0x00000004ul
#define V9X_DD_ENGINE_CAP_VBLANK        0x00000008ul
#define V9X_DD_ENGINE_CAP_D3D           0x00000010ul

#endif /* VELOCITY9X_ENGINE_ABI_H */
