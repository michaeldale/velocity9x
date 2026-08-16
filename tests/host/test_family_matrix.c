/*
 * Family matrix tests.
 *
 * The family manifests are the single statement of which chip belongs to which
 * family, what its engine claims, and which modes its INF advertises. The C
 * side restates parts of that by hand - the backend registry decides PCI
 * dispatch from literal device ids, and each backend decides for itself which
 * modes it can lay out. Nothing made the two agree.
 *
 * build-host.ps1 generates v9x_family_matrix.h from the manifests, and these
 * assertions are what turn that duplication from something that can quietly
 * diverge into something that fails the build.
 *
 * Scope note on the mode check: validate_mode is a layout calculator bounded by
 * VRAM, not a whitelist, so it cannot be asserted to accept *only* the
 * advertised modes - it will happily lay out plenty of modes no INF mentions.
 * What it can be held to, and what actually matters, is the other direction:
 * every mode the INF advertises must be one the backend can serve, and a mode
 * that does not fit the declared VRAM must be refused.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/backend_registry.h"
#include "velocity9x/engine_abi.h"

#include "v9x_family_matrix.h"

static unsigned int matrix_failures = 0u;

#define MCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++matrix_failures; \
    } \
} while (0)

#define MCHECK_CHIP(chip, expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s/%s: %s\n", __FILE__, (unsigned int)__LINE__, \
               (chip)->family_id, (chip)->chip_id, #expression); \
        ++matrix_failures; \
    } \
} while (0)

static const struct v9x_backend_ops *ops_for(const struct v9x_family_matrix_chip *chip)
{
    struct v9x_pci_identity pci;

    pci.vendor_id = chip->vendor_id;
    pci.device_id = chip->device_id;
    pci.revision = 0u;
    return v9x_backend_for_pci(&pci);
}

/*
 * An empty matrix would make every loop below vacuous and every assertion in
 * this file pass without testing anything. The count is a compile-time
 * constant, so this is a compile-time check rather than one the runner could
 * report.
 */
typedef char v9x_family_matrix_is_not_empty[V9X_FAMILY_MATRIX_COUNT > 0u ? 1 : -1];

/* Every declared chip resolves to a backend. A manifest may not name hardware
 * the driver has no code for. */
static void test_every_declared_chip_resolves(void)
{
    unsigned int index;

    for (index = 0u; index < V9X_FAMILY_MATRIX_COUNT; ++index) {
        const struct v9x_family_matrix_chip *chip = &v9x_family_matrix[index];
        MCHECK_CHIP(chip, ops_for(chip) != 0);
    }
}

/*
 * Chips of one family share a backend; chips of different families do not.
 *
 * This is the invariant that makes "family" mean something in the registry
 * rather than only in the packaging: the S3 ViRGE and Trio64 are one binary and
 * one backend, and the Matrox is neither.
 */
static void test_family_backends_are_distinct(void)
{
    unsigned int outer;
    unsigned int inner;

    for (outer = 0u; outer < V9X_FAMILY_MATRIX_COUNT; ++outer) {
        const struct v9x_family_matrix_chip *a = &v9x_family_matrix[outer];
        for (inner = outer + 1u; inner < V9X_FAMILY_MATRIX_COUNT; ++inner) {
            const struct v9x_family_matrix_chip *b = &v9x_family_matrix[inner];
            int same_family = strcmp(a->family_id, b->family_id) == 0;

            if (same_family) {
                MCHECK_CHIP(a, ops_for(a) == ops_for(b));
            } else {
                MCHECK_CHIP(a, ops_for(a) != ops_for(b));
            }
        }
    }
}

/* Hardware no manifest claims must not resolve. A registry that answered for
 * an undeclared id would install this driver on a card it was never built
 * for. */
static void test_undeclared_hardware_is_refused(void)
{
    struct v9x_pci_identity pci;
    unsigned int index;

    pci.vendor_id = 0x1234u;
    pci.device_id = 0x5678u;
    pci.revision = 0u;
    MCHECK(v9x_backend_for_pci(&pci) == 0);

    MCHECK(v9x_backend_for_pci(0) == 0);

    /* A known vendor with an unknown device is the more realistic mistake:
     * another S3 or Matrox part in the same machine. */
    for (index = 0u; index < V9X_FAMILY_MATRIX_COUNT; ++index) {
        pci.vendor_id = v9x_family_matrix[index].vendor_id;
        pci.device_id = 0xfffeu;
        MCHECK(v9x_backend_for_pci(&pci) == 0);
    }
}

