/*
 * 3dfx Glide's EXT_HWC escape. See include\velocity9x\tdfx_hwc.h for the
 * contract and docs\specifications\vsa100-ext-hwc.md section 3 for every
 * layout and value used here.
 */
#include "velocity9x/tdfx_hwc.h"

/* resStatus values Glide tests for (vsa100-ext-hwc.md, "Request and reply
 * layout"). */
#define TDFX_HWC_OK                 1ul
#define TDFX_HWC_FAILED             0ul
#define TDFX_HWC_NO                 0xfffffffful

/* GETDEVICECONFIG reply DWORDs (hwcExtDeviceConfigRes_t). */
#define TDFX_HWC_CFG_DEVNUM         1u
#define TDFX_HWC_CFG_VENDOR         2u
#define TDFX_HWC_CFG_DEVICE         3u
#define TDFX_HWC_CFG_FB_RAM         4u
#define TDFX_HWC_CFG_CHIP_REV       5u
#define TDFX_HWC_CFG_IS_MASTER      9u
#define TDFX_HWC_CFG_NUM_CHIPS      10u

/* GETLINEARADDR reply DWORDs (hwcExtLinearAddrRes_t). */
#define TDFX_HWC_LIN_COUNT          1u
#define TDFX_HWC_LIN_BAR0           2u
#define TDFX_HWC_LIN_BAR1           3u
#define TDFX_HWC_LIN_APERTURES      2ul

/* The VSA-100: one id on every shipped board. Glide reads 6..15 as Napalm. */
#define TDFX_HWC_VENDOR_3DFX        0x121aul
#define TDFX_HWC_DEVICE_VSA100      0x0009ul

static void v9x_tdfx_hwc_clear(struct v9x_tdfx_hwc_answer *answer)
{
    v9x_u32 index;

    for (index = 0u; index < V9X_TDFX_HWC_REPLY_DWORDS; ++index) {
        answer->reply[index] = 0ul;
    }
    answer->reply_dwords = 0u;
    answer->escape_return = 0l;
    answer->action = V9X_TDFX_HWC_ACTION_NONE;
}

/* A handled call: resStatus first, the whole reply copied out. */
static void v9x_tdfx_hwc_handled(struct v9x_tdfx_hwc_answer *answer,
                                 v9x_u32 status, v9x_u32 action)
{
    answer->reply[0] = status;
    answer->reply_dwords = V9X_TDFX_HWC_REPLY_DWORDS;
    answer->escape_return = 1l;
    answer->action = action;
}

