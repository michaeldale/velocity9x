/*
 * Dialog and control identifiers for V9XUPD.EXE, shared by update_win32.c
 * and update.rc.
 */
#ifndef V9X_UPDATE_RESOURCE_H
#define V9X_UPDATE_RESOURCE_H

/* Send a report: the files, the privacy summary, a description. */
#define V9X_UPD_DLG_REPORT    100
/* One line of progress while a worker thread talks to the server. */
#define V9X_UPD_DLG_PROGRESS  101
/* The report code, with Copy. */
#define V9X_UPD_DLG_SENT      102
/* A newer release: versions, release notes, Update now / Cancel. */
#define V9X_UPD_DLG_UPDATE    103

#define V9X_UPD_IDC_FILES       1001
#define V9X_UPD_IDC_TOTAL       1002
#define V9X_UPD_IDC_DESCRIPTION 1003
#define V9X_UPD_IDC_STATUS      1004
#define V9X_UPD_IDC_CODE        1005
#define V9X_UPD_IDC_MESSAGE     1006
#define V9X_UPD_IDC_COPY        1007
#define V9X_UPD_IDC_HEADLINE    1008
#define V9X_UPD_IDC_NOTES       1009

#endif
