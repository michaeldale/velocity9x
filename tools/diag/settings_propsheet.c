/*
 * Velocity9x Display Properties settings page.
 *
 * This is a 32-bit shell property-sheet extension for Windows 98. The
 * Display control panel loads it through the registry key
 *
 *   HKLM\Software\Microsoft\Windows\CurrentVersion\
 *       Controls Folder\Display\shellex\PropertySheetHandlers
 *
 * and the page appears as a "Velocity9x" tab inside the native Display
 * Properties dialog. It renders the same driver-published INI facts as the
 * standalone V9XSET.EXE panel and offers the clipboard report, and it
 * performs no hardware access.
 *
 * The first tab only reports. The Advanced tab writes keys the driver
 * already reads - [Velocity9x] Direct3D, Direct3DDdi, VSync, HighColor,
 * GdiAccel, D3DSoftSysMem and the [Velocity9x.Direct3DDdi] program list -
 * and C:\V9XDIAG\NOWC.TXT, write combining's off switch.
 *
 * The module is built without a C runtime, so COM is implemented with
 * explicit vtables and static singleton objects.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <prsht.h>

#include "velocity9x/build.h"
#include "velocity9x/d3dmode.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/vsync.h"
#include "settings_propsheet.h"
#include "settings_status.h"

/* Where the one writable setting lives. The same pair is in
 * tools\diag\settings_status.c, src\display16\dd16.c and
 * src\display16\gdi_accel.c; scripts\check-tree.ps1 asserts they agree. */
#define V9X_SETTINGS_INI     "SYSTEM.INI"
#define V9X_SETTINGS_SECTION "Velocity9x"

#define V9X_S_OK                       ((LONG)0x00000000l)
#define V9X_S_FALSE                    ((LONG)0x00000001l)
#define V9X_E_NOTIMPL                  ((LONG)0x80004001l)
#define V9X_E_NOINTERFACE              ((LONG)0x80004002l)
#define V9X_E_POINTER                  ((LONG)0x80004003l)
#define V9X_E_FAIL                     ((LONG)0x80004005l)
#define V9X_E_OUTOFMEMORY              ((LONG)0x8000000El)
#define V9X_CLASS_E_NOAGGREGATION      ((LONG)0x80040110l)
#define V9X_CLASS_E_CLASSNOTAVAILABLE  ((LONG)0x80040111l)

typedef struct v9x_guid {
    DWORD data1;
    WORD data2;
    WORD data3;
    BYTE data4[8];
} V9X_GUID;

/* {91925DA2-2EF0-4E20-B4E9-A53ED37E14B1} */
static const V9X_GUID v9x_clsid_settings_page =
    { 0x91925da2ul, 0x2ef0u, 0x4e20u,
      { 0xb4u, 0xe9u, 0xa5u, 0x3eu, 0xd3u, 0x7eu, 0x14u, 0xb1u } };
static const V9X_GUID v9x_iid_unknown =
    { 0x00000000ul, 0x0000u, 0x0000u,
      { 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } };
static const V9X_GUID v9x_iid_class_factory =
    { 0x00000001ul, 0x0000u, 0x0000u,
      { 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } };
static const V9X_GUID v9x_iid_shell_ext_init =
    { 0x000214e8ul, 0x0000u, 0x0000u,
      { 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } };
static const V9X_GUID v9x_iid_shell_propsheet_ext =
    { 0x000214e9ul, 0x0000u, 0x0000u,
      { 0xc0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x46u } };

typedef BOOL (CALLBACK *V9X_ADD_PAGE_PROC)(HPROPSHEETPAGE, LPARAM);

static HINSTANCE v9x_page_instance;
static LONG v9x_object_count;
static V9X_SETTINGS_STATUS v9x_page_status;
static const char v9x_page_caption[] = "Velocity9x Settings";

/*
 * The Direct3D selector's state, between WM_INITDIALOG and PSN_APPLY.
 *
 * v9x_page_d3d_loaded is what SYSTEM.INI said when the page opened. Apply
 * compares against it and writes nothing when they match, so opening the page
 * on a machine somebody configured by hand and pressing OK cannot rewrite the
 * file - including when the value is a mode this build does not implement.
 */
static int v9x_page_d3d_loaded;

/*
 * The 16-bit colour layout selector's state, on the same terms.
 *
 * Three values and the first is an absence: 0 means the HighColor key is not
 * in SYSTEM.INI and the driver decides - 5:5:5 when Direct3D resolved to an
 * engine that wants it, 5:6:5 otherwise. The ViRGE's S3D unit writes ZRGB1555
 * into every 16-bit target and has no other layout to offer; the Rage IIC
 * draws either, and 5:5:5 is what ATI's driver ran and what 3DCIF titles
 * assume (dd16.c, v9x_dd_engine_wants_555). 3DCIF needs hardware Direct3D,
 * so Automatic gives it 5:5:5 wherever it can run; only an explicit 5:6:5
 * takes that away, which the label says. 15 and 16 are S3's own values
 * for the same key, so a SYSTEM.INI carried over from their driver reads the
 * same way here. Choosing Automatic deletes the key rather than writing 0, so
 * the file says what the page says.
 */
static int v9x_page_layout_loaded;

static const struct v9x_layout_choice {
    int value;
    const char *label;
} v9x_page_layout_choices[] = {
    { 0,  "Automatic (the 3D engine's layout)" },
    { 16, "5:6:5 - more greens; some 3D games miscolour" },
    { 15, "5:5:5 - what some 3D engines and games need" }
};
#define V9X_PAGE_LAYOUT_CHOICE_COUNT \
    (sizeof(v9x_page_layout_choices) / sizeof(v9x_page_layout_choices[0]))

/*
 * Only what this build implements is offered.
 *
 * Adding "Hybrid" and "Offload" here the day before they exist is the same
 * defect as publishing a Direct3D capability the engine does not serve, which
 * this driver has shipped once: the control would promise a rendering path and
 * deliver none. They join the list when they render pixels. The order is the
 * list order, and the value is what goes in the file.
 *
 * "Software" joined it on 2026-09-01, when it started rendering them - depth
 * tested, textured Gouraud triangles on a Trio64, which is a card with no 3D
 * engine at all
 * (docs\decisions\2026-09-01-software-textures-and-caps.md).
 *
 * `needs_engine` decides which entries a given card sees, and it hides the
 * entry rather than relabelling it. Only Hardware needs an engine: Software
 * runs on the framebuffer and Disabled describes an absence, so both are
 * offered everywhere. A Trio64, an ATI or a VESA card therefore sees exactly
 * Software and Disabled, and only a chip with a Direct3D engine the driver
 * actually implements - the ViRGE's S3D unit today - is offered Hardware.
 *
 * Offering it everywhere and labelling it "this card has no 3D engine" was
 * tried first and is worse. It puts an entry in the list whose only effect is
 * to produce no Direct3D, which is what the entry below it already says, and
 * it invites the reading that the mode is a choice the card is failing at
 * rather than one it was never offered.
 *
 * The default is still reachable. HARDWARE is zero, which is what an absent
 * key means, so a card that is not offered it can still be sitting on it - see
 * the tail of v9x_page_fill_d3d, which gives that state its own entry carrying
 * the card's own words for it.
 */
static const struct v9x_d3d_choice {
    int value;
    int needs_engine;
    const char *label;
} v9x_page_d3d_choices[] = {
    { (int)V9X_D3D_REQUEST_HARDWARE, 1, "Hardware (the chip's own engine)" },
    { (int)V9X_D3D_REQUEST_SOFTWARE, 0, "Software (CPU rasterizer - slow)" },
    { (int)V9X_D3D_REQUEST_DISABLED, 0, "Disabled - advertise no Direct3D" }
};
#define V9X_PAGE_D3D_CHOICE_COUNT \
    (sizeof(v9x_page_d3d_choices) / sizeof(v9x_page_d3d_choices[0]))

/*
 * Fill the selector and select the current value.
 *
 * The control used to be greyed out on a chip with no 3D engine, on the
 * grounds that no value there could produce Direct3D. That stopped being true
 * on 2026-09-01: the software rasterizer serves Direct3D from the CPU on every
 * card the driver supports, and the cards with no engine are precisely the
 * ones it exists for. Leaving the control disabled would have hidden the mode
 * from its own audience - the setting worked, and only the page could not
 * reach it.
 *
 * So the control is always live, and what the card can do decides which
 * entries exist rather than what they are called.
 *
 * The tail is the case worth the code, and it now covers two things. When
 * SYSTEM.INI holds a value the list does not offer, that value gets its own
 * entry and is selected, so the page reports what is actually in the file
 * instead of quietly presenting something else and writing it back on OK. That
 * happens for a mode from a later build or a typo - and also, routinely, for
 * the default on a card with no engine: HARDWARE is zero, an absent key reads
 * as zero, and such a card is not offered zero. That case gets the card's own
 * description of what it is doing rather than the wording for a mode this
 * build does not have, because "Not advertised on this chip" is true and
 * "Mode set in SYSTEM.INI, not in this build" would not be.
 *
 * Either way the entry carries the loaded value, so selecting nothing changes
 * nothing: v9x_page_apply_d3d returns early when the selection equals what was
 * loaded.
 */
