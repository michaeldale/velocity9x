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
 * `needs_engine` is what decides which entries a given card sees. Only
 * Hardware needs one; Software runs on the framebuffer and Disabled describes
 * an absence. It is not a "hide it" flag - Hardware is still offered on a card
 * without an engine, because it is the default and the file's absent-key
 * value, and a page that could not select it could not undo a change. What it
 * changes is the label, so the entry does not promise silicon that is not
 * there.
 */
static const struct v9x_d3d_choice {
    int value;
    int needs_engine;
    const char *label;
    const char *label_no_engine;
} v9x_page_d3d_choices[] = {
    { (int)V9X_D3D_REQUEST_HARDWARE, 1, "Hardware (the chip's own engine)",
      "Hardware (this card has no 3D engine)" },
    { (int)V9X_D3D_REQUEST_SOFTWARE, 0, "Software (CPU rasterizer - slow)",
      "Software (CPU rasterizer - slow)" },
    { (int)V9X_D3D_REQUEST_DISABLED, 0, "Disabled - advertise no Direct3D",
      "Disabled - advertise no Direct3D" }
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
 * So the control is always live and the *labels* carry what the card can do.
 * The one case left worth the code is the last: when SYSTEM.INI holds a value
 * that is not among the implemented modes - one from a later build, or a
 * typo - it gets its own entry and is selected, so the page reports what is
 * actually in the file instead of quietly presenting the default and writing
 * it back on OK.
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
        const char *label =
            (v9x_page_d3d_choices[index].needs_engine != 0 &&
             !v9x_page_status.direct3d_capable)
                ? v9x_page_d3d_choices[index].label_no_engine
                : v9x_page_d3d_choices[index].label;

        item = SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)label);
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
        item = SendMessageA(combo, CB_ADDSTRING, 0,
                            (LPARAM)"Mode set in SYSTEM.INI, not in this build");
        if (item >= 0) {
            SendMessageA(combo, CB_SETITEMDATA, (WPARAM)item,
                         (LPARAM)v9x_page_d3d_loaded);
            SendMessageA(combo, CB_SETCURSEL, (WPARAM)item, 0);
        }
    }
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
static void v9x_page_apply_d3d(HWND dialog)
{
    int selected = v9x_page_selected_d3d(dialog);
    char value[2];

    if (selected == v9x_page_d3d_loaded) {
        return;
    }
    /* One digit covers every value this page can select, and the page never
     * writes a value it did not put in the list. */
    if (selected < 0 || selected > 9) {
        return;
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
        return;
    }
    v9x_page_d3d_loaded = selected;
    MessageBoxA(dialog,
                "The Direct3D setting has been saved.\n\n"
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
 * Property page dialog procedure.
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
        SetDlgItemTextA(dialog, V9X_IDC_VIDEO_MEMORY,
                        v9x_page_status.video_memory);
        SetDlgItemTextA(dialog, V9X_IDC_ACTIVE_MODE,
                        v9x_page_status.active_mode);
        SetDlgItemTextA(dialog, V9X_IDC_CORE_CLOCK,
                        v9x_page_status.core_clock);
        SetDlgItemTextA(dialog, V9X_IDC_RENDERING,
                        v9x_page_status.rendering);
        SetDlgItemTextA(dialog, V9X_IDC_DIRECTDRAW,
                        v9x_page_status.directdraw);
        v9x_page_fill_d3d(dialog);
        SetDlgItemTextA(dialog, V9X_IDC_MODE_SWITCH,
                        v9x_page_status.mode_switching);
        SetDlgItemTextA(dialog, V9X_IDC_VERSION,
                        "Version: " V9X_VERSION_STRING);
        SetDlgItemTextA(dialog, V9X_IDC_BUILD, "Build: " V9X_BUILD_ID);
        SetDlgItemTextA(dialog, V9X_IDC_FRAMEBUFFER,
                        v9x_page_status.framebuffer_status);
        SetDlgItemTextA(dialog, V9X_IDC_GDI_TEST,
                        v9x_page_status.gdi_status);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == V9X_IDC_COPY_REPORT) {
            (void)v9x_settings_copy_report(dialog, v9x_page_caption,
                                           v9x_page_status.report);
            return TRUE;
        }
        /* Enable Apply only once the selection actually differs from the
         * file, so OK on an untouched page writes nothing. */
        if (LOWORD(wparam) == V9X_IDC_DIRECT3D_MODE &&
            HIWORD(wparam) == CBN_SELCHANGE) {
            if (v9x_page_selected_d3d(dialog) != v9x_page_d3d_loaded) {
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
        /* Every row but the Direct3D selector is a statement of fact, so
         * Apply has exactly one thing to do. It succeeds either way: a failed
         * write reports itself in its own box rather than keeping the user in
         * a dialog they cannot leave. */
        if (((NMHDR FAR *)lparam)->code == (UINT)PSN_APPLY) {
            v9x_page_apply_d3d(dialog);
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

    (void)self;
    if (add_page == 0) {
        return V9X_E_POINTER;
    }
    for (index = 0u; index < sizeof(page); ++index) {
        bytes[index] = 0u;
    }
    page.dwSize = sizeof(page);
    page.dwFlags = PSP_DEFAULT;
    page.hInstance = v9x_page_instance;
    page.pszTemplate = MAKEINTRESOURCEA(V9X_ID_PAGE_DIALOG);
    page.pfnDlgProc = (DLGPROC)v9x_page_dialog_proc;
    handle = CreatePropertySheetPageA(&page);
    if (handle == 0) {
        return V9X_E_OUTOFMEMORY;
    }
    if (!add_page(handle, lparam)) {
        DestroyPropertySheetPage(handle);
        return V9X_E_FAIL;
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
