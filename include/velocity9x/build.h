#ifndef VELOCITY9X_BUILD_H
#define VELOCITY9X_BUILD_H

#include "velocity9x/types.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "dev-unversioned"
#endif

/*
 * The single source of the product version. scripts\common.ps1 parses
 * V9X_VERSION_STRING out of this header, so the package manifest, the floppy
 * README, packages.json and the release zip name all follow from here, as do
 * the settings page and V9XSET.EXE, which compile it in directly.
 *
 * Keep the string and the three numbers in step: the host test asserts the
 * build identity carries them.
 */
#define V9X_VERSION_MAJOR 0u
#define V9X_VERSION_MINOR 4u
#define V9X_VERSION_PATCH 3u
#define V9X_VERSION_STRING "0.4.3"

struct v9x_build_identity {
    v9x_u16 major;
    v9x_u16 minor;
    v9x_u16 patch;
    const char *build_id;
};

const struct v9x_build_identity *v9x_get_build_identity(void);

#endif
