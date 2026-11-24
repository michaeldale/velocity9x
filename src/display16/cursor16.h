/*
 * GDI's SetCursor and MoveCursor on the Gen3 hardware cursor: the interface
 * the rest of the 16-bit driver needs. The implementation is
 * src\display16\cursor16.c; see that file's header.
 */
#ifndef VELOCITY9X_CURSOR16_H
#define VELOCITY9X_CURSOR16_H

#include "velocity9x/leaf16.h"

/*
 * At Enable and ReEnable, from v9x_gdi_accel_configure, with what it read
 * from the chip's hooks: cursor16.c is in LEAFCODE and cannot call them
 * near. engine_type and the two windows are the engine descriptor's,
 * reserve is where the reserve_video_memory hook put the reserve for
 * vbe_bytes of video memory.
 */
void V9X_LEAF_FAR v9x_cursor_configure(unsigned long engine_type,
                                       unsigned long control_base,
                                       unsigned long control_bytes,
                                       unsigned long gtt_base,
                                       unsigned long reserve,
                                       unsigned long vbe_bytes,
                                       unsigned short device_id);

/* The HwCursor= word V9XHW.INI carries. */
const char FAR * V9X_LEAF_FAR v9x_cursor_state_text(void);

/* The overlay off while the screen is someone else's (Disable, a
 * full-screen DOS box), and back once this driver may draw again. Far, for
 * sswitch16.c's segment. */
void FAR PASCAL V9xCursorHide(void);
void FAR PASCAL V9xCursorRestore(void);

#endif
