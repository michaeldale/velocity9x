/*
 * Tests for the Direct3D back-end decision.
 *
 * Every row of the resolve table is here, both values of chip_has_d3d, and
 * the two properties that matter more than any single row: exactly one state
 * advertises Direct3D, and no request can make a chip without a 3D engine
 * advertise one.
 *
 * That second property is the one worth a test rather than a comment. This
 * driver has already shipped Direct3D capabilities it did not implement -
 * depth testing was published complete and drew in submission order for weeks
 * while every HRESULT reported success - and a mode selector is four more
 * chances to do it. A settings key that could turn an advertisement on is
 * exactly that shape of defect, so it is held to a table.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/d3dmode.h"

static unsigned int d3dmode_failures = 0u;

#define D3CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++d3dmode_failures; \
    } \
} while (0)

/*
 * The ViRGE column: a chip whose engine descriptor claims D3D.
 */
static void test_resolve_on_a_chip_with_3d(void)
{
    D3CHECK(v9x_d3d_mode_resolve(V9X_D3D_REQUEST_HARDWARE, V9X_TRUE) ==
            V9X_D3D_STATE_HARDWARE);
    D3CHECK(v9x_d3d_mode_resolve(V9X_D3D_REQUEST_DISABLED, V9X_TRUE) ==
            V9X_D3D_STATE_DISABLED);
    /* Software resolves to itself even on a chip that has a 3D engine: the
     * point of asking for it is to get the rasterizer instead. */
    D3CHECK(v9x_d3d_mode_resolve(V9X_D3D_REQUEST_SOFTWARE, V9X_TRUE) ==
            V9X_D3D_STATE_SOFTWARE);
    D3CHECK(v9x_d3d_mode_resolve(V9X_D3D_REQUEST_HYBRID, V9X_TRUE) ==
            V9X_D3D_STATE_UNIMPLEMENTED);
    D3CHECK(v9x_d3d_mode_resolve(V9X_D3D_REQUEST_OFFLOAD, V9X_TRUE) ==
            V9X_D3D_STATE_UNIMPLEMENTED);
}

/*
 * The Trio64, ATI, Matrox and tier-0 column: no D3D in the descriptor, and on
 * four of the six families no engine descriptor at all.
 */
static void test_resolve_on_a_chip_without_3d(void)
{
    D3CHECK(v9x_d3d_mode_resolve(V9X_D3D_REQUEST_HARDWARE, V9X_FALSE) ==
            V9X_D3D_STATE_NONE);
    /* Turning off what was never on reports the card's answer, not the
     * setting's, so the page does not invite somebody to switch it back. */
    D3CHECK(v9x_d3d_mode_resolve(V9X_D3D_REQUEST_DISABLED, V9X_FALSE) ==
            V9X_D3D_STATE_NONE);
    /* Software is the mode whose whole purpose is a card with no 3D engine,
     * so it resolves to itself here - this is the case it exists for, and the
     * day it landed is the day this assertion changed from UNIMPLEMENTED. */
    D3CHECK(v9x_d3d_mode_resolve(V9X_D3D_REQUEST_SOFTWARE, V9X_FALSE) ==
            V9X_D3D_STATE_SOFTWARE);
    D3CHECK(v9x_d3d_mode_resolve(V9X_D3D_REQUEST_HYBRID, V9X_FALSE) ==
            V9X_D3D_STATE_UNIMPLEMENTED);
    D3CHECK(v9x_d3d_mode_resolve(V9X_D3D_REQUEST_OFFLOAD, V9X_FALSE) ==
            V9X_D3D_STATE_UNIMPLEMENTED);
}

/*
 * A value this build does not know reads as the behaviour the machine had
 * before the key existed. Includes 0xffff, which is what a GetPrivateProfileInt
 * default would be if anybody supplied one, and every value up to 64.
 */
static void test_unrecognised_requests_change_nothing(void)
{
    v9x_u16 requested;

    for (requested = 5u; requested <= 64u; ++requested) {
        D3CHECK(v9x_d3d_mode_resolve(requested, V9X_TRUE) ==
                V9X_D3D_STATE_HARDWARE);
        D3CHECK(v9x_d3d_mode_resolve(requested, V9X_FALSE) ==
                V9X_D3D_STATE_NONE);
    }
    D3CHECK(v9x_d3d_mode_resolve((v9x_u16)0xffffu, V9X_TRUE) ==
            V9X_D3D_STATE_HARDWARE);
    D3CHECK(v9x_d3d_mode_resolve((v9x_u16)0xffffu, V9X_FALSE) ==
            V9X_D3D_STATE_NONE);
}

