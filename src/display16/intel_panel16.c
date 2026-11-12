/*
 * The Intel panel-mode plan at Enable: read the shadowed video BIOS and the
 * panel's EDID, ask v9x_vbios_panel_plan whether the BIOS's panel slot needs
 * the panel's own timing, and leave the answer where the mini-VDD reads it
 * at the next boot (include\velocity9x\vbios_panel.h has the whole split).
 *
 * LEAFCODE: called far from intel_hw16.c's diagnostics hook. It calls only
 * far entries (runtime.asm, KERNEL, the far v9x_edid_parse and
 * v9x_vbios_panel_plan), never a near function in _TEXT.
 */
#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/diagpaths.h"
#include "velocity9x/edid.h"
#include "velocity9x/vbios_panel.h"

extern WORD FAR PASCAL V9xRomSelector(void);
extern WORD FAR PASCAL V9xMiniVbeEdidChunk(WORD index);
extern DWORD v9x_minivdd_edid0;
extern DWORD v9x_minivdd_edid1;
extern DWORD v9x_minivdd_edid2;
extern DWORD v9x_minivdd_edid3;

/* The one chip whose BIOS layout and PAM registers have been checked: the
 * GM965's (TOMMOHP, 2026-11-12). Another chip gets the plan once its own
 * BIOS has been read. */
#define V9X_PANEL_DEVICE_GM965 0x2A02u

/* A readable ROM window: the selector's limit. */
#define V9X_PANEL_ROM_BYTES ((v9x_u16)0xFFFFu)

#define V9X_PANEL_SYSTEM_INI "SYSTEM.INI"
#define V9X_PANEL_HW_SECTION "Velocity9xHardware"
#define V9X_PANEL_HW_KEY "VbiosPanel"

static const char *const v9x_panel_codes[] = {
    "?", "planned", "already", "no-edid", "not-rom", "no-table", "tables",
    "no-slot", "slots", "split"
};

/* The EDID base block the mini-VDD collected, 16 bytes a call. */
static WORD v9x_panel_read_edid(v9x_u8 *block)
{
    WORD chunk;

    for (chunk = 0u; chunk < (WORD)(V9X_EDID_BLOCK_BYTES / 16u); ++chunk) {
        DWORD *slice = (DWORD *)&block[chunk * 16u];

        if (V9xMiniVbeEdidChunk(chunk) == 0u) {
            return 0u;
        }
        slice[0] = v9x_minivdd_edid0;
        slice[1] = v9x_minivdd_edid1;
        slice[2] = v9x_minivdd_edid2;
        slice[3] = v9x_minivdd_edid3;
    }
    return 1u;
}

static void v9x_panel_note(const char *text)
{
    WritePrivateProfileString(V9X_PANEL_HW_SECTION, V9X_PANEL_HW_KEY, text,
                              V9X_DIAG_HW_INI);
}

void V9X_LEAF_FAR v9x_intel_plan_panel(unsigned short device_id)
{
    static v9x_u8 edid[V9X_EDID_BLOCK_BYTES];
    static struct v9x_vbios_patch patch;
    static char record[V9X_VBIOS_PATCH_TEXT_BYTES];
    static char note[V9X_VBIOS_PATCH_TEXT_BYTES + 16];
    WORD selector;
    v9x_u16 code;
    WORD at = 0u;
    const char *name;

    if (device_id != V9X_PANEL_DEVICE_GM965) {
        return;
    }
    if (v9x_panel_read_edid(edid) == 0u) {
        v9x_panel_note("no-edid-api");
        return;
    }
    selector = V9xRomSelector();
    if (selector == 0u) {
        v9x_panel_note("no-selector");
        return;
    }
    code = v9x_vbios_panel_plan((const v9x_u8 *)MAKELP(selector, 0),
                                V9X_PANEL_ROM_BYTES, edid, &patch);

    /* The code's name, then the record when there is one: what was planned,
     * or, for "already", what the mini-VDD wrote at this boot. */
    name = code < (v9x_u16)(sizeof(v9x_panel_codes) /
                            sizeof(v9x_panel_codes[0])) ?
           v9x_panel_codes[code] : v9x_panel_codes[0];
    while (*name != '\0') {
        note[at++] = *name++;
    }
    if (code == V9X_VBIOS_PANEL_PLANNED || code == V9X_VBIOS_PANEL_ALREADY) {
        WORD index = 0u;

        v9x_vbios_panel_format(&patch, record);
        note[at++] = ' ';
        while (record[index] != '\0') {
            note[at++] = record[index++];
        }
    }
    note[at] = '\0';
    v9x_panel_note(note);

    /* Only a new plan is stored. "Already" leaves the record that produced
     * it in place, and a refusal leaves any earlier record to the mini-VDD's
     * own check of the bytes it would replace. */
    if (code == V9X_VBIOS_PANEL_PLANNED) {
        WritePrivateProfileString(V9X_VBIOS_PATCH_SECTION,
                                  V9X_VBIOS_PATCH_KEY, record,
                                  V9X_PANEL_SYSTEM_INI);
    }
}
