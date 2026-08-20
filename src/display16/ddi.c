/*
 * Velocity9x first active DIB Engine path.
 *
 * Scope is restricted to an unaccelerated standard VBE mode matrix. The
 * assembly runtime performs VBE mode entry and DPMI mapping; every drawing
 * operation remains with the Windows DIB Engine.
 *
 * This file is chip-agnostic. Everything that differs between cards - the
 * mode table, the PCI identity, the C:\V9XHW.INI strings, and the two places
 * where a family must run its own code - comes from the statically linked
 * v9x_hw16 table declared in include\velocity9x\hw16.h.
 */
#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/build.h"
#include "velocity9x/hw16.h"
#include "win9x_display_abi.h"

#define V9X_BITMAP_HEADER_SIZE     40u
#define V9X_PALETTE_ENTRIES       256u
#define V9X_PALETTE_BYTES        1024u

#define V9X_COM1_DATA_PORT      0x03f8u
#define V9X_COM1_LCR_PORT       0x03fbu
#define V9X_COM1_LSR_PORT       0x03fdu
#define V9X_COM1_TX_EMPTY          0x20u
#define V9X_SERIAL_SPIN_LIMIT   0xffffu
#define V9X_HARDWARE_INFO_PATH "C:\\V9XHW.INI"

#define V9X_COLOR_NONSTATIC        0x80u
#define V9X_COLOR_MAP_TO_WHITE     0x40u

#ifndef V9X_FORCE_MODE_INDEX
#define V9X_FORCE_MODE_INDEX         -1
#endif

extern WORD FAR PASCAL V9xDibEnableCall(LPVOID, WORD, LPSTR, LPSTR, LPVOID);
extern DWORD FAR PASCAL V9xCreateDibPDeviceCall(LPBITMAPINFO, LPVOID,
                                                LPVOID, WORD);
extern void FAR PASCAL V9xDibBeginAccess(void);
extern void FAR PASCAL V9xDibEndAccess(void);
extern DWORD FAR PASCAL V9xDibSetPaletteCall(WORD, WORD, LPVOID, LPVOID);
extern DWORD FAR PASCAL V9xDibSetPaletteTranslateCall(LPVOID, LPVOID);
extern WORD FAR PASCAL V9xHardwarePresent(void);
/* enable16.c. V9xHardwarePresent plus the family's view of a miss: a tier-0
 * family accepts a card its device list does not name. Both call sites below
 * use this rather than the raw scan so they cannot disagree. */
extern WORD v9x_hardware_acceptable(void);
extern WORD FAR PASCAL V9xHardwareEnable(void);
extern WORD FAR PASCAL V9xHardwareStage(void);
extern WORD FAR PASCAL V9xHardwareReset(void);
extern DWORD FAR PASCAL V9xHardwareBase(void);
/* enable16.c: the off-screen heap size, read from the chip on a family with a
 * read_video_memory hook, from VBE 4F00h at tier-0, and 0 when neither
 * established one - in which case dd16.c applies its own default. */
extern DWORD v9x_vbe_vram_bytes;
/* enable16.c: the card's total VRAM as claimed by the chip hook or VBE 4F00h,
 * before any deduction for the visible surface, and 0 when unmeasured. This is
 * the figure a mode has to fit inside, so it is what ValidateMode tests. */
extern DWORD v9x_vbe_vram_reported;
extern void FAR PASCAL V9xHardwareDisable(void);
extern WORD FAR PASCAL V9xVddPreMode(void);
extern WORD FAR PASCAL V9xVddRegister(void);
extern WORD FAR PASCAL V9xVddReregister(void);
extern void FAR PASCAL V9xVddPostMode(void);
extern void FAR PASCAL V9xVddUnregister(void);
extern WORD FAR PASCAL V9xVddGetDisplayConfig(V9X_DISPLAY_INFO FAR *);

/* The mode table, the PCI identity and the C:\V9XHW.INI strings are family
 * data now, supplied by the statically linked v9x_hw16 table. */
#define v9x_modes      (v9x_hw16.modes)
#define V9X_MODE_COUNT (v9x_hw16.mode_count)

V9X_DIB_ENGINE FAR *v9x_driver_pdevice;
/* These four are read by runtime.asm. LibMain applies the family's first mode
 * to them before any DDI entry point can run, so the initialisers here only
 * have to be a sane VGA-ish default rather than a per-family one. */
WORD v9x_active_vbe_mode = 0x0101u;
DWORD v9x_active_visible_bytes = 307200ul;
WORD v9x_active_width = 640u;
WORD v9x_active_pitch = 640u;
WORD v9x_palettized = 1u;

/*
 * The rest of what runtime.asm needs from the family table, flattened into
 * DGROUP so the assembly can read it without walking a struct of far
 * pointers. Stamped once at load, below.
 *
 * V9xFindPciDevice walks v9x_pci_vendor/device, which is what lets one family
 * binary serve more than one card.
 */
#define V9X_PCI_ID_LIMIT 8u
WORD v9x_pci_vendor[V9X_PCI_ID_LIMIT];
WORD v9x_pci_device[V9X_PCI_ID_LIMIT];
WORD v9x_pci_count = 0u;
/*
 * Index of the entry V9xFindPciDevice matched, or 0xFFFF before it has run or
 * when nothing answered. It is the one thing the scan learns that the family
 * table cannot state in advance, and with more than one chip per binary it
 * decides whose hooks run and which identity is published.
 */
