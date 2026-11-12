/*
 * The panel-mode slot patch for Intel video BIOSes. Policy only: see
 * include\velocity9x\vbios_panel.h for the split and for the table layout.
 */
#include "velocity9x/vbios_panel.h"
#include "velocity9x/edid.h"

/* An option ROM's signature and its length byte, in 512-byte units. */
#define V9X_VBIOS_SIGNATURE_0 ((v9x_u8)0x55u)
#define V9X_VBIOS_SIGNATURE_1 ((v9x_u8)0xAAu)
#define V9X_VBIOS_LENGTH_BYTE ((v9x_u16)2u)

/* The first detailed timing descriptor of an EDID base block. */
#define V9X_VBIOS_EDID_DTD ((v9x_u16)54u)

/* The three mode numbers 915resolution recognises the table by. */
#define V9X_VBIOS_TABLE_MODE_0 ((v9x_u8)0x30u)
#define V9X_VBIOS_TABLE_MODE_1 ((v9x_u8)0x32u)
#define V9X_VBIOS_TABLE_MODE_2 ((v9x_u8)0x34u)

/* Offsets within a panel slot's header. */
#define V9X_VBIOS_SLOT_WIDTH ((v9x_u16)2u)
#define V9X_VBIOS_SLOT_HEIGHT ((v9x_u16)4u)

static v9x_u16 v9x_vbios_word(const v9x_u8 *at)
{
    return (v9x_u16)((v9x_u16)at[0] | ((v9x_u16)at[1] << 8));
}

static void v9x_vbios_zero(struct v9x_vbios_patch *patch)
{
    v9x_u8 *at = (v9x_u8 *)patch;
    v9x_u16 index;

    for (index = 0u; index < (v9x_u16)sizeof(*patch); ++index) {
        at[index] = 0u;
    }
}

/*
 * Where the table starts, or 0: the only offset in the ROM where entries
 * numbered 30h, 32h and 34h follow at the entry stride. 0 is not a valid
 * answer anyway, being the 55AAh signature. A second match is reported in
 * *count, and the caller refuses: patching the wrong table is worse than
 * patching none.
 */
static v9x_u16 v9x_vbios_find_table(const v9x_u8 *rom, v9x_u16 rom_bytes,
                                    v9x_u16 *count)
{
    v9x_u16 at;
    v9x_u16 found = 0u;
    v9x_u16 span = (v9x_u16)(V9X_VBIOS_MODE_ENTRY_BYTES * 2u + 1u);

    *count = 0u;
    if (rom_bytes <= span) {
        return 0u;
    }
    for (at = 0u; at < (v9x_u16)(rom_bytes - span); ++at) {
        if (rom[at] == V9X_VBIOS_TABLE_MODE_0 &&
            rom[at + V9X_VBIOS_MODE_ENTRY_BYTES] == V9X_VBIOS_TABLE_MODE_1 &&
            rom[at + V9X_VBIOS_MODE_ENTRY_BYTES * 2u] ==
                V9X_VBIOS_TABLE_MODE_2) {
            if (*count == 0u) {
                found = at;
            }
            ++*count;
        }
    }
    return found;
}

/*
 * The one block among the table's panel slots headed width x height, or 0
 * (*count says whether there were none or several). Several entries share a
 * block - one each for 8, 16 and 32 bpp - so only distinct blocks count.
 */
static v9x_u16 v9x_vbios_find_slot(const v9x_u8 *rom, v9x_u16 rom_bytes,
                                   v9x_u16 table, v9x_u16 width,
                                   v9x_u16 height, v9x_u16 *count)
{
    v9x_u16 entry;
    v9x_u16 found = 0u;

    *count = 0u;
    for (entry = 0u; entry < V9X_VBIOS_MODE_TABLE_MAX; ++entry) {
        v9x_u16 at = (v9x_u16)(table + entry * V9X_VBIOS_MODE_ENTRY_BYTES);
        v9x_u16 block;

        if ((v9x_u32)at + V9X_VBIOS_MODE_ENTRY_BYTES > (v9x_u32)rom_bytes ||
            rom[at] == V9X_VBIOS_MODE_END) {
            break;
        }
        if (rom[at] < V9X_VBIOS_PANEL_MODE_FIRST) {
            continue;
        }
        block = v9x_vbios_word(&rom[at + 2u]);
        if (block == 0u ||
            (v9x_u32)block + V9X_VBIOS_PANEL_BLOCK_BYTES > (v9x_u32)rom_bytes) {
            continue;
        }
        if (v9x_vbios_word(&rom[block + V9X_VBIOS_SLOT_WIDTH]) != width ||
            v9x_vbios_word(&rom[block + V9X_VBIOS_SLOT_HEIGHT]) != height) {
            continue;
        }
        if (block == found) {
            continue;
        }
        found = block;
        ++*count;
    }
    return *count == 1u ? found : 0u;
}

