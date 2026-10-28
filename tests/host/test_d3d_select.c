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
        /* Mach64 selects its own engine, never another chip's. */
        { 1, V9X_DD_ENGINE_TYPE_ATI_MACH64, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_MACH64 },
        { 0, V9X_DD_ENGINE_TYPE_ATI_MACH64, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        /* The Rage II class has the Mach64 2D engine but no setup engine:
         * it selects its own CPU-setup engine, never d3d_mach64.c. */
        { 1, V9X_DD_ENGINE_TYPE_ATI_RAGE2, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_RAGE2 },
        { 1, V9X_DD_ENGINE_TYPE_ATI_RAGE2,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_SCREEN_COPY,
          V9X_D3D_SELECT_RAGE2 },
        { 0, V9X_DD_ENGINE_TYPE_ATI_RAGE2, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_ATI_RAGE2, V9X_DD_ENGINE_CAP_D3D_SOFTWARE,
          V9X_D3D_SELECT_SOFTWARE },
        /* The SiS 6326's engine is d3d_sis6326.c; an invalid descriptor
         * selects nothing, and software still wins. */
        { 1, V9X_DD_ENGINE_TYPE_SIS_6326, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_SIS6326 },
        { 1, V9X_DD_ENGINE_TYPE_SIS_6326,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_SCREEN_COPY,
          V9X_D3D_SELECT_SIS6326 },
        { 0, V9X_DD_ENGINE_TYPE_SIS_6326, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_SIS_6326, V9X_DD_ENGINE_CAP_D3D_SOFTWARE,
          V9X_D3D_SELECT_SOFTWARE },
        /* One MGA type for the family: the 2064W has a 2D engine and no 3D
         * one, so the type selects d3d_mga.c only with the D3D cap. The
         * 2164W's hook stamps MGA_DEPTH32 beside it and gets the 32-bit Z
         * record; the Mystique's does not and gets Z16 only. */
        { 1, V9X_DD_ENGINE_TYPE_MGA,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_SCREEN_COPY,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_MGA,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_D3D |
              V9X_DD_ENGINE_CAP_MGA_DEPTH32,
          V9X_D3D_SELECT_MGA },
        { 1, V9X_DD_ENGINE_TYPE_MGA,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_MGA16 },
        { 1, V9X_DD_ENGINE_TYPE_MGA, V9X_DD_ENGINE_CAP_MGA_DEPTH32,
          V9X_D3D_SELECT_NONE },
        { 0, V9X_DD_ENGINE_TYPE_MGA, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_MGA, V9X_DD_ENGINE_CAP_D3D_SOFTWARE,
          V9X_D3D_SELECT_SOFTWARE },
        /* The VSA-100 selects d3d_tdfx.c only with the D3D cap its hook
         * stamps, as the MGA does; invalid selects nothing, and software
         * still wins. */
        { 1, V9X_DD_ENGINE_TYPE_TDFX,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_SCREEN_COPY,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_TDFX,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_TDFX },
        { 0, V9X_DD_ENGINE_TYPE_TDFX, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_TDFX, V9X_DD_ENGINE_CAP_D3D_SOFTWARE,
          V9X_D3D_SELECT_SOFTWARE },
        /* Haswell: the blitter alone selects nothing; the render ring's
         * CAP_D3D selects the Gen7.5 engine; software still wins. */
        { 1, V9X_DD_ENGINE_TYPE_INTEL_GEN75,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_SCREEN_COPY,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_INTEL_GEN75, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_GEN75 },
        { 0, V9X_DD_ENGINE_TYPE_INTEL_GEN75, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_INTEL_GEN75, V9X_DD_ENGINE_CAP_D3D_SOFTWARE,
          V9X_D3D_SELECT_SOFTWARE },
        /* The Rage 128: the 2D engine alone selects nothing; CAP_D3D
         * selects the CCE engine; software still wins. */
        { 1, V9X_DD_ENGINE_TYPE_ATI_RAGE128,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_SCREEN_COPY,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_ATI_RAGE128, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_R128 },
        { 0, V9X_DD_ENGINE_TYPE_ATI_RAGE128, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_ATI_RAGE128, V9X_DD_ENGINE_CAP_D3D_SOFTWARE,
          V9X_D3D_SELECT_SOFTWARE },
        /* Evergreen: the DMA engine alone selects nothing; CAP_D3D, the
         * 3D window mapped, selects the CP engine; software still wins. */
        { 1, V9X_DD_ENGINE_TYPE_ATI_EVERGREEN,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_SCREEN_COPY,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_ATI_EVERGREEN, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_EVERGREEN },
        { 0, V9X_DD_ENGINE_TYPE_ATI_EVERGREEN, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_ATI_EVERGREEN,
          V9X_DD_ENGINE_CAP_D3D_SOFTWARE, V9X_D3D_SELECT_SOFTWARE },
        /* Carrizo: SDMA alone selects nothing; CAP_D3D, the window the CP
         * shares mapped, selects the CP engine; software still wins. */
        { 1, V9X_DD_ENGINE_TYPE_ATI_VI,
          V9X_DD_ENGINE_CAP_SOLID_FILL | V9X_DD_ENGINE_CAP_SCREEN_COPY,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_ATI_VI, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_VI },
        { 0, V9X_DD_ENGINE_TYPE_ATI_VI, V9X_DD_ENGINE_CAP_D3D,
          V9X_D3D_SELECT_NONE },
        { 1, V9X_DD_ENGINE_TYPE_ATI_VI, V9X_DD_ENGINE_CAP_D3D_SOFTWARE,
          V9X_D3D_SELECT_SOFTWARE },
        { 1, 13ul, V9X_DD_ENGINE_CAP_D3D, V9X_D3D_SELECT_NONE },
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