static void v9x_page_fill_d3d(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, V9X_IDC_DIRECT3D_MODE);
    UINT index;
    LRESULT item;

    if (combo == 0) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    v9x_page_d3d_loaded = v9x_page_status.direct3d_request;
    EnableWindow(combo, TRUE);

    for (index = 0u; index < V9X_PAGE_D3D_CHOICE_COUNT; ++index) {
        if (v9x_page_d3d_choices[index].needs_engine != 0 &&
            !v9x_page_status.direct3d_capable) {
            continue;
        }
        item = SendMessageA(combo, CB_ADDSTRING, 0,
                            (LPARAM)v9x_page_d3d_choices[index].label);
        if (item < 0) {
            continue;
        }
        SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                     (LPARAM)v9x_page_d3d_choices[index].value);
        if (v9x_page_d3d_choices[index].value == v9x_page_d3d_loaded) {
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
        }
    }

    if (SendMessageA(combo, CB_GETCURSEL, 0, 0) == CB_ERR) {
        const char *label =
            v9x_page_d3d_loaded == (int)V9X_D3D_REQUEST_HARDWARE
                ? v9x_page_status.direct3d
                : "Mode set in SYSTEM.INI, not in this build";

        item = SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)label);
        if (item >= 0) {
            SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                         (LPARAM)v9x_page_d3d_loaded);
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
        }
    }
}

/*
 * The vertical sync selector, on the same terms as the layout selector.
 *
 * Game decides is the key's absence and today's behaviour; choosing it
 * deletes the key. Every card gets all three entries: the rule is applied by
 * the shared flip path, and on a card whose flips DirectDraw copies instead
 * it changes nothing, which is not worth hiding the control for. "Always
 * off" says it may tear: an unsynced flip can show a split frame, and
 * VSync=2 has been seen to flicker.
 */
static int v9x_page_vsync_loaded;

static const struct v9x_vsync_choice {
    int value;
    const char *label;
} v9x_page_vsync_choices[] = {
    { (int)V9X_VSYNC_REQUEST_APPLICATION, "Game decides" },
    { (int)V9X_VSYNC_REQUEST_ON,          "Always on" },
    { (int)V9X_VSYNC_REQUEST_OFF,         "Always off (may tear)" }
};
#define V9X_PAGE_VSYNC_CHOICE_COUNT \
    (sizeof(v9x_page_vsync_choices) / sizeof(v9x_page_vsync_choices[0]))

static void v9x_page_fill_vsync(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, V9X_IDC_VSYNC);
    UINT index;
    LRESULT item;

    if (combo == 0) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    v9x_page_vsync_loaded = v9x_page_status.vsync_request;
    for (index = 0u; index < V9X_PAGE_VSYNC_CHOICE_COUNT; ++index) {
        item = SendMessageA(combo, CB_ADDSTRING, 0,
                            (LPARAM)v9x_page_vsync_choices[index].label);
        if (item < 0) {
            continue;
        }
        SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                     (LPARAM)v9x_page_vsync_choices[index].value);
        if (v9x_page_vsync_choices[index].value == v9x_page_vsync_loaded) {
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
        }
    }
    /* A value this page does not offer keeps its own entry, so OK on an
     * untouched page does not rewrite it. The driver reads it as Game
     * decides. */
    if (SendMessageA(combo, CB_GETCURSEL, 0, 0) == CB_ERR) {
        item = SendMessageA(combo, CB_ADDSTRING, 0,
                            (LPARAM)"Other (SYSTEM.INI)");
        if (item >= 0) {
            SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                         (LPARAM)v9x_page_vsync_loaded);
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
        }
    }
}

static int v9x_page_selected_vsync(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, V9X_IDC_VSYNC);
    LRESULT selection;
    LRESULT data;

    if (combo == 0) {
        return v9x_page_vsync_loaded;
    }
    selection = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) {
        return v9x_page_vsync_loaded;
    }
    data = SendMessageA(combo, CB_GETITEMDATA, (WPARAM)selection, 0);
    if (data == CB_ERR) {
        return v9x_page_vsync_loaded;
    }
    return (int)data;
}

static int v9x_page_apply_vsync(HWND dialog)
{
    int selected = v9x_page_selected_vsync(dialog);
    const char *value;

    if (selected == v9x_page_vsync_loaded) {
        return 0;
    }
    if (selected == (int)V9X_VSYNC_REQUEST_APPLICATION) {
        value = 0;
    } else if (selected == (int)V9X_VSYNC_REQUEST_ON) {
        value = "1";
    } else if (selected == (int)V9X_VSYNC_REQUEST_OFF) {
        value = "2";
    } else {
        return 0;
    }
    if (!WritePrivateProfileStringA(V9X_SETTINGS_SECTION,
                                    V9X_VSYNC_SETTING_KEY, value,
                                    V9X_SETTINGS_INI)) {
        MessageBoxA(dialog,
                    "Could not write the VSync setting to SYSTEM.INI.\n\n"
                    "The file may be read-only or in use.",
                    v9x_page_caption, MB_OK | MB_ICONWARNING);
        return 0;
    }
    v9x_page_vsync_loaded = selected;
    return 1;
}

/*
 * The Direct3D interface selector ([Velocity9x] Direct3DDdi), on the same
 * terms as the VSync one.
 *
 * Automatic is the key's absence, and the driver chooses per program
 * (v9x_d3d_dp2_for_process): DrawPrimitives2 for programs that load
 * Direct3D 8 or 9, for every program on a two-stage engine, and for any
 * [Velocity9x.Direct3DDdi] lists; the DX5 interface for the rest. On Haswell
 * DrawPrimitives2 is answered with DDI 7 or 8, so the labels name the
 * interface and not a DDI number. 5 removes DrawPrimitives2 from every
 * program, and the Direct3D 8 runtime has no hardware device without it,
 * which the label says. Like VSync it needs no restart.
 */
static int v9x_page_ddi_loaded;

static const struct v9x_ddi_choice {
    int value;
    const char *label;
} v9x_page_ddi_choices[] = {
    { 0, "Automatic (per program)" },
    { 5, "DirectX 5 only - DX8 games lose 3D" },
    { 6, "DrawPrimitives2 for every program" }
};
#define V9X_PAGE_DDI_CHOICE_COUNT \
    (sizeof(v9x_page_ddi_choices) / sizeof(v9x_page_ddi_choices[0]))

static void v9x_page_fill_ddi(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, V9X_IDC_DDI);
    UINT index;
    LRESULT item;

    if (combo == 0) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    v9x_page_ddi_loaded = v9x_page_status.ddi_request;
    for (index = 0u; index < V9X_PAGE_DDI_CHOICE_COUNT; ++index) {
        item = SendMessageA(combo, CB_ADDSTRING, 0,
                            (LPARAM)v9x_page_ddi_choices[index].label);
        if (item < 0) {
            continue;
        }
        SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                     (LPARAM)v9x_page_ddi_choices[index].value);
        if (v9x_page_ddi_choices[index].value == v9x_page_ddi_loaded) {
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
        }
    }
}

static int v9x_page_selected_ddi(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, V9X_IDC_DDI);
    LRESULT selection;
    LRESULT data;

    if (combo == 0) {
        return v9x_page_ddi_loaded;
    }
    selection = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) {
        return v9x_page_ddi_loaded;
    }
    data = SendMessageA(combo, CB_GETITEMDATA, (WPARAM)selection, 0);
    if (data == CB_ERR) {
        return v9x_page_ddi_loaded;
    }
    return (int)data;
}

static int v9x_page_apply_ddi(HWND dialog)
{
    int selected = v9x_page_selected_ddi(dialog);
    const char *value;

    if (selected == v9x_page_ddi_loaded) {
        return 0;
    }
    if (selected == 0) {
        value = 0;
    } else if (selected == 5) {
        value = "5";
    } else if (selected == 6) {
        value = "6";
    } else {
        return 0;
    }
    if (!WritePrivateProfileStringA(V9X_SETTINGS_SECTION, "Direct3DDdi",
                                    value, V9X_SETTINGS_INI)) {
        MessageBoxA(dialog,
                    "Could not write the Direct3D interface setting to "
                    "SYSTEM.INI.\n\n"
                    "The file may be read-only or in use.",
                    v9x_page_caption, MB_OK | MB_ICONWARNING);
        return 0;
    }
    v9x_page_ddi_loaded = selected;
    return 1;
}

