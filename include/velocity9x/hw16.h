/*
 * Velocity9x 16-bit hardware layer.
 *
 * One statically linked v9x_hw16_ops table per family binary. It carries the
 * data the display driver needs about the cards it supports - PCI identity,
 * the audited VBE mode table, the strings published to C:\V9XHW.INI - plus
 * nullable hooks for the few places where a family must run its own code.
 *
 * A NULL hook means "use the chip-agnostic default". A family whose hooks are
 * all NULL is the generic VBE package: that rule is what makes the tier-0
 * backend a consequence of the interface rather than a special case.
 *
 * This header is the 16-bit hardware layer, not the pure policy layer. It is
 * allowed to do port I/O and INT 10h/1Ah/31h, none of which can be host
 * tested; include\velocity9x\backend.h stays I/O-free for that.
 *
 * Hooks must stay near. The driver builds with wcc -mc, so code is near and a
 * hook table costs a near call only as long as every implementation lives in
 * the one code segment.
 *
 * See docs\plans\multi-chip-restructure.md and
 * docs\specifications\family-manifest.md.
 */
#ifndef VELOCITY9X_HW16_H
#define VELOCITY9X_HW16_H

/*
 * Deliberately no <windows.h>. A chip module publishes its diagnostics through
 * the writer callback below rather than calling WritePrivateProfileString
 * itself, which keeps src\chipsets free of the OS boundary that
 * check-tree.ps1 confines to src\display16 and src\display32.
 *
 * Plain C types throughout: under wcc -mc a bare pointer is already far, so
 * these signatures match the LPVOID/DWORD spellings on the display16 side.
 */

/*
 * One entry per VBE mode the family publishes.
 *
 * The order is the order GDI enumerates the MODES registry key, which is why
 * 640x400 sits after the other 8-bpp rows rather than first: Doom95 asks
 * DirectDraw for 640x400, and the driver's list has to agree with the
 * registry's.
 *
 * english_low and english_high are the GDIINFO logical-inch dimensions for
 * the mode.
 */
typedef struct v9x_hw16_mode {
    unsigned short width;
    unsigned short height;
    unsigned short bits_per_pixel;
    unsigned short pitch;
    unsigned short vbe_mode;
    short english_low;
    short english_high;
} V9X_HW16_MODE;

/*
 * One entry per chip in the family. The strings are exactly what the driver
 * writes to C:\V9XHW.INI, so they are data here rather than #ifdef'd literals
 * in the diagnostics publisher.
 *
 * acceleration and direct3d may be null for a family that publishes neither.
 */
typedef struct v9x_hw16_device {
    unsigned short vendor_id;
    unsigned short device_id;
    const char *adapter;
    const char *vendor_text;
    const char *device_text;
    const char *clock_detector;
    const char *mode_switching;
    const char *acceleration;
    const char *direct3d;
} V9X_HW16_DEVICE;

/* VBE 4F02h mode-set flags. The S3 BIOS wants the S3/VBE no-clear bit; the
 * generic linear-framebuffer bit is what a tier-0 card needs. */
#define V9X_HW16_VBE_NO_CLEAR   0x8000u
#define V9X_HW16_VBE_LINEAR     0x4000u

/*
 * Writes one key to the driver's hardware diagnostics file. Supplied by the
 * display16 side, which owns the section name and the path; the chip module
 * owns the key order, which is part of the diagnostic contract.
 */
typedef void (*v9x_hw16_write_fn)(const char *key, const char *value);

typedef struct v9x_hw16_ops {
    /* Family id, matching packaging\families\<id>. Diagnostic only. */
    const char *family_id;

    const V9X_HW16_DEVICE *devices;
    unsigned short device_count;

    const V9X_HW16_MODE *modes;
    unsigned short mode_count;

    /*
     * OR'd into BX for the VBE 4F02h mode set. The S3 BIOS wants the S3/VBE
     * no-clear bit; a generic card wants the linear-framebuffer bit.
     *
     * Known limit: the ViRGE/DX BIOS ignores the generic 0x4000 bit, which is
     * why the S3 families cannot simply use the tier-0 value.
     */
    unsigned short vbe_mode_flags;

    /*
     * Size of the DPMI physical mapping, as the page count DPMI 0800h wants
     * in SI:DI and 0008h wants in CX:DX. Every family maps the whole 64 MiB
     * PCI aperture today; a card with a smaller BAR sets less.
     */
    unsigned short map_pages_hi;
    unsigned short map_pages_lo;

    /*
     * Publish this family's C:\V9XHW.INI block. The caller has already
     * cleared the section.
     *
     * Not nullable today: every family describes itself. It stays a hook
     * rather than data because the S3 families read live registers - CR36 for
     * installed memory, SR10/SR11 for the PLL.
     */
    void (*publish_diagnostics)(const V9X_HW16_DEVICE *device,
                                v9x_hw16_write_fn write);

    /*
     * Runs immediately after the VBE 4F02h mode set and before the aperture is
     * read. Non-zero to continue. NULL when the mode set is sufficient, which
     * is the S3 and tier-0 case; the Millennium II forces its scan-line pitch
     * through 4F06h here and rejects a BIOS that picks a different stride.
     *
     * Failure reports stage code 9.
     */
    unsigned short (*post_mode_set)(void);

    /*
     * Returns the physical framebuffer aperture, or 0 if it cannot be trusted.
     *
     * NULL means ask the BIOS through VBE 4F01h, which is the chip-agnostic
     * answer and the one tier-0 will use. The S3 families read CR59/CR5A and
     * the Matrox family reads PCI BAR0, because neither card has a 4F01h that
     * can be relied on.
     *
     * Failure reports stage code 3.
     */
    unsigned long (*read_aperture)(void);

    /*
     * Enables linear addressing on the chip once the aperture is known.
     * Non-zero to continue. NULL when the VBE mode set already did everything,
     * which is the tier-0 and Millennium II case.
     *
     * Failure reports stage code 8.
     */
    unsigned short (*enable_aperture)(void);

    /*
     * Build the screen PDEVICE, returning non-zero on success.
     *
     * NULL uses the DIB Engine's CreateDIBPDevice, which is the proven S3
     * path. A family supplies its own only when its BIOS-selected scan-line
     * layout is not reconstructed reliably from the BITMAPINFO - the
     * Millennium II case, where every surface field is set explicitly so it
     * can be audited.
     */
    unsigned long (*build_screen_pdevice)(void *device_info,
                                          void *bitmap_info,
                                          const V9X_HW16_MODE *mode,
                                          unsigned short screen_selector,
                                          unsigned short pdevice_flags);
} V9X_HW16_OPS;

/* Defined once per family binary, in src\chipsets\<vendor>\<chip>\*_hw16.c. */
extern const V9X_HW16_OPS v9x_hw16;

#endif /* VELOCITY9X_HW16_H */
