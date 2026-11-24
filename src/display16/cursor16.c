/*
 * GDI's SetCursor (ordinal 102) and MoveCursor (103) for V9XDISP.DRV in the
 * intel-gma family: the Gen3 hardware cursor
 * (docs\plans\gen3-hardware-cursor.md). dib_thunks.asm's SetCursor and
 * MoveCursor jump to V9xCursorSet and V9xCursorMove below.
 *
 * The DIB Engine draws the cursor into the frame buffer in software. Every
 * GDI call that touches the screen has to lift it off and put it back
 * (deBeginAccess and deEndAccess with CURSOREXCLUDE), and a cursor over
 * something being redrawn flickers. The display engine can overlay a 64x64
 * ARGB image on each pipe instead, which nothing GDI draws ever touches.
 *
 * A shape the overlay can show exactly, a monochrome one with no inverting
 * pixel, goes to the hardware, and the DIB Engine is told there is no
 * cursor. Any other shape - the text I-beam inverts, a colour cursor has
 * no monochrome form - goes to the DIB Engine as before, and the overlay is
 * turned off. Every position also goes to the DIB Engine, so whichever of
 * the two shows the next shape starts in the right place.
 *
 * The image is the reserve's last 16 KiB (intel_gma.h,
 * V9X_I9XX_CURSOR_RESERVE_OFFSET), written through the aperture. On the
 * 915GM and 945GM the cursor reads a physical address, taken from the
 * image's own GTT entries, the way the HAL finds the status page's
 * (d3d_i9xx.c, v9x_d3d_i9xx_hws_open). The policy is pure C in
 * src\chipsets\intel\i9xx_cursor.c; the register and aperture accesses are
 * runtime.asm's.
 *
 * The overlay is off whenever this driver does not own the screen: at
 * Disable and when a full-screen DOS box takes it (sswitch16.c). It is put
 * back after the mode is set again, because a mode set may clear video
 * memory and the image with it.
 *
 * MoveCursor can be called at interrupt time, from the mouse driver: it
 * writes two registers per pipe and calls the DIB Engine, nothing else.
 *
 * LEAFCODE (velocity9x\leaf16.h), with i9xx_cursor.c: nothing here calls
 * into _TEXT. The chip's hooks are read by gdi_accel.c and passed to
 * v9x_cursor_configure, and every multiply has 16-bit operands.
 */
#include <windows.h>

#include "velocity9x/engine_abi.h"
#include "velocity9x/i9xx_cursor.h"
#include "velocity9x/leaf16.h"
#include "velocity9x/win9x_ddraw_abi.h"
#include "win9x_display_abi.h"
#include "cursor16.h"

/* runtime.asm. */
extern WORD FAR PASCAL V9xI9xxDisplaySelector(DWORD linear_base, WORD limit);
extern DWORD FAR PASCAL V9xI9xxDisplayRead(WORD offset);
extern WORD FAR PASCAL V9xI9xxDisplayWrite(WORD offset, DWORD value);
extern WORD FAR PASCAL V9xGmadrCopy(DWORD offset, const DWORD FAR *source,
                                    WORD dwords);
extern void FAR PASCAL V9xDibSetCursorCall(LPVOID shape,
                                           V9X_DIB_ENGINE FAR *device);
extern void FAR PASCAL V9xDibMoveCursorCall(short x, short y,
                                            V9X_DIB_ENGINE FAR *device);

/* ddi.c. */
extern V9X_DIB_ENGINE FAR *v9x_driver_pdevice;

#define V9X_CURSOR_INI_FILE    "SYSTEM.INI"
#define V9X_CURSOR_INI_SECTION "Velocity9x"

/* The display registers' window: BAR0 + 70000h, 64 KiB. Registers are
 * addressed through it by their offset from that base. */
#define V9X_CURSOR_WINDOW_BASE ((DWORD)0x00070000ul)
#define V9X_CURSOR_WINDOW_LIMIT 0xffffu
#define V9X_CURSOR_REG(reg) ((WORD)((reg) - V9X_CURSOR_WINDOW_BASE))

/* The CURSORSHAPE header GDI passes: five ints and two bytes (Win16
 * windows.h, tagCURSORSHAPE), then the AND mask, then the XOR mask. */
typedef struct {
    short hot_x;
    short hot_y;
    short width;
    short height;
    short width_bytes;
    BYTE planes;
    BYTE bits_pixel;
} V9X_CURSOR_HEADER;

typedef char v9x_cursor_header_is_12_bytes[
    sizeof(V9X_CURSOR_HEADER) == 12u ? 1 : -1];