static int v9x_page_selected_d3d(HWND dialog);

/*
 * The controls that follow the Direct3D selector, not the file, so choosing
 * Software on an engineless card enables them at once. Values are kept
 * either way; a greyed control still writes nothing new.
 *
 * The interface selector and its per-program list are live only where the
 * driver reads them: dd16.c consults Direct3DDdi only beside the D3D
 * capability bit, which a hardware engine or the software rasterizer sets
 * and Disabled clears. The software-texture box only under Software, the
 * one engine that reads D3DSoftSysMem.
 */
static void v9x_page_update_ddi_enable(HWND dialog)
{
    int selected = v9x_page_selected_d3d(dialog);
    BOOL live;

    live = selected != (int)V9X_D3D_REQUEST_DISABLED &&
           (v9x_page_status.direct3d_capable ||
            selected == (int)V9X_D3D_REQUEST_SOFTWARE);
    EnableWindow(GetDlgItem(dialog, V9X_IDC_DDI), live);
    EnableWindow(GetDlgItem(dialog, V9X_IDC_DDI_PROGRAMS), live);
    EnableWindow(GetDlgItem(dialog, V9X_IDC_SOFT_SYSMEM),
                 selected == (int)V9X_D3D_REQUEST_SOFTWARE);
}

/* The selector's current value, or the loaded one when nothing is selected. */
static int v9x_page_selected_d3d(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, V9X_IDC_DIRECT3D_MODE);
    LRESULT selection;
    LRESULT data;

    if (combo == 0) {
        return v9x_page_d3d_loaded;
    }
    selection = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) {
        return v9x_page_d3d_loaded;
    }
    data = SendMessageA(combo, CB_GETITEMDATA, (WPARAM)selection, 0);
    if (data == CB_ERR) {
        return v9x_page_d3d_loaded;
    }
    return (int)data;
}

/*
 * Fill the layout selector and select the loaded value.
 *
 * Every card gets all three entries: unlike Hardware Direct3D, 5:5:5 is
 * something every card in this driver can be asked for - it is a VESA mode
 * number away - and a value nobody defined is given its own entry so the page
 * reports the file rather than rewriting it, as the Direct3D selector does.
 */
static void v9x_page_fill_layout(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, V9X_IDC_COLOUR_LAYOUT);
    UINT index;
    LRESULT item;

    if (combo == 0) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    v9x_page_layout_loaded = v9x_page_status.highcolor_request;
    for (index = 0u; index < V9X_PAGE_LAYOUT_CHOICE_COUNT; ++index) {
        item = SendMessageA(combo, CB_ADDSTRING, 0,
                            (LPARAM)v9x_page_layout_choices[index].label);
        if (item < 0) {
            continue;
        }
        SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                     (LPARAM)v9x_page_layout_choices[index].value);
        if (v9x_page_layout_choices[index].value == v9x_page_layout_loaded) {
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
        }
    }
    if (SendMessageA(combo, CB_GETCURSEL, 0, 0) == CB_ERR) {
        item = SendMessageA(combo, CB_ADDSTRING, 0,
                            (LPARAM)"Value set in SYSTEM.INI, not one this "
                                    "page offers");
        if (item >= 0) {
            SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                         (LPARAM)v9x_page_layout_loaded);
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
        }
    }
}

static int v9x_page_selected_layout(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, V9X_IDC_COLOUR_LAYOUT);
    LRESULT selection;
    LRESULT data;

    if (combo == 0) {
        return v9x_page_layout_loaded;
    }
    selection = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) {
        return v9x_page_layout_loaded;
    }
    data = SendMessageA(combo, CB_GETITEMDATA, (WPARAM)selection, 0);
    if (data == CB_ERR) {
        return v9x_page_layout_loaded;
    }
    return (int)data;
}

/*
 * Write the layout selector to SYSTEM.INI. Returns non-zero when the file
 * changed, so the caller can say once what happened; a failed write reports
 * itself here and returns zero.
 */
static int v9x_page_apply_layout(HWND dialog)
{
    int selected = v9x_page_selected_layout(dialog);
    const char *value;

    if (selected == v9x_page_layout_loaded) {
        return 0;
    }
    /* Automatic is the key's absence. Deleting it, rather than writing 0,
     * keeps the file readable by eye and by S3's own driver, which knows the
     * same two numbers and nothing about a zero. */
    if (selected == 0) {
        value = 0;
    } else if (selected == 15) {
        value = "15";
    } else if (selected == 16) {
        value = "16";
    } else {
        return 0;
    }
    if (!WritePrivateProfileStringA(V9X_SETTINGS_SECTION, "HighColor", value,
                                    V9X_SETTINGS_INI)) {
        MessageBoxA(dialog,
                    "Could not write the 16-bit colour setting to "
                    "SYSTEM.INI.\n\nThe file may be read-only or in use.",
                    v9x_page_caption, MB_OK | MB_ICONWARNING);
        return 0;
    }
    v9x_page_layout_loaded = selected;
    return 1;
}

/*
 * Write the selector to SYSTEM.INI, and say when it takes effect.
 *
 * "After you restart" is measured, not a hedge:
 * docs\decisions\2026-08-30-d3d-mode-disabled-gate.md. A re-enable does move
 * the driver, but DDRAW keeps offering the Direct3D HAL device it enumerated
 * from the previous session and creating it then fails with E_NOINTERFACE -
 * an application picks a hardware device and dies instead of falling back to
 * software. Only a fresh boot removes the entry, so the page must not offer a
 * shorter promise.
 */
static int v9x_page_apply_d3d(HWND dialog)
{
    int selected = v9x_page_selected_d3d(dialog);
    char value[2];

    if (selected == v9x_page_d3d_loaded) {
        return 0;
    }
    /* One digit covers every value this page can select, and the page never
     * writes a value it did not put in the list. */
    if (selected < 0 || selected > 9) {
        return 0;
    }
    value[0] = (char)('0' + selected);
    value[1] = '\0';
    if (!WritePrivateProfileStringA(V9X_SETTINGS_SECTION,
                                    V9X_D3D_SETTING_KEY, value,
                                    V9X_SETTINGS_INI)) {
        MessageBoxA(dialog,
                    "Could not write the Direct3D setting to SYSTEM.INI.\n\n"
                    "The file may be read-only or in use.",
                    v9x_page_caption, MB_OK | MB_ICONWARNING);
        return 0;
    }
    v9x_page_d3d_loaded = selected;
    return 1;
}

static void v9x_page_widen_list(HWND dialog, int control);

/* A combo's selected item data, or `fallback` when nothing is selected. */
static int v9x_page_combo_value(HWND dialog, int control, int fallback)
{
    HWND combo = GetDlgItem(dialog, control);
    LRESULT selection;
    LRESULT data;

    if (combo == 0) {
        return fallback;
    }
    selection = SendMessageA(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) {
        return fallback;
    }
    data = SendMessageA(combo, CB_GETITEMDATA, (WPARAM)selection, 0);
    return data == CB_ERR ? fallback : (int)data;
}

/*
 * 2D acceleration ([Velocity9x] GdiAccel), on the same terms as the VSync
 * selector.
 *
 * Only the master key. gdi_accel.c reads it at Enable and, when it is not
 * zero, turns on each primitive its own key and the chip allow; Off sends
 * every GDI call to the DIB engine, which is the troubleshooting switch a
 * user needs. The per-primitive keys stay hand-edited. On is the key's
 * absence (V9X_GDI_DEFAULT_MASTER is 1), so choosing it deletes the key.
 *
 * Greyed on a chip whose engine reported no fill and no copy, where the key
 * changes nothing - unless the file already turns it off, so it can always
 * be turned back on from here.
 */
static int v9x_page_gdi_loaded;

static const struct v9x_gdi_choice {
    int value;
    const char *label;
} v9x_page_gdi_choices[] = {
    { 1, "On (the driver's default)" },
    { 0, "Off - the CPU draws all 2D" }
};
#define V9X_PAGE_GDI_CHOICE_COUNT \
    (sizeof(v9x_page_gdi_choices) / sizeof(v9x_page_gdi_choices[0]))

