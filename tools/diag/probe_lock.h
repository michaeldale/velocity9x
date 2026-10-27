/*
 * One release probe at a time.
 *
 * Two probes running together share one engine and one HAL, and the second
 * reports the first's interference as its own failures: A8U4I5's 0.32.0
 * V9XGLIDP said "FAIL failures=2" because it started while V9XGLP's report
 * still read Result=RUNNING, and passed when run alone
 * (docs\decisions\2026-10-27-0.32.0-release-checks-ati-ironlake.md).
 *
 * Each probe takes this named mutex before touching DirectDraw or a
 * driver, and answers Result=BUSY without running when another holds it.
 * The mutex is the kernel's: a probe that crashes or is killed leaves it
 * abandoned, which the next taker gets with WAIT_ABANDONED and may use, so
 * a dead probe never locks the others out. Held to process exit; Win9x
 * releases it then.
 *
 * Header-only and static, as the probes are single-file programs.
 */
#ifndef VELOCITY9X_PROBE_LOCK_H
#define VELOCITY9X_PROBE_LOCK_H

#include <windows.h>

#define V9X_PROBE_LOCK_NAME "Velocity9xReleaseProbe"

/* 1 when this process now holds the lock, 0 when another probe does. A
 * mutex that cannot be created at all answers 1: the probe runs as it did
 * before the lock existed rather than refusing for a reason of its own. */
static int v9x_probe_lock_take(void)
{
    HANDLE lock = CreateMutexA(0, FALSE, V9X_PROBE_LOCK_NAME);
    DWORD waited;

    if (lock == 0) {
        return 1;
    }
    waited = WaitForSingleObject(lock, 0ul);
    if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) {
        return 1;
    }
    CloseHandle(lock);
    return 0;
}

#endif