/* Configured at this Enable: the window described, the base known. */
static WORD v9x_cursor_ready;
/* The current shape is the overlay's, not the DIB Engine's. */
static WORD v9x_cursor_hardware;
/* The pipes the overlay is on now, as v9x_i9xx_cursor_pipes gives them. */
static WORD v9x_cursor_shown;
static DWORD v9x_cursor_image;
static DWORD v9x_cursor_base;
/* The image's top-left corner relative to the hot spot. */
static short v9x_cursor_hot_x;
static short v9x_cursor_hot_y;
/* The last position GDI gave, the hot spot's. */
static short v9x_cursor_x;
static short v9x_cursor_y;
/* The shape, kept for a mode set that clears the image. */
static struct v9x_i9xx_cursor_shape v9x_cursor_shape;
static v9x_u8 v9x_cursor_and[V9X_I9XX_CURSOR_MASK_BYTES];
static v9x_u8 v9x_cursor_xor[V9X_I9XX_CURSOR_MASK_BYTES];
static v9x_u32 v9x_cursor_row_pixels[V9X_I9XX_CURSOR_SIZE];
static char v9x_cursor_state[40];

/* "on base=XXXXXXXX pipes=N", or the reason it is off. */
static void v9x_cursor_set_state(const char *text)
{
    WORD at = 0u;

    while (text[at] != '\0' && at + 1u < (WORD)sizeof(v9x_cursor_state)) {
        v9x_cursor_state[at] = text[at];
        ++at;
    }
    v9x_cursor_state[at] = '\0';
}

static void v9x_cursor_set_on_state(DWORD base, WORD pipes)
{
    static const char digits[] = "0123456789ABCDEF";
    static const char head[] = "on base=";
    WORD at;
    WORD i;

    for (at = 0u; head[at] != '\0'; ++at) {
        v9x_cursor_state[at] = head[at];
    }
    for (i = 0u; i < 8u; ++i) {
        v9x_cursor_state[at++] = digits[(WORD)(base >> (28u - 4u * i)) & 0xfu];
    }
    v9x_cursor_state[at++] = ' ';
    v9x_cursor_state[at++] = 'p';
    v9x_cursor_state[at++] = 'i';
    v9x_cursor_state[at++] = 'p';
    v9x_cursor_state[at++] = 'e';
    v9x_cursor_state[at++] = 's';
    v9x_cursor_state[at++] = '=';
    v9x_cursor_state[at++] = digits[pipes & 0xfu];
    v9x_cursor_state[at] = '\0';
}

const char FAR * V9X_LEAF_FAR v9x_cursor_state_text(void)
{
    return v9x_cursor_state[0] != '\0' ? v9x_cursor_state : "none";
}

static WORD v9x_cursor_pipes_now(void)
{
    return v9x_i9xx_cursor_pipes(
        V9xI9xxDisplayRead(V9X_CURSOR_REG(V9X_I9XX_REG_DSPACNTR)),
        V9xI9xxDisplayRead(V9X_CURSOR_REG(V9X_I9XX_REG_DSPBCNTR)));
}

/*
 * One cursor's registers, in i915's order (intel_cursor.c,
 * i9xx_cursor_update_arm): CNTR, POS, then BASE, whose write arms the
 * update on these parts. pipe 0 is cursor A, 1 is cursor B.
 */
static void v9x_cursor_write(WORD pipe, DWORD control, DWORD position,
                             DWORD base)
{
    DWORD offset = pipe == 0u ? 0ul :
                   V9X_I9XX_REG_CURBCNTR - V9X_I9XX_REG_CURACNTR;

    (void)V9xI9xxDisplayWrite(
        V9X_CURSOR_REG(V9X_I9XX_REG_CURACNTR + offset), control);
    (void)V9xI9xxDisplayWrite(
        V9X_CURSOR_REG(V9X_I9XX_REG_CURAPOS + offset), position);
    (void)V9xI9xxDisplayWrite(
        V9X_CURSOR_REG(V9X_I9XX_REG_CURABASE + offset), base);
}

static DWORD v9x_cursor_position_now(void)
{
    return v9x_i9xx_cursor_position(
        (v9x_s32)v9x_cursor_x - (v9x_s32)v9x_cursor_hot_x,
        (v9x_s32)v9x_cursor_y - (v9x_s32)v9x_cursor_hot_y);
}

/* Turn the overlay off on every pipe it is on. */
static void v9x_cursor_off(void)
{
    WORD pipe;

    if (v9x_cursor_ready == 0u) {
        return;
    }
    for (pipe = 0u; pipe < 2u; ++pipe) {
        if ((v9x_cursor_shown & (1u << pipe)) != 0u) {
            v9x_cursor_write(pipe, 0ul, 0ul, 0ul);
        }
    }
    v9x_cursor_shown = 0u;
}

