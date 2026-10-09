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
 * One control writes: the Direct3D selector, which puts [Velocity9x] Direct3D
 * into SYSTEM.INI for the 16-bit driver to read at its next Enable. Every
 * other row is a statement of fact.
 *
 * The module is built without a C runtime, so COM is implemented with
 * explicit vtables and static singleton objects.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <prsht.h>

#include "velocity9x/build.h"
#include "velocity9x/d3dmode.h"
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
 * in SYSTEM.INI and the driver decides - 5:5:5 when Direct3D resolved to the
 * chip's own S3D engine, which writes ZRGB1555 into every 16-bit target and
 * has no other layout to offer, 5:6:5 otherwise. 15 and 16 are S3's own values
 * for the same key, so a SYSTEM.INI carried over from their driver reads the
 * same way here. Choosing Automatic deletes the key rather than writing 0, so
 * the file says what the page says.
 */
static int v9x_page_layout_loaded;

static const struct v9x_layout_choice {
    int value;
    const char *label;
} v9x_page_layout_choices[] = {
    { 0,  "Automatic (5:5:5 under hardware Direct3D)" },
    { 16, "5:6:5 - 64 greens, the usual layout" },
    { 15, "5:5:5 - what the S3D engine draws" }
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
 * it changes nothing, which is not worth hiding the control for. The labels
 * are short because the closed control is 68 units wide.
 */
static int v9x_page_vsync_loaded;

static const struct v9x_vsync_choice {
    int value;
    const char *label;
} v9x_page_vsync_choices[] = {
    { (int)V9X_VSYNC_REQUEST_APPLICATION, "Game decides" },
    { (int)V9X_VSYNC_REQUEST_ON,          "Always on" },
    { (int)V9X_VSYNC_REQUEST_OFF,         "Always off" }
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
 * The DDI 6 selector, on the same terms as the VSync one.
 *
 * Automatic is the key's absence: DrawPrimitives2 for programs that load
 * Direct3D 8, and for any [Velocity9x.Direct3DDdi] lists with 6; the DX5
 * interface for the rest (engine_abi.h, docs\plans\ddi6-drawprimitives2.md
 * Part B). Never and Always write 5 and 6. Like VSync it needs no restart.
 */
static int v9x_page_ddi_loaded;

static const struct v9x_ddi_choice {
    int value;
    const char *label;
} v9x_page_ddi_choices[] = {
    { 0, "Automatic" },
    { 5, "Never" },
    { 6, "Always" }
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
                    "Could not write the DDI 6 setting to SYSTEM.INI.\n\n"
                    "The file may be read-only or in use.",
                    v9x_page_caption, MB_OK | MB_ICONWARNING);
        return 0;
    }
    v9x_page_ddi_loaded = selected;
    return 1;
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

/*
 * Both selectors, one message.
 *
 * The Direct3D wording is measured, not a hedge:
 * docs\decisions\2026-08-30-d3d-mode-disabled-gate.md - DDRAW keeps the
 * device it enumerated until a fresh boot. The colour layout is decided at
 * Enable, and the desktop, DirectDraw and the DIB engine all read it there,
 * so it too is a restart and not a re-enable.
 */
static void v9x_page_apply(HWND dialog)
{
    int wrote_d3d = v9x_page_apply_d3d(dialog);
    int wrote_layout = v9x_page_apply_layout(dialog);
    int wrote_vsync = v9x_page_apply_vsync(dialog);
    int wrote_ddi = v9x_page_apply_ddi(dialog);

    if (!wrote_d3d && !wrote_layout && !wrote_vsync && !wrote_ddi) {
        return;
    }
    /* VSync and the DDI alone need no restart: the 16-bit driver reads both
     * each time DirectDraw creates its driver object. */
    if (!wrote_d3d && !wrote_layout) {
        MessageBoxA(dialog,
                    "The setting has been saved to SYSTEM.INI.\n\n"
                    "It takes effect the next time a DirectDraw or "
                    "Direct3D program starts.",
                    v9x_page_caption, MB_OK | MB_ICONINFORMATION);
        return;
    }
    MessageBoxA(dialog,
                "The setting has been saved to SYSTEM.INI.\n\n"
                "It takes effect after you restart Windows. Until then "
                "DirectDraw applications continue to see the previous "
                "setting.",
                v9x_page_caption, MB_OK | MB_ICONINFORMATION);
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
                "Settings on this page are stored in SYSTEM.INI under "
                "[Velocity9x]. Copy report puts the full diagnostic report "
                "on the clipboard.",
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

/* Disabled controls for features this driver does not have yet, each with
 * the one entry that says so. */
static void v9x_page_fill_unavailable(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, V9X_IDC_TEXTURE_FILTER);

    if (combo != 0) {
        SendMessageA(combo, CB_RESETCONTENT, 0, 0);
        SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)"Not available");
        SendMessageA(combo, CB_SETCURSEL, 0, 0);
    }
    CheckDlgButton(dialog, V9X_IDC_WRITE_COMBINE, BST_UNCHECKED);
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
        SetDlgItemTextA(dialog, V9X_IDC_GDI_TEST, v9x_page_status.gdi_short);
        SetDlgItemTextA(dialog, V9X_IDC_DDRAW_TEST,
                        v9x_page_status.ddraw_test);
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

/*
 * The Velocity9x Advanced page: every control that writes SYSTEM.INI, and
 * the disabled controls for features not built yet. It collects its own
 * status, since the sheet may open on either page.
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
        v9x_page_fill_unavailable(dialog);
        v9x_page_widen_list(dialog, V9X_IDC_DIRECT3D_MODE);
        v9x_page_widen_list(dialog, V9X_IDC_COLOUR_LAYOUT);
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
        /* Enable Apply only once the selection actually differs from the
         * file, so OK on an untouched page writes nothing. */
        if ((LOWORD(wparam) == V9X_IDC_DIRECT3D_MODE ||
             LOWORD(wparam) == V9X_IDC_COLOUR_LAYOUT ||
             LOWORD(wparam) == V9X_IDC_VSYNC ||
             LOWORD(wparam) == V9X_IDC_DDI) &&
            HIWORD(wparam) == CBN_SELCHANGE) {
            if (v9x_page_selected_d3d(dialog) != v9x_page_d3d_loaded ||
                v9x_page_selected_layout(dialog) !=
                    v9x_page_layout_loaded ||
                v9x_page_selected_vsync(dialog) != v9x_page_vsync_loaded ||
                v9x_page_selected_ddi(dialog) != v9x_page_ddi_loaded) {
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
