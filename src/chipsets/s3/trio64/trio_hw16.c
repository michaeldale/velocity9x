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

/*
 * The Trio64's aliases: further PCI ids this object's two hooks drive
 * unchanged.
 *
 * They are separate device entries rather than a list inside the one above
 * because the PCI scan matches an entry per id - ddi.c copies vendor_id and
 * device_id out of this array into the table V9xFindPciDevice walks - and
 * because each carries its own name, so C:\V9XDIAG\V9XHW.INI reports the part
 * the machine has rather than the sibling whose code is running. Everything
 * after the identity is deliberately identical to v9x_trio_device: same clock
 * detector, same 8514/A engine with no S3D core, same aperture enable.
 *
 * What separates these from the two chips is evidence, not code. No guest and
 * no card has run any of them, and the family's mode matrix does not cover
 * them; they inherit the Trio64's declared modes because they are the same
 * register interface, which is a reason to bind an id and not a measurement.
 * docs\decisions\2026-08-29-s3-device-id-survey.md records which ids were read
 * off an option ROM (8901) and which are documentation (8810, 8812, 8813,
 * 8814). Promoting one to a chip is what a guest run would license.
 */
#define V9X_TRIO_ALIAS_TAIL \
    "s3-virge-pll-v1", \
    "live-any-depth", \
    "directdraw-fill-blt", \
    "not-advertised", \
    v9x_s3_enable_linear_aperture, \
    v9x_trio_fill_engine

const V9X_HW16_DEVICE v9x_trio32_device = {
    0x5333u, 0x8810u,
    "S3 Trio32 86C732",
    "5333", "8810",
    V9X_TRIO_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_aurora64_device = {
    0x5333u, 0x8812u,
    "S3 Aurora64V+ 86C862",
    "5333", "8812",
    V9X_TRIO_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_trio32_64_device = {
    0x5333u, 0x8813u,
    "S3 Trio32/64 86C732/86C764",
    "5333", "8813",
    V9X_TRIO_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_trio64uv_device = {
    0x5333u, 0x8814u,
    "S3 Trio64UV+ 86C767",
    "5333", "8814",
    V9X_TRIO_ALIAS_TAIL
};

const V9X_HW16_DEVICE v9x_trio64v2_device = {
    0x5333u, 0x8901u,
    "S3 Trio64V2/DX or /GX 86C775/86C785",
    "5333", "8901",
    V9X_TRIO_ALIAS_TAIL
};