static void v9x_page_fill_gdi(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, V9X_IDC_GDI_ACCEL);
    UINT index;
    LRESULT item;

    if (combo == 0) {
        return;
    }
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    v9x_page_gdi_loaded = v9x_page_status.gdi_accel_request;
    for (index = 0u; index < V9X_PAGE_GDI_CHOICE_COUNT; ++index) {
        item = SendMessageA(combo, CB_ADDSTRING, 0,
                            (LPARAM)v9x_page_gdi_choices[index].label);
        if (item < 0) {
            continue;
        }
        SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                     (LPARAM)v9x_page_gdi_choices[index].value);
        if (v9x_page_gdi_choices[index].value == v9x_page_gdi_loaded) {
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
        }
    }
    /* gdi_accel.c reads any non-zero value as on; this page offers only the
     * absent key for that, so another number keeps its own entry. */
    if (SendMessageA(combo, CB_GETCURSEL, 0, 0) == CB_ERR) {
        item = SendMessageA(combo, CB_ADDSTRING, 0,
                            (LPARAM)"On (value set in SYSTEM.INI)");
        if (item >= 0) {
            SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                         (LPARAM)v9x_page_gdi_loaded);
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
        }
    }
    EnableWindow(combo, v9x_page_status.gdi_engine_capable ||
                        v9x_page_gdi_loaded == 0);
}

static int v9x_page_apply_gdi(HWND dialog)
{
    int selected = v9x_page_combo_value(dialog, V9X_IDC_GDI_ACCEL,
                                        v9x_page_gdi_loaded);
    const char *value;

    if (selected == v9x_page_gdi_loaded) {
        return 0;
    }
    if (selected == 1) {
        value = 0;
    } else if (selected == 0) {
        value = "0";
    } else {
        return 0;
    }
    if (!WritePrivateProfileStringA(V9X_SETTINGS_SECTION, "GdiAccel", value,
                                    V9X_SETTINGS_INI)) {
        MessageBoxA(dialog,
                    "Could not write the 2D acceleration setting to "
                    "SYSTEM.INI.\n\nThe file may be read-only or in use.",
                    v9x_page_caption, MB_OK | MB_ICONWARNING);
        return 0;
    }
    v9x_page_gdi_loaded = selected;
    return 1;
}

/*
 * [Velocity9x] D3DSoftSysMem: whether the software rasterizer advertises
 * textures in system memory as well as video memory (d3d_soft.c). Latched
 * at Enable beside the Direct3D mode (dd16.c), so a restart. Live only
 * while the Direct3D selector says Software, the one engine that reads it;
 * off is the key's absence. It bought Final Reality nothing when measured
 * (docs\decisions\2026-09-11-d3dsoftsysmem-buys-final-reality-nothing.md),
 * which is why it is a checkbox and not the default.
 */
static int v9x_page_soft_loaded;

static int v9x_page_apply_soft(HWND dialog)
{
    int selected = IsDlgButtonChecked(dialog, V9X_IDC_SOFT_SYSMEM) ==
                   BST_CHECKED;

    if (selected == v9x_page_soft_loaded) {
        return 0;
    }
    if (!WritePrivateProfileStringA(V9X_SETTINGS_SECTION, "D3DSoftSysMem",
                                    selected ? "1" : 0, V9X_SETTINGS_INI)) {
        MessageBoxA(dialog,
                    "Could not write the software texture setting to "
                    "SYSTEM.INI.\n\nThe file may be read-only or in use.",
                    v9x_page_caption, MB_OK | MB_ICONWARNING);
        return 0;
    }
    v9x_page_soft_loaded = selected;
    return 1;
}

/*
 * Write combining's switch: C:\V9XDIAG\NOWC.TXT, which enable16.c checks
 * before it writes the MTRR (or a chip's PAT route), and which until now
 * only V9X3D OFF and RECOVER.TXT wrote (diagpaths.h). Checked means the
 * file is absent. Greyed on a chip that does not opt in, where the file
 * changes nothing, unless the file exists, so it can always be removed.
 *
 * Off takes a cold start, not a restart: enable16.c finds a range the CPU
 * kept across a Windows restart ("present") before it looks for the file.
 */
static int v9x_page_wc_loaded;
static const char v9x_page_nowc_text[] =
    "Written by the Velocity9x settings page. While this file exists the "
    "driver does not make the framebuffer write-combining. Delete it, or "
    "tick Write combining on the Velocity9x Advanced tab, to allow it.\r\n";

static int v9x_page_apply_wc(HWND dialog)
{
    int selected = IsDlgButtonChecked(dialog, V9X_IDC_WRITE_COMBINE_SWITCH) ==
                   BST_CHECKED;
    HANDLE file;
    DWORD written = 0ul;

    if (selected == v9x_page_wc_loaded) {
        return 0;
    }
    if (selected) {
        if (!DeleteFileA(V9X_DIAG_NOWC_TXT)) {
            MessageBoxA(dialog,
                        "Could not delete " V9X_DIAG_NOWC_TXT ".",
                        v9x_page_caption, MB_OK | MB_ICONWARNING);
            return 0;
        }
    } else {
        (void)CreateDirectoryA(V9X_DIAG_DIR, 0);
        file = CreateFileA(V9X_DIAG_NOWC_TXT, GENERIC_WRITE, 0, 0,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file == INVALID_HANDLE_VALUE) {
            MessageBoxA(dialog,
                        "Could not create " V9X_DIAG_NOWC_TXT ".",
                        v9x_page_caption, MB_OK | MB_ICONWARNING);
            return 0;
        }
        (void)WriteFile(file, v9x_page_nowc_text,
                        sizeof(v9x_page_nowc_text) - 1u, &written, 0);
        CloseHandle(file);
    }
    v9x_page_wc_loaded = selected;
    return selected ? 1 : 2;
}

/*
 * Every control, one message that says when each change lands.
 *
 * The Direct3D wording is measured, not a hedge:
 * docs\decisions\2026-08-30-d3d-mode-disabled-gate.md - DDRAW keeps the
 * device it enumerated until a fresh boot. The colour layout, GdiAccel and
 * D3DSoftSysMem are read at Enable, so they too are a restart. VSync and the
 * interface are read each time DirectDraw creates its driver object.
 */
static void v9x_page_apply(HWND dialog)
{
    int wrote_d3d = v9x_page_apply_d3d(dialog);
    int wrote_layout = v9x_page_apply_layout(dialog);
    int wrote_vsync = v9x_page_apply_vsync(dialog);
    int wrote_ddi = v9x_page_apply_ddi(dialog);
    int wrote_gdi = v9x_page_apply_gdi(dialog);
    int wrote_soft = v9x_page_apply_soft(dialog);
    int wrote_wc = v9x_page_apply_wc(dialog);
    char text[640];

    if (!wrote_d3d && !wrote_layout && !wrote_vsync && !wrote_ddi &&
        !wrote_gdi && !wrote_soft && !wrote_wc) {
        return;
    }
    lstrcpyA(text, "Your changes have been saved.");
    if (wrote_vsync || wrote_ddi) {
        lstrcatA(text, "\n\nVertical sync and the Direct3D interface apply "
                       "from the next DirectDraw or Direct3D program.");
    }
    if (wrote_d3d || wrote_layout || wrote_gdi || wrote_soft ||
        wrote_wc == 1) {
        lstrcatA(text, "\n\nDirect3D, the 16-bit colour layout, 2D "
                       "acceleration, software textures and turning write "
                       "combining on take effect after you restart Windows. "
                       "Until then programs see the previous settings.");
    }
    if (wrote_wc == 2) {
        lstrcatA(text, "\n\nWrite combining stops after the computer is "
                       "switched off and on again: a Windows restart can "
                       "keep the processor's existing setting.");
    }
    MessageBoxA(dialog, text, v9x_page_caption, MB_OK | MB_ICONINFORMATION);
}

/*
 * The per-program interface dialog: [Velocity9x.Direct3DDdi] <file>=N.
 *
 * d3d_core.c (v9x_d3d_dp2_for_process) reads it by the program's file name
 * when a driver object is made: 5 is the DX5 interface, 6 is DDI 6, 7 is
 * DDI 7 on an engine that runs T&L and DDI 6 elsewhere, 8 is DDI 8 where
 * the engine has it and the step below otherwise. It is read only where
 * DrawPrimitives2 is available at all, so it cannot lift a program above a
 * global DirectX 5 only.
 *
 * Writes on OK, not on the sheet's Apply: the dialog is its own transaction.
 * Every key read at open and absent at OK is deleted, every listed one is
 * written, and a value this dialog does not offer is kept as it was.
 */
#define V9X_DDI_PROGRAMS_SECTION "Velocity9x.Direct3DDdi"
/* Under 32767, the Windows 9x profile API's limit; on the heap for the
 * reason given over V9X_MINIVDD_READ_BYTES in settings_status.c. */
#define V9X_DDI_PROGRAMS_BYTES   8192ul
/* The list's tab stop, in dialog units: room for an 8.3 name and more. */
#define V9X_DDI_PROGRAMS_TAB     96

static char *v9x_programs_loaded;
static int v9x_programs_dirty;

