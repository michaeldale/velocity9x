/*
 * The Gen3 hardware cursor's policy: which shapes it can show, the ARGB
 * image, CURxPOS, CURxCNTR, which pipes, and CURxBASE. No MMIO;
 * src\display16\cursor16.c writes what this computes.
 * docs\plans\gen3-hardware-cursor.md.
 *
 * In the 16-bit driver's LEAFCODE with cursor16.c, so no 32-bit multiply:
 * every product has 16-bit operands (V9X_LEAF_MUL16) or is a shift.
 */
#include "velocity9x/i9xx_cursor.h"
#include "velocity9x/leaf16.h"

/* The mask bit for pixel x of a row: most significant bit leftmost. */
static v9x_u16 v9x_i9xx_cursor_bit(const v9x_u8 *mask, v9x_u32 row_start,
                                   v9x_u32 x)
{
    return (v9x_u16)((mask[row_start + (x >> 3)] >> (7u - (unsigned)(x & 7ul))) &
                     1u);
}

v9x_status v9x_i9xx_cursor_check(const struct v9x_i9xx_cursor_shape *shape)
{
    v9x_u32 row;
    v9x_u32 x;
    v9x_u32 row_start;

    if (shape == 0 || shape->and_mask == 0 || shape->xor_mask == 0 ||
        shape->width == 0u || shape->height == 0u ||
        ((v9x_u32)shape->width_bytes << 3) < (v9x_u32)shape->width) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (shape->planes != 1u || shape->bits_pixel != 1u ||
        (v9x_u32)shape->width > V9X_I9XX_CURSOR_SIZE ||
        (v9x_u32)shape->height > V9X_I9XX_CURSOR_SIZE ||
        V9X_LEAF_MUL16(shape->width_bytes, shape->height) >
            (v9x_u32)V9X_I9XX_CURSOR_MASK_BYTES) {
        return V9X_STATUS_UNSUPPORTED;
    }
    /* A pixel that inverts the screen has no ARGB equivalent. */
    for (row = 0ul; row < shape->height; ++row) {
        row_start = V9X_LEAF_MUL16(row, shape->width_bytes);
        for (x = 0ul; x < shape->width; ++x) {
            if (v9x_i9xx_cursor_bit(shape->and_mask, row_start, x) != 0u &&
                v9x_i9xx_cursor_bit(shape->xor_mask, row_start, x) != 0u) {
                return V9X_STATUS_UNSUPPORTED;
            }
        }
    }
    return V9X_STATUS_OK;
}

void v9x_i9xx_cursor_row(const struct v9x_i9xx_cursor_shape *shape,
                         v9x_u32 row, v9x_u32 *pixels)
{
    v9x_u32 x;
    v9x_u32 row_start = V9X_LEAF_MUL16(row, shape->width_bytes);

    for (x = 0ul; x < V9X_I9XX_CURSOR_SIZE; ++x) {
        if (row >= shape->height || x >= shape->width ||
            v9x_i9xx_cursor_bit(shape->and_mask, row_start, x) != 0u) {
            pixels[x] = V9X_I9XX_CURSOR_TRANSPARENT;
        } else if (v9x_i9xx_cursor_bit(shape->xor_mask, row_start, x) != 0u) {
            pixels[x] = V9X_I9XX_CURSOR_WHITE;
        } else {
            pixels[x] = V9X_I9XX_CURSOR_BLACK;
        }
    }
}

v9x_u32 v9x_i9xx_cursor_position(v9x_s32 x, v9x_s32 y)
{
    v9x_u32 position = 0ul;

    if (x < 0) {
        position |= V9X_I9XX_CURSOR_POS_X_SIGN;
        x = -x;
    }
    position |= (v9x_u32)x & V9X_I9XX_CURSOR_POS_MASK;
    if (y < 0) {
        position |= V9X_I9XX_CURSOR_POS_Y_SIGN;
        y = -y;
    }
    position |= ((v9x_u32)y & V9X_I9XX_CURSOR_POS_MASK) << 16;
    return position;
}

