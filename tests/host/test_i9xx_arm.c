#include <stdio.h>

#include "velocity9x/intel_gma.h"

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

static void test_crc(void)
{
    static const v9x_u32 stream[2] = {
        V9X_I9XX_MI_NOOP, V9X_I9XX_MI_FLUSH
    };
    CHECK(v9x_i9xx_crc32_dwords(stream, 2ul) == 0x8b2cbe45ul);
    CHECK(v9x_i9xx_crc32_dwords(0, 2ul) == 0ul);
}

static void test_arm_contract(void)
{
    struct v9x_i9xx_arm_request request;
    v9x_u16 rejection;

    request.token = "phase4-test-001";
    request.in_flight = "phase4-test-001";
    request.configured_crc = 0x8b2cbe45ul;
    request.packet_crc = 0x8b2cbe45ul;
    request.enable_this_boot = 1u;
    request.safe_mode = V9X_FALSE;
    request.errata_gate = V9X_TRUE;
    request.vendor_id = 0x8086u;
    request.device_id = 0x27aeu;
    request.revision = 3u;
    request.phase = 4u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) == V9X_STATUS_OK);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_NONE);

    request.errata_gate = V9X_FALSE;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_ERRATA);
    request.errata_gate = V9X_TRUE;

    request.enable_this_boot = 0u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_DISABLED);
    request.enable_this_boot = 1u;

    request.safe_mode = V9X_TRUE;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_SAFE_MODE);
    request.safe_mode = V9X_FALSE;

    request.device_id = 0x27a2u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_IDENTITY);
    request.device_id = 0x27aeu;

    request.phase = 5u;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_PHASE);
    request.phase = 4u;

    request.in_flight = "phase4-test-002";
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_TOKEN);
    request.in_flight = request.token;

    request.configured_crc ^= 1ul;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_CRC);
    request.configured_crc ^= 1ul;

    request.token = "bad token";
    request.in_flight = request.token;
    CHECK(v9x_i9xx_arm_evaluate(&request, &rejection) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(rejection == V9X_I9XX_ARM_REJECT_TOKEN);
    CHECK(v9x_i9xx_arm_evaluate(0, &rejection) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_i9xx_arm_evaluate(&request, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

unsigned int v9x_run_i9xx_arm_tests(void)
{
    test_crc();
    test_arm_contract();
    return failures;
}
