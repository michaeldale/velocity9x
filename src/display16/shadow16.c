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
extern void FAR PASCAL V9xShadowFetch(DWORD offset, WORD row_bytes,
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

/* What the HAL's engine copies left stale, in the DirectDraw shared block
 * (V9X_DD_SHARED.shadow_stale); null until dd16.c attaches it. */
static struct v9x_shadow_dirty FAR *v9x_shadow_stale;

/* The event log beside it (V9X_DD_SHARED.shadow_log), for V9XFBCMP. */
static struct v9x_shadow_event FAR *v9x_shadow_log;
static DWORD FAR *v9x_shadow_log_count;

/* And the count of entries that arrived with the direction flag set
 * (V9X_DD_SHARED.shadow_direction_set). */
static DWORD FAR *v9x_shadow_direction_set;

/* Append one event. No lock: the log is diagnostic, and an event lost to
 * the mouse interrupt or the HAL writing at the same moment costs a line. */
static void v9x_shadow_note(WORD kind, WORD detail,
                            const struct v9x_shadow_rect *rect)
{
    struct v9x_shadow_event FAR *event;

    if (v9x_shadow_log == 0 || v9x_shadow_log_count == 0) {
        return;
    }
    event = &v9x_shadow_log[(WORD)*v9x_shadow_log_count &
                            (V9X_SHADOW_LOG_EVENTS - 1u)];
    ++*v9x_shadow_log_count;
    event->kind = kind;
    event->detail = detail;
    if (rect != 0) {
        event->rect = *rect;
    } else {
        event->rect.left = 0;
        event->rect.top = 0;
        event->rect.right = 0;
        event->rect.bottom = 0;
    }
}

/* The same with a rectangle given by its sides. */
static void v9x_shadow_note_sides(WORD kind, WORD detail, v9x_s16 left,
                                  v9x_s16 top, v9x_s16 right, v9x_s16 bottom)
{
    struct v9x_shadow_rect rect;

    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    v9x_shadow_note(kind, detail, &rect);
}

/* deBeginAccess's flags (98DDK\inc\win98\inc16\DIBENG.INC:124-127): the
 * caller reads or writes the frame buffer, as against only excluding the
 * cursor (CURSOREXCLUDE, 8). The DDK samples' own engine drawing passes
 * CURSOREXCLUDE alone (s3v\RECT.ASM, framebuf\SPECIAL.ASM). */
#define V9X_SHADOW_FB_ACCESS 0x0001u

/* A bracket with FB_ACCESS began since the nest was last empty: what is
 * pending holds GDI drawing, not only cursor rectangles. */
static WORD v9x_shadow_drew;

/* A copy out is widened to whole dwords (v9x_shadow_span): up to three
 * pixels at 8 bpp either side. What is read back for it is widened as far,
 * so the widening never writes a stale pixel out. */
#define V9X_SHADOW_WIDEN_PIXELS 4

/*
 * Read a rectangle from the frame buffer into the shadow, exactly: not
 * widened to dwords as a copy out is, because a pixel beside the rectangle
 * may hold drawing not yet copied out. The products are 16 x 16 bits
 * (leaf16.h).
 */
static void v9x_shadow_fetch(const struct v9x_shadow_rect *rect)
{
    WORD bytes = (WORD)(v9x_shadow_bpp / 8u);

    if (rect->right <= rect->left || rect->bottom <= rect->top) {
        return;
    }
    V9xShadowFetch(V9X_LEAF_MUL16(rect->top, v9x_shadow_pitch) +
                       V9X_LEAF_MUL16(rect->left, bytes),
                   (WORD)V9X_LEAF_MUL16(rect->right - rect->left, bytes),
                   (WORD)(rect->bottom - rect->top), v9x_shadow_pitch);
}

/* A rectangle widened by V9X_SHADOW_WIDEN_PIXELS across, clipped. */
static void v9x_shadow_widen(const struct v9x_shadow_rect *rect,
                             struct v9x_shadow_rect *out)
{
    *out = *rect;
    out->left = (v9x_s16)(rect->left > V9X_SHADOW_WIDEN_PIXELS ?
                          rect->left - V9X_SHADOW_WIDEN_PIXELS : 0);
    out->right = (v9x_s16)(rect->right + V9X_SHADOW_WIDEN_PIXELS);
    if (out->right > (v9x_s16)v9x_shadow_width) {
        out->right = (v9x_s16)v9x_shadow_width;
    }
}

/* GDI is about to read or draw `rect`, or copy it out: when that touches
 * the stale rectangle, read all of it back (v9x_shadow_stale_take). */
static void v9x_shadow_take_stale(const struct v9x_shadow_rect *rect)
{
    struct v9x_shadow_rect wide;
    struct v9x_shadow_rect fetch;

    if (v9x_shadow_stale == 0) {
        return;
    }
    v9x_shadow_widen(rect, &wide);
    if (v9x_shadow_stale_take(v9x_shadow_stale, &wide, &fetch) !=
        V9X_FALSE) {
        v9x_shadow_note(V9X_SHADOW_EVENT_TAKE, 0u, &fetch);
        v9x_shadow_fetch(&fetch);
    }
}

/* The cursor is about to be drawn or lifted over `rect`: read back the
 * stale part of it only (v9x_shadow_stale_overlap); interrupt time. */
static void v9x_shadow_overlap_stale(const struct v9x_shadow_rect *rect)
{
    struct v9x_shadow_rect wide;
    struct v9x_shadow_rect fetch;

    if (v9x_shadow_stale == 0) {
        return;
    }
    v9x_shadow_widen(rect, &wide);
    if (v9x_shadow_stale_overlap(v9x_shadow_stale, &wide, &fetch) !=
        V9X_FALSE) {
        v9x_shadow_note(V9X_SHADOW_EVENT_OVERLAP, 0u, &fetch);
        v9x_shadow_fetch(&fetch);
    }
}

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

WORD V9X_LEAF_FAR v9x_shadow16_enable(WORD width, WORD height,
                                      WORD bits_per_pixel, DWORD pitch)
{
    WORD selector;

    v9x_shadow_on = V9X_FALSE;
    /* 16 x 16 bits, inline (leaf16.h): a mode's pitch and height. */
    selector = V9xShadowAlloc(V9X_LEAF_MUL16(pitch, height));
    if (selector == 0u) {
        return 0u;
    }

    v9x_shadow_width = width;
    v9x_shadow_height = height;
    v9x_shadow_bpp = bits_per_pixel;
    v9x_shadow_pitch = pitch;
    v9x_shadow_dirty_clear(&v9x_shadow_pending);
    v9x_shadow_pending.depth = 0u;
    if (v9x_shadow_stale != 0) {
        v9x_shadow_stale->pending = V9X_FALSE;
    }

    /* Black in both, so they agree before the first repaint: whatever the
     * mode set left in the frame buffer is not in the shadow. */
    V9xShadowZero();
    v9x_shadow_on = V9X_TRUE;
    v9x_shadow16_flush_all();
    return selector;
}

void V9X_LEAF_FAR v9x_shadow16_disable(void)
{
    v9x_shadow_on = V9X_FALSE;
    V9xShadowFree();
}

WORD V9X_LEAF_FAR v9x_shadow16_active(void)
{
    return v9x_shadow_on;
}

DWORD V9X_LEAF_FAR v9x_shadow16_linear(void)
{
    if (v9x_shadow_on == V9X_FALSE) {
        return 0ul;
    }
    return V9xShadowLinear();
}

void V9X_LEAF_FAR v9x_shadow16_flush_all(void)
{
    struct v9x_shadow_dirty all;

    if (v9x_shadow_on == V9X_FALSE) {
        return;
    }
    /* A mode restore: the frame buffer lost what it held, stale parts
     * included, so they are not read back. USER repaints the desktop and an
     * application its window. */
    if (v9x_shadow_stale != 0) {
        v9x_shadow_stale->pending = V9X_FALSE;
    }
    v9x_shadow_dirty_clear(&all);
    v9x_shadow_dirty_add_all(&all, v9x_shadow_width, v9x_shadow_height);
    v9x_shadow_note(V9X_SHADOW_EVENT_FLUSH_ALL, 0u, &all.rect);
    v9x_shadow_copy(&all.rect);
}

void V9X_LEAF_FAR v9x_shadow16_attach_stale(
    struct v9x_shadow_dirty FAR *stale, struct v9x_shadow_event FAR *log,
    DWORD FAR *log_count, DWORD FAR *direction_set)
{
    if (stale != 0) {
        v9x_shadow_dirty_clear(stale);
    }
    v9x_shadow_stale = stale;
    v9x_shadow_log = log;
    v9x_shadow_log_count = log_count;
    v9x_shadow_direction_set = direction_set;
}

static WORD v9x_shadow_begin_access(void FAR *device,
                                     WORD left, WORD top,
                                     WORD right, WORD bottom,
                                     WORD flags)
{
    struct v9x_shadow_dirty access;
    struct v9x_shadow_rect cursor;

    if (v9x_shadow_on != V9X_FALSE) {
        v9x_shadow_note_sides(V9X_SHADOW_EVENT_BEGIN, flags, (v9x_s16)left,
                              (v9x_s16)top, (v9x_s16)right, (v9x_s16)bottom);
    }
    if (v9x_shadow_on != V9X_FALSE &&
        (flags & V9X_SHADOW_FB_ACCESS) == 0u) {
        /* A cursor exclusion and nothing else: DirectDraw's around a blit
         * or Lock of the primary (the DDK's deCursorExclude is this entry),
         * or a driver's own engine drawing. The DIB Engine changes only the
         * cursor's pixels in the shadow, so only its rectangle is read back
         * and copied out. Treated as drawing over the whole access, every
         * windowed present read its window back: 5.0 fps in windowed
         * Quake 3 on Carrizo against 18.7 without the shadow. */
        v9x_shadow_cursor_rect(v9x_cursor_x, v9x_cursor_y, v9x_cursor_hot_x,
                               v9x_cursor_hot_y, v9x_cursor_width,
                               v9x_cursor_height, &cursor);
        v9x_shadow_overlap_stale(&cursor);
        v9x_shadow_dirty_add_rect(&v9x_shadow_pending, &cursor,
                                  v9x_shadow_width, v9x_shadow_height);
        ++v9x_shadow_pending.depth;
        return V9xDibBeginAccessRect(device, left, top, right, bottom, flags);
    }
    if (v9x_shadow_on != V9X_FALSE) {
        /* Before the DIB Engine lifts the cursor or reads anything: what an
         * engine left stale under this access comes back first. */
        v9x_shadow_drew = V9X_TRUE;
        v9x_shadow_dirty_clear(&access);
        v9x_shadow_dirty_add_within(&access, (v9x_s16)left, (v9x_s16)top,
                                    (v9x_s16)right, (v9x_s16)bottom,
                                    v9x_shadow_bounded != V9X_FALSE ?
                                        &v9x_shadow_bound : 0,
                                    v9x_shadow_width, v9x_shadow_height);
        if (access.pending != V9X_FALSE) {
            v9x_shadow_take_stale(&access.rect);
        }
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

static WORD v9x_shadow_end_access(void FAR *device, WORD flags)
{
    WORD result;
    struct v9x_shadow_rect cursor;

    /* The end of a cursor exclusion: whatever wrote the screen meanwhile
     * (DirectDraw's blit) left the cursor's rectangle stale, and the DIB
     * Engine is about to save it again from the shadow to put the cursor
     * back, so the stale part comes back first. */
    if (v9x_shadow_on != V9X_FALSE &&
        (flags & V9X_SHADOW_FB_ACCESS) == 0u) {
        v9x_shadow_cursor_rect(v9x_cursor_x, v9x_cursor_y, v9x_cursor_hot_x,
                               v9x_cursor_hot_y, v9x_cursor_width,
                               v9x_cursor_height, &cursor);
        v9x_shadow_overlap_stale(&cursor);
    }

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
    v9x_shadow_note(V9X_SHADOW_EVENT_END,
                    (WORD)((flags & 0x00ffu) |
                           (v9x_shadow_pending.depth << 8)),
                    v9x_shadow_pending.pending != V9X_FALSE ?
                        &v9x_shadow_pending.rect : 0);
    /*
     * Copied at every end, nested or not, and cleared only at the outermost:
     * an inner end must not drop what the outer bracket drew before it, and
     * an end that never sees its begin (a depth that leaked) still copies
     * rather than freezing the screen.
     */
    if (v9x_shadow_pending.pending != V9X_FALSE) {
        /* A union of drawing accesses can span stale pixels no access
         * touched. A nest of cursor exclusions only holds cursor
         * rectangles, already read back above. */
        if (v9x_shadow_drew != V9X_FALSE) {
            v9x_shadow_take_stale(&v9x_shadow_pending.rect);
        }
        v9x_shadow_note(V9X_SHADOW_EVENT_COPY, v9x_shadow_drew,
                        &v9x_shadow_pending.rect);
        v9x_shadow_copy(&v9x_shadow_pending.rect);
        if (v9x_shadow_pending.depth == 0u) {
            v9x_shadow_dirty_clear(&v9x_shadow_pending);
            v9x_shadow_drew = V9X_FALSE;
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
static WORD v9x_shadow_output(void FAR *device, WORD style,
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
        v9x_shadow_drew = V9X_TRUE;
        v9x_shadow_note(V9X_SHADOW_EVENT_OUTPUT, style, &v9x_shadow_bound);
        /* Its own bound is added to what is pending below without passing
         * through an access, so it is read back here. */
        v9x_shadow_take_stale(&v9x_shadow_bound);
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
        v9x_shadow_take_stale(&v9x_shadow_pending.rect);
        v9x_shadow_note(V9X_SHADOW_EVENT_COPY, 2u, &v9x_shadow_pending.rect);
        v9x_shadow_copy(&v9x_shadow_pending.rect);
        v9x_shadow_dirty_clear(&v9x_shadow_pending);
        v9x_shadow_drew = V9X_FALSE;
    }
    return result;
}

/*
 * Before the DIB Engine draws or lifts the cursor: it saves the pixels under
 * the shape from the shadow and puts them back from there, so an engine's
 * stale pixels under the old and new rectangles come back first. Called by
 * the cursor thunks ahead of the DIB Engine, from the mouse interrupt.
 */
static void v9x_shadow_cursor_overlap(short x, short y, short hot_x,
                                      short hot_y, WORD width, WORD height)
{
    struct v9x_shadow_rect rect;

    v9x_shadow_cursor_rect(x, y, hot_x, hot_y, width, height, &rect);
    v9x_shadow_overlap_stale(&rect);
}

static void v9x_shadow_before_set_cursor(void FAR *shape)
{
    const V9X_CURSOR_SHAPE FAR *cursor = (const V9X_CURSOR_SHAPE FAR *)shape;

    if (v9x_shadow_on == V9X_FALSE) {
        return;
    }
    v9x_shadow_note_sides(V9X_SHADOW_EVENT_CURSOR, 0u, v9x_cursor_x,
                          v9x_cursor_y,
                          (v9x_s16)(cursor != 0 ? cursor->csWidth : 0),
                          (v9x_s16)(cursor != 0 ? cursor->csHeight : 0));
    v9x_shadow_cursor_overlap(v9x_cursor_x, v9x_cursor_y, v9x_cursor_hot_x,
                              v9x_cursor_hot_y, v9x_cursor_width,
                              v9x_cursor_height);
    if (cursor != 0 && cursor->csWidth > 0 && cursor->csHeight > 0) {
        v9x_shadow_cursor_overlap(v9x_cursor_x, v9x_cursor_y,
                                  cursor->csHotX, cursor->csHotY,
                                  (WORD)cursor->csWidth,
                                  (WORD)cursor->csHeight);
    }
}

static void v9x_shadow_before_move_cursor(WORD x, WORD y)
{
    if (v9x_shadow_on == V9X_FALSE) {
        return;
    }
    v9x_shadow_note_sides(V9X_SHADOW_EVENT_CURSOR, 1u, (v9x_s16)x,
                          (v9x_s16)y, 0, 0);
    v9x_shadow_cursor_overlap(v9x_cursor_x, v9x_cursor_y, v9x_cursor_hot_x,
                              v9x_cursor_hot_y, v9x_cursor_width,
                              v9x_cursor_height);
    v9x_shadow_cursor_overlap((short)x, (short)y, v9x_cursor_hot_x,
                              v9x_cursor_hot_y, v9x_cursor_width,
                              v9x_cursor_height);
}

static void v9x_shadow_before_check_cursor(void)
{
    if (v9x_shadow_on == V9X_FALSE) {
        return;
    }
    v9x_shadow_cursor_overlap(v9x_cursor_x, v9x_cursor_y, v9x_cursor_hot_x,
                              v9x_cursor_hot_y, v9x_cursor_width,
                              v9x_cursor_height);
}

static void v9x_shadow_set_cursor(void FAR *shape)
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

static void v9x_shadow_move_cursor(WORD x, WORD y)
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
static void v9x_shadow_check_cursor(void)
{
    if (v9x_shadow_on == V9X_FALSE) {
        return;
    }
    v9x_shadow_copy_cursor();
}

/*
 * The entries the DIB Engine and the mouse interrupt call, around the
 * bodies above.
 *
 * Each saves the flags, clears the direction flag and puts the flags back
 * on the way out. Compiled C assumes the direction flag clear - Watcom
 * copies a structure with MOVSW - and the DIB Engine does not always call
 * with it clear: on M-XP7053 (boot 58, 2026-11-12) a deEndAccess arrived
 * with it set, v9x_shadow_widen's four MOVSWs ran downwards over its own
 * return address, and Quake 3 faulted in V9XDISP.DRV at LEAFCODE 1068h.
 * The copy loops in runtime.asm have always begun with CLD. The flags are
 * restored rather than left clear because the caller set them.
 */
static WORD v9x_shadow_flags_clear_direction(void);
#pragma aux v9x_shadow_flags_clear_direction = \
    "pushf" \
    "pop ax" \
    "cld" \
    value [ax] \
    modify exact [ax];

static void v9x_shadow_flags_restore(WORD flags);
#pragma aux v9x_shadow_flags_restore = \
    "push ax" \
    "popf" \
    parm [ax] \
    modify exact [];

/* The direction flag in the saved flags word. */
#define V9X_SHADOW_FLAGS_DIRECTION 0x0400u

/* Clear the direction flag on entry, and count it when it was set. */
static WORD v9x_shadow_enter(void)
{
    WORD saved = v9x_shadow_flags_clear_direction();

    if ((saved & V9X_SHADOW_FLAGS_DIRECTION) != 0u &&
        v9x_shadow_direction_set != 0) {
        ++*v9x_shadow_direction_set;
    }
    return saved;
}

WORD __loadds FAR PASCAL V9xShadowBeginAccess(void FAR *device,
                                              WORD left, WORD top,
                                              WORD right, WORD bottom,
                                              WORD flags)
{
    WORD saved = v9x_shadow_enter();
    WORD result = v9x_shadow_begin_access(device, left, top, right, bottom,
                                          flags);

    v9x_shadow_flags_restore(saved);
    return result;
}

WORD __loadds FAR PASCAL V9xShadowEndAccess(void FAR *device, WORD flags)
{
    WORD saved = v9x_shadow_enter();
    WORD result = v9x_shadow_end_access(device, flags);

    v9x_shadow_flags_restore(saved);
    return result;
}

WORD __loadds FAR PASCAL V9xShadowOutput(void FAR *device, WORD style,
                                         WORD count, void FAR *points,
                                         void FAR *pen, void FAR *brush,
                                         void FAR *draw_mode,
                                         void FAR *clip)
{
    WORD saved = v9x_shadow_enter();
    WORD result = v9x_shadow_output(device, style, count, points, pen,
                                    brush, draw_mode, clip);

    v9x_shadow_flags_restore(saved);
    return result;
}

void __loadds FAR PASCAL V9xShadowBeforeSetCursor(void FAR *shape)
{
    WORD saved = v9x_shadow_enter();

    v9x_shadow_before_set_cursor(shape);
    v9x_shadow_flags_restore(saved);
}

void __loadds FAR PASCAL V9xShadowBeforeMoveCursor(WORD x, WORD y)
{
    WORD saved = v9x_shadow_enter();

    v9x_shadow_before_move_cursor(x, y);
    v9x_shadow_flags_restore(saved);
}

void __loadds FAR PASCAL V9xShadowBeforeCheckCursor(void)
{
    WORD saved = v9x_shadow_enter();

    v9x_shadow_before_check_cursor();
    v9x_shadow_flags_restore(saved);
}

void __loadds FAR PASCAL V9xShadowSetCursor(void FAR *shape)
{
    WORD saved = v9x_shadow_enter();

    v9x_shadow_set_cursor(shape);
    v9x_shadow_flags_restore(saved);
}

void __loadds FAR PASCAL V9xShadowMoveCursor(WORD x, WORD y)
{
    WORD saved = v9x_shadow_enter();

    v9x_shadow_move_cursor(x, y);
    v9x_shadow_flags_restore(saved);
}

void __loadds FAR PASCAL V9xShadowCheckCursor(void)
{
    WORD saved = v9x_shadow_enter();

    v9x_shadow_check_cursor();
    v9x_shadow_flags_restore(saved);
}
