/*
 * Tests for the diagnostic log roll-over rule (include\velocity9x\diag_log.h).
 *
 * The cases: a file under the limit grows; the line that would cross it
 * starts a new file instead of being split; an empty file takes even an
 * oversized line; and the rolled-over name is the log's own with OLD for
 * its extension, for each of the six logs the rule covers.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/diag_log.h"
#include "velocity9x/diagpaths.h"

static unsigned int diag_log_failures = 0u;

#define DLCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++diag_log_failures; \
    } \
} while (0)

static void diag_log_roll_tests(void)
{
    /* Under the limit, and exactly reaching it: appended. */
    DLCHECK(!v9x_diag_log_must_roll(0ul, 100ul, 1000ul));
    DLCHECK(!v9x_diag_log_must_roll(500ul, 100ul, 1000ul));
    DLCHECK(!v9x_diag_log_must_roll(900ul, 100ul, 1000ul));

    /* One byte past it: rolled, so the line is not split. */
    DLCHECK(v9x_diag_log_must_roll(901ul, 100ul, 1000ul));
    DLCHECK(v9x_diag_log_must_roll(1000ul, 1ul, 1000ul));

    /* Already past it (written before the rule, or by a writer that lost a
     * rename): rolled at the next line. */
    DLCHECK(v9x_diag_log_must_roll(67108864ul, 80ul, V9X_DIAG_LOG_LIMIT));

    /* An empty file takes the line whatever its length. */
    DLCHECK(!v9x_diag_log_must_roll(0ul, 5000ul, 1000ul));

    /* No wrap: a size near 4 GiB plus a line still reads as past. */
    DLCHECK(v9x_diag_log_must_roll(0xFFFFFF00ul, 0x200ul, 1000ul));
}

static void diag_log_path_tests(void)
{
    char out[64];

    DLCHECK(v9x_diag_log_old_path(V9X_DIAG_GL_LOG, out, sizeof(out)));
    DLCHECK(strcmp(out, "C:\\V9XDIAG\\V9XGL.OLD") == 0);
    DLCHECK(v9x_diag_log_old_path(V9X_DIAG_GLIDE_LOG, out, sizeof(out)));
    DLCHECK(strcmp(out, "C:\\V9XDIAG\\V9XGLIDE.OLD") == 0);
    DLCHECK(v9x_diag_log_old_path(V9X_DIAG_GLIDE3_LOG, out, sizeof(out)));
    DLCHECK(strcmp(out, "C:\\V9XDIAG\\V9XGLD3.OLD") == 0);
    DLCHECK(v9x_diag_log_old_path(V9X_DIAG_CIF_LOG, out, sizeof(out)));
    DLCHECK(strcmp(out, "C:\\V9XDIAG\\V9XCIF.OLD") == 0);
    DLCHECK(v9x_diag_log_old_path("C:\\V9XDIAG\\V9XD7.LOG", out, sizeof(out)));
    DLCHECK(strcmp(out, "C:\\V9XDIAG\\V9XD7.OLD") == 0);

    /* A dot in a directory name is not the extension. */
    DLCHECK(!v9x_diag_log_old_path("C:\\A.B\\LOGFILE", out, sizeof(out)));
    DLCHECK(out[0] == '\0');

    /* Already OLD: renaming it onto itself would lose it. */
    DLCHECK(!v9x_diag_log_old_path("C:\\V9XDIAG\\V9XGL.OLD", out, sizeof(out)));

    /* A short extension grows to three letters; it must still fit. */
    DLCHECK(v9x_diag_log_old_path("C:\\X.L", out, 9ul));
    DLCHECK(strcmp(out, "C:\\X.OLD") == 0);
    DLCHECK(!v9x_diag_log_old_path("C:\\X.L", out, 8ul));
    DLCHECK(out[0] == '\0');

    /* No room at all is refused, not written. */
    DLCHECK(!v9x_diag_log_old_path(V9X_DIAG_GL_LOG, out, 0ul));
}

unsigned int v9x_run_diag_log_tests(void)
{
    diag_log_roll_tests();
    diag_log_path_tests();
    return diag_log_failures;
}