static const struct v9x_level_choice {
    int value;
    const char *label;
} v9x_program_levels[] = {
    { 5, "DirectX 5 interface" },
    { 6, "DDI 6 (DrawPrimitives2)" },
    { 7, "DDI 7 with hardware T&L, else DDI 6" },
    { 8, "DDI 8 where available, else the best below" }
};
#define V9X_PROGRAM_LEVEL_COUNT \
    (sizeof(v9x_program_levels) / sizeof(v9x_program_levels[0]))

static const char *v9x_programs_label(int level)
{
    UINT index;

    for (index = 0u; index < V9X_PROGRAM_LEVEL_COUNT; ++index) {
        if (v9x_program_levels[index].value == level) {
            return v9x_program_levels[index].label;
        }
    }
    return "Value set by hand (kept)";
}

/* The name part of list item `index` (the text before its tab). */
static int v9x_programs_item_name(HWND list, int index, char *name, int size)
{
    char text[MAX_PATH + 64];
    LRESULT length = SendMessageA(list, LB_GETTEXTLEN, (WPARAM)index, 0);
    int at = 0;

    if (length == LB_ERR || length >= (LRESULT)sizeof(text)) {
        return 0;
    }
    SendMessageA(list, LB_GETTEXT, (WPARAM)index, (LPARAM)text);
    while (text[at] != '\0' && text[at] != '\t' && at + 1 < size) {
        name[at] = text[at];
        ++at;
    }
    name[at] = '\0';
    return 1;
}

static int v9x_programs_find(HWND list, const char *name)
{
    char other[MAX_PATH];
    LRESULT count = SendMessageA(list, LB_GETCOUNT, 0, 0);
    int index;

    for (index = 0; index < (int)count; ++index) {
        if (v9x_programs_item_name(list, index, other, sizeof(other)) &&
            lstrcmpiA(other, name) == 0) {
            return index;
        }
    }
    return -1;
}

/* Add, or replace in place, one entry; selects it. */
static void v9x_programs_put(HWND list, const char *name, int level)
{
    char text[MAX_PATH + 64];
    int index = v9x_programs_find(list, name);
    LRESULT item;

    lstrcpynA(text, name, MAX_PATH);
    lstrcatA(text, "\t");
    lstrcatA(text, v9x_programs_label(level));
    if (index >= 0) {
        SendMessageA(list, LB_DELETESTRING, (WPARAM)index, 0);
        item = SendMessageA(list, LB_INSERTSTRING, (WPARAM)index,
                            (LPARAM)text);
    } else {
        item = SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)text);
    }
    if (item >= 0) {
        SendMessageA(list, LB_SETITEMDATA, (WPARAM)item, (LPARAM)level);
        SendMessageA(list, LB_SETCURSEL, (WPARAM)item, 0);
    }
}

/*
 * The program name as typed, reduced to what d3d_core.c compares: the file
 * name with no path, since it strips its own module path at the last \, /
 * or :. Empty, or carrying a character a profile key cannot hold, is
 * refused.
 */
static int v9x_programs_clean(const char *typed, char *name, int size)
{
    const char *start = typed;
    const char *at;
    int length = 0;

    for (at = typed; *at != '\0'; ++at) {
        if (*at == '\\' || *at == '/' || *at == ':') {
            start = at + 1;
        }
    }
    while (*start == ' ') {
        ++start;
    }
    while (start[length] != '\0' && length + 1 < size) {
        char c = start[length];

        if (c == '=' || c == '[' || c == ']' || c == ';' || c == '\t') {
            return 0;
        }
        name[length] = c;
        ++length;
    }
    while (length > 0 && name[length - 1] == ' ') {
        --length;
    }
    name[length] = '\0';
    return length > 0;
}

static void v9x_programs_init(HWND dialog)
{
    HWND list = GetDlgItem(dialog, V9X_IDC_PROG_LIST);
    HWND combo = GetDlgItem(dialog, V9X_IDC_PROG_LEVEL);
    int tab = V9X_DDI_PROGRAMS_TAB;
    const char *line;
    UINT index;
    LRESULT item;

    v9x_programs_dirty = 0;
    SendMessageA(list, LB_SETTABSTOPS, 1, (LPARAM)&tab);
    for (index = 0u; index < V9X_PROGRAM_LEVEL_COUNT; ++index) {
        item = SendMessageA(combo, CB_ADDSTRING, 0,
                            (LPARAM)v9x_program_levels[index].label);
        if (item >= 0) {
            SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                         (LPARAM)v9x_program_levels[index].value);
            if (v9x_program_levels[index].value == 6) {
                SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
            }
        }
    }
    v9x_page_widen_list(dialog, V9X_IDC_PROG_LEVEL);

    v9x_programs_loaded = (char *)HeapAlloc(GetProcessHeap(), 0,
                                            V9X_DDI_PROGRAMS_BYTES);
    if (v9x_programs_loaded == 0) {
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        return;
    }
    if (GetPrivateProfileSectionA(V9X_DDI_PROGRAMS_SECTION,
                                  v9x_programs_loaded,
                                  V9X_DDI_PROGRAMS_BYTES,
                                  V9X_SETTINGS_INI) == 0ul) {
        v9x_programs_loaded[0] = '\0';
        v9x_programs_loaded[1] = '\0';
    }
    for (line = v9x_programs_loaded; *line != '\0';
         line += lstrlenA(line) + 1) {
        char name[MAX_PATH];
        int length = 0;

        while (line[length] != '\0' && line[length] != '=' &&
               length + 1 < (int)sizeof(name)) {
            name[length] = line[length];
            ++length;
        }
        name[length] = '\0';
        if (length == 0) {
            continue;
        }
        v9x_programs_put(list, name,
                         (int)GetPrivateProfileIntA(V9X_DDI_PROGRAMS_SECTION,
                                                    name, 0,
                                                    V9X_SETTINGS_INI));
    }
    SendMessageA(list, LB_SETCURSEL, (WPARAM)-1, 0);
}

/* Write the list back; non-zero on success. */
static int v9x_programs_save(HWND dialog)
{
    HWND list = GetDlgItem(dialog, V9X_IDC_PROG_LIST);
    LRESULT count = SendMessageA(list, LB_GETCOUNT, 0, 0);
    const char *line;
    int ok = 1;
    int index;

    for (line = v9x_programs_loaded; *line != '\0';
         line += lstrlenA(line) + 1) {
        char name[MAX_PATH];
        int length = 0;

        while (line[length] != '\0' && line[length] != '=' &&
               length + 1 < (int)sizeof(name)) {
            name[length] = line[length];
            ++length;
        }
        name[length] = '\0';
        if (length != 0 && v9x_programs_find(list, name) < 0 &&
            !WritePrivateProfileStringA(V9X_DDI_PROGRAMS_SECTION, name, 0,
                                        V9X_SETTINGS_INI)) {
            ok = 0;
        }
    }
    for (index = 0; index < (int)count; ++index) {
        char name[MAX_PATH];
        char value[12];
        int level = (int)SendMessageA(list, LB_GETITEMDATA, (WPARAM)index,
                                      0);

        if (!v9x_programs_item_name(list, index, name, sizeof(name))) {
            continue;
        }
        wsprintfA(value, "%d", level);
        if (!WritePrivateProfileStringA(V9X_DDI_PROGRAMS_SECTION, name, value,
                                        V9X_SETTINGS_INI)) {
            ok = 0;
        }
    }
    return ok;
}

