/*
 * Who produced a trace snapshot's counters: the builds that were running,
 * when this boot's shared block was made, and which programs drew.
 *
 * A snapshot from a user's machine used to name only the build of the tool
 * that read it. marxveix's Rage archive (2026-10-05) had to be attributed by
 * guesswork: which driver build wrote the counters, whether two files came
 * from one boot - the counters are cumulative per boot, so only same-boot
 * snapshots subtract - and which game ran, since nothing recorded a program
 * name anywhere. These fields answer those three questions from the file.
 *
 * The struct lives in the shared block (win9x_ddraw_abi.h) and is copied
 * whole into V9X_DD_TRACE_SNAPSHOT. It is written by both sides - the 16-bit
 * driver stamps its build and the clock when it creates the block, the HAL
 * its build and the process table - so it uses only fixed-width types that
 * lay out the same in both memory models.
 *
 * The process table's rules are here rather than in the HAL because they
 * are pure logic, and the HAL cannot be tested on the host.
 */
#ifndef VELOCITY9X_DIAG_IDENTITY_H
#define VELOCITY9X_DIAG_IDENTITY_H

#include "velocity9x/types.h"

/* "V9XHAL build=" plus a 7-digit hash and a suffix fits; a longer id is cut,
 * which still identifies the commit. */
#define V9X_DIAG_BUILD_BYTES        32u
/* An 8.3 name with its NUL is 13; a long file name is cut. */
#define V9X_DIAG_PROCESS_NAME_BYTES 16u
/* More than one sitting runs; past this the sightings are only counted. */
#define V9X_DIAG_PROCESS_SLOTS      8u

#define V9X_DIAG_PROCESS_D3D 0u
#define V9X_DIAG_PROCESS_GL  1u

struct v9x_diag_process {
    /* Executable base name, NUL-terminated, the rest of the array zero. */
    char name[V9X_DIAG_PROCESS_NAME_BYTES];
    /* Direct3D contexts the process created. */
    v9x_u32 d3d_contexts;
    /* Render-interface describes: one per OpenGL session, plus one per
     * redescribe after a mode change. */
    v9x_u32 gl_describes;
    /* Milliseconds since Windows started when it was first seen, against
     * which the snapshot's DumpUptimeMs orders it. */
    v9x_u32 first_uptime_ms;
};

struct v9x_diag_identity {
    /* The 16-bit driver's V9X_BUILD_ID, stamped when it creates the block. */
    char driver_build[V9X_DIAG_BUILD_BYTES];
    /* The HAL's, stamped at every DriverInit: a HAL replaced without a
     * reboot shows here while driver_build keeps the old one. */
    char hal_build[V9X_DIAG_BUILD_BYTES];
    /*
     * The DOS clock when the 16-bit driver created the shared block, once
     * per boot: year << 16 | month << 8 | day, and hour << 24 |
     * minute << 16 | second << 8 | hundredths. Two snapshots with the same
     * stamp are from one boot whatever the clock says - marxveix's reads
     * 2005 - and only those subtract.
     */
    v9x_u32 block_date;
    v9x_u32 block_time;
    /* Distinct processes in processes[]. */
    v9x_u32 process_count;
    /* Sightings that found the table full, unattributed. */
    v9x_u32 process_unrecorded;
    struct v9x_diag_process processes[V9X_DIAG_PROCESS_SLOTS];
};

/*
 * Copy `source` into `out`, at most `bytes` - 1 characters, and zero the
 * rest of `out` so the shared block holds no stale tail.
 */
void v9x_diag_copy_text(char *out, const char *source, v9x_u32 bytes);

/*
 * The file name of `path` with its directory removed - after the last '\\',
 * '/' or ':' - copied as v9x_diag_copy_text does.
 */
void v9x_diag_base_name(char *out, const char *path, v9x_u32 bytes);

/*
 * Count one `kind` sighting of the process called `name` (a base name), at
 * `uptime_ms`. The name matches case-insensitively, because Windows 98
 * reports one program's path in either case depending on how it was
 * started. A new name takes the next free slot; with none free the sighting
 * is counted in process_unrecorded. An empty name is not recorded at all.
 */
void v9x_diag_note_process(struct v9x_diag_identity *identity,
                           const char *name, v9x_u32 kind,
                           v9x_u32 uptime_ms);

#endif /* VELOCITY9X_DIAG_IDENTITY_H */
