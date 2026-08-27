/*
 * Velocity9x diagnostic file paths.
 *
 * Every diagnostic the driver or a tool leaves behind lands in one directory,
 * C:\V9XDIAG, instead of the sixteen-plus root files the first hardware run
 * collected (docs\issues\2026-08-27-netbook-gma950-findings.md item 3). One
 * header defines them all, included by the 16-bit driver, the 32-bit HAL, the
 * Win32 diagnostics and the DOS tools alike, so a file's writer and its
 * readers can never drift apart - settings_status.c alone reads five of these
 * back, and a reader left on the old path would silently report nothing.
 *
 * WritePrivateProfileString does not create directories and fails silently
 * into a missing one, so every writer must ensure the directory exists before
 * its first write:
 *
 *   - 16-bit driver: call V9xEnsureDiagDir() (runtime.asm, INT 21h AH=39h,
 *     guarded so it runs once).
 *   - 32-bit HAL and Win32 tools: CreateDirectoryA(V9X_DIAG_DIR, 0) and
 *     ignore ERROR_ALREADY_EXISTS.
 *   - DOS tools: INT 21h AH=39h, ignoring errors 3 and 5.
 *
 * No <windows.h>, no includes: string literals only, so this header is legal
 * everywhere from the wcc -mc driver to a real-mode DOS tool.
 */
#ifndef VELOCITY9X_DIAGPATHS_H
#define VELOCITY9X_DIAGPATHS_H

/* No trailing backslash: this is the spelling CreateDirectoryA and INT 21h
 * AH=39h want. V9X_DIAG_PATH supplies the separator. */
#define V9X_DIAG_DIR "C:\\V9XDIAG"

#define V9X_DIAG_PATH(name) V9X_DIAG_DIR "\\" name

/* 16-bit display driver. */
#define V9X_DIAG_BOOT_INI    V9X_DIAG_PATH("V9XBOOT.INI")   /* enable16.c, ddi.c */
#define V9X_DIAG_HW_INI      V9X_DIAG_PATH("V9XHW.INI")     /* ddi.c, gdi_accel.c */
#define V9X_DIAG_MODES_INI   V9X_DIAG_PATH("V9XMODES.INI")  /* modes16.c */
#define V9X_DIAG_DDHOOK_INI  V9X_DIAG_PATH("V9XDDH.INI")    /* dd16.c */

/* 32-bit DirectDraw HAL. */
#define V9X_DIAG_TRACE_INI   V9X_DIAG_PATH("V9XTRACE.INI")  /* ddhal_core.c */

/* Win32 diagnostics (tools\diag). */
#define V9X_DIAG_DD_INI      V9X_DIAG_PATH("V9XDD.INI")     /* ddraw_probe_win32.c */
#define V9X_DIAG_SNAP_INI    V9X_DIAG_PATH("V9XSNAP.INI")   /* d3d_trace_dump_win32.c */
#define V9X_DIAG_MSW_INI     V9X_DIAG_PATH("V9XMSW.INI")    /* mode_switch_win32.c */
#define V9X_DIAG_PWR_INI     V9X_DIAG_PATH("V9XPWR.INI")    /* power_cycle_win32.c */
#define V9X_DIAG_PAL_INI     V9X_DIAG_PATH("V9XPAL.INI")    /* palette_smoke_win32.c */
#define V9X_DIAG_SURF_INI    V9X_DIAG_PATH("V9XSURF.INI")   /* surface_step_win32.c */
#define V9X_DIAG_WND_INI     V9X_DIAG_PATH("V9XWND.INI")    /* window_list_win32.c */
#define V9X_DIAG_MGA_INI     V9X_DIAG_PATH("V9XMGA.INI")    /* matrox_inventory / mmio_query */
#define V9X_DIAG_MGAMM_INI   V9X_DIAG_PATH("V9XMGAMM.INI")  /* matrox_mmio_query_win32.c */
#define V9X_DIAG_GDI_INI     V9X_DIAG_PATH("V9XGDI.INI")    /* gdi_smoke_win32.c */
#define V9X_DIAG_ACCEL_INI   V9X_DIAG_PATH("V9XACCE.INI")   /* gdi_smoke_win32.c */
#define V9X_DIAG_SYNC_INI    V9X_DIAG_PATH("V9XSYNC.INI")   /* settings_syncmodes.c */
#define V9X_DIAG_TC32_INI    V9X_DIAG_PATH("V9XTC32.INI")   /* trio_ctx_probe.c (Win32 arm) */
#define V9X_DIAG_TC16_INI    V9X_DIAG_PATH("V9XTC16.INI")   /* trio_ctx_probe.c (Win16 arm) */

/* DOS tools. Defaults only: each accepts /out: to redirect, which stays the
 * recovery route when C: is absent or read-only (vga_survey_dos.c prints it
 * as the advice when the default path fails). */
#define V9X_DIAG_SURV_INI    V9X_DIAG_PATH("V9XSURV.INI")   /* vga_survey_dos.c */
#define V9X_DIAG_VBE_TXT     V9X_DIAG_PATH("V9XVBE.TXT")    /* vbe_inventory_dos.c */
#define V9X_DIAG_APER_INI    V9X_DIAG_PATH("V9XAPER.INI")   /* vlb_aperture_dos.c */

#endif /* VELOCITY9X_DIAGPATHS_H */