v9x_u16 V9X_LEAF_FAR v9x_vbios_panel_plan(const v9x_u8 *rom,
                                          v9x_u16 rom_bytes,
                                          const v9x_u8 *edid,
                                          struct v9x_vbios_patch *patch)
{
    struct v9x_edid_summary summary;
    v9x_u16 table;
    v9x_u16 slot;
    v9x_u16 count;
    v9x_u16 length;
    v9x_u16 offset;
    v9x_u16 last;
    v9x_u16 index;
    v9x_u16 same = 1u;

    v9x_vbios_zero(patch);
    if (v9x_edid_parse(edid, &summary) != V9X_TRUE) {
        return V9X_VBIOS_PANEL_NO_EDID;
    }

    /* The ROM's own length bounds the search, so bytes past its end that
     * happen to be readable are never taken for a table. */
    if (rom_bytes <= V9X_VBIOS_LENGTH_BYTE ||
        rom[0] != V9X_VBIOS_SIGNATURE_0 || rom[1] != V9X_VBIOS_SIGNATURE_1) {
        return V9X_VBIOS_PANEL_NOT_ROM;
    }
    length = (v9x_u16)rom[V9X_VBIOS_LENGTH_BYTE];
    if (length == 0u) {
        return V9X_VBIOS_PANEL_NOT_ROM;
    }
    /* 128 or more blocks is 64 KiB, which no 16-bit offset reaches past. */
    if (length < 128u && (v9x_u16)(length * 512u) < rom_bytes) {
        rom_bytes = (v9x_u16)(length * 512u);
    }

    table = v9x_vbios_find_table(rom, rom_bytes, &count);
    if (count == 0u) {
        return V9X_VBIOS_PANEL_NO_TABLE;
    }
    if (count != 1u) {
        return V9X_VBIOS_PANEL_TABLES;
    }
    slot = v9x_vbios_find_slot(rom, rom_bytes, table,
                               summary.preferred_width,
                               summary.preferred_height, &count);
    if (count == 0u) {
        return V9X_VBIOS_PANEL_NO_SLOT;
    }
    if (count != 1u) {
        return V9X_VBIOS_PANEL_SLOTS;
    }

    /* The write must stay inside one 16 KiB half of one PAM register: the
     * mini-VDD opens exactly one. */
    offset = (v9x_u16)(slot + V9X_VBIOS_PANEL_HEADER_BYTES);
    last = (v9x_u16)(offset + V9X_VBIOS_DTD_BYTES - 1u);
    if ((offset / V9X_VBIOS_PAM_HALF_BYTES) !=
        (last / V9X_VBIOS_PAM_HALF_BYTES)) {
        return V9X_VBIOS_PANEL_SPLIT;
    }

    patch->rom_offset = offset;
    patch->pam_register = (v9x_u8)(V9X_VBIOS_PAM1 +
                                   (offset / (V9X_VBIOS_PAM_HALF_BYTES * 2u)));
    patch->pam_rw = ((offset / V9X_VBIOS_PAM_HALF_BYTES) & 1u) != 0u ?
                    V9X_VBIOS_PAM_HIGH_RW : V9X_VBIOS_PAM_LOW_RW;
    patch->width = summary.preferred_width;
    patch->height = summary.preferred_height;
    for (index = 0u; index < V9X_VBIOS_DTD_BYTES; ++index) {
        patch->old_bytes[index] = rom[offset + index];
        patch->new_bytes[index] = edid[V9X_VBIOS_EDID_DTD + index];
        if (patch->old_bytes[index] != patch->new_bytes[index]) {
            same = 0u;
        }
    }
    return same ? V9X_VBIOS_PANEL_ALREADY : V9X_VBIOS_PANEL_PLANNED;
}

static char v9x_vbios_hex_digit(v9x_u16 value)
{
    value &= 0x0Fu;
    return (char)(value < 10u ? '0' + value : 'A' + (value - 10u));
}

static v9x_u16 v9x_vbios_hex(char *text, v9x_u16 at, v9x_u16 value,
                             v9x_u16 digits)
{
    while (digits != 0u) {
        --digits;
        text[at++] = v9x_vbios_hex_digit((v9x_u16)(value >> (digits * 4u)));
    }
    return at;
}

void V9X_LEAF_FAR v9x_vbios_panel_format(const struct v9x_vbios_patch *patch,
                                         char *text)
{
    v9x_u16 at = 0u;
    v9x_u16 index;

    at = v9x_vbios_hex(text, at, patch->rom_offset, 4u);
    text[at++] = ' ';
    at = v9x_vbios_hex(text, at, patch->pam_register, 2u);
    text[at++] = ' ';
    at = v9x_vbios_hex(text, at, patch->pam_rw, 2u);
    text[at++] = ' ';
    for (index = 0u; index < V9X_VBIOS_DTD_BYTES; ++index) {
        at = v9x_vbios_hex(text, at, patch->old_bytes[index], 2u);
    }
    text[at++] = ' ';
    for (index = 0u; index < V9X_VBIOS_DTD_BYTES; ++index) {
        at = v9x_vbios_hex(text, at, patch->new_bytes[index], 2u);
    }
    text[at] = '\0';
}