/*
 * A chip claiming no engine may claim no engine capability.
 *
 * The 32-bit HAL resolves no ops table for V9X_DD_ENGINE_TYPE_NONE and sends
 * everything to the CPU path, so a capability declared against that engine type
 * would be advertised to DirectDraw and then never served. The manifest loader
 * rejects this too; asserting it here keeps the rule true for anything that
 * reaches the C side by another route.
 */
static void test_engine_caps_match_engine_type(void)
{
    unsigned int index;

    for (index = 0u; index < V9X_FAMILY_MATRIX_COUNT; ++index) {
        const struct v9x_family_matrix_chip *chip = &v9x_family_matrix[index];

        if (chip->engine_type == V9X_DD_ENGINE_TYPE_NONE) {
            MCHECK_CHIP(chip, chip->engine_caps == 0ul);
        } else {
            MCHECK_CHIP(chip, chip->engine_caps != 0ul);
        }
        /* D3D is the capability with a whole code module behind it, and the
         * only one this driver serves from a single engine. */
        if ((chip->engine_caps & V9X_DD_ENGINE_CAP_D3D) != 0ul) {
            MCHECK_CHIP(chip,
                        chip->engine_type == V9X_DD_ENGINE_TYPE_S3_VIRGE_DX);
        }
    }
}

/*
 * Every mode the generated INF advertises must be one the backend can lay out
 * in the VRAM the manifest declares, and a mode that does not fit must be
 * refused. This is the first check that holds the INF and the driver to the
 * same answer.
 */
static void test_advertised_modes_are_servable(void)
{
    unsigned int index;
    unsigned int mode_index;

    for (index = 0u; index < V9X_FAMILY_MATRIX_COUNT; ++index) {
        const struct v9x_family_matrix_chip *chip = &v9x_family_matrix[index];
        const struct v9x_backend_ops *ops = ops_for(chip);
        struct v9x_backend_state state;
        struct v9x_pci_identity pci;
        struct v9x_pci_bar_resource bar;
        struct v9x_mode_request request;
        struct v9x_mode_layout layout;

        if (ops == 0) {
            continue;   /* already reported by the resolution test */
        }

        pci.vendor_id = chip->vendor_id;
        pci.device_id = chip->device_id;
        pci.revision = 0u;
        memset(&state, 0, sizeof(state));
        MCHECK_CHIP(chip, ops->probe(&state, &pci) == V9X_STATUS_OK);

        /* The aperture every family maps today is the full 64 MiB BAR; only
         * the first vram_bytes of it is allocatable. */
        bar.physical_base = 0xe0000000ul;
        bar.aperture_bytes = 64ul * 1024ul * 1024ul;
        bar.flags = V9X_PCI_BAR_MEMORY | V9X_PCI_BAR_PREFETCHABLE;
        MCHECK_CHIP(chip,
                    ops->bind_framebuffer(&state, &bar,
                                          chip->video_memory_bytes,
                                          0ul) == V9X_STATUS_OK);

        for (mode_index = 0u; mode_index < chip->mode_count; ++mode_index) {
            const struct v9x_family_matrix_mode *mode = &chip->modes[mode_index];

            request.width = mode->width;
            request.height = mode->height;
            request.bits_per_pixel = mode->bits_per_pixel;
            request.pitch_alignment = 8u;
            request.framebuffer_bytes = chip->video_memory_bytes;
            if (ops->validate_mode(&state, &request, &layout) != V9X_STATUS_OK) {
                printf("FAIL %s:%u: %s/%s advertises %ux%ux%u, "
                       "which its backend will not lay out in %lu bytes\n",
                       __FILE__, (unsigned int)__LINE__,
                       chip->family_id, chip->chip_id,
                       (unsigned int)mode->width, (unsigned int)mode->height,
                       (unsigned int)mode->bits_per_pixel,
                       (unsigned long)chip->video_memory_bytes);
                ++matrix_failures;
            }
        }

        /* The other direction: something far past the declared VRAM has to be
         * refused, or the check above would pass on any backend that accepted
         * everything. */
        request.width = 4096u;
        request.height = 4096u;
        request.bits_per_pixel = 32u;
        request.pitch_alignment = 8u;
        request.framebuffer_bytes = chip->video_memory_bytes;
        MCHECK_CHIP(chip,
                    ops->validate_mode(&state, &request, &layout) != V9X_STATUS_OK);
    }
}

unsigned int v9x_run_family_matrix_tests(void)
{
    test_every_declared_chip_resolves();
    test_family_backends_are_distinct();
    test_undeclared_hardware_is_refused();
    test_engine_caps_match_engine_type();
    test_advertised_modes_are_servable();
    return matrix_failures;
}
