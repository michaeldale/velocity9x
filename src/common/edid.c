#include "velocity9x/edid.h"

/* The first detailed-timing descriptor. EDID 1.x puts four 18-byte
 * descriptors at 54, 72, 90 and 108; only the first is consulted, because
 * that is the one the preferred-timing feature bit promotes and the only one
 * this driver acts on. */
#define V9X_EDID_DTD_OFFSET 54u

v9x_u16 v9x_edid_parse(const v9x_u8 *block, struct v9x_edid_summary *out)
{
    static const v9x_u8 header[8] = {
        0x00u, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u
    };
    const v9x_u8 *dtd;
    v9x_u16 index;
    v9x_u8 sum;
    v9x_u16 width;
    v9x_u16 height;

    if (out == 0) {
        return V9X_FALSE;
    }
    out->version = 0u;
    out->preferred_width = 0u;
    out->preferred_height = 0u;
    out->extension_count = 0u;
    if (block == 0) {
        return V9X_FALSE;
    }

    for (index = 0u; index < 8u; ++index) {
        if (block[index] != header[index]) {
            return V9X_FALSE;
        }
    }

    sum = 0u;
    for (index = 0u; index < V9X_EDID_BLOCK_BYTES; ++index) {
        sum = (v9x_u8)(sum + block[index]);
    }
    if (sum != 0u) {
        return V9X_FALSE;
    }

    if (block[18] != 1u) {
        return V9X_FALSE;
    }

    dtd = block + V9X_EDID_DTD_OFFSET;
    /* Pixel clock zero marks a display descriptor, not a timing: a block
     * whose first slot is a name or a range limit states no preference this
     * driver can read. */
    if (dtd[0] == 0u && dtd[1] == 0u) {
        return V9X_FALSE;
    }
    /* Interlaced: bit 7 of the flags byte. */
    if ((dtd[17] & 0x80u) != 0u) {
        return V9X_FALSE;
    }

    width = (v9x_u16)(dtd[2] | ((v9x_u16)(dtd[4] & 0xf0u) << 4));
    height = (v9x_u16)(dtd[5] | ((v9x_u16)(dtd[7] & 0xf0u) << 4));
    if (width == 0u || height == 0u) {
        return V9X_FALSE;
    }

    out->version = (v9x_u16)(((v9x_u16)block[18] << 8) | block[19]);
    out->preferred_width = width;
    out->preferred_height = height;
    out->extension_count = block[126];
    return V9X_TRUE;
}