/*
 * Write the kept shape's image and turn the overlay on for every pipe that
 * scans out the desktop. Only while this driver owns the screen.
 */
static void v9x_cursor_on(void)
{
    DWORD row;
    DWORD position;
    WORD pipes;
    WORD pipe;

    if (v9x_cursor_ready == 0u || v9x_cursor_hardware == 0u ||
        v9x_driver_pdevice == 0 ||
        (v9x_driver_pdevice->deFlags & V9X_DE_BUSY) != 0u) {
        return;
    }
    for (row = 0ul; row < V9X_I9XX_CURSOR_SIZE; ++row) {
        v9x_i9xx_cursor_row(&v9x_cursor_shape, row, v9x_cursor_row_pixels);
        (void)V9xGmadrCopy(v9x_cursor_image +
                               V9X_LEAF_MUL16(row, V9X_I9XX_CURSOR_PITCH),
                           (const DWORD FAR *)v9x_cursor_row_pixels,
                           (WORD)V9X_I9XX_CURSOR_SIZE);
    }
    pipes = v9x_cursor_pipes_now();
    position = v9x_cursor_position_now();
    for (pipe = 0u; pipe < 2u; ++pipe) {
        if ((pipes & (1u << pipe)) != 0u) {
            v9x_cursor_write(pipe, v9x_i9xx_cursor_control(pipe), position,
                             v9x_cursor_base);
        } else if ((v9x_cursor_shown & (1u << pipe)) != 0u) {
            v9x_cursor_write(pipe, 0ul, 0ul, 0ul);
        }
    }
    v9x_cursor_shown = pipes;
}

/*
 * At Enable and ReEnable, from GDI acceleration's configure: find the window, the
 * image and its base for this mode. The overlay is put back if the last
 * shape was the hardware's.
 */
void V9X_LEAF_FAR v9x_cursor_configure(unsigned long engine_type,
                                       unsigned long control_base,
                                       unsigned long control_bytes,
                                       unsigned long gtt_base,
                                       unsigned long reserve,
                                       unsigned long vbe_bytes,
                                       unsigned short device_id)
{
    DWORD entries[V9X_I9XX_CURSOR_PAGES];
    DWORD page;
    WORD index;
    WORD pipes;

    v9x_cursor_ready = 0u;
    v9x_cursor_shown = 0u;
    v9x_cursor_set_state("no-engine");
    if (engine_type != V9X_DD_ENGINE_TYPE_INTEL_GEN3 || control_base == 0ul ||
        gtt_base == 0ul || control_bytes < V9X_I9XX_MMIO_BYTES) {
        return;
    }
    if (GetPrivateProfileInt(V9X_CURSOR_INI_SECTION, "HwCursor", 1,
                             V9X_CURSOR_INI_FILE) == 0) {
        v9x_cursor_set_state("off-ini");
        return;
    }

    /* The image: the top of the reserve dd16.c ends the heap at. A hook that
     * found no room hands back the size unchanged. */
    if (reserve == 0ul || reserve >= vbe_bytes ||
        reserve + V9X_I9XX_GTT_RESERVE_BYTES > vbe_bytes) {
        v9x_cursor_set_state("no-reserve");
        return;
    }
    v9x_cursor_image = reserve + V9X_I9XX_CURSOR_RESERVE_OFFSET;

    /* The image's four GTT entries, through the same selector before it is
     * moved to the registers. A GTT entry is four bytes a 4 KiB page. */
    page = v9x_cursor_image >> 12;
    if (page + V9X_I9XX_CURSOR_PAGES > V9X_I9XX_GTT_ENTRY_COUNT ||
        V9xI9xxDisplaySelector(gtt_base + (page << 2),
                               (WORD)(V9X_I9XX_CURSOR_PAGES * 4u - 1u)) ==
            0u) {
        v9x_cursor_set_state("gtt-unread");
        return;
    }
    for (index = 0u; index < (WORD)V9X_I9XX_CURSOR_PAGES; ++index) {
        entries[index] = V9xI9xxDisplayRead((WORD)(index * 4u));
    }
    if (v9x_i9xx_cursor_base(device_id, v9x_cursor_image,
                             (const v9x_u32 *)entries, &v9x_cursor_base) !=
        V9X_STATUS_OK) {
        v9x_cursor_set_state("base-refused");
        return;
    }

    if (V9xI9xxDisplaySelector(control_base + V9X_CURSOR_WINDOW_BASE,
                               V9X_CURSOR_WINDOW_LIMIT) == 0u) {
        v9x_cursor_set_state("no-selector");
        return;
    }
    pipes = v9x_cursor_pipes_now();
    if (pipes == 0u) {
        v9x_cursor_set_state("no-planes");
        return;
    }
    v9x_cursor_set_on_state(v9x_cursor_base, pipes);
    v9x_cursor_ready = 1u;
    v9x_cursor_on();
}