WORD v9x_pci_match = 0xffffu;
WORD v9x_vbe_mode_flags = 0x8000u;
WORD v9x_map_pages_hi = 0x03ffu;
WORD v9x_map_pages_lo = 0xffffu;
static RGBQUAD FAR *v9x_color_table;
static const V9X_HW16_MODE *v9x_selected_mode;
static const V9X_HW16_MODE *v9x_active_mode;
static WORD v9x_dib_pdevice_size;
static WORD v9x_screen_selector;
static WORD v9x_enabled;
/* Non-zero while ReEnable rebuilds the PDEVICE in place for a live mode
 * switch (mirrors vmdisp9x's bReEnabling): keeps the realized palette and
 * suppresses the boot-style teardown on failure. */
static WORD v9x_reenabling;
static WORD v9x_dpi = 96u;
/* Enable/Disable lifecycle counts published through the HAL trace. */
static WORD v9x_enable_count;
static WORD v9x_disable_count;
/* The PDEVICE size reported to GDI at the first query. GDI allocates once
 * from that value, so a later live depth change has to fit inside it. */
static WORD v9x_pdevice_allocated;
/* Non-zero while ReEnable rebuilds at a different colour depth. */
static WORD v9x_depth_changed;
/*
 * Latched on the first successful Enable and never cleared.
 *
 * The boot trace records the furthest stage reached, so a GDIINFO query must
 * not overwrite an existing enable-ok marker. Guarding those writes on
 * v9x_enabled is not enough: Disable clears it, and Windows disables and
 * re-enables the display during startup, so a query arriving while the driver
 * was between the two rewrote the marker back to query-ok. The settings page
 * then reported a healthy driver as "Not confirmed".
 */
static WORD v9x_ever_enabled;

#ifdef V9X_BOOT_TRACE
static BOOL v9x_boot_trace(const char FAR *stage)
{
    return WritePrivateProfileString("Velocity9x", "Stage", stage,
                                     "C:\\V9XBOOT.INI");
}

static void v9x_trace_hardware_failure(void)
{
    switch (V9xHardwareStage()) {
    case 1u: v9x_boot_trace("fail-hardware-pci"); break;
    case 2u: v9x_boot_trace("fail-hardware-vbe-mode"); break;
    case 3u: v9x_boot_trace("fail-hardware-aperture"); break;
    case 4u: v9x_boot_trace("fail-hardware-selector"); break;
    case 5u: v9x_boot_trace("fail-hardware-dpmi-map"); break;
    case 6u: v9x_boot_trace("fail-hardware-selector-base"); break;
    case 7u: v9x_boot_trace("fail-hardware-selector-limit"); break;
    case 8u: v9x_boot_trace("fail-hardware-s3-linear-aperture"); break;
    case 9u: v9x_boot_trace("fail-hardware-vbe-pitch"); break;
    case 10u: v9x_boot_trace("fail-hardware-matrox-direct-format"); break;
    default: v9x_boot_trace("fail-hardware-unknown"); break;
    }
}
#else
#define v9x_boot_trace(stage) ((void)0)
#define v9x_trace_hardware_failure() ((void)0)
#endif

static BYTE v9x_port_in(WORD port);
#pragma aux v9x_port_in = "in al,dx" parm [dx] value [al] modify exact [al]

static void v9x_port_out(WORD port, BYTE value);
#pragma aux v9x_port_out = "out dx,al" parm [dx] [al] modify exact []

/*
 * Publish the hardware diagnostics file the settings page and the support
 * instructions both read.
 *
 * The section is cleared here, and the family hook appends its keys. Key
 * order is part of the contract - the file is written by appending - so the
 * hook owns it rather than this function.
 */
static void v9x_write_hardware_info(const char *key, const char *value)
{
    if (value == 0) {
        return;
    }
    WritePrivateProfileString("Velocity9xHardware", (LPCSTR)key,
                              (LPCSTR)value, V9X_HARDWARE_INFO_PATH);
}

/*
 * The chip this binary is actually driving.
 *
 * Falls back to the first entry when the PCI scan has not run or matched
 * nothing, which is what a single-chip family always saw. For a family with
 * several chips the fallback is only ever reached before Enable, and every
 * caller of a per-chip hook runs after it.
 */
const V9X_HW16_DEVICE *v9x_hw16_active_device(void)
{
    if (v9x_hw16.device_count == 0u) {
        return 0;
    }
    if (v9x_pci_match >= v9x_hw16.device_count) {
        return v9x_hw16.devices[0];
    }
    return v9x_hw16.devices[v9x_pci_match];
}

static void v9x_publish_hardware_diagnostics(void)
{
    const V9X_HW16_DEVICE *device = v9x_hw16_active_device();

    if (v9x_hw16.publish_diagnostics == 0 || device == 0) {
        return;
    }
    WritePrivateProfileString("Velocity9xHardware", 0, 0,
                              V9X_HARDWARE_INFO_PATH);
    v9x_hw16.publish_diagnostics(device, v9x_write_hardware_info);
}

