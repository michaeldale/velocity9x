/*
 * Win16 NE global initializer for the active DIB Engine display driver.
 * Windows 9x display DRVs use a DriverInit entry point rather than the
 * ordinary per-instance LibMain convention used by general Win16 DLLs.
 * windows.h is supplied by the external Open Watcom toolchain.
 */
#include <windows.h>

#include "velocity9x/build.h"
#include "velocity9x/components.h"
#ifdef V9X_INTEL_GMA_FAMILY
/* v9x_intel_boot_arm_prepare lives in the Intel family's second code segment,
 * so the call below is far. This is the only reference to a moved Intel unit
 * anywhere outside the Intel files. */
#include "velocity9x/intel16.h"
#endif

static struct v9x_logger v9x_display_logger;
static struct v9x_backend_state v9x_display_backend;
static struct v9x_component_state v9x_display_component;
static const struct v9x_build_identity *v9x_display_build_identity;

extern void v9x_display_boot_log(void);
extern void v9x_display_boot_mark(const char FAR *stage);
extern void v9x_display_boot_note(const char FAR *key, const char FAR *value);

/*
 * How many times Windows has entered DriverInit this boot.
 *
 * Measured 2026-09-15: the netbook reached `arm-post` and then GDI never
 * called Enable at all, which means the driver loaded, initialised, and was
 * rejected. The one way this function reports failure is
 * v9x_display16_start returning V9X_STATUS_INVALID_STATE, and the only thing
 * that produces that is a second entry into an already-started component -
 * so whether this is called once or twice separates "rejected because it said
 * no" from "rejected for a reason outside this file".
 */
static WORD v9x_driverinit_calls;

/* The display-driver loader supplies heap size in CX, module handle in DI,
 * and the command line in ES:SI. This is the entry contract used by the
 * Windows 98 DDK display samples and vmdisp9x. */
#pragma aux DriverInit parm [cx] [di] [es si]

#pragma off (unreferenced)
UINT FAR DriverInit(UINT heap_size,
                    UINT module,
                    LPSTR command_line)
#pragma on (unreferenced)
{
    v9x_status started;

    /*
     * The boot trace goes first. Measured on the netbook 2026-09-13: running
     * the Intel arm transaction ahead of it left no trace at all when
     * DriverInit failed, which cost a boot to diagnose. With this order a
     * `libmain` marker on disk and nothing after it names the arm transaction
     * as the failure, exactly as an absent marker names this function.
     */
    v9x_display_boot_log();
    /*
     * After the boot log, so the diagnostic directory and the `libmain` marker
     * are already on disk, and in its own key so a second entry cannot
     * overwrite the evidence of the first.
     */
    ++v9x_driverinit_calls;
    {
        char count[4];

        count[0] = (char)('0' + (char)(v9x_driverinit_calls % 10u));
        count[1] = '\0';
        v9x_display_boot_note("DriverInitCall", count);
    }
#ifdef V9X_INTEL_GMA_FAMILY
    /*
     * Bracket the one far call this function makes into I9XXCODE.
     *
     * `arm-pre` is written from _TEXT, `arm-in` from inside the second code
     * segment as arm_prepare's first statement, and `arm-post` from _TEXT
     * after it returns. Which of the three is last on disk names the failing
     * transition directly instead of leaving it to be deduced:
     *
     *   libmain, no arm-pre   - died in _TEXT before the call site
     *   arm-pre, no arm-in    - the inbound far call into I9XXCODE
     *   arm-in, no arm-dir    - the outbound far call to V9xEnsureDiagDir
     *   arm-dir, no arm-post  - inside arm_prepare's own body
     *   arm-post              - arm_prepare returned; the fault is later
     *
     * Three extra profile writes on one boot, only in a trace build.
     */
    v9x_display_boot_mark("arm-pre");
    v9x_intel_boot_arm_prepare();
    v9x_display_boot_mark("arm-post");
#endif
    v9x_display_build_identity = v9x_get_build_identity();
    v9x_log_init(&v9x_display_logger, 0, 0);
    started = v9x_display16_start(&v9x_display_component,
                                  &v9x_display_logger,
                                  &v9x_display_backend);
    /*
     * The return value, recorded rather than inferred. A driver that fails
     * here is not asked for Enable, so from the outside it looks identical to
     * one that was never loaded - and the boot trace would stop at the same
     * place either way.
     */
    v9x_display_boot_note("DriverInitResult",
                          started == V9X_STATUS_OK ? "ok" : "refused");
    return started == V9X_STATUS_OK;
}