/* sswitch16.c's and Disable's: the screen is someone else's from now. */
void FAR PASCAL V9xCursorHide(void)
{
    v9x_cursor_off();
}

/* After the mode is set again and the DIB Engine may draw. */
void FAR PASCAL V9xCursorRestore(void)
{
    v9x_cursor_on();
}

/*
 * Keep a shape the overlay can show. 0 when it cannot, which leaves the
 * kept shape as it was.
 */
static WORD v9x_cursor_take(const V9X_CURSOR_HEADER FAR *header)
{
    struct v9x_i9xx_cursor_shape shape;
    const v9x_u8 FAR *masks = (const v9x_u8 FAR *)(header + 1);
    WORD bytes;
    WORD at;

    if (header->width <= 0 || header->height <= 0 ||
        header->width_bytes <= 0) {
        return 0u;
    }
    shape.hot_x = (v9x_u16)header->hot_x;
    shape.hot_y = (v9x_u16)header->hot_y;
    shape.width = (v9x_u16)header->width;
    shape.height = (v9x_u16)header->height;
    shape.width_bytes = (v9x_u16)header->width_bytes;
    shape.planes = header->planes;
    shape.bits_pixel = header->bits_pixel;
    shape.and_mask = masks;
    /* Only read past the AND mask once its size is known to be bounded:
     * the check below rejects anything larger than the copy. */
    if (V9X_LEAF_MUL16(shape.width_bytes, shape.height) >
        (DWORD)V9X_I9XX_CURSOR_MASK_BYTES) {
        return 0u;
    }
    bytes = (WORD)(shape.width_bytes * shape.height);
    shape.xor_mask = masks + bytes;
    if (v9x_i9xx_cursor_check(&shape) != V9X_STATUS_OK) {
        return 0u;
    }
    for (at = 0u; at < bytes; ++at) {
        v9x_cursor_and[at] = shape.and_mask[at];
        v9x_cursor_xor[at] = shape.xor_mask[at];
    }
    shape.and_mask = v9x_cursor_and;
    shape.xor_mask = v9x_cursor_xor;
    v9x_cursor_shape = shape;
    v9x_cursor_hot_x = header->hot_x;
    v9x_cursor_hot_y = header->hot_y;
    return 1u;
}

void __loadds FAR PASCAL V9xCursorSet(LPVOID shape)
{
    /* GDI may call while the display is disabled; the DIB Engine's cursor
     * routines dereference the PDEVICE, which is null then. */
    if (v9x_driver_pdevice == 0) {
        return;
    }
    if (shape != 0 && v9x_cursor_ready != 0u &&
        v9x_cursor_take((const V9X_CURSOR_HEADER FAR *)shape) != 0u) {
        if (v9x_cursor_hardware == 0u) {
            /* Take the DIB Engine's cursor off the screen first. */
            V9xDibSetCursorCall(0, v9x_driver_pdevice);
            v9x_cursor_hardware = 1u;
        }
        v9x_cursor_on();
        return;
    }
    if (v9x_cursor_hardware != 0u) {
        v9x_cursor_off();
        v9x_cursor_hardware = 0u;
    }
    V9xDibSetCursorCall(shape, v9x_driver_pdevice);
}

void __loadds FAR PASCAL V9xCursorMove(short x, short y)
{
    DWORD position;
    WORD pipe;

    if (v9x_driver_pdevice == 0) {
        return;
    }
    v9x_cursor_x = x;
    v9x_cursor_y = y;
    /*
     * Not gated on DE_BUSY: the GDI copies set it for the length of a
     * submission, and a move dropped then would leave the overlay a
     * move behind until the mouse moved again. When the screen is not this
     * driver's the overlay is off and v9x_cursor_shown is zero.
     */
    if (v9x_cursor_shown != 0u) {
        /* POS then BASE: on these parts the BASE write arms the move. */
        position = v9x_cursor_position_now();
        for (pipe = 0u; pipe < 2u; ++pipe) {
            if ((v9x_cursor_shown & (1u << pipe)) != 0u) {
                DWORD offset = pipe == 0u ? 0ul :
                               V9X_I9XX_REG_CURBCNTR - V9X_I9XX_REG_CURACNTR;

                (void)V9xI9xxDisplayWrite(
                    V9X_CURSOR_REG(V9X_I9XX_REG_CURAPOS + offset), position);
                (void)V9xI9xxDisplayWrite(
                    V9X_CURSOR_REG(V9X_I9XX_REG_CURABASE + offset),
                    v9x_cursor_base);
            }
        }
    }
    V9xDibMoveCursorCall(x, y, v9x_driver_pdevice);
}
