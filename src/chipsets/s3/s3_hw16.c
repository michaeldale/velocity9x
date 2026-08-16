/*
 * S3 family hardware table.
 *
 * One binary, both chips. This file holds what the ViRGE/DX and the Trio32/64
 * genuinely share - the audited VBE mode table, the mode-set flags, the
 * aperture mapping size, the diagnostics publisher and the CR59/CR5A aperture
 * read - and nothing that is true of only one of them.
 *
 * Everything that differs stays in the chip modules and is reached through the
 * device list below. Each entry carries its own enable_aperture and
 * fill_engine_descriptor, so the ViRGE opens its new-MMIO window and describes
 * an S3D engine while the Trio64 does neither, out of the same image. The PCI
 * scan picks the entry; nothing in this file branches on chip.
 *
 * Device order is the scan order. ViRGE first is not load bearing: a machine
 * has one of these cards, so the two ids cannot both match.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/s3_regs16.h"

extern const V9X_HW16_DEVICE v9x_virge_device;
extern const V9X_HW16_DEVICE v9x_trio_device;

static const V9X_HW16_DEVICE * const v9x_s3_devices[] = {
    &v9x_virge_device,
    &v9x_trio_device
};

/*
 * Both chips take the same VBE modes: the same S3 BIOS mode numbers and the
 * same audited pitches. A table per chip would have been two copies of one
 * fact, and the pair could then disagree.
 */
static const V9X_HW16_MODE v9x_s3_modes[] = {
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 },
    {  800u, 600u,  8u,  800u, 0x0103u, 318, 159 },
    { 1024u, 768u,  8u, 1024u, 0x0105u, 407, 203 },
    /* 640x400 is VBE mode 100h, the first mode VESA defined and the default
     * screen size Doom95 asks DirectDraw for. Without it SetDisplayMode fails,
     * the game keeps the 16-bpp desktop mode and writes its 8-bpp frame into
     * it: one byte per pixel into a two-byte pitch renders the picture at half
     * width in garbage colours. It sits after the other 8-bpp entries so this
     * list runs in the same order as the MODES registry key GDI enumerates. */
    {  640u, 400u,  8u,  640u, 0x0100u, 254, 127 },
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    {  800u, 600u, 16u, 1600u, 0x0114u, 318, 159 },
    { 1024u, 768u, 16u, 2048u, 0x0117u, 407, 203 }
};

const V9X_HW16_OPS v9x_hw16 = {
    "s3",
    v9x_s3_devices,
    (unsigned short)(sizeof(v9x_s3_devices) / sizeof(v9x_s3_devices[0])),
    v9x_s3_modes,
    (unsigned short)(sizeof(v9x_s3_modes) / sizeof(v9x_s3_modes[0])),
    /* The Windows 98 S3 ViRGE sample uses the S3/VBE no-clear flag for these
     * modes, and the Trio64 BIOS wants the same. Neither honours the generic
     * VBE linear-framebuffer bit, which is why this family cannot use the
     * tier-0 value. */
    V9X_HW16_VBE_NO_CLEAR,
    /* Map the complete 64-MiB PCI BAR: the first 4 MiB is allocatable VRAM and
     * the ViRGE new-MMIO window sits at BAR + 16 MiB. The Trio64 maps the same
     * span and simply uses less of it. */
    0x03ffu, 0xffffu,
    v9x_s3_publish_diagnostics,
    /* The mode set needs no follow-up on either chip. */
    0,
    v9x_s3_read_aperture,
    /* CreateDIBPDevice builds the screen PDEVICE on both. */
    0,
    /* The card must be one of ours: CR59/CR5A and the CR58/CR53 pokes below
     * are S3 registers and mean something else on anything not S3. */
    0u
};
