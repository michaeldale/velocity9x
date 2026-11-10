/*
 * The vbe screen shadow, 16-bit half (shadow16.c). Built only into families
 * whose manifest defines V9X_SCREEN_SHADOW.
 */
#ifndef V9X_SHADOW16_H
#define V9X_SHADOW16_H

#include "win9x_display_abi.h"

/*
 * Allocate (or keep) a shadow for a width x height screen at this depth and
 * pitch, clear it and the frame buffer to agree, and return its selector for
 * the screen PDEVICE's bits. 0 when there is no shadow: the PDEVICE then draws
 * to the frame buffer as it always did.
 */
WORD v9x_shadow16_enable(WORD width, WORD height, WORD bits_per_pixel,
                         DWORD pitch);

/* Free the shadow. GDI draws to the frame buffer again after the next Enable
 * that does not allocate one. */
void v9x_shadow16_disable(void);

/* V9X_TRUE while the screen PDEVICE draws into the shadow. */
WORD v9x_shadow16_active(void);

/* The shadow's linear address for the HAL, 0 when there is none. */
DWORD v9x_shadow16_linear(void);

/* Copy the whole shadow to the frame buffer: after a mode restore, the
 * shadow still holds the desktop the frame buffer has lost. */
void v9x_shadow16_flush_all(void);

/* The screen PDEVICE's deBeginAccess and deEndAccess while the shadow is
 * active. */
WORD __loadds FAR PASCAL V9xShadowBeginAccess(void FAR *device,
                                              WORD left, WORD top,
                                              WORD right, WORD bottom,
                                              WORD flags);
WORD __loadds FAR PASCAL V9xShadowEndAccess(void FAR *device, WORD flags);

/* Output (ordinal 8) on this family; the thunk in dib_thunks.asm jumps here
 * with Output's own frame. */
WORD __loadds FAR PASCAL V9xShadowOutput(void FAR *device, WORD style,
                                         WORD count, void FAR *points,
                                         void FAR *pen, void FAR *brush,
                                         void FAR *draw_mode,
                                         void FAR *clip);

/* Called by the cursor thunks in dib_thunks.asm after the DIB Engine has
 * drawn the cursor into the shadow. */
void __loadds FAR PASCAL V9xShadowSetCursor(void FAR *shape);
void __loadds FAR PASCAL V9xShadowMoveCursor(WORD x, WORD y);
void __loadds FAR PASCAL V9xShadowCheckCursor(void);

#endif
