/*
 * S3 Trio32/64 86C764 hardware table.
 *
 * Same VBE mode table and same shared S3 register access as the ViRGE; the
 * differences are the PCI device id and that this chip has no S3d core, so it
 * publishes Direct3D as not advertised.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/s3_regs16.h"

/* Not static: see the note in virge_hw16.c. */
const V9X_HW16_DEVICE v9x_trio_devices[] = {
    {
        0x5333u, 0x8811u,
        "S3 Trio32/64 86C764",
        "5333", "8811",
        "s3-virge-pll-v1",
        "live-any-depth",
        "directdraw-fill-blt",
        "not-advertised"
    }
};

static const V9X_HW16_MODE v9x_trio_modes[] = {
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 },
    {  800u, 600u,  8u,  800u, 0x0103u, 318, 159 },
    { 1024u, 768u,  8u, 1024u, 0x0105u, 407, 203 },
    /* 640x400 after the other 8-bpp rows: see the note in virge_hw16.c. */
    {  640u, 400u,  8u,  640u, 0x0100u, 254, 127 },
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    {  800u, 600u, 16u, 1600u, 0x0114u, 318, 159 },
    { 1024u, 768u, 16u, 2048u, 0x0117u, 407, 203 }
};

const V9X_HW16_OPS v9x_hw16 = {
    "s3-trio64",
    v9x_trio_devices,
    (unsigned short)(sizeof(v9x_trio_devices) / sizeof(v9x_trio_devices[0])),
    v9x_trio_modes,
    (unsigned short)(sizeof(v9x_trio_modes) / sizeof(v9x_trio_modes[0])),
    V9X_HW16_VBE_NO_CLEAR,
    0x03ffu, 0xffffu,
    v9x_s3_publish_diagnostics,
    0,
    v9x_s3_read_aperture,
    /* No CR53 new-MMIO window on this chip: the shared sequence is all of it. */
    v9x_s3_enable_linear_aperture,
    0
};
