#include <stdio.h>

#include "velocity9x/engine_abi.h"
#include "../../src/display32/d3d/d3d_select.h"

static unsigned int failures = 0u;
#define CHECK(e) do { if (!(e)) { \
    printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #e); \
    ++failures; } } while (0)

struct select_case {
    int valid;
    v9x_u32 type;
    v9x_u32 caps;
    v9x_u32 expected;
};

static void test_every_engine_type(void)
{
    static const struct select_case cases[] = {
        /* A valid descriptor selects the chip's own engine or none. */
        { 1, V9X_DD_ENGINE_TYPE_NONE, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_S3_VIRGE_DX, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_VIRGE },
        { 1, V9X_DD_ENGINE_TYPE_S3_TRIO64, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_INTEL_GEN3, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_GEN3 },
        /* No Mach64 Direct3D engine exists yet: never another chip's. */
        { 1, V9X_DD_ENGINE_TYPE_ATI_MACH64, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, 5ul, V9X_DD_ENGINE_CAP_D3D, V9X_D3D_SELECT_NONE },
        { 1, 0xfffffffful, 0ul, V9X_D3D_SELECT_NONE },

        /* The publish-time case the old default got wrong: an unstamped
         * descriptor used to publish the ViRGE's caps. */
        { 0, V9X_DD_ENGINE_TYPE_NONE, 0ul, V9X_D3D_SELECT_NONE },
        { 0, V9X_DD_ENGINE_TYPE_S3_VIRGE_DX, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 0, V9X_DD_ENGINE_TYPE_INTEL_GEN3, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },

        /* Software is chosen by capability, before validity and type. */
        { 0, V9X_DD_ENGINE_TYPE_NONE, V9X_DD_ENGINE_CAP_D3D_SOFTWARE,
          V9X_D3D_SELECT_SOFTWARE },
        { 1, V9X_DD_ENGINE_TYPE_S3_VIRGE_DX,
          V9X_DD_ENGINE_CAP_D3D | V9X_DD_ENGINE_CAP_D3D_SOFTWARE,
          V9X_D3D_SELECT_SOFTWARE },
        { 1, V9X_DD_ENGINE_TYPE_ATI_MACH64, V9X_DD_ENGINE_CAP_D3D_SOFTWARE,
          V9X_D3D_SELECT_SOFTWARE },

        /* Mismatched caps and type: the type decides which engine; the
         * 16-bit side, not this selector, decides whether D3D is exposed. */
        { 1, V9X_DD_ENGINE_TYPE_S3_VIRGE_DX, 0ul, V9X_D3D_SELECT_VIRGE },
        { 1, V9X_DD_ENGINE_TYPE_INTEL_GEN3, V9X_DD_ENGINE_CAP_SOLID_FILL,
          V9X_D3D_SELECT_GEN3 },
        { 1, V9X_DD_ENGINE_TYPE_S3_TRIO64,
          V9X_DD_ENGINE_CAP_D3D | V9X_DD_ENGINE_CAP_S3D_TWO_PASS,
          V9X_D3D_SELECT_NONE }
    };
    unsigned int index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        CHECK(v9x_d3d_select_engine(cases[index].valid, cases[index].type,
                                    cases[index].caps) ==
              cases[index].expected);
    }
}

unsigned int v9x_run_d3d_select_tests(void)
{
    test_every_engine_type();
    return failures;
}