v9x_status v9x_tdfx_hwc_answer(const v9x_u32 *request, v9x_u32 request_dwords,
                               const struct v9x_tdfx_hwc_device *device,
                               struct v9x_tdfx_hwc_answer *answer)
{
    if (answer == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    v9x_tdfx_hwc_clear(answer);
    if (request == 0 || device == 0 ||
        request_dwords <= V9X_TDFX_HWC_WHICH) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Only the chip this family drives; anything else is left to Glide's
     * own "no driver" path. */
    if (device->vendor_id != TDFX_HWC_VENDOR_3DFX ||
        device->device_id != TDFX_HWC_DEVICE_VSA100) {
        return V9X_STATUS_OK;
    }

    switch (request[V9X_TDFX_HWC_WHICH]) {
    case V9X_TDFX_HWC_GETDEVICECONFIG:
        /* One master chip; fbRam is what Glide may overwrite from 0x18000
         * up, so it is the usable size, not the strapped one. */
        answer->reply[TDFX_HWC_CFG_DEVNUM] = 0ul;
        answer->reply[TDFX_HWC_CFG_VENDOR] = device->vendor_id;
        answer->reply[TDFX_HWC_CFG_DEVICE] = device->device_id;
        answer->reply[TDFX_HWC_CFG_FB_RAM] = device->fb_bytes;
        answer->reply[TDFX_HWC_CFG_CHIP_REV] = device->revision;
        answer->reply[TDFX_HWC_CFG_IS_MASTER] = 1ul;
        answer->reply[TDFX_HWC_CFG_NUM_CHIPS] = 1ul;
        v9x_tdfx_hwc_handled(answer, TDFX_HWC_OK, V9X_TDFX_HWC_ACTION_NONE);
        break;

    case V9X_TDFX_HWC_GETLINEARADDR:
        /* Glide dereferences both from ring 3 in its own process. A missing
         * mapping is resStatus 0, which Glide treats as failure. */
        if (device->bar0_linear == 0ul || device->bar1_linear == 0ul) {
            v9x_tdfx_hwc_handled(answer, TDFX_HWC_FAILED,
                                 V9X_TDFX_HWC_ACTION_NONE);
            break;
        }
        answer->reply[TDFX_HWC_LIN_COUNT] = TDFX_HWC_LIN_APERTURES;
        answer->reply[TDFX_HWC_LIN_BAR0] = device->bar0_linear;
        answer->reply[TDFX_HWC_LIN_BAR1] = device->bar1_linear;
        v9x_tdfx_hwc_handled(answer, TDFX_HWC_OK, V9X_TDFX_HWC_ACTION_NONE);
        break;

    case V9X_TDFX_HWC_SETEXCLUSIVE:
        v9x_tdfx_hwc_handled(answer, TDFX_HWC_OK, V9X_TDFX_HWC_ACTION_TAKE);
        break;

    case V9X_TDFX_HWC_RLSEXCLUSIVE:
        v9x_tdfx_hwc_handled(answer, TDFX_HWC_OK, V9X_TDFX_HWC_ACTION_RELEASE);
        break;

    case V9X_TDFX_HWC_SHARE_CONTEXT_DWORD:
        /* No lost-context word in shared memory yet: -1 both ways is the
         * "no" Glide answers with its own private word. 0 would not do -
         * Glide takes it as success and uses a garbage pointer. */
        answer->reply[0] = TDFX_HWC_NO;
        answer->reply_dwords = V9X_TDFX_HWC_REPLY_DWORDS;
        answer->escape_return = -1l;
        break;

    case V9X_TDFX_HWC_UNMAP_MEMORY:
        /* The mappings are the driver's for the session, not the
         * process's; nothing to undo. Glide ignores the reply. */
        v9x_tdfx_hwc_handled(answer, TDFX_HWC_OK, V9X_TDFX_HWC_ACTION_NONE);
        break;

    case V9X_TDFX_HWC_PCI_OP:
        /* Acknowledged and not performed. On one chip Glide's six writes
         * clear SLI, AA and video-control registers that a single-chip
         * board leaves at 0; config writes on behalf of any process are
         * refused until a measurement says one is needed. A read answers
         * 0. Glide never checks the return. */
        answer->reply[1] = 0ul;
        v9x_tdfx_hwc_handled(answer, TDFX_HWC_OK, V9X_TDFX_HWC_ACTION_NONE);
        break;

    default:
        break;
    }
    return V9X_STATUS_OK;
}

v9x_u32 v9x_tdfx_hwc_hold_apply(struct v9x_tdfx_hwc_hold *hold,
                                v9x_u32 action, v9x_u32 flags,
                                v9x_u32 valid)
{
    if (hold == 0) {
        return flags;
    }
    if (action == V9X_TDFX_HWC_ACTION_TAKE) {
        /* A second take finds the bit already withheld; what the first one
         * saved is what the release must give back. */
        if (hold->held == 0ul) {
            hold->saved_valid = flags & valid;
            hold->held = 1ul;
        }
        return flags & ~valid;
    }
    if (action == V9X_TDFX_HWC_ACTION_RELEASE && hold->held != 0ul) {
        hold->held = 0ul;
        return (flags & ~valid) | hold->saved_valid;
    }
    return flags;
}

v9x_u32 v9x_tdfx_hwc_hold_restamp(struct v9x_tdfx_hwc_hold *hold,
                                  v9x_u32 flags, v9x_u32 valid)
{
    if (hold == 0 || hold->held == 0ul) {
        return flags;
    }
    hold->saved_valid = flags & valid;
    return flags & ~valid;
}