static BOOL CALLBACK v9x_programs_dialog_proc(HWND dialog,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam)
{
    HWND list;
    char typed[MAX_PATH];
    char name[MAX_PATH];
    LRESULT selection;

    (void)lparam;
    switch (message) {
    case WM_INITDIALOG:
        v9x_programs_init(dialog);
        return TRUE;
    case WM_DESTROY:
        if (v9x_programs_loaded != 0) {
            HeapFree(GetProcessHeap(), 0, v9x_programs_loaded);
            v9x_programs_loaded = 0;
        }
        return FALSE;
    case WM_COMMAND:
        list = GetDlgItem(dialog, V9X_IDC_PROG_LIST);
        switch (LOWORD(wparam)) {
        case V9X_IDC_PROG_LIST:
            /* Selecting an entry loads it into the editor below. */
            if (HIWORD(wparam) == LBN_SELCHANGE) {
                selection = SendMessageA(list, LB_GETCURSEL, 0, 0);
                if (selection != LB_ERR &&
                    v9x_programs_item_name(list, (int)selection, name,
                                           sizeof(name))) {
                    HWND combo = GetDlgItem(dialog, V9X_IDC_PROG_LEVEL);
                    int level = (int)SendMessageA(list, LB_GETITEMDATA,
                                                  (WPARAM)selection, 0);
                    LRESULT count = SendMessageA(combo, CB_GETCOUNT, 0, 0);
                    int index;

                    SetDlgItemTextA(dialog, V9X_IDC_PROG_NAME, name);
                    for (index = 0; index < (int)count; ++index) {
                        if ((int)SendMessageA(combo, CB_GETITEMDATA,
                                              (WPARAM)index, 0) == level) {
                            SendMessageA(combo, CB_SETCURSEL, (WPARAM)index,
                                         0);
                        }
                    }
                }
            }
            return TRUE;
        case V9X_IDC_PROG_ADD:
            GetDlgItemTextA(dialog, V9X_IDC_PROG_NAME, typed, sizeof(typed));
            if (!v9x_programs_clean(typed, name, sizeof(name))) {
                MessageBoxA(dialog,
                            "Type the program's file name, such as "
                            "QUAKE2.EXE. It cannot contain = [ ] or ;.",
                            v9x_page_caption, MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            v9x_programs_put(list, name,
                             v9x_page_combo_value(dialog, V9X_IDC_PROG_LEVEL,
                                                  6));
            v9x_programs_dirty = 1;
            return TRUE;
        case V9X_IDC_PROG_REMOVE:
            selection = SendMessageA(list, LB_GETCURSEL, 0, 0);
            if (selection != LB_ERR) {
                SendMessageA(list, LB_DELETESTRING, (WPARAM)selection, 0);
                v9x_programs_dirty = 1;
            }
            return TRUE;
        case IDOK:
            if (v9x_programs_dirty && !v9x_programs_save(dialog)) {
                MessageBoxA(dialog,
                            "Could not write every entry to SYSTEM.INI.\n\n"
                            "The file may be read-only or in use.",
                            v9x_page_caption, MB_OK | MB_ICONWARNING);
            } else if (v9x_programs_dirty) {
                MessageBoxA(dialog,
                            "Saved. A listed program gets its interface the "
                            "next time it starts.",
                            v9x_page_caption, MB_OK | MB_ICONINFORMATION);
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static BOOL v9x_guid_equal(const V9X_GUID *left, const V9X_GUID *right)
{
    const BYTE *a = (const BYTE *)left;
    const BYTE *b = (const BYTE *)right;
    WORD index;

    for (index = 0u; index < sizeof(V9X_GUID); ++index) {
        if (a[index] != b[index]) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * Let a combo's dropped list be wider than the closed control, so a label cut
 * short in the closed box reads in full when the list is open.
 */
static void v9x_page_widen_list(HWND dialog, int control)
{
    HWND combo = GetDlgItem(dialog, control);
    RECT units;

    if (combo == 0) {
        return;
    }
    units.left = 0;
    units.top = 0;
    units.right = 200;
    units.bottom = 8;
    MapDialogRect(dialog, &units);
    SendMessageA(combo, CB_SETDROPPEDWIDTH, (WPARAM)units.right, 0);
}

static void v9x_page_about(HWND dialog)
{
    MessageBoxA(dialog,
                "Velocity9x " V9X_VERSION_STRING "\n"
                "Build " V9X_BUILD_ID "\n\n"
                "A display driver for Windows 95 and 98: a 16-bit display "
                "driver, a DirectDraw and Direct3D HAL, and a mini-VDD.\n\n"
                "Settings on the Velocity9x Advanced tab are stored in "
                "SYSTEM.INI under [Velocity9x]. Copy report puts the full "
                "diagnostic report on the clipboard.",
                "About Velocity9x", MB_OK | MB_ICONINFORMATION);
}

/*
 * Start V9XUPD.EXE from the system directory, where the INF installs it.
 * The network work runs in that process, never in Display Properties
 * (docs\plans\optional-update-checker-and-auto-updater.md), so a slow or
 * failed connection cannot hang this dialog.
 */
static void v9x_page_launch_update(HWND dialog, const char *switch_text)
{
    char command[MAX_PATH + 16];
    UINT length = GetSystemDirectoryA(command + 1, MAX_PATH);

    if (length == 0u || length + 32u > sizeof(command)) {
        return;
    }
    command[0] = '"';
    lstrcatA(command, "\\V9XUPD.EXE");
    if (GetFileAttributesA(command + 1) == 0xFFFFFFFFul) {
        MessageBoxA(dialog,
                    "V9XUPD.EXE is not installed in the Windows system "
                    "folder. Reinstall Velocity9x from a release that "
                    "includes it.",
                    v9x_page_caption, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    lstrcatA(command, "\" ");
    lstrcatA(command, switch_text);
    if (WinExec(command, SW_SHOWNORMAL) < 32u) {
        MessageBoxA(dialog, "V9XUPD.EXE could not be started.",
                    v9x_page_caption, MB_OK | MB_ICONEXCLAMATION);
    }
}

/*
 * The Velocity9x page: what the card and driver are. Read-only, apart from
 * the three buttons, none of which writes anything.
 */
static BOOL CALLBACK v9x_page_dialog_proc(HWND dialog,
                                          UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        (void)lparam;
        v9x_settings_collect(&v9x_page_status, V9X_VERSION_STRING,
                             V9X_BUILD_ID);
        SetDlgItemTextA(dialog, V9X_IDC_ADAPTER,
                        v9x_page_status.adapter_name);
        SetDlgItemTextA(dialog, V9X_IDC_PCI_ID, v9x_page_status.pci_id);
        SetDlgItemTextA(dialog, V9X_IDC_REVISION, v9x_page_status.revision);
        SetDlgItemTextA(dialog, V9X_IDC_VIDEO_MEMORY,
                        v9x_page_status.video_memory);
        SetDlgItemTextA(dialog, V9X_IDC_MINIVDD,
                        v9x_page_status.minivdd_build);
        SetDlgItemTextA(dialog, V9X_IDC_RESOLUTION,
                        v9x_page_status.resolution);
        SetDlgItemTextA(dialog, V9X_IDC_REFRESH,
                        v9x_page_status.refresh_rate);
        SetDlgItemTextA(dialog, V9X_IDC_COLOUR_DEPTH,
                        v9x_page_status.colour_depth);
        SetDlgItemTextA(dialog, V9X_IDC_MODE_SWITCH,
                        v9x_page_status.mode_switching);
        SetDlgItemTextA(dialog, V9X_IDC_RENDERING,
                        v9x_page_status.gdi_rendering);
        SetDlgItemTextA(dialog, V9X_IDC_DIRECTDRAW,
                        v9x_page_status.directdraw);
        SetDlgItemTextA(dialog, V9X_IDC_DIRECT3D, v9x_page_status.direct3d);
        SetDlgItemTextA(dialog, V9X_IDC_FRAMEBUFFER,
                        v9x_page_status.driver_short);
        SetDlgItemTextA(dialog, V9X_IDC_DDI_STATE, v9x_page_status.ddi);
        SetDlgItemTextA(dialog, V9X_IDC_VERSION,
                        "Version: " V9X_VERSION_STRING);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == V9X_IDC_COPY_REPORT) {
            (void)v9x_settings_copy_report(dialog, v9x_page_caption,
                                           v9x_page_status.report);
            return TRUE;
        }
        if (LOWORD(wparam) == V9X_IDC_ABOUT) {
            v9x_page_about(dialog);
            return TRUE;
        }
        if (LOWORD(wparam) == V9X_IDC_CHECK_UPDATES) {
            v9x_page_launch_update(dialog, "/CHECK");
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Whether any control differs from what the page loaded, for Apply. */
static int v9x_page_any_changed(HWND dialog)
{
    return v9x_page_selected_d3d(dialog) != v9x_page_d3d_loaded ||
           v9x_page_selected_layout(dialog) != v9x_page_layout_loaded ||
           v9x_page_selected_vsync(dialog) != v9x_page_vsync_loaded ||
           v9x_page_selected_ddi(dialog) != v9x_page_ddi_loaded ||
           v9x_page_combo_value(dialog, V9X_IDC_GDI_ACCEL,
                                v9x_page_gdi_loaded) != v9x_page_gdi_loaded ||
           (IsDlgButtonChecked(dialog, V9X_IDC_SOFT_SYSMEM) ==
                BST_CHECKED) != v9x_page_soft_loaded ||
           (IsDlgButtonChecked(dialog, V9X_IDC_WRITE_COMBINE_SWITCH) ==
                BST_CHECKED) != v9x_page_wc_loaded;
}

/*
 * The Velocity9x Advanced page: every control that writes SYSTEM.INI, and
 * the write-combining switch. It collects its own status, since the sheet
 * may open on either page.
 */
static BOOL CALLBACK v9x_advanced_dialog_proc(HWND dialog,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG:
        (void)lparam;
        v9x_settings_collect(&v9x_page_status, V9X_VERSION_STRING,
                             V9X_BUILD_ID);
        v9x_page_fill_d3d(dialog);
        v9x_page_fill_layout(dialog);
        v9x_page_fill_vsync(dialog);
        v9x_page_fill_ddi(dialog);
        v9x_page_fill_gdi(dialog);
        v9x_page_soft_loaded = v9x_page_status.soft_sysmem_request != 0;
        CheckDlgButton(dialog, V9X_IDC_SOFT_SYSMEM,
                       v9x_page_soft_loaded ? BST_CHECKED : BST_UNCHECKED);
        v9x_page_wc_loaded = !v9x_page_status.wc_marker;
        CheckDlgButton(dialog, V9X_IDC_WRITE_COMBINE_SWITCH,
                       v9x_page_wc_loaded ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(dialog, V9X_IDC_WRITE_COMBINE_SWITCH),
                     v9x_page_status.wc_applicable ||
                         v9x_page_status.wc_marker);
        v9x_page_update_ddi_enable(dialog);
        v9x_page_widen_list(dialog, V9X_IDC_DIRECT3D_MODE);
        v9x_page_widen_list(dialog, V9X_IDC_COLOUR_LAYOUT);
        v9x_page_widen_list(dialog, V9X_IDC_DDI);
        v9x_page_widen_list(dialog, V9X_IDC_GDI_ACCEL);
        SetDlgItemTextA(dialog, V9X_IDC_WRITE_COMBINE,
                        v9x_page_status.write_combining);
        SetDlgItemTextA(dialog, V9X_IDC_CORE_CLOCK,
                        v9x_page_status.core_clock);
        SetDlgItemTextA(dialog, V9X_IDC_MEMORY_CLOCK,
                        v9x_page_status.memory_clock);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == V9X_IDC_SEND_REPORT) {
            v9x_page_launch_update(dialog, "/REPORT");
            return TRUE;
        }
        if (LOWORD(wparam) == V9X_IDC_DDI_PROGRAMS) {
            (void)DialogBoxParamA(v9x_page_instance,
                                  MAKEINTRESOURCEA(V9X_ID_DDI_PROGRAMS),
                                  dialog, (DLGPROC)v9x_programs_dialog_proc,
                                  0);
            return TRUE;
        }
        /* Enable Apply only once the selection actually differs from the
         * file, so OK on an untouched page writes nothing. */
        if (((LOWORD(wparam) == V9X_IDC_DIRECT3D_MODE ||
              LOWORD(wparam) == V9X_IDC_COLOUR_LAYOUT ||
              LOWORD(wparam) == V9X_IDC_VSYNC ||
              LOWORD(wparam) == V9X_IDC_DDI ||
              LOWORD(wparam) == V9X_IDC_GDI_ACCEL) &&
             HIWORD(wparam) == CBN_SELCHANGE) ||
            ((LOWORD(wparam) == V9X_IDC_SOFT_SYSMEM ||
              LOWORD(wparam) == V9X_IDC_WRITE_COMBINE_SWITCH) &&
             HIWORD(wparam) == BN_CLICKED)) {
            if (LOWORD(wparam) == V9X_IDC_DIRECT3D_MODE) {
                v9x_page_update_ddi_enable(dialog);
            }
            if (v9x_page_any_changed(dialog)) {
                SendMessageA(GetParent(dialog), PSM_CHANGED,
                             (WPARAM)dialog, 0);
            } else {
                SendMessageA(GetParent(dialog), PSM_UNCHANGED,
                             (WPARAM)dialog, 0);
            }
            return TRUE;
        }
        break;
    case WM_NOTIFY:
        /* Apply succeeds either way: a failed write reports itself in its
         * own box rather than keeping the user in a dialog they cannot
         * leave. */
        if (((NMHDR FAR *)lparam)->code == (UINT)PSN_APPLY) {
            v9x_page_apply(dialog);
            SetWindowLongA(dialog, DWL_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/*
 * IShellPropSheetExt and IShellExtInit singleton.
 *
 * Both interfaces share one static object; the reference count only gates
 * DllCanUnloadNow. QueryInterface hands out the vtable that matches the
 * requested interface.
 */
typedef struct v9x_ext_object {
    const struct v9x_propsheet_vtbl *propsheet_vtbl;
    const struct v9x_extinit_vtbl *extinit_vtbl;
} V9X_EXT_OBJECT;

struct v9x_propsheet_vtbl {
    LONG (WINAPI *QueryInterface)(void *self, const V9X_GUID *iid,
                                  void **object);
    DWORD (WINAPI *AddRef)(void *self);
    DWORD (WINAPI *Release)(void *self);
    LONG (WINAPI *AddPages)(void *self, V9X_ADD_PAGE_PROC add_page,
                            LPARAM lparam);
    LONG (WINAPI *ReplacePage)(void *self, UINT page_id,
                               V9X_ADD_PAGE_PROC replace_page,
                               LPARAM lparam);
};

struct v9x_extinit_vtbl {
    LONG (WINAPI *QueryInterface)(void *self, const V9X_GUID *iid,
                                  void **object);
    DWORD (WINAPI *AddRef)(void *self);
    DWORD (WINAPI *Release)(void *self);
    LONG (WINAPI *Initialize)(void *self, void *folder_pidl,
                              void *data_object, HKEY prog_id_key);
};

static V9X_EXT_OBJECT v9x_ext_object;

static LONG WINAPI v9x_ext_query_interface(void *self,
                                           const V9X_GUID *iid,
                                           void **object)
{
    (void)self;
    if (object == 0) {
        return V9X_E_POINTER;
    }
    if (v9x_guid_equal(iid, &v9x_iid_unknown) ||
        v9x_guid_equal(iid, &v9x_iid_shell_propsheet_ext)) {
        *object = (void *)&v9x_ext_object.propsheet_vtbl;
        InterlockedIncrement(&v9x_object_count);
        return V9X_S_OK;
    }
    if (v9x_guid_equal(iid, &v9x_iid_shell_ext_init)) {
        *object = (void *)&v9x_ext_object.extinit_vtbl;
        InterlockedIncrement(&v9x_object_count);
        return V9X_S_OK;
    }
    *object = 0;
    return V9X_E_NOINTERFACE;
}

static DWORD WINAPI v9x_ext_add_ref(void *self)
{
    (void)self;
    return (DWORD)InterlockedIncrement(&v9x_object_count);
}

static DWORD WINAPI v9x_ext_release(void *self)
{
    LONG count = InterlockedDecrement(&v9x_object_count);
    (void)self;
    if (count < 0l) {
        count = 0l;
        v9x_object_count = 0l;
    }
    return (DWORD)count;
}

static LONG WINAPI v9x_ext_add_pages(void *self,
                                     V9X_ADD_PAGE_PROC add_page,
                                     LPARAM lparam)
{
    PROPSHEETPAGEA page;
    HPROPSHEETPAGE handle;
    BYTE *bytes = (BYTE *)&page;
    WORD index;

    /* Two pages at the stock tab size rather than one larger page, which
     * would resize the whole native dialog: the information page, then the
     * settings page. */
    static const struct {
        WORD resource;
        DLGPROC procedure;
    } pages[2] = {
        { V9X_ID_PAGE_DIALOG, (DLGPROC)v9x_page_dialog_proc },
        { V9X_ID_PAGE_ADVANCED, (DLGPROC)v9x_advanced_dialog_proc }
    };
    WORD which;

    (void)self;
    if (add_page == 0) {
        return V9X_E_POINTER;
    }
    for (which = 0u; which < 2u; ++which) {
        for (index = 0u; index < sizeof(page); ++index) {
            bytes[index] = 0u;
        }
        page.dwSize = sizeof(page);
        page.dwFlags = PSP_DEFAULT;
        page.hInstance = v9x_page_instance;
        page.pszTemplate = MAKEINTRESOURCEA(pages[which].resource);
        page.pfnDlgProc = pages[which].procedure;
        handle = CreatePropertySheetPageA(&page);
        if (handle == 0) {
            return V9X_E_OUTOFMEMORY;
        }
        if (!add_page(handle, lparam)) {
            DestroyPropertySheetPage(handle);
            return V9X_E_FAIL;
        }
    }
    return V9X_S_OK;
}

static LONG WINAPI v9x_ext_replace_page(void *self,
                                        UINT page_id,
                                        V9X_ADD_PAGE_PROC replace_page,
                                        LPARAM lparam)
{
    (void)self;
    (void)page_id;
    (void)replace_page;
    (void)lparam;
    return V9X_E_NOTIMPL;
}

static LONG WINAPI v9x_ext_initialize(void *self,
                                      void *folder_pidl,
                                      void *data_object,
                                      HKEY prog_id_key)
{
    (void)self;
    (void)folder_pidl;
    (void)data_object;
    (void)prog_id_key;
    return V9X_S_OK;
}

static const struct v9x_propsheet_vtbl v9x_propsheet_vtbl_instance = {
    v9x_ext_query_interface,
    v9x_ext_add_ref,
    v9x_ext_release,
    v9x_ext_add_pages,
    v9x_ext_replace_page
};

static const struct v9x_extinit_vtbl v9x_extinit_vtbl_instance = {
    v9x_ext_query_interface,
    v9x_ext_add_ref,
    v9x_ext_release,
    v9x_ext_initialize
};

/*
 * IClassFactory singleton.
 */
struct v9x_factory_vtbl {
    LONG (WINAPI *QueryInterface)(void *self, const V9X_GUID *iid,
                                  void **object);
    DWORD (WINAPI *AddRef)(void *self);
    DWORD (WINAPI *Release)(void *self);
    LONG (WINAPI *CreateInstance)(void *self, void *outer,
                                  const V9X_GUID *iid, void **object);
    LONG (WINAPI *LockServer)(void *self, BOOL lock);
};

static const struct v9x_factory_vtbl *v9x_factory_object;

static LONG WINAPI v9x_factory_query_interface(void *self,
                                               const V9X_GUID *iid,
                                               void **object)
{
    if (object == 0) {
        return V9X_E_POINTER;
    }
    if (v9x_guid_equal(iid, &v9x_iid_unknown) ||
        v9x_guid_equal(iid, &v9x_iid_class_factory)) {
        *object = self;
        InterlockedIncrement(&v9x_object_count);
        return V9X_S_OK;
    }
    *object = 0;
    return V9X_E_NOINTERFACE;
}

static LONG WINAPI v9x_factory_create_instance(void *self,
                                               void *outer,
                                               const V9X_GUID *iid,
                                               void **object)
{
    (void)self;
    if (object == 0) {
        return V9X_E_POINTER;
    }
    *object = 0;
    if (outer != 0) {
        return V9X_CLASS_E_NOAGGREGATION;
    }
    return v9x_ext_query_interface(0, iid, object);
}

static LONG WINAPI v9x_factory_lock_server(void *self, BOOL lock)
{
    (void)self;
    if (lock) {
        InterlockedIncrement(&v9x_object_count);
    } else {
        InterlockedDecrement(&v9x_object_count);
    }
    return V9X_S_OK;
}

static const struct v9x_factory_vtbl v9x_factory_vtbl_instance = {
    v9x_factory_query_interface,
    v9x_ext_add_ref,
    v9x_ext_release,
    v9x_factory_create_instance,
    v9x_factory_lock_server
};

/*
 * Registration.
 *
 * An INF cannot register this page on its own. Windows 98 validates every
 * Display property-sheet handler against a "Tag" DWORD that is specific to
 * the machine: a handler whose Tag does not check out is ignored, and the
 * shell removes the key. The value is
 *
 *     Tag = seed + w0 + w1
 *
 * where the seed is a per-machine constant and w0/w1 are the first eight
 * characters of the handler's own CLSID text read as two little-endian
 * DWORDs. Only the sum of w0 and w1 is ever needed, so that is what
 * v9x_clsid_words returns.
 *
 * The seed is not published anywhere, but it is recoverable by inverting the
 * same expression over any handler Windows has already accepted - Windows
 * ships two on a stock install, and they agree. Reading it back from a
 * working neighbour is the whole trick.
 *
 * The INF runs this through RunOnce at the first boot after the install:
 *
 *     rundll32.exe v9xsetp.dll,V9xRegisterPage
 */
#define V9X_HANDLERS_KEY \
    "Software\\Microsoft\\Windows\\CurrentVersion\\Controls Folder\\Display" \
    "\\shellex\\PropertySheetHandlers"
#define V9X_APPROVED_KEY \
    "Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved"
#define V9X_PAGE_NAME  "Velocity9x"
#define V9X_PAGE_TITLE "Velocity9x Settings Page"
#define V9X_PAGE_CLSID_TEXT "{91925DA2-2EF0-4E20-B4E9-A53ED37E14B1}"

static DWORD v9x_clsid_words(const char *clsid)
{
    DWORD low = 0ul;
    DWORD high = 0ul;
    int index;

    for (index = 0; index < 4; ++index) {
        low |= ((DWORD)(BYTE)clsid[index]) << (index * 8);
        high |= ((DWORD)(BYTE)clsid[index + 4]) << (index * 8);
    }
    return low + high;
}

static BOOL v9x_page_seed(DWORD *seed)
{
    HKEY handlers;
    DWORD index = 0ul;
    BOOL found = FALSE;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, V9X_HANDLERS_KEY, 0ul, KEY_READ,
                      &handlers) != ERROR_SUCCESS) {
        return FALSE;
    }
    while (!found) {
        char name[128];
        char clsid[64];
        HKEY handler;
        DWORD length = sizeof(name);
        DWORD type = 0ul;
        DWORD size;
        DWORD tag = 0ul;

        if (RegEnumKeyExA(handlers, index, name, &length, 0, 0, 0, 0) !=
                ERROR_SUCCESS) {
            break;
        }
        ++index;
        /* Never seed from our own entry: a stale Tag would reproduce itself. */
        if (lstrcmpiA(name, V9X_PAGE_NAME) == 0) {
            continue;
        }
        if (RegOpenKeyExA(handlers, name, 0ul, KEY_READ, &handler) !=
                ERROR_SUCCESS) {
            continue;
        }
        size = sizeof(clsid);
        if (RegQueryValueExA(handler, 0, 0, &type, (BYTE *)clsid, &size) ==
                ERROR_SUCCESS && type == REG_SZ && size > 8ul) {
            clsid[sizeof(clsid) - 1] = '\0';
            size = sizeof(tag);
            type = 0ul;
            if (RegQueryValueExA(handler, "Tag", 0, &type, (BYTE *)&tag,
                                 &size) == ERROR_SUCCESS &&
                    type == REG_DWORD && size == sizeof(tag)) {
                *seed = tag - v9x_clsid_words(clsid);
                found = TRUE;
            }
        }
        RegCloseKey(handler);
    }
    RegCloseKey(handlers);
    return found;
}

void CALLBACK V9xRegisterPage(HWND owner, HINSTANCE instance, LPSTR command,
                              int show)
{
    DWORD seed = 0ul;
    DWORD tag;
    DWORD disposition;
    HKEY key;

    (void)owner;
    (void)instance;
    (void)command;
    (void)show;

    /* No accepted neighbour means no recoverable seed. Writing a handler
     * without a valid Tag would leave a key the shell deletes again, so
     * leave the registry alone instead. */
    if (!v9x_page_seed(&seed)) {
        return;
    }
    tag = seed + v9x_clsid_words(V9X_PAGE_CLSID_TEXT);

    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE,
                        V9X_HANDLERS_KEY "\\" V9X_PAGE_NAME, 0ul, 0,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key,
                        &disposition) == ERROR_SUCCESS) {
        RegSetValueExA(key, 0, 0ul, REG_SZ,
                       (const BYTE *)V9X_PAGE_CLSID_TEXT,
                       sizeof(V9X_PAGE_CLSID_TEXT));
        RegSetValueExA(key, "Tag", 0ul, REG_DWORD, (const BYTE *)&tag,
                       sizeof(tag));
        RegCloseKey(key);
    }
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, V9X_APPROVED_KEY, 0ul, 0,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key,
                        &disposition) == ERROR_SUCCESS) {
        RegSetValueExA(key, V9X_PAGE_CLSID_TEXT, 0ul, REG_SZ,
                       (const BYTE *)V9X_PAGE_TITLE, sizeof(V9X_PAGE_TITLE));
        RegCloseKey(key);
    }
}

/*
 * DLL exports.
 */
LONG WINAPI DllGetClassObject(const V9X_GUID *clsid,
                              const V9X_GUID *iid,
                              void **object)
{
    if (object == 0) {
        return V9X_E_POINTER;
    }
    *object = 0;
    if (!v9x_guid_equal(clsid, &v9x_clsid_settings_page)) {
        return V9X_CLASS_E_CLASSNOTAVAILABLE;
    }
    return v9x_factory_query_interface((void *)&v9x_factory_object,
                                       iid, object);
}

LONG WINAPI DllCanUnloadNow(void)
{
    return v9x_object_count == 0l ? V9X_S_OK : V9X_S_FALSE;
}

BOOL WINAPI V9xPageEntry(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        v9x_page_instance = instance;
        v9x_ext_object.propsheet_vtbl = &v9x_propsheet_vtbl_instance;
        v9x_ext_object.extinit_vtbl = &v9x_extinit_vtbl_instance;
        v9x_factory_object = &v9x_factory_vtbl_instance;
    }
    return TRUE;
}