void v9x_serial_write(const char FAR *message)
{
    BYTE saved_lcr;
    WORD spins;

    if (v9x_port_in(V9X_COM1_LSR_PORT) == 0xffu) {
        return;
    }
    saved_lcr = v9x_port_in(V9X_COM1_LCR_PORT);
    v9x_port_out(V9X_COM1_LCR_PORT, (BYTE)(saved_lcr & 0x7fu));

    while (*message != '\0') {
        spins = V9X_SERIAL_SPIN_LIMIT;
        while ((v9x_port_in(V9X_COM1_LSR_PORT) & V9X_COM1_TX_EMPTY) == 0u) {
            if (--spins == 0u) {
                v9x_port_out(V9X_COM1_LCR_PORT, saved_lcr);
                return;
            }
        }
        v9x_port_out(V9X_COM1_DATA_PORT, (BYTE)*message++);
    }
    v9x_port_out(V9X_COM1_LCR_PORT, saved_lcr);
}

static void v9x_serial_write_hex32(DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    char text[9];
    short shift;

    for (shift = 28; shift >= 0; shift -= 4) {
        text[(28 - shift) / 4] = digits[(WORD)(value >> shift) & 0x000fu];
    }
    text[8] = '\0';
    v9x_serial_write(text);
}

static void v9x_serial_write_u16(WORD value)
{
    char text[6];
    WORD length = 0u;
    WORD index;

    do {
        text[length++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && length < 5u);
    for (index = 0u; index < length / 2u; ++index) {
        char temporary = text[index];
        text[index] = text[length - index - 1u];
        text[length - index - 1u] = temporary;
    }
    text[length] = '\0';
    v9x_serial_write(text);
}

static void v9x_serial_write_mode(const char FAR *prefix)
{
    v9x_serial_write(prefix);
    v9x_serial_write_u16(v9x_selected_mode->width);
    v9x_serial_write("x");
    v9x_serial_write_u16(v9x_selected_mode->height);
    v9x_serial_write("x");
    v9x_serial_write_u16(v9x_selected_mode->bits_per_pixel);
}

static const V9X_HW16_MODE *v9x_find_mode(WORD width,
                                              WORD height,
                                              WORD bits_per_pixel)
{
    WORD index;

    for (index = 0u; index < V9X_MODE_COUNT; ++index) {
        if (v9x_modes[index].width == width &&
            v9x_modes[index].height == height &&
            v9x_modes[index].bits_per_pixel == bits_per_pixel) {
            return &v9x_modes[index];
        }
    }
    return 0;
}

static void v9x_apply_mode(const V9X_HW16_MODE *mode)
{
    v9x_selected_mode = mode;
    v9x_active_vbe_mode = mode->vbe_mode;
    v9x_active_visible_bytes =
        (DWORD)mode->pitch * (DWORD)mode->height;
    v9x_active_width = mode->width;
    v9x_active_pitch = mode->pitch;
    v9x_palettized = mode->bits_per_pixel == 8u ? 1u : 0u;
}

static void v9x_select_requested_mode(void)
{
    const V9X_HW16_MODE *requested = 0;

#if V9X_FORCE_MODE_INDEX >= 0
    requested = &v9x_modes[V9X_FORCE_MODE_INDEX];
#else
    V9X_DISPLAY_INFO display_info;
    BYTE *bytes = (BYTE *)&display_info;
    WORD index;

    for (index = 0u; index < sizeof(display_info); ++index) {
        bytes[index] = 0u;
    }
    if (V9xVddGetDisplayConfig(&display_info) != 0u) {
        requested = v9x_find_mode(display_info.width, display_info.height,
                                  display_info.bits_per_pixel);
        if (display_info.dpi >= 72u && display_info.dpi <= 200u) {
            v9x_dpi = display_info.dpi;
        }
    }
#endif
    if (requested == 0) {
        requested = &v9x_modes[0];
    }
    v9x_apply_mode(requested);
}

/* DirectDraw glue accessors (dd16.c). A family with no DirectDraw HAL links
 * the no-op forms, so these calls need no per-target guard here. */
extern WORD FAR PASCAL V9xDdCreateDriverObject(WORD reset);
extern void FAR PASCAL V9xDdInvalidate(void);

LPVOID v9x_dd_active_pdevice(void)
{
    return (LPVOID)v9x_driver_pdevice;
}

/* Diagnostics for the DIBENG fault investigation: the live framebuffer
 * selector, and how many times the driver has been enabled and disabled.
 * Disable frees this selector, so a stale copy anywhere else would dangle. */
WORD v9x_dd_screen_selector(void)
{
    return v9x_screen_selector;
}

WORD v9x_dd_enable_count(void)
{
    return v9x_enable_count;
}

WORD v9x_dd_disable_count(void)
{
    return v9x_disable_count;
}

/*
 * The mode the hardware sequence is currently bringing up.
 *
 * Deliberately not v9x_dd_active_mode: that one reports what the DIB Engine is
 * drawing into and so is gated on v9x_enabled, which is not set until the
 * PDEVICE has been built. V9xHardwareEnable runs before that and needs the row
 * it is enabling, to ask the BIOS whether the mode it just set agrees with the
 * family's table. v9x_selected_mode is stamped by v9x_apply_mode, which every
 * path runs before the hardware sequence starts.
 */
WORD v9x_selected_mode_geometry(WORD FAR *width, WORD FAR *height,
                                WORD FAR *bpp, WORD FAR *pitch)
{
    if (v9x_selected_mode == 0) {
        return 0u;
    }
    *width = v9x_selected_mode->width;
    *height = v9x_selected_mode->height;
    *bpp = v9x_selected_mode->bits_per_pixel;
    *pitch = v9x_selected_mode->pitch;
    return 1u;
}

WORD v9x_dd_active_mode(WORD FAR *width, WORD FAR *height,
                        WORD FAR *bpp, WORD FAR *pitch)
{
    if (v9x_enabled == 0u || v9x_active_mode == 0) {
        return 0u;
    }
    *width = v9x_active_mode->width;
    *height = v9x_active_mode->height;
    *bpp = v9x_active_mode->bits_per_pixel;
    *pitch = v9x_active_mode->pitch;
    return 1u;
}

void v9x_display_boot_log(void)
{
    WORD index;

    /* Stamp the family table into the DGROUP variables runtime.asm reads,
     * before any DDI entry point can run. V9XHARDWAREENABLE is reached only
     * through Enable, which is long after LibMain. */
    v9x_pci_count = v9x_hw16.device_count;
    if (v9x_pci_count > V9X_PCI_ID_LIMIT) {
        v9x_pci_count = V9X_PCI_ID_LIMIT;
    }
    for (index = 0u; index < v9x_pci_count; ++index) {
        v9x_pci_vendor[index] = v9x_hw16.devices[index]->vendor_id;
        v9x_pci_device[index] = v9x_hw16.devices[index]->device_id;
    }
    v9x_vbe_mode_flags = v9x_hw16.vbe_mode_flags;
    v9x_map_pages_hi = v9x_hw16.map_pages_hi;
    v9x_map_pages_lo = v9x_hw16.map_pages_lo;
    v9x_apply_mode(&v9x_hw16.modes[0]);
    v9x_serial_write("V9X-DRV load build=" V9X_BUILD_ID "\r\n");
    /* Boot-capture evidence shows ring-3 serial writes from LibMain do not
     * normally reach the host log. The INI marker is strong load evidence,
     * but its absence is inconclusive because the early file write can fail. */
#ifdef V9X_BOOT_TRACE
    if (!v9x_boot_trace("libmain")) {
        v9x_serial_write("V9X-DRV trace-write-fail stage=libmain build="
                         V9X_BUILD_ID "\r\n");
    }
#endif
}

static void v9x_set_color(RGBQUAD FAR *entry,
                          BYTE red,
                          BYTE green,
                          BYTE blue,
                          BYTE flags)
{
    entry->rgbRed = red;
    entry->rgbGreen = green;
    entry->rgbBlue = blue;
    entry->rgbReserved = flags;
}

static void v9x_build_palette(RGBQUAD FAR *palette)
{
    WORD index;

    for (index = 0u; index < V9X_PALETTE_ENTRIES; ++index) {
        v9x_set_color(&palette[index], 0u, 0u, 0u, V9X_COLOR_NONSTATIC);
    }

    v9x_set_color(&palette[0],   0u,   0u,   0u, 0u);
    v9x_set_color(&palette[1], 128u,   0u,   0u, 0u);
    v9x_set_color(&palette[2],   0u, 128u,   0u, 0u);
    v9x_set_color(&palette[3], 128u, 128u,   0u, 0u);
    v9x_set_color(&palette[4],   0u,   0u, 128u, 0u);
    v9x_set_color(&palette[5], 128u,   0u, 128u, 0u);
    v9x_set_color(&palette[6],   0u, 128u, 128u, 0u);
    v9x_set_color(&palette[7], 192u, 192u, 192u, V9X_COLOR_MAP_TO_WHITE);
    v9x_set_color(&palette[8], 192u, 220u, 192u,
                  V9X_COLOR_NONSTATIC | V9X_COLOR_MAP_TO_WHITE);
    v9x_set_color(&palette[9], 166u, 202u, 240u,
                  V9X_COLOR_NONSTATIC | V9X_COLOR_MAP_TO_WHITE);

    v9x_set_color(&palette[246], 255u, 251u, 240u,
                  V9X_COLOR_NONSTATIC | V9X_COLOR_MAP_TO_WHITE);
    v9x_set_color(&palette[247], 160u, 160u, 164u,
                  V9X_COLOR_NONSTATIC | V9X_COLOR_MAP_TO_WHITE);
    v9x_set_color(&palette[248], 128u, 128u, 128u, V9X_COLOR_MAP_TO_WHITE);
    v9x_set_color(&palette[249], 255u,   0u,   0u, 0u);
    v9x_set_color(&palette[250],   0u, 255u,   0u, V9X_COLOR_MAP_TO_WHITE);
    v9x_set_color(&palette[251], 255u, 255u,   0u, V9X_COLOR_MAP_TO_WHITE);
    v9x_set_color(&palette[252],   0u,   0u, 255u, 0u);
    v9x_set_color(&palette[253], 255u,   0u, 255u, 0u);
    v9x_set_color(&palette[254],   0u, 255u, 255u, V9X_COLOR_MAP_TO_WHITE);
    v9x_set_color(&palette[255], 255u, 255u, 255u, V9X_COLOR_MAP_TO_WHITE);
}

static void v9x_program_palette(WORD start, WORD count)
{
    RGBQUAD FAR *entry;

    if (v9x_color_table == 0 || start >= V9X_PALETTE_ENTRIES) {
        return;
    }
    if (count > V9X_PALETTE_ENTRIES - start) {
        count = V9X_PALETTE_ENTRIES - start;
    }

    v9x_port_out(0x03c8u, (BYTE)start);
    entry = &v9x_color_table[start];
    while (count-- != 0u) {
        v9x_port_out(0x03c9u, (BYTE)(entry->rgbRed >> 2));
        v9x_port_out(0x03c9u, (BYTE)(entry->rgbGreen >> 2));
        v9x_port_out(0x03c9u, (BYTE)(entry->rgbBlue >> 2));
        ++entry;
    }
}

static void v9x_set_point(V9X_POINT_TYPE FAR *point, short x, short y)
{
    point->x = x;
    point->y = y;
}

static WORD v9x_fill_gdi_info(V9X_GDI_INFO FAR *info,
                              LPSTR destination_type,
                              LPSTR output_file,
                              LPVOID data)
{
    WORD result;
    WORD extra_size;

    if (v9x_ever_enabled == 0u) {
        v9x_boot_trace("query-start");
    }
    if (v9x_enabled == 0u) {
        v9x_select_requested_mode();
    }
    if (v9x_ever_enabled == 0u) {
        v9x_boot_trace("query-mode-selected");
    }

    result = V9xDibEnableCall(info, 1u, destination_type, output_file, data);
    if (result == 0u || info->dpDEVICEsize <= 0) {
        v9x_boot_trace("fail-dib-query");
        return 0u;
    }

    v9x_dib_pdevice_size = (WORD)info->dpDEVICEsize;
    info->dpVersion = V9X_DRV_VERSION;
    info->dpTechnology = V9X_DT_RASDISPLAY;
    info->dpHorzSize = 208;
    info->dpVertSize = 156;
    info->dpHorzRes = v9x_selected_mode->width;
    info->dpVertRes = v9x_selected_mode->height;
    info->dpBitsPixel = v9x_selected_mode->bits_per_pixel;
    info->dpPlanes = 1;
    info->dpNumBrushes = -1;
    info->dpNumFonts = 0;
    /*
     * Reserve the colour table at every depth, not just at 8 bpp.
     *
     * GDI allocates the PDEVICE once, from the size reported by the first
     * query, and a live depth change rebuilds inside that same allocation.
     * Sizing it for the current depth made the 8-bpp PDEVICE 1 KiB larger
     * than the 16-bpp one, so switching down could not fit and the driver
     * had to refuse depth changes outright. Reserving the maximum costs
     * 1 KiB at 16 bpp and makes the switch expressible.
     */
    extra_size = V9X_BITMAP_HEADER_SIZE + V9X_PALETTE_BYTES;
    info->dpRaster |= V9X_RC_DIBTODEV;
    if (v9x_palettized != 0u) {
        info->dpNumPens = 16;
        info->dpNumColors = 20;
        info->dpRaster |= V9X_RC_PALETTE;
        info->dpNumPalReg = V9X_PALETTE_ENTRIES;
        info->dpPalReserved = 20u;
        info->dpColorRes = 18u;
    } else {
        info->dpNumPens = -1;
        info->dpNumColors = -1;
        info->dpRaster &= (WORD)~V9X_RC_PALETTE;
        info->dpNumPalReg = 0u;
        info->dpPalReserved = 0u;
        info->dpColorRes = 0u;
    }
    info->dpDEVICEsize = (short)(v9x_dib_pdevice_size + extra_size);
    if (v9x_pdevice_allocated == 0u) {
        v9x_pdevice_allocated = (WORD)info->dpDEVICEsize;
    }

    v9x_set_point(&info->dpMLoWin, 2080, 1560);
    v9x_set_point(&info->dpMLoVpt, (short)v9x_selected_mode->width,
                  -(short)v9x_selected_mode->height);
    v9x_set_point(&info->dpMHiWin, 20800, 15600);
    v9x_set_point(&info->dpMHiVpt, (short)v9x_selected_mode->width,
                  -(short)v9x_selected_mode->height);
    v9x_set_point(&info->dpELoWin, 325, 325);
    v9x_set_point(&info->dpELoVpt, v9x_selected_mode->english_low,
                  -v9x_selected_mode->english_low);
    v9x_set_point(&info->dpEHiWin, 1625, 1625);
    v9x_set_point(&info->dpEHiVpt, v9x_selected_mode->english_high,
                  -v9x_selected_mode->english_high);
    v9x_set_point(&info->dpTwpWin, 2340, 2340);
    v9x_set_point(&info->dpTwpVpt, v9x_selected_mode->english_high,
                  -v9x_selected_mode->english_high);

    info->dpLogPixelsX = (short)v9x_dpi;
    info->dpLogPixelsY = (short)v9x_dpi;
    info->dpDCManage = V9X_DC_IGNORE_DFNP;
    /* C1_DIBENGINE is a statement of fact to GDI: this driver builds its
     * PDEVICE with CreateDIBPDevice and forwards output to the DIB Engine.
     * It was briefly dropped on the theory that it caused DirectDraw to
     * publish DDCAPS_NOHARDWARE; that cause was measured to be an incomplete
     * dwRops table (see dd16.c), so the accurate declaration is restored.
     * C1_SLOW_CARD stays off now that fills reach the Trio64 engine. */
    info->dpCaps1 |= V9X_C1_DIBENGINE | V9X_C1_REINIT_ABLE |
                     V9X_C1_BYTE_PACKED | V9X_C1_COLORCURSOR;
    if (v9x_ever_enabled == 0u) {
        v9x_boot_trace("query-ok");
    }
    return V9X_GDIINFO_SIZE;
}

static WORD v9x_build_pdevice(LPVOID device_info,
                              LPSTR destination_type,
                              LPSTR output_file,
                              LPVOID data)
{
    BITMAPINFO FAR *bitmap_info;
    DWORD created;
    WORD pdevice_flags = V9X_DE_MINIDRIVER | V9X_DE_VRAM;

    if (v9x_dib_pdevice_size == 0u) {
        v9x_boot_trace("fail-pdevice-size");
        return 0u;
    }
    v9x_boot_trace("enable-start");
    if (v9x_hardware_acceptable() == 0u) {
        v9x_boot_trace("fail-hardware-present");
        v9x_serial_write("V9X-DRV enable-fail stage=device-id\r\n");
        return 0u;
    }
    if (V9xVddPreMode() == 0u) {
        v9x_boot_trace("fail-vdd-pre-mode");
        v9x_serial_write("V9X-DRV enable-fail stage=vdd-pre-mode\r\n");
        return 0u;
    }
    v9x_screen_selector = V9xHardwareEnable();
    if (v9x_screen_selector == 0u) {
        v9x_trace_hardware_failure();
        v9x_serial_write("V9X-DRV enable-fail stage=mode-map\r\n");
        return 0u;
    }
    if ((v9x_reenabling != 0u ? V9xVddReregister()
                              : V9xVddRegister()) == 0u) {
        v9x_boot_trace("fail-vdd-register");
        v9x_serial_write("V9X-DRV enable-fail stage=vdd-register\r\n");
        if (v9x_reenabling == 0u) {
            V9xHardwareDisable();
            v9x_screen_selector = 0u;
        }
        return 0u;
    }

    if (V9xDibEnableCall(device_info, 0u, destination_type,
                         output_file, data) == 0u) {
        v9x_boot_trace("fail-dib-enable");
        v9x_serial_write("V9X-DRV enable-fail stage=dib-enable\r\n");
        if (v9x_reenabling == 0u) {
            V9xVddUnregister();
            V9xHardwareDisable();
            v9x_screen_selector = 0u;
        }
        return 0u;
    }
    /*
     * Three-way, not two. DIBENG.INC defines exactly two layout flags -
     * PALETTIZED and FIVE6FIVE ("16 bpp, 565 color format") - and nothing for
     * 24 or 32 bpp, where the engine takes the layout from biBitCount in the
     * BITMAPINFO built below. Claiming FIVE6FIVE at those depths, which the
     * old else-branch did for anything that was not 8 bpp, would tell the
     * engine to pack three channels into the first two bytes of each pixel.
     */
    if (v9x_palettized != 0u) {
        (void)V9xDibSetPaletteTranslateCall(0, device_info);
        pdevice_flags |= V9X_DE_PALETTIZED;
    } else if (v9x_selected_mode->bits_per_pixel == 16u) {
        pdevice_flags |= V9X_DE_FIVE6FIVE;
    }

    /*
     * The PDEVICE is GDI's allocation and cannot grow. Reserving the colour
     * table at every depth is what makes a live depth change fit, but verify
     * it rather than trusting it: overrunning GDI's buffer would corrupt the
     * heap silently, while refusing the switch is recoverable.
     */
    if (v9x_pdevice_allocated != 0u &&
        (DWORD)v9x_dib_pdevice_size + V9X_BITMAP_HEADER_SIZE +
            V9X_PALETTE_BYTES > (DWORD)v9x_pdevice_allocated) {
        v9x_boot_trace("fail-pdevice-too-small");
        return 0u;
    }
    bitmap_info = (BITMAPINFO FAR *)
        ((BYTE FAR *)device_info + v9x_dib_pdevice_size);
    bitmap_info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info->bmiHeader.biWidth = v9x_selected_mode->width;
    bitmap_info->bmiHeader.biHeight = v9x_selected_mode->height;
    bitmap_info->bmiHeader.biPlanes = 1u;
    bitmap_info->bmiHeader.biBitCount = v9x_selected_mode->bits_per_pixel;
    bitmap_info->bmiHeader.biCompression = BI_RGB;
    bitmap_info->bmiHeader.biSizeImage = v9x_active_visible_bytes;
    bitmap_info->bmiHeader.biXPelsPerMeter = 0;
    bitmap_info->bmiHeader.biYPelsPerMeter = 0;
    bitmap_info->bmiHeader.biClrUsed =
        v9x_palettized != 0u ? V9X_PALETTE_ENTRIES : 0u;
    bitmap_info->bmiHeader.biClrImportant = bitmap_info->bmiHeader.biClrUsed;

    if (v9x_palettized != 0u) {
        v9x_color_table = bitmap_info->bmiColors;
        /* During a live switch the BITMAPINFO color table in the reused
         * PDEVICE still holds the realized palette; do not reset it to the
         * defaults (vmdisp9x preserves it the same way). */
        if (v9x_reenabling == 0u || v9x_depth_changed != 0u) {
            v9x_build_palette(v9x_color_table);
        }
    } else {
        v9x_color_table = 0;
    }
    /* A family supplies build_screen_pdevice only when its BIOS-selected
     * scan-line layout is not reconstructed reliably by CreateDIBPDevice.
     * The proven S3 path leaves the hook null and uses the DIB Engine. */
    if (v9x_hw16.build_screen_pdevice != 0) {
        created = v9x_hw16.build_screen_pdevice(device_info, bitmap_info,
                                                v9x_selected_mode,
                                                v9x_screen_selector,
                                                pdevice_flags);
    } else {
        created = V9xCreateDibPDeviceCall(bitmap_info, device_info,
                                         MAKELP(v9x_screen_selector, 0u),
                                         pdevice_flags);
    }
    if (created == 0ul) {
        v9x_boot_trace("fail-create-pdevice");
        v9x_serial_write("V9X-DRV enable-fail stage=create-pdevice\r\n");
        if (v9x_reenabling == 0u) {
            V9xVddUnregister();
            V9xHardwareDisable();
            v9x_screen_selector = 0u;
            v9x_color_table = 0;
        }
        return 0u;
    }

    v9x_driver_pdevice = (V9X_DIB_ENGINE FAR *)device_info;
    v9x_driver_pdevice->deBeginAccess = V9xDibBeginAccess;
    v9x_driver_pdevice->deEndAccess = V9xDibEndAccess;
    v9x_driver_pdevice->deVersion = V9X_DE_VERSION;
    if (v9x_palettized != 0u) {
        v9x_program_palette(0u, V9X_PALETTE_ENTRIES);
    }
    v9x_enabled = 1u;
    v9x_ever_enabled = 1u;
    ++v9x_enable_count;
    v9x_active_mode = v9x_selected_mode;
    v9x_serial_write("V9X-DRV lfb=0x");
    v9x_serial_write_hex32(V9xHardwareBase());
    /* The figure actually in force, not a literal.
     *
     * This printed 00400000 unconditionally, which was true of every card the
     * driver had run on and is the same 4 MiB assumption that let a 2 MiB
     * card's over-advertised heap go unnoticed - the checkpoint agreed with the
     * bug. A checkpoint that cannot disagree with the code is not a checkpoint.
     *
     * Documented as 00400000 in the package INSTALL.TXT, which is now wrong for
     * any card that is not 4 MiB. */
    v9x_serial_write(" bytes=0x");
    v9x_serial_write_hex32(v9x_vbe_vram_bytes);
    v9x_serial_write("\r\n");
    v9x_serial_write_mode("V9X-DRV enable-ok mode=");
    v9x_serial_write(" lfb-mapped\r\n");
    v9x_publish_hardware_diagnostics();
    /* A live ReEnable owns a DIBENGINE BeginAccessRect exclusion until the
     * rebuilt PDEVICE has been finalized below. Calling DIBENGINE's SetInfo
     * from inside that exclusion re-enters DIBENG and can fault. The
     * ReEnable path publishes the refreshed HAL after EndAccessRect instead.
     * Normal Enable still performs the harmless pre-DDRAW refresh here. */
    if (v9x_reenabling == 0u) {
        (void)V9xDdCreateDriverObject(1u);
    }
    v9x_boot_trace("enable-ok");
    return 1u;
}

WORD __loadds FAR PASCAL Enable(LPVOID device_info,
                                WORD action,
                                LPSTR destination_type,
                                LPSTR output_file,
                                LPVOID data)
{
    if ((action & 1u) != 0u) {
        return v9x_fill_gdi_info((V9X_GDI_INFO FAR *)device_info,
                                 destination_type, output_file, data);
    }
    return v9x_build_pdevice(device_info, destination_type, output_file, data);
}

WORD __loadds FAR PASCAL Disable(LPVOID destination_device)
{
    V9X_DIB_ENGINE FAR *device =
        (V9X_DIB_ENGINE FAR *)destination_device;

    if (device != 0) {
        device->deFlags |= V9X_DE_BUSY;
    }
    v9x_enabled = 0u;
    ++v9x_disable_count;
    v9x_active_mode = 0;
    v9x_driver_pdevice = 0;
    v9x_color_table = 0;
    V9xDdInvalidate();
    V9xVddUnregister();
    V9xHardwareDisable();
    v9x_screen_selector = 0u;
    v9x_serial_write("V9X-DRV disable\r\n");
    return 0xffffu;
}

WORD __loadds FAR PASCAL ReEnable(LPVOID destination_device,
                                  LPVOID gdi_info)
{
    V9X_DIB_ENGINE FAR *device =
        (V9X_DIB_ENGINE FAR *)destination_device;
    const V9X_HW16_MODE *previous_mode;

    if (device == 0 || gdi_info == 0 || v9x_enabled == 0u ||
        v9x_active_mode == 0) {
        return 0u;
    }

    /* GDI writes the requested mode to the registry before calling
     * ReEnable; re-read it the way vmdisp9x's ReEnable does. */
    previous_mode = v9x_active_mode;
    v9x_select_requested_mode();

    if (v9x_selected_mode == previous_mode) {
        /* Unchanged mode: restore the current mode, e.g. returning from a
         * full-screen DOS box. */
        if (v9x_fill_gdi_info((V9X_GDI_INFO FAR *)gdi_info, 0, 0, 0) == 0u ||
            V9xHardwareReset() == 0u) {
            v9x_serial_write("V9X-DRV reenable-fail\r\n");
            return 0u;
        }
        device->deFlags &= (WORD)~V9X_DE_BUSY;
        if (v9x_palettized != 0u) {
            v9x_program_palette(0u, V9X_PALETTE_ENTRIES);
        }
        v9x_publish_hardware_diagnostics();
        V9xVddPostMode();
        v9x_serial_write("V9X-DRV reenable-ok\r\n");
        return 1u;
    }

    /* Live same-depth mode switch: rebuild the PDEVICE in place. DIBENGINE's
     * reference mini-driver does not wrap this operation in BeginAccess /
     * EndAccess: CreateDIBPDevice replaces cursor bookkeeping inside the same
     * PDEVICE, so an exclusion begun against the old contents cannot safely
     * be ended against the rebuilt contents. */
    v9x_reenabling = 1u;
    /* Arriving at 8 bpp from 16 bpp there is no realized palette in the
     * reused colour table to preserve, so it has to be rebuilt. */
    v9x_depth_changed = v9x_selected_mode->bits_per_pixel !=
                        previous_mode->bits_per_pixel ? 1u : 0u;
    if (v9x_build_pdevice(device, 0, 0, 0) == 0u) {
        /* Bring the previous mode back before reporting failure. */
        v9x_apply_mode(previous_mode);
        (void)v9x_build_pdevice(device, 0, 0, 0);
        v9x_reenabling = 0u;
        v9x_serial_write("V9X-DRV switch-fail\r\n");
        return 0u;
    }
    v9x_reenabling = 0u;
    v9x_depth_changed = 0u;
    if (v9x_fill_gdi_info((V9X_GDI_INFO FAR *)gdi_info, 0, 0, 0) == 0u) {
        v9x_serial_write("V9X-DRV switch-fail stage=gdi-info\r\n");
        return 0u;
    }
    device->deFlags &= (WORD)~V9X_DE_BUSY;
    /* PDEVICE reconstruction is complete; it is now safe to call the
     * runtime's SetInfo reset callback. */
    (void)V9xDdCreateDriverObject(1u);
    v9x_serial_write_mode("V9X-DRV switch-ok mode=");
    v9x_serial_write("\r\n");
    return 1u;
}

WORD __loadds FAR PASCAL ValidateMode(LPVOID display_info)
{
    V9X_DISPLAY_VALIDATE_MODE FAR *mode =
        (V9X_DISPLAY_VALIDATE_MODE FAR *)display_info;
    const V9X_HW16_MODE *candidate;

    if (mode == 0 || mode->size < sizeof(*mode)) {
        return V9X_VALMODE_NO_WRONG_DRIVER;
    }
    if (v9x_hardware_acceptable() == 0u) {
        return V9X_VALMODE_NO_WRONG_DRIVER;
    }
    candidate = v9x_find_mode((WORD)mode->width, (WORD)mode->height,
                              mode->bits_per_pixel);
    if (candidate == 0) {
        return V9X_VALMODE_NO_NOMEM;
    }
    /*
     * A row being in the table is not the same as the card having the memory
     * for it. The table is per family, the VRAM is per card: the 2 MiB
     * physical Trio64 shares its rows with a 4 MiB ViRGE, and 1024x768 at
     * 24 bpp needs 2.25 MiB. Refuse here rather than letting Enable set a
     * mode the card cannot scan out.
     *
     * v9x_vbe_vram_reported is zero until something has established it, and
     * zero means "not known" rather than "no memory" - an unmeasured card
     * keeps the old behaviour of trusting the table.
     *
     * NO_NOMEM is also what "not in the table" returns above. The two causes
     * are different but the answer GDI needs is the same, and the ValidateMode
     * contract has no code that distinguishes them.
     */
    if (v9x_vbe_vram_reported != 0ul &&
        (DWORD)candidate->pitch * (DWORD)candidate->height >
            v9x_vbe_vram_reported) {
        return V9X_VALMODE_NO_NOMEM;
    }
    return V9X_VALMODE_YES;
}

DWORD __loadds FAR PASCAL SetPalette(WORD start,
                                     WORD count,
                                     LPVOID palette)
{
    DWORD result;

    if (v9x_driver_pdevice == 0 || palette == 0 || v9x_palettized == 0u) {
        return 0ul;
    }
    result = V9xDibSetPaletteCall(start, count, palette,
                                 v9x_driver_pdevice);
    if ((v9x_driver_pdevice->deFlags & V9X_DE_BUSY) == 0u) {
        v9x_program_palette(start, count);
    }
    return result;
}

void __loadds FAR PASCAL ResetHiResMode(void)
{
    if (V9xHardwareReset() != 0u) {
        if (v9x_palettized != 0u) {
            v9x_program_palette(0u, V9X_PALETTE_ENTRIES);
        }
    }
}