static void test_only_hardware_advertises(void)
{
    D3CHECK(v9x_d3d_mode_advertises(V9X_D3D_STATE_HARDWARE) == V9X_TRUE);
    D3CHECK(v9x_d3d_mode_advertises(V9X_D3D_STATE_NONE) == V9X_FALSE);
    D3CHECK(v9x_d3d_mode_advertises(V9X_D3D_STATE_DISABLED) == V9X_FALSE);
    D3CHECK(v9x_d3d_mode_advertises(V9X_D3D_STATE_UNIMPLEMENTED) == V9X_FALSE);
    D3CHECK(v9x_d3d_mode_advertises(V9X_D3D_STATE_SOFTWARE) == V9X_TRUE);
    /* A state code this build does not know must not advertise either. */
    D3CHECK(v9x_d3d_mode_advertises((v9x_u16)9u) == V9X_FALSE);
}

/*
 * The property, over the whole request space: no setting can advertise
 * Direct3D on a chip that does not claim it, and no setting other than
 * DISABLED can take it away from a chip that does.
 */
static void test_the_chip_is_the_authority(void)
{
    v9x_u32 requested;

    for (requested = 0ul; requested <= 0xfffful; ++requested) {
        v9x_u16 request = (v9x_u16)requested;
        v9x_u16 without = v9x_d3d_mode_resolve(request, V9X_FALSE);
        v9x_u16 with = v9x_d3d_mode_resolve(request, V9X_TRUE);

        if (request == V9X_D3D_REQUEST_SOFTWARE) {
            /* The one request that advertises Direct3D on a card with no 3D
             * engine, because the rasterizer is what serves it. Every other
             * value still obeys "the chip is the authority". */
            D3CHECK(v9x_d3d_mode_advertises(without) == V9X_TRUE);
        } else {
            D3CHECK(v9x_d3d_mode_advertises(without) == V9X_FALSE);
        }
        if (request == V9X_D3D_REQUEST_DISABLED ||
            request == V9X_D3D_REQUEST_HYBRID ||
            request == V9X_D3D_REQUEST_OFFLOAD) {
            D3CHECK(v9x_d3d_mode_advertises(with) == V9X_FALSE);
        } else {
            D3CHECK(v9x_d3d_mode_advertises(with) == V9X_TRUE);
        }
    }
}

/*
 * settings_status.c compares against these spellings, so a rename here is a
 * silent break there. The strings are the contract.
 */
static void test_state_text_is_stable(void)
{
    D3CHECK(strcmp(v9x_d3d_mode_text(V9X_D3D_STATE_HARDWARE),
                   "hardware") == 0);
    D3CHECK(strcmp(v9x_d3d_mode_text(V9X_D3D_STATE_NONE), "none") == 0);
    D3CHECK(strcmp(v9x_d3d_mode_text(V9X_D3D_STATE_DISABLED),
                   "user-disabled") == 0);
    D3CHECK(strcmp(v9x_d3d_mode_text(V9X_D3D_STATE_UNIMPLEMENTED),
                   "mode-unimplemented") == 0);
    D3CHECK(strcmp(v9x_d3d_mode_text(V9X_D3D_STATE_SOFTWARE),
                   "software") == 0);
    D3CHECK(strcmp(v9x_d3d_mode_text((v9x_u16)9u), "unknown") == 0);

    /* Distinct and non-empty, including against the unknown fallback: two
     * states that printed the same word would make the page's Direct3D row
     * unable to say which one it is reporting. */
    {
        v9x_u16 left;
        v9x_u16 right;

        for (left = 0u; left <= 4u; ++left) {
            const char *left_text = v9x_d3d_mode_text(left);

            D3CHECK(left_text[0] != '\0');
            D3CHECK(strcmp(left_text, "unknown") != 0);
            for (right = (v9x_u16)(left + 1u); right <= 4u; ++right) {
                D3CHECK(strcmp(left_text, v9x_d3d_mode_text(right)) != 0);
            }
        }
    }
}

/*
 * The request text, which is what V9XHW.INI's Direct3DDefault= carries and
 * what settings_propsheet.c preselects its selector from.
 *
 * Every known request must round-trip to its own number, because the page
 * matches the value against its combo entries: a request that printed
 * another request's digit would select the wrong entry, and an OK on that
 * page writes the selection back. An unknown request prints "0", the number
 * v9x_d3d_mode_resolve will actually treat it as.
 */
