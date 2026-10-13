#include <stdio.h>
#include <string.h>

#include "velocity9x/tdfx_hwc.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

/* The Voodoo4 4500 in A8U4I5: 121A:0009 revision 01, 32 MiB less the page
 * the BIOS leaves tiled, both apertures mapped. */
static void base_device(struct v9x_tdfx_hwc_device *device)
{
    device->vendor_id = 0x121aul;
    device->device_id = 0x0009ul;
    device->revision = 0x01ul;
    device->fb_bytes = 33550336ul;
    device->bar0_linear = 0x83400000ul;
    device->bar1_linear = 0x85400000ul;
}

/* A request as the shipped Glide sent it on boot 418: DWORD 0 and 2-15
 * are stack contents, DWORD 1 the sub-function. */
static void base_request(v9x_u32 *request, v9x_u32 which)
{
    v9x_u32 index;

    for (index = 0u; index < V9X_TDFX_HWC_REQUEST_DWORDS; ++index) {
        request[index] = 0x81ef5000ul + index;
    }
    request[V9X_TDFX_HWC_WHICH] = which;
}

static void test_device_config(void)
{
    struct v9x_tdfx_hwc_device device;
    struct v9x_tdfx_hwc_answer answer;
    v9x_u32 request[V9X_TDFX_HWC_REQUEST_DWORDS];

    base_device(&device);
    base_request(request, V9X_TDFX_HWC_GETDEVICECONFIG);
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == 1l);
    CHECK(answer.reply_dwords == V9X_TDFX_HWC_REPLY_DWORDS);
    CHECK(answer.action == V9X_TDFX_HWC_ACTION_NONE);
    CHECK(answer.reply[0] == 1ul);          /* resStatus */
    CHECK(answer.reply[1] == 0ul);          /* devNum */
    CHECK(answer.reply[2] == 0x121aul);     /* vendorID */
    CHECK(answer.reply[3] == 0x0009ul);     /* deviceID: Napalm range */
    CHECK(answer.reply[4] == 33550336ul);   /* fbRam, bytes */
    CHECK(answer.reply[5] == 0x01ul);       /* chipRev */
    CHECK(answer.reply[6] == 0ul && answer.reply[7] == 0ul &&
          answer.reply[8] == 0ul);          /* surface-path strides */
    CHECK(answer.reply[9] == 1ul);          /* isMaster */
    CHECK(answer.reply[10] == 1ul);         /* numChips */
}

static void test_linear_addresses(void)
{
    struct v9x_tdfx_hwc_device device;
    struct v9x_tdfx_hwc_answer answer;
    v9x_u32 request[V9X_TDFX_HWC_REQUEST_DWORDS];

    base_device(&device);
    base_request(request, V9X_TDFX_HWC_GETLINEARADDR);
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == 1l);
    CHECK(answer.reply[0] == 1ul);
    CHECK(answer.reply[1] == 2ul);          /* numBaseAddrs */
    CHECK(answer.reply[2] == 0x83400000ul); /* BAR0 */
    CHECK(answer.reply[3] == 0x85400000ul); /* BAR1 */

    /* An aperture the driver could not map: resStatus 0, which Glide
     * treats as failure. */
    device.bar0_linear = 0ul;
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == 1l);
    CHECK(answer.reply[0] == 0ul);
}

static void test_exclusive_and_cleanup(void)
{
    struct v9x_tdfx_hwc_device device;
    struct v9x_tdfx_hwc_answer answer;
    v9x_u32 request[V9X_TDFX_HWC_REQUEST_DWORDS];

    base_device(&device);
    base_request(request, V9X_TDFX_HWC_SETEXCLUSIVE);
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == 1l && answer.reply[0] == 1ul);
    CHECK(answer.action == V9X_TDFX_HWC_ACTION_TAKE);

    base_request(request, V9X_TDFX_HWC_RLSEXCLUSIVE);
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == 1l && answer.reply[0] == 1ul);
    CHECK(answer.action == V9X_TDFX_HWC_ACTION_RELEASE);

    base_request(request, V9X_TDFX_HWC_UNMAP_MEMORY);
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == 1l && answer.reply[0] == 1ul);
    CHECK(answer.action == V9X_TDFX_HWC_ACTION_NONE);

    /* PCI_OP: acknowledged, nothing written; a read answers 0. */
    base_request(request, V9X_TDFX_HWC_PCI_OP);
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == 1l && answer.reply[0] == 1ul &&
          answer.reply[1] == 0ul);
}

/* SHARE_CONTEXT_DWORD: -1 both ways is Glide's documented "no"; 0 would be
 * taken as success with a garbage pointer. */
static void test_share_context_no(void)
{
    struct v9x_tdfx_hwc_device device;
    struct v9x_tdfx_hwc_answer answer;
    v9x_u32 request[V9X_TDFX_HWC_REQUEST_DWORDS];

    base_device(&device);
    base_request(request, V9X_TDFX_HWC_SHARE_CONTEXT_DWORD);
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == -1l);
    CHECK(answer.reply[0] == 0xfffffffful);
    CHECK(answer.reply_dwords >= 1u);
}

