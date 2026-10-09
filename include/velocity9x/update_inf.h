/*
 * The subset of a Win9x INF that V9XUPD.EXE applies itself.
 *
 * An update installs the new package the way SetupX would for the model
 * already installed, without Device Manager
 * (docs\plans\optional-update-checker-and-auto-updater.md): the install
 * section named by the driver key's InfSection value, its CopyFiles,
 * DelReg and AddReg lists, in that order (DelReg before AddReg, as SetupX
 * does - our own INF deletes DEFAULT and MODES and then writes them back).
 * LogConfig is the device's resource list, which an update does not change,
 * so it is accepted and skipped.
 *
 * The subset is exactly what scripts\lib\inf.ps1 generates, and nothing
 * else is guessed at: any other directive, root, flag, destination, string
 * substitution or over-long field makes the whole plan fail, and the
 * updater then refuses a direct update. build-active-package.ps1 plans
 * every model of every generated INF with this code, so a generator change
 * outside the subset fails the build rather than a user's update.
 *
 * Pure text handling: no Win32, no C library.
 */
#ifndef VELOCITY9X_UPDATE_INF_H
#define VELOCITY9X_UPDATE_INF_H

#include "velocity9x/types.h"

#define V9X_INF_OP_COPY       ((v9x_u16)1u)  /* name = file, flags */
#define V9X_INF_OP_DEL_KEY    ((v9x_u16)2u)  /* root, key and everything below */
#define V9X_INF_OP_DEL_VALUE  ((v9x_u16)3u)  /* root, key, name */
#define V9X_INF_OP_SET_STRING ((v9x_u16)4u)  /* root, key, name, data (REG_SZ) */

/* HKR is the device's driver key, Class\Display\NNNN. */
#define V9X_INF_ROOT_NONE ((v9x_u16)0u)
#define V9X_INF_ROOT_HKR  ((v9x_u16)1u)
#define V9X_INF_ROOT_HKLM ((v9x_u16)2u)
#define V9X_INF_ROOT_HKCR ((v9x_u16)3u)

/* CopyFiles flags the generator uses. 12 is COPYFLG_NOVERSIONCHECK (0x4)
 * plus the in-use flag (0x8): always replace. 40 is
 * COPYFLG_NO_VERSION_DIALOG (0x20) plus 0x8: do not replace a newer file -
 * GLIDE2X.DLL, so a 3dfx card's own survives. */
#define V9X_INF_COPY_ALWAYS     12u
#define V9X_INF_COPY_IF_NEWER   40u

#define V9X_INF_KEY_MAX  160u
#define V9X_INF_NAME_MAX 64u
#define V9X_INF_DATA_MAX 160u
#define V9X_INF_OPS_MAX  256u

struct v9x_inf_op {
    v9x_u16 kind;
    v9x_u16 root;
    v9x_u16 copy_flags;
    char key[V9X_INF_KEY_MAX];
    /* A value name ("" for the key's default value), or the file name of a
     * copy. */
    char name[V9X_INF_NAME_MAX];
    char data[V9X_INF_DATA_MAX];
};

struct v9x_inf_plan {
    struct v9x_inf_op ops[V9X_INF_OPS_MAX];
    v9x_u32 count;
    v9x_u32 copies;
    /* Why a plan failed, for the user and the log. */
    char error[96];
};

/*
 * The install section the new INF offers for this device, the way SetupX
 * chooses one: the model in [Manufacturer]'s models section whose ID list
 * names device_id (the driver key's MatchingDeviceId), compared without
 * case. Section names change between releases - A8U4I5's Matrox key said
 * Velocity9x.Install, which the 2026-10-09 INF calls
 * V9x.Install.mga2064w - so the old InfSection is only the fallback, used
 * when no model lists the ID (a manual-select install) and the INF still
 * has a section of that name. V9X_FALSE when neither finds one.
 */
v9x_u16 v9x_inf_find_section(const char *text, v9x_u32 length,
                             const char *device_id,
                             const char *old_section,
                             char *section, v9x_u32 capacity);

/* Plan the install section of the INF text. V9X_FALSE, with plan->error
 * set, for anything outside the subset above. */
v9x_u16 v9x_inf_plan(const char *text, v9x_u32 length,
                     const char *install_section,
                     struct v9x_inf_plan *plan);

#endif
