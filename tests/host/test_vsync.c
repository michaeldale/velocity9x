/*
 * Tests for the vsync setting.
 *
 * The property worth a table is that nothing but an explicit request for
 * off turns vsync off: not an absent key, not a typo, not a value from a
 * later build. A flip that tears because of a stray character in SYSTEM.INI
 * would look like a driver defect and be reported as one.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/vsync.h"

static unsigned int vsync_failures = 0u;

#define VSCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++vsync_failures; \
    } \
} while (0)

static void test_known_requests_resolve_to_themselves(void)
{
    VSCHECK(v9x_vsync_resolve(V9X_VSYNC_REQUEST_APPLICATION) ==
            V9X_VSYNC_STATE_APPLICATION);
    VSCHECK(v9x_vsync_resolve(V9X_VSYNC_REQUEST_ON) == V9X_VSYNC_STATE_ON);
    VSCHECK(v9x_vsync_resolve(V9X_VSYNC_REQUEST_OFF) == V9X_VSYNC_STATE_OFF);
}

/* GetPrivateProfileInt returns 0 for text it cannot parse, and whatever
 * number a later build or a hand edit put there otherwise. */
static void test_unknown_requests_leave_the_application_in_charge(void)
{
    VSCHECK(v9x_vsync_resolve((v9x_u16)3u) == V9X_VSYNC_STATE_APPLICATION);
    VSCHECK(v9x_vsync_resolve((v9x_u16)9u) == V9X_VSYNC_STATE_APPLICATION);
    VSCHECK(v9x_vsync_resolve((v9x_u16)0xffffu) ==
            V9X_VSYNC_STATE_APPLICATION);
}

static void test_the_flip_table(void)
{
    VSCHECK(v9x_vsync_flip_novsync(V9X_VSYNC_STATE_APPLICATION, V9X_FALSE) ==
            V9X_FALSE);
    VSCHECK(v9x_vsync_flip_novsync(V9X_VSYNC_STATE_APPLICATION, V9X_TRUE) ==
            V9X_TRUE);
    VSCHECK(v9x_vsync_flip_novsync(V9X_VSYNC_STATE_ON, V9X_FALSE) ==
            V9X_FALSE);
    VSCHECK(v9x_vsync_flip_novsync(V9X_VSYNC_STATE_ON, V9X_TRUE) ==
            V9X_FALSE);
    VSCHECK(v9x_vsync_flip_novsync(V9X_VSYNC_STATE_OFF, V9X_FALSE) ==
            V9X_TRUE);
    VSCHECK(v9x_vsync_flip_novsync(V9X_VSYNC_STATE_OFF, V9X_TRUE) ==
            V9X_TRUE);
}

/* A state the HAL could only get from a corrupt word behaves as the
 * application asked, never as off. */
static void test_an_unknown_state_follows_the_application(void)
{
    VSCHECK(v9x_vsync_flip_novsync((v9x_u16)7u, V9X_FALSE) == V9X_FALSE);
    VSCHECK(v9x_vsync_flip_novsync((v9x_u16)7u, V9X_TRUE) == V9X_TRUE);
}

/* settings_status.c compares against these spellings. */
static void test_state_text_is_stable(void)
{
    VSCHECK(strcmp(v9x_vsync_text(V9X_VSYNC_STATE_APPLICATION),
                   "application") == 0);
    VSCHECK(strcmp(v9x_vsync_text(V9X_VSYNC_STATE_ON), "always-on") == 0);
    VSCHECK(strcmp(v9x_vsync_text(V9X_VSYNC_STATE_OFF), "always-off") == 0);
    VSCHECK(strcmp(v9x_vsync_text((v9x_u16)7u), "application") == 0);
}

unsigned int v9x_run_vsync_tests(void)
{
    test_known_requests_resolve_to_themselves();
    test_unknown_requests_leave_the_application_in_charge();
    test_the_flip_table();
    test_an_unknown_state_follows_the_application();
    test_state_text_is_stable();
    return vsync_failures;
}
