/*
 * S3 Trio32/64 86C764 chip module.
 *
 * The differences from its ViRGE sibling are all here: a different PCI device
 * id, no new-MMIO window to open, and an 8514/A engine with no S3D core, so it
 * publishes Direct3D as not advertised and claims no D3D capability. The mode
 * table and the rest of the family description are shared, in
 * src\chipsets\s3\s3_hw16.c.
 *
 * This object holds Trio64 code and no ViRGE code - notably not the CR53
 * new-MMIO poke, which this chip does not have. The per-object audit asserts
 * that in both directions.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/s3_regs16.h"

/*
 * Trio64 engine: the 8514/A-compatible command set, reached through port I/O.
 * There is no MMIO control window to map and no S3D core, so no D3D.
 */
static void v9x_trio_fill_engine(unsigned long framebuffer_linear_base,
                                 unsigned long *control_linear_base,
                                 unsigned long *mapped_aperture_bytes,
                                 unsigned long *engine_type,
                                 unsigned long *engine_caps)
{
    (void)framebuffer_linear_base;
    *control_linear_base = 0ul;
    *mapped_aperture_bytes = 0ul;
    *engine_type = V9X_DD_ENGINE_TYPE_S3_TRIO64;
    *engine_caps = V9X_DD_ENGINE_CAP_SOLID_FILL |
                   V9X_DD_ENGINE_CAP_SCREEN_COPY |
                   V9X_DD_ENGINE_CAP_FLIP |
                   V9X_DD_ENGINE_CAP_VBLANK;
}

/* Not static: see the note in virge_hw16.c. */
const V9X_HW16_DEVICE v9x_trio_device = {
    0x5333u, 0x8811u,
    "S3 Trio32/64 86C764",
    "5333", "8811",
    "s3-virge-pll-v1",
    "live-any-depth",
    "directdraw-fill-blt",
    "not-advertised",
    /* No CR53 new-MMIO window on this chip: the shared sequence is all of it,
     * so the S3 common linear-aperture enable is the whole hook. */
    v9x_s3_enable_linear_aperture,
    v9x_trio_fill_engine
};
