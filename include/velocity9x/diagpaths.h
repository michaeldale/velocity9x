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
#define V9X_DIAG_INTELMM_TXT V9X_DIAG_PATH("INTELMM.TXT")   /* intel_diag16.c */
#define V9X_DIAG_INTELGTT_BIN V9X_DIAG_PATH("INTELGTT.BIN") /* intel_gtt16.c */
#define V9X_DIAG_INTELGTT_TXT V9X_DIAG_PATH("INTELGTT.TXT") /* intel_gtt16.c */
#define V9X_DIAG_INTELEVT_TXT V9X_DIAG_PATH("INTELEVT.TXT") /* intel_event16.c */
#define V9X_DIAG_INTELRNG_TXT V9X_DIAG_PATH("INTELRNG.TXT") /* intel_ring16.c */
/*
 * Phase 4 arm state. Deliberately not SYSTEM.INI: writing the file GDI is
 * reading in order to load this very driver stopped DriverInit returning on
 * the netbook (2026-09-13), and the driver has no evidence it can write
 * SYSTEM.INI at any point. It has written the diagnostic directory at every
 * load for weeks, so the one-shot transaction lives here instead.
 */
#define V9X_DIAG_INTELARM_TXT V9X_DIAG_PATH("INTELARM.TXT") /* intel_boot16.c, intel_exec16.c */
#define V9X_DIAG_INTEL3D0_TXT V9X_DIAG_PATH("INTEL3D0.TXT") /* intel_3d16.c */

/* 32-bit DirectDraw HAL. */
#define V9X_DIAG_TRACE_INI   V9X_DIAG_PATH("V9XTRACE.INI")  /* ddhal_core.c */

/* Win32 diagnostics (tools\diag). */
#define V9X_DIAG_DD_INI      V9X_DIAG_PATH("V9XDD.INI")     /* ddraw_probe_win32.c */
#define V9X_DIAG_SOFTBENCH_INI V9X_DIAG_PATH("V9XSOFT.INI") /* software_bench_win32.c */
#define V9X_DIAG_SNAP_INI    V9X_DIAG_PATH("V9XSNAP.INI")   /* d3d_trace_dump_win32.c */
/* One presented frame, as a binary PPM, written by the 32-bit HAL itself -
 * the dump tool cannot read video memory. Once per session: the counters
 * beside it are colour differences and only the image says what the scene
 * actually looks like. See i9xx_scanout.c. */
#define V9X_DIAG_FRAME_PPM   V9X_DIAG_PATH("V9XFRAME.PPM")  /* i9xx_scanout.c */
/* The replacement is built here and moved over the one above only once it is
 * complete. Opening the real file with CREATE_ALWAYS truncated a good image
 * before the new one was known to be writable, so a failure part-way through
 * destroyed the capture its own metadata still described as valid. */
#define V9X_DIAG_FRAME_TMP   V9X_DIAG_PATH("V9XFRAME.TMP")  /* i9xx_scanout.c */
#define V9X_DIAG_MSW_INI     V9X_DIAG_PATH("V9XMSW.INI")    /* mode_switch_win32.c */
#define V9X_DIAG_PWR_INI     V9X_DIAG_PATH("V9XPWR.INI")    /* power_cycle_win32.c */
#define V9X_DIAG_PAL_INI     V9X_DIAG_PATH("V9XPAL.INI")    /* palette_smoke_win32.c */
#define V9X_DIAG_SURF_INI    V9X_DIAG_PATH("V9XSURF.INI")   /* surface_step_win32.c */
#define V9X_DIAG_WND_INI     V9X_DIAG_PATH("V9XWND.INI")    /* window_list_win32.c */
#define V9X_DIAG_DOSBOX_INI  V9X_DIAG_PATH("V9XDOSBX.INI")  /* dos_box_test_win32.c */
#define V9X_DIAG_MGA_INI     V9X_DIAG_PATH("V9XMGA.INI")    /* matrox_inventory / mmio_query */
#define V9X_DIAG_MGAMM_INI   V9X_DIAG_PATH("V9XMGAMM.INI")  /* matrox_mmio_query_win32.c */
#define V9X_DIAG_GDI_INI     V9X_DIAG_PATH("V9XGDI.INI")    /* gdi_smoke_win32.c */
#define V9X_DIAG_TEXT_INI    V9X_DIAG_PATH("V9XTEXT.INI")   /* gdi_smoke_win32.c /textdump */
#define V9X_DIAG_IOTR_INI    V9X_DIAG_PATH("V9XIOTR.INI")   /* io_trace_win32.c */
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
