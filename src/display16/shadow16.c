/*
 * The vbe screen shadow, 16-bit half (docs\plans\vbe-shadow-framebuffer.md).
 *
 * The screen PDEVICE's bits point at a copy of the desktop in locked system
 * memory, so everything the DIB Engine reads stays in memory. What it writes
 * is copied to the frame buffer here, at two kinds of moment:
 *
 *  - When the DIB Engine ends an access. It brackets its screen work with the
 *    PDEVICE's deBeginAccess(device, left, top, right, bottom, flags) and
 *    deEndAccess(device, flags) - the same callback it lifts the software
 *    cursor with, which is why the rectangle has to bound what it touches
 *    (98DDK\src\display\mini\s3v\ACCESS.ASM:55-120). The rectangles of a
 *    bracket are collected and the union is copied when the outermost one
 *    ends.
 *  - When the cursor is set, moved or checked. The DIB Engine draws the
 *    software cursor into the shadow from the mouse interrupt, outside any
 *    bracket, so the cursor thunks copy its old and new rectangles.
 *
 * The rectangle arithmetic is src\common\shadow.c, host-tested. This file
 * keeps state and calls the copy in runtime.asm. Nothing here touches a file
 * or a port, allocates, or waits: the cursor half runs at interrupt time.
 */
#include "velocity9x/shadow.h"
#include "win9x_display_abi.h"
#include "shadow16.h"

/* ddi.c's screen PDEVICE, already external for dib_thunks.asm's cursor and
 * PDevice forwards; null while the display is disabled. */
extern V9X_DIB_ENGINE FAR *v9x_driver_pdevice;

extern WORD FAR PASCAL V9xShadowAlloc(DWORD bytes);
extern void FAR PASCAL V9xShadowFree(void);
extern DWORD FAR PASCAL V9xShadowLinear(void);
extern void FAR PASCAL V9xShadowFlush(DWORD offset, WORD row_bytes,
                                      WORD rows, DWORD pitch);
extern void FAR PASCAL V9xShadowZero(void);
extern WORD FAR PASCAL V9xDibBeginAccessRect(void FAR *device,
                                             WORD left, WORD top,
                                             WORD right, WORD bottom,
                                             WORD flags);
extern WORD FAR PASCAL V9xDibEndAccessRect(void FAR *device, WORD flags);
extern WORD FAR PASCAL V9xDibOutputCall(void FAR *device, WORD style,
                                        WORD count, void FAR *points,
                                        void FAR *pen, void FAR *brush,
                                        void FAR *draw_mode,
                                        void FAR *clip);

/* The DDK's cursorShape header (98DDK\inc\win98\inc16\WINDEFS.INC:110-116):
 * hot spot, then the shape's size in pixels. Only these four words are read.
 * The shape's top-left is the position less the hot spot, as the XGA sample
 * places its sprite (98DDK\src\display\mini\xga\CURSOR.ASM:535-546). */
typedef struct v9x_cursor_shape {
    short csHotX;
    short csHotY;
    short csWidth;
    short csHeight;
} V9X_CURSOR_SHAPE;

static WORD v9x_shadow_on;
static WORD v9x_shadow_width;
static WORD v9x_shadow_height;
static WORD v9x_shadow_bpp;
static DWORD v9x_shadow_pitch;
static struct v9x_shadow_dirty v9x_shadow_pending;

/* While an Output call whose points bound it is in the DIB Engine, its
 * access rectangles are cut to that bound (V9xShadowOutput). */
static struct v9x_shadow_rect v9x_shadow_bound;
static WORD v9x_shadow_bounded;

/* The cursor as last set and moved. Written by the mouse interrupt; a torn
 * read costs a copy of the wrong 32x32, never a missed drawing call. */
static short v9x_cursor_x;
static short v9x_cursor_y;
static short v9x_cursor_hot_x;
static short v9x_cursor_hot_y;
static WORD v9x_cursor_width;
static WORD v9x_cursor_height;

static void v9x_shadow_copy(const struct v9x_shadow_rect *rect)
{
    struct v9x_shadow_span span;

    if (v9x_shadow_span(rect, v9x_shadow_bpp, v9x_shadow_pitch, &span) ==
        V9X_FALSE) {
        return;
    }
    V9xShadowFlush(span.offset, span.row_bytes, span.rows,
                   v9x_shadow_pitch);
}