static void test_request_text_is_stable(void)
{
    D3CHECK(strcmp(v9x_d3d_request_text(V9X_D3D_REQUEST_HARDWARE), "0") == 0);
    D3CHECK(strcmp(v9x_d3d_request_text(V9X_D3D_REQUEST_DISABLED), "1") == 0);
    D3CHECK(strcmp(v9x_d3d_request_text(V9X_D3D_REQUEST_SOFTWARE), "2") == 0);
    D3CHECK(strcmp(v9x_d3d_request_text(V9X_D3D_REQUEST_HYBRID), "3") == 0);
    D3CHECK(strcmp(v9x_d3d_request_text(V9X_D3D_REQUEST_OFFLOAD), "4") == 0);
    D3CHECK(strcmp(v9x_d3d_request_text((v9x_u16)5u), "0") == 0);
    D3CHECK(strcmp(v9x_d3d_request_text((v9x_u16)0xffffu), "0") == 0);
}

/*
 * The build's absent-key default.
 *
 * The host build defines no family override, so this asserts the fallback
 * that every family except vbe ships: an absent key is the chip's own
 * engine, which is the behaviour the driver had before the key existed. The
 * vbe package's override to SOFTWARE is asserted against its manifest by
 * scripts\check-tree.ps1, because the value only exists on that compile.
 *
 * The second assertion is the one with teeth: whatever the default is, it
 * must be a request the resolve table knows. A default of, say, 7 would
 * compile, would resolve to the chip's answer, and would leave the settings
 * page preselecting an entry that is not in its list.
 */
static void test_the_default_request_is_a_known_request(void)
{
    /* Through a variable, not the macro directly: the operands are compile
     * time constants and Watcom folds the comparison, then reports the rest
     * of the chain as unreachable code, which this build treats as an
     * error. */
    v9x_u16 build_default = (v9x_u16)V9X_D3D_DEFAULT_REQUEST;

    D3CHECK(build_default == V9X_D3D_REQUEST_HARDWARE);

    D3CHECK(build_default == V9X_D3D_REQUEST_HARDWARE ||
            build_default == V9X_D3D_REQUEST_DISABLED ||
            build_default == V9X_D3D_REQUEST_SOFTWARE ||
            build_default == V9X_D3D_REQUEST_HYBRID ||
            build_default == V9X_D3D_REQUEST_OFFLOAD);

    /* And it must print as a digit the page can match, which is the same
     * round trip test_request_text_is_stable makes for every other value. */
    D3CHECK(strcmp(v9x_d3d_request_text(build_default), "0") == 0);
}

/*
 * What the vbe family's override actually buys, resolved rather than assumed.
 *
 * A tier-0 card claims no 3D engine, so this is the pair of outcomes the
 * default chooses between: nothing at all, or the rasterizer. Written as a
 * test rather than a comment because the first arm is what shipped for every
 * release up to 0.7.1, and a later change to the resolve table that quietly
 * restored it would otherwise pass every other row here.
 */
static void test_the_vbe_default_serves_direct3d_on_a_chip_with_none(void)
{
    v9x_u16 without_override =
        v9x_d3d_mode_resolve(V9X_D3D_REQUEST_HARDWARE, V9X_FALSE);
    v9x_u16 with_override =
        v9x_d3d_mode_resolve(V9X_D3D_REQUEST_SOFTWARE, V9X_FALSE);

    D3CHECK(without_override == V9X_D3D_STATE_NONE);
    D3CHECK(v9x_d3d_mode_advertises(without_override) == V9X_FALSE);

    D3CHECK(with_override == V9X_D3D_STATE_SOFTWARE);
    D3CHECK(v9x_d3d_mode_advertises(with_override) == V9X_TRUE);
}

unsigned int v9x_run_d3dmode_tests(void)
{
    test_resolve_on_a_chip_with_3d();
    test_resolve_on_a_chip_without_3d();
    test_unrecognised_requests_change_nothing();
    test_only_hardware_advertises();
    test_the_chip_is_the_authority();
    test_state_text_is_stable();
    test_request_text_is_stable();
    test_the_default_request_is_a_known_request();
    test_the_vbe_default_serves_direct3d_on_a_chip_with_none();
    return d3dmode_failures;
}