v9x_u32 v9x_i9xx_cursor_control(v9x_u32 pipe)
{
    return V9X_I9XX_CURSOR_MODE_64_ARGB |
           ((pipe & V9X_I9XX_PLANE_PIPE_MASK) << V9X_I9XX_CURSOR_PIPE_SHIFT);
}

/* The pipe bit an enabled plane scans out on, or 0. Only pipes A and B
 * exist on Gen3. */
static v9x_u16 v9x_i9xx_cursor_plane_pipe(v9x_u32 control)
{
    v9x_u32 pipe;

    if ((control & V9X_I9XX_PLANE_ENABLE) == 0ul) {
        return 0u;
    }
    pipe = (control >> V9X_I9XX_PLANE_PIPE_SHIFT) & V9X_I9XX_PLANE_PIPE_MASK;
    return pipe < 2ul ? (v9x_u16)(1u << (unsigned)pipe) : 0u;
}

v9x_u16 v9x_i9xx_cursor_pipes(v9x_u32 plane_a_control,
                              v9x_u32 plane_b_control)
{
    return (v9x_u16)(v9x_i9xx_cursor_plane_pipe(plane_a_control) |
                     v9x_i9xx_cursor_plane_pipe(plane_b_control));
}

/* i915's cursor_needs_physical among the parts this family claims or may
 * meet under the same driver: 915G, 915GM, 945G, 945GM and 945GME/GSE. */
static v9x_u16 v9x_i9xx_cursor_needs_physical(v9x_u16 device_id)
{
    return device_id == 0x2582u || device_id == V9X_PCI_DEVICE_GMA900_915GM ||
                   device_id == 0x2772u || device_id == 0x27a2u ||
                   device_id == V9X_PCI_DEVICE_GMA950_945GSE
               ? V9X_TRUE
               : V9X_FALSE;
}

/* Pineview (desktop and mobile) and G33 take the graphics address. */
static v9x_u16 v9x_i9xx_cursor_takes_graphics(v9x_u16 device_id)
{
    return device_id == 0xa001u || device_id == V9X_PCI_DEVICE_GMA3150_PNV ||
                   device_id == V9X_PCI_DEVICE_GMA3100_G33
               ? V9X_TRUE
               : V9X_FALSE;
}

/* A Gen3 GTT entry: valid in bit 0, address bits 31:12, and address bits
 * 35:32 in 7:4, which must be clear for a 32-bit base. Pages are 4 KiB
 * (V9X_I9XX_GTT_PAGE_BYTES), a shift of 12. */
#define V9X_I9XX_CURSOR_ENTRY_VALID    ((v9x_u32)0x00000001ul)
#define V9X_I9XX_CURSOR_ENTRY_HIGH     ((v9x_u32)0x000000f0ul)
#define V9X_I9XX_CURSOR_ENTRY_ADDRESS  ((v9x_u32)0xfffff000ul)

v9x_status v9x_i9xx_cursor_base(v9x_u16 device_id, v9x_u32 graphics_offset,
                                const v9x_u32 *entries, v9x_u32 *base)
{
    v9x_u32 first;
    v9x_u32 page;

    if (base != 0) {
        *base = 0ul;
    }
    if (base == 0 ||
        (graphics_offset & (V9X_I9XX_GTT_PAGE_BYTES - 1ul)) != 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (v9x_i9xx_cursor_takes_graphics(device_id) != V9X_FALSE) {
        *base = graphics_offset;
        return V9X_STATUS_OK;
    }
    if (v9x_i9xx_cursor_needs_physical(device_id) == V9X_FALSE) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (entries == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    first = entries[0] & V9X_I9XX_CURSOR_ENTRY_ADDRESS;
    for (page = 0ul; page < V9X_I9XX_CURSOR_PAGES; ++page) {
        if ((entries[page] & V9X_I9XX_CURSOR_ENTRY_VALID) == 0ul ||
            (entries[page] & V9X_I9XX_CURSOR_ENTRY_HIGH) != 0ul ||
            (entries[page] & V9X_I9XX_CURSOR_ENTRY_ADDRESS) !=
                first + (page << 12)) {
            return V9X_STATUS_INSUFFICIENT_MEMORY;
        }
    }
    *base = first;
    return V9X_STATUS_OK;
}