/* Copy one rectangle on its own, without touching what a drawing call has
 * pending: this is the interrupt-time path. */
static void v9x_shadow_copy_alone(const struct v9x_shadow_rect *rect)
{
    struct v9x_shadow_dirty one;

    v9x_shadow_dirty_clear(&one);
    v9x_shadow_dirty_add_rect(&one, rect, v9x_shadow_width,
                              v9x_shadow_height);
    if (one.pending != V9X_FALSE) {
        v9x_shadow_copy(&one.rect);
    }
}

static void v9x_shadow_copy_cursor(void)
{
    struct v9x_shadow_rect rect;

    v9x_shadow_cursor_rect(v9x_cursor_x, v9x_cursor_y,
                           v9x_cursor_hot_x, v9x_cursor_hot_y,
                           v9x_cursor_width, v9x_cursor_height, &rect);
    v9x_shadow_copy_alone(&rect);
}

WORD v9x_shadow16_enable(WORD width, WORD height, WORD bits_per_pixel,
                         DWORD pitch)
{
    WORD selector;

    v9x_shadow_on = V9X_FALSE;
    selector = V9xShadowAlloc(pitch * (DWORD)height);
    if (selector == 0u) {
        return 0u;
    }

    v9x_shadow_width = width;
    v9x_shadow_height = height;
    v9x_shadow_bpp = bits_per_pixel;
    v9x_shadow_pitch = pitch;
    v9x_shadow_dirty_clear(&v9x_shadow_pending);
    v9x_shadow_pending.depth = 0u;

    /* Black in both, so they agree before the first repaint: whatever the
     * mode set left in the frame buffer is not in the shadow. */
    V9xShadowZero();
    v9x_shadow_on = V9X_TRUE;
    v9x_shadow16_flush_all();
    return selector;
}

void v9x_shadow16_disable(void)
{
    v9x_shadow_on = V9X_FALSE;
    V9xShadowFree();
}

WORD v9x_shadow16_active(void)
{
    return v9x_shadow_on;
}

DWORD v9x_shadow16_linear(void)
{
    if (v9x_shadow_on == V9X_FALSE) {
        return 0ul;
    }
    return V9xShadowLinear();
}

void v9x_shadow16_flush_all(void)
{
    struct v9x_shadow_dirty all;

    if (v9x_shadow_on == V9X_FALSE) {
        return;
    }
    v9x_shadow_dirty_clear(&all);
    v9x_shadow_dirty_add_all(&all, v9x_shadow_width, v9x_shadow_height);
    v9x_shadow_copy(&all.rect);
}

WORD __loadds FAR PASCAL V9xShadowBeginAccess(void FAR *device,
                                              WORD left, WORD top,
                                              WORD right, WORD bottom,
                                              WORD flags)
{
    if (v9x_shadow_on != V9X_FALSE) {
        v9x_shadow_dirty_add_within(&v9x_shadow_pending,
                                    (v9x_s16)left, (v9x_s16)top,
                                    (v9x_s16)right, (v9x_s16)bottom,
                                    v9x_shadow_bounded != V9X_FALSE ?
                                        &v9x_shadow_bound : 0,
                                    v9x_shadow_width, v9x_shadow_height);
        ++v9x_shadow_pending.depth;
    }
    return V9xDibBeginAccessRect(device, left, top, right, bottom, flags);
}

WORD __loadds FAR PASCAL V9xShadowEndAccess(void FAR *device, WORD flags)
{
    WORD result;

    /* The DIB Engine first, because ending the access is also where it puts
     * a lifted cursor back - into the shadow, so the copy below has to come
     * after it. Outside the drawn rectangle the cursor comes back exactly as
     * it was, so the copy need not reach the cursor's whole rectangle. */
    result = V9xDibEndAccessRect(device, flags);
    if (v9x_shadow_on == V9X_FALSE) {
        return result;
    }

    if (v9x_shadow_pending.depth != 0u) {
        --v9x_shadow_pending.depth;
    }
    /*
     * Copied at every end, nested or not, and cleared only at the outermost:
     * an inner end must not drop what the outer bracket drew before it, and
     * an end that never sees its begin (a depth that leaked) still copies
     * rather than freezing the screen.
     */
    if (v9x_shadow_pending.pending != V9X_FALSE) {
        v9x_shadow_copy(&v9x_shadow_pending.rect);
        if (v9x_shadow_pending.depth == 0u) {
            v9x_shadow_dirty_clear(&v9x_shadow_pending);
        }
    }
    return result;
}

