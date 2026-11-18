/*
 * The 8 bpp palette onto the card: the VGA DAC ports, or VBE 4F09h through
 * the mini-VDD (V9XMAPI.INC, VBE_PAL_STAGE and VBE_PAL_LOAD).
 *
 * 4F09h when 4F00h's capabilities say the controller is not VGA compatible
 * (bit 1), which is what VBE 3.0 provides the function for. Measured on the
 * Matrox Millennium P650 (docs\decisions\2026-11-18-p650-vbe-palette-and-
 * stride.md): in VBE modes its BIOS loads its own LUT registers and the DAC
 * ports reach a table the screen does not use, so the desktop came up in
 * the BIOS's default VGA colours - navy as red, white as black. No other
 * card on record sets the bit, so every other card keeps the ports.
 *
 * Decided at each Enable. A load the mini-VDD cannot make (no V86 scratch,
 * or the BIOS refused) drops back to the ports for the rest of that Enable,
 * and Palette= in V9XBOOT.INI says which route ran and how it ended.
 *
 * Its own module so the ati family can compile it into LEAFCODE: its _TEXT
 * had about 30 bytes left. Calls only runtime.asm's FAR entries and Windows.
 */
#include "palette16.h"

#include "velocity9x/diagpaths.h"
#include "velocity9x/vbe_parse.h"

#define V9X_PALETTE16_ENTRIES     256u
/* V9X_VBE_PAL_SLOTS in V9XMAPI.INC: the 512-byte V86 scratch. */
#define V9X_PALETTE16_VBE_SLOTS   128u
#define V9X_PALETTE16_ROUTE_PORTS 0u
#define V9X_PALETTE16_ROUTE_VBE   1u

/* runtime.asm. */
extern WORD FAR PASCAL V9xMiniVbeController(void);
extern WORD FAR PASCAL V9xMiniVbePalStage(WORD slot, DWORD e0, DWORD e1,
                                          DWORD e2, DWORD e3);
extern WORD FAR PASCAL V9xMiniVbePalLoad(WORD first, WORD count);
extern void FAR PASCAL V9xEnsureDiagDir(void);
/* enable16.c: 4F00h's capabilities, valid once V9xMiniVbeController()
 * has answered. */
extern DWORD v9x_minivdd_capabilities;

static void v9x_palette16_out(WORD port, BYTE value);
#pragma aux v9x_palette16_out = "out dx,al" parm [dx] [al] modify exact []

static WORD v9x_palette16_route = V9X_PALETTE16_ROUTE_PORTS;
/* The mini-VDD's answer to the last VBE load: 1 loaded, 2 the BIOS refused,
 * 0 no API or scratch. */
static WORD v9x_palette16_vbe_result = 0u;

void V9X_LEAF_FAR v9x_palette16_choose(void)
{
    v9x_palette16_route = V9X_PALETTE16_ROUTE_PORTS;
    v9x_palette16_vbe_result = 0u;
    if (V9xMiniVbeController() != 0u &&
        (v9x_minivdd_capabilities & V9X_VBE_CAP_NOT_VGA_COMPAT) != 0ul) {
        v9x_palette16_route = V9X_PALETTE16_ROUTE_VBE;
    }
}

/* One table entry in 4F09h's layout: blue, green, red, 0, six bits each,
 * the DAC width the BIOS starts in and this driver never changes. Past the
 * table is 0: those slots are staged but never loaded. */
static DWORD v9x_palette16_vbe_entry(const RGBQUAD FAR *table, WORD index)
{
    const RGBQUAD FAR *entry;

    if (index >= V9X_PALETTE16_ENTRIES) {
        return 0ul;
    }
    entry = &table[index];
    return (DWORD)(entry->rgbBlue >> 2) |
           ((DWORD)(entry->rgbGreen >> 2) << 8) |
           ((DWORD)(entry->rgbRed >> 2) << 16);
}

/* Non-zero when every entry was loaded through 4F09h. */
static WORD v9x_palette16_load_vbe(const RGBQUAD FAR *table, WORD start,
                                   WORD count)
{
    WORD chunk;
    WORD slot;
    WORD at;

    while (count != 0u) {
        chunk = count > V9X_PALETTE16_VBE_SLOTS ? V9X_PALETTE16_VBE_SLOTS
                                                : count;
        for (slot = 0u; slot < chunk; slot += 4u) {
            at = (WORD)(start + slot);
            if (V9xMiniVbePalStage(slot,
                                   v9x_palette16_vbe_entry(table, at),
                                   v9x_palette16_vbe_entry(table,
                                                           (WORD)(at + 1u)),
                                   v9x_palette16_vbe_entry(table,
                                                           (WORD)(at + 2u)),
                                   v9x_palette16_vbe_entry(table,
                                                           (WORD)(at + 3u)))
                    != 1u) {
                v9x_palette16_vbe_result = 0u;
                return 0u;
            }
        }
        v9x_palette16_vbe_result = V9xMiniVbePalLoad(start, chunk);
        if (v9x_palette16_vbe_result != 1u) {
            return 0u;
        }
        start = (WORD)(start + chunk);
        count = (WORD)(count - chunk);
    }
    return 1u;
}

void V9X_LEAF_FAR v9x_palette16_load(const RGBQUAD FAR *table, WORD start,
                                     WORD count)
{
    const RGBQUAD FAR *entry;

    if (v9x_palette16_route == V9X_PALETTE16_ROUTE_VBE) {
        if (v9x_palette16_load_vbe(table, start, count) != 0u) {
            return;
        }
        v9x_palette16_route = V9X_PALETTE16_ROUTE_PORTS;
    }

    v9x_palette16_out(0x03c8u, (BYTE)start);
    entry = &table[start];
    while (count-- != 0u) {
        v9x_palette16_out(0x03c9u, (BYTE)(entry->rgbRed >> 2));
        v9x_palette16_out(0x03c9u, (BYTE)(entry->rgbGreen >> 2));
        v9x_palette16_out(0x03c9u, (BYTE)(entry->rgbBlue >> 2));
        ++entry;
    }
}

void V9X_LEAF_FAR v9x_palette16_trace(void)
{
    /* "route=N vbe=N": both values are single digits. */
    static char text[] = "route=0 vbe=0";

    text[6] = (char)('0' + v9x_palette16_route);
    text[12] = (char)('0' + v9x_palette16_vbe_result);
    V9xEnsureDiagDir();
    WritePrivateProfileString("Velocity9x", "Palette", (LPCSTR)text,
                              V9X_DIAG_BOOT_INI);
}
