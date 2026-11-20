/*
 * Shared control identifiers for the Velocity9x Display Properties page.
 * Included by both the C implementation and the resource script.
 */
#ifndef V9X_SETTINGS_PROPSHEET_H
#define V9X_SETTINGS_PROPSHEET_H

#define V9X_ID_LOGO_BITMAP     101
#define V9X_ID_PAGE_DIALOG    2000
/* The second page: every control that writes SYSTEM.INI. */
#define V9X_ID_PAGE_ADVANCED  2100
#define V9X_IDC_ADAPTER       2001
#define V9X_IDC_FRAMEBUFFER   2009
#define V9X_IDC_COPY_REPORT   2011
#define V9X_IDC_VERSION       2013
#define V9X_IDC_PCI_ID        2014
#define V9X_IDC_VIDEO_MEMORY  2015
#define V9X_IDC_RENDERING     2016
#define V9X_IDC_DIRECTDRAW    2017
#define V9X_IDC_DIRECT3D      2018
#define V9X_IDC_MODE_SWITCH   2019
/* The Direct3D selector: Hardware, Software or Disabled ([Velocity9x]
 * Direct3D). */
#define V9X_IDC_DIRECT3D_MODE 2020
/* The 16-bit colour layout selector: Automatic, 5:6:5 or 5:5:5. */
#define V9X_IDC_COLOUR_LAYOUT 2021
/* The vertical sync selector: Game decides, Always on or Always off. */
#define V9X_IDC_VSYNC         2022
/* The DDI 6 selector: Automatic, Never or Always ([Velocity9x]
 * Direct3DDdi absent, 5 or 6). */
#define V9X_IDC_DDI           2023
/* The 2026-10-09 layout's rows. */
#define V9X_IDC_REVISION        2024
#define V9X_IDC_MINIVDD         2026
#define V9X_IDC_RESOLUTION      2028
#define V9X_IDC_COLOUR_DEPTH    2029
#define V9X_IDC_REFRESH         2030
#define V9X_IDC_ABOUT           2032
/* The Advanced tab's clock rows, from the driver's clock detector. */
#define V9X_IDC_CORE_CLOCK      2036
#define V9X_IDC_MEMORY_CLOCK    2037
/* 2033 and 2034 were Run diagnostics and Texture filtering, disabled
 * placeholders removed on 2026-11-21. Not reused. */
/* The Advanced tab's write-combining state text, beside its switch. */
#define V9X_IDC_WRITE_COMBINE   2035
/* The two buttons that start V9XUPD.EXE: Send report... on the Advanced
 * tab, Check for updates... on the Velocity9x tab. */
#define V9X_IDC_SEND_REPORT     2038
#define V9X_IDC_CHECK_UPDATES   2039
/* The Velocity9x tab's row for the Direct3D interface the driver last
 * stamped (replaced the GDI and DirectDraw test rows, which the report
 * keeps). */
#define V9X_IDC_DDI_STATE       2040
/* The Advanced tab's controls for keys the driver already read (2026-11-21):
 * the per-program interface button, D3DSoftSysMem, GdiAccel, and the
 * NOWC.TXT switch. */
#define V9X_IDC_DDI_PROGRAMS          2041
#define V9X_IDC_SOFT_SYSMEM           2042
#define V9X_IDC_GDI_ACCEL             2043
#define V9X_IDC_WRITE_COMBINE_SWITCH  2044
/* The per-program interface dialog and its controls. */
#define V9X_ID_DDI_PROGRAMS   2200
#define V9X_IDC_PROG_LIST     2201
#define V9X_IDC_PROG_NAME     2202
#define V9X_IDC_PROG_LEVEL    2203
#define V9X_IDC_PROG_ADD      2204
#define V9X_IDC_PROG_REMOVE   2205

#endif