static void test_declines(void)
{
    struct v9x_tdfx_hwc_device device;
    struct v9x_tdfx_hwc_answer answer;
    v9x_u32 request[V9X_TDFX_HWC_REQUEST_DWORDS];

    base_device(&device);

    /* Sub-functions a single-chip fullscreen session does not need. */
    base_request(request, 0x0ful);          /* VIDTIMING */
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == 0l && answer.reply_dwords == 0u);
    base_request(request, 0x1bul);          /* SLI_AA_REQUEST */
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == 0l && answer.reply_dwords == 0u);

    /* Too short to name a sub-function. */
    CHECK(v9x_tdfx_hwc_answer(request, 1u, &device, &answer) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(answer.escape_return == 0l && answer.reply_dwords == 0u);

    /* A device that is not a VSA-100 answers nothing. */
    base_request(request, V9X_TDFX_HWC_GETDEVICECONFIG);
    device.device_id = 0x0005ul;
    CHECK(v9x_tdfx_hwc_answer(request, V9X_TDFX_HWC_REQUEST_DWORDS, &device,
                              &answer) == V9X_STATUS_OK);
    CHECK(answer.escape_return == 0l && answer.reply_dwords == 0u);
}

/* The shared block's V9X_DD_ENGINE_VALID, and a neighbour that must
 * survive every transition. */
#define HOLD_VALID 0x00000001ul
#define HOLD_OTHER 0x00000010ul

static void test_hold_take_release(void)
{
    struct v9x_tdfx_hwc_hold hold;
    v9x_u32 flags;

    memset(&hold, 0, sizeof(hold));
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_TAKE,
                                    HOLD_VALID | HOLD_OTHER, HOLD_VALID);
    CHECK(flags == HOLD_OTHER);
    CHECK(hold.held != 0ul);
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_RELEASE,
                                    flags, HOLD_VALID);
    CHECK(flags == (HOLD_VALID | HOLD_OTHER));
    CHECK(hold.held == 0ul);

    /* The HAL had given the engine up before Glide took it: release must
     * not hand it back. */
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_TAKE,
                                    HOLD_OTHER, HOLD_VALID);
    CHECK(flags == HOLD_OTHER);
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_RELEASE,
                                    flags, HOLD_VALID);
    CHECK(flags == HOLD_OTHER);

    /* A second take while held keeps what the first one saved. */
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_TAKE,
                                    HOLD_VALID, HOLD_VALID);
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_TAKE,
                                    flags, HOLD_VALID);
    CHECK(flags == 0ul);
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_RELEASE,
                                    flags, HOLD_VALID);
    CHECK(flags == HOLD_VALID);

    /* A release with nothing held, and an action that is neither. */
    CHECK(v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_RELEASE,
                                  HOLD_OTHER, HOLD_VALID) == HOLD_OTHER);
    CHECK(v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_NONE,
                                  HOLD_VALID, HOLD_VALID) == HOLD_VALID);
    CHECK(hold.held == 0ul);
}

static void test_hold_restamp(void)
{
    struct v9x_tdfx_hwc_hold hold;
    v9x_u32 flags;

    /* Not held: a restamp stands. */
    memset(&hold, 0, sizeof(hold));
    CHECK(v9x_tdfx_hwc_hold_restamp(&hold, HOLD_VALID | HOLD_OTHER,
                                    HOLD_VALID) == (HOLD_VALID | HOLD_OTHER));

    /* Held: a mode set inside Glide's session restamps VALID; it is
     * withheld, and the release gives it back. */
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_TAKE,
                                    HOLD_OTHER, HOLD_VALID);
    flags = v9x_tdfx_hwc_hold_restamp(&hold, HOLD_VALID | HOLD_OTHER,
                                      HOLD_VALID);
    CHECK(flags == HOLD_OTHER);
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_RELEASE,
                                    flags, HOLD_VALID);
    CHECK(flags == (HOLD_VALID | HOLD_OTHER));

    /* Held, and the restamp found no engine: the release must not invent
     * one. */
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_TAKE,
                                    HOLD_VALID, HOLD_VALID);
    flags = v9x_tdfx_hwc_hold_restamp(&hold, 0ul, HOLD_VALID);
    CHECK(flags == 0ul);
    flags = v9x_tdfx_hwc_hold_apply(&hold, V9X_TDFX_HWC_ACTION_RELEASE,
                                    flags, HOLD_VALID);
    CHECK(flags == 0ul);
}

unsigned int v9x_run_tdfx_hwc_tests(void)
{
    failures = 0u;
    test_device_config();
    test_linear_addresses();
    test_exclusive_and_cleanup();
    test_share_context_no();
    test_declines();
    test_hold_take_release();
    test_hold_restamp();
    return failures;
}
