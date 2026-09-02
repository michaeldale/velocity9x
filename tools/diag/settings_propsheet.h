/*
 * Shared control identifiers for the Velocity9x Display Properties page.
 * Included by both the C implementation and the resource script.
 */
#ifndef V9X_SETTINGS_PROPSHEET_H
#define V9X_SETTINGS_PROPSHEET_H

#define V9X_ID_LOGO_BITMAP     101
#define V9X_ID_PAGE_DIALOG    2000
#define V9X_IDC_ADAPTER       2001
#define V9X_IDC_ACTIVE_MODE   2002
#define V9X_IDC_CORE_CLOCK    2003
#define V9X_IDC_BUILD         2008
#define V9X_IDC_FRAMEBUFFER   2009
#define V9X_IDC_GDI_TEST      2010
#define V9X_IDC_COPY_REPORT   2011
#define V9X_IDC_NOTICE        2012
#define V9X_IDC_VERSION       2013
#define V9X_IDC_PCI_ID        2014
#define V9X_IDC_VIDEO_MEMORY  2015
#define V9X_IDC_RENDERING     2016
#define V9X_IDC_DIRECTDRAW    2017
#define V9X_IDC_DIRECT3D      2018
#define V9X_IDC_MODE_SWITCH   2019
/* The Direct3D selector. The only control on this page that changes
 * anything; every other row is a statement of fact. */
#define V9X_IDC_DIRECT3D_MODE 2020
/* The 16-bit colour layout selector: Automatic, 5:6:5 or 5:5:5. The second
 * control that changes anything. */
#define V9X_IDC_COLOUR_LAYOUT 2021

#endif
