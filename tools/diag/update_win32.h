/*
 * Shared between V9XUPD.EXE's sources: update_win32.c (the dialogs, the
 * check and the download) and update_install_win32.c (finding the installed
 * driver, staging an update and checking it after the restart).
 */
#ifndef V9X_UPDATE_WIN32_H
#define V9X_UPDATE_WIN32_H

#include "velocity9x/update_inf.h"
#include "velocity9x/update_release.h"

#define V9X_DISPLAY_CLASS_KEY \
    "System\\CurrentControlSet\\Services\\Class\\Display"

/* The installed Velocity9x instance an update applies to. */
struct v9x_install {
    char instance[16];           /* "0001": Class\Display\0001 */
    char family[32];             /* its V9xFamily value */
    char inf_section[64];        /* its InfSection value */
    char device_id[96];          /* its MatchingDeviceId value */
    char inf_path[MAX_PATH];     /* the live OEM INF, from InfPath */
};

/* One file an update stages: where it goes, and what it must hash to. */
struct v9x_staged_file {
    char name[16];
    char staged[MAX_PATH];
    char target[MAX_PATH];
    BYTE sha256[32];
};

#define V9X_STAGED_MAX 16u

struct v9x_update_job {
    struct v9x_install install;
    struct v9x_release_info release;
    /* The package, verified against the signed SHA-256. */
    BYTE *zip;
    DWORD zip_length;
    /* The new INF and its plan for this install's section. */
    char *inf;
    DWORD inf_length;
    struct v9x_inf_plan *plan;
    struct v9x_staged_file files[V9X_STAGED_MAX];
    unsigned int file_count;
    char backup_dir[MAX_PATH];
    /* Why the last step failed, for the user. */
    char error[256];
};

/* update_win32.c: progress text from a worker thread. */
void v9x_progress_set(const char *text);

/* update_install_win32.c */
BOOL v9x_install_find(struct v9x_install *install, char *why, DWORD why_size);
BOOL v9x_install_prepare(struct v9x_update_job *job);
BOOL v9x_install_commit(struct v9x_update_job *job);
void v9x_install_finish(void);
void v9x_install_result(void);

#endif