/*
 * Ordinal 8, reached from the Output thunk in dib_thunks.asm with Output's
 * own frame. The DIB Engine brackets an Output call with a rectangle that
 * does not bound it: on the i5-4570 every line cost a copy of the whole
 * screen (2026-11-11). For the styles whose points do bound it, the
 * brackets inside the call are cut to that bound; every other style keeps
 * the DIB Engine's rectangle.
 */
WORD __loadds FAR PASCAL V9xShadowOutput(void FAR *device, WORD style,
                                         WORD count, void FAR *points,
                                         void FAR *pen, void FAR *brush,
                                         void FAR *draw_mode,
                                         void FAR *clip)
{
    WORD result;

    /* GDI calls this for the driver's own device bitmaps too. Only the
     * screen draws into the shadow, so only the screen is bounded or
     * copied; a bitmap's call passes straight through. */
    if (device != (void FAR *)v9x_driver_pdevice) {
        return V9xDibOutputCall(device, style, count, points, pen, brush,
                                draw_mode, clip);
    }

    if (v9x_shadow_on != V9X_FALSE &&
        v9x_shadow_output_bound(style, count, (const v9x_s16 FAR *)points,
                                &v9x_shadow_bound) != V9X_FALSE) {
        v9x_shadow_bounded = V9X_TRUE;
        /* Added here as well as through the brackets inside the call, so a
         * scanline drawn inside a series bracket (which adds nothing, see
         * shadow.h) is copied whether or not it has a bracket of its own. */
        v9x_shadow_dirty_add_rect(&v9x_shadow_pending, &v9x_shadow_bound,
                                  v9x_shadow_width, v9x_shadow_height);
    }
    result = V9xDibOutputCall(device, style, count, points, pen, brush,
                              draw_mode, clip);
    v9x_shadow_bounded = V9X_FALSE;

    /* Outside any bracket, nothing else will copy it: do it now. Inside one,
     * its end does. */
    if (v9x_shadow_on != V9X_FALSE && v9x_shadow_pending.depth == 0u &&
        v9x_shadow_pending.pending != V9X_FALSE) {
        v9x_shadow_copy(&v9x_shadow_pending.rect);
        v9x_shadow_dirty_clear(&v9x_shadow_pending);
    }
    return result;
}

void __loadds FAR PASCAL V9xShadowSetCursor(void FAR *shape)
{
    const V9X_CURSOR_SHAPE FAR *cursor = (const V9X_CURSOR_SHAPE FAR *)shape;

    if (v9x_shadow_on == V9X_FALSE) {
        return;
    }

    /* Where the old shape was, then where the new one is. */
    v9x_shadow_copy_cursor();
    if (cursor == 0 || cursor->csWidth <= 0 || cursor->csHeight <= 0) {
        v9x_cursor_width = 0u;
        v9x_cursor_height = 0u;
        return;
    }
    v9x_cursor_hot_x = cursor->csHotX;
    v9x_cursor_hot_y = cursor->csHotY;
    v9x_cursor_width = (WORD)cursor->csWidth;
    v9x_cursor_height = (WORD)cursor->csHeight;
    v9x_shadow_copy_cursor();
}

void __loadds FAR PASCAL V9xShadowMoveCursor(WORD x, WORD y)
{
    if (v9x_shadow_on == V9X_FALSE) {
        return;
    }

    v9x_shadow_copy_cursor();
    v9x_cursor_x = (short)x;
    v9x_cursor_y = (short)y;
    v9x_shadow_copy_cursor();
}

/* CheckCursor is where the DIB Engine draws a cursor it had to leave lifted,
 * so the current rectangle is copied whether or not anything changed: two
 * kilobytes or so, a few times a second. */
void __loadds FAR PASCAL V9xShadowCheckCursor(void)
{
    if (v9x_shadow_on == V9X_FALSE) {
        return;
    }
    v9x_shadow_copy_cursor();
}
