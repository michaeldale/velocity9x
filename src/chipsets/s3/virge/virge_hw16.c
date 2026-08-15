/*
 * S3 ViRGE/DX 86C375 hardware table.
 *
 * The chip data that used to be #ifdef'd into src\display16\ddi.c: the PCI
 * identity, the audited VBE mode table, and the strings published to
 * C:\V9XHW.INI. Register access is shared with the other S3 families in
 * src\chipsets\s3\common\s3_regs16.c.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/s3_regs16.h"

/* Not static: the link map is where the per-chip audit looks for it, now that
 * the PCI identity is data in this object rather than an immediate in the
 * assembled runtime. */
const V9X_HW16_DEVICE v9x_virge_devices[] = {
    {
        0x5333u, 0x8a01u,
        "S3 ViRGE/DX 86C375",
        "5333", "8A01",
        "s3-virge-pll-v1",
        "live-any-depth",
        "directdraw-fill-blt",
        "hardware-s3d"
    }
};

static const V9X_HW16_MODE v9x_virge_modes[] = {
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 },
    {  800u, 600u,  8u,  800u, 0x0103u, 318, 159 },
    { 1024u, 768u,  8u, 1024u, 0x0105u, 407, 203 },
    /* 640x400 is VBE mode 100h, the first mode VESA defined and the default
     * screen size Doom95 asks DirectDraw for. Without it SetDisplayMode
     * fails, the game keeps the 16-bpp desktop mode and writes its 8-bpp
     * frame into it: one byte per pixel into a two-byte pitch renders the
     * picture at half width in garbage colours. It sits after the other
     * 8-bpp entries so this list runs in the same order as the MODES
     * registry key GDI enumerates. */
    {  640u, 400u,  8u,  640u, 0x0100u, 254, 127 },
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    {  800u, 600u, 16u, 1600u, 0x0114u, 318, 159 },
    { 1024u, 768u, 16u, 2048u, 0x0117u, 407, 203 }
};

const V9X_HW16_OPS v9x_hw16 = {
    "s3-virge",
    v9x_virge_devices,
    (unsigned short)(sizeof(v9x_virge_devices) / sizeof(v9x_virge_devices[0])),
    v9x_virge_modes,
    (unsigned short)(sizeof(v9x_virge_modes) / sizeof(v9x_virge_modes[0])),
    /* The Windows 98 S3 ViRGE sample uses the S3/VBE no-clear flag for these
     * modes. It only requests the generic VBE linear-framebuffer bit on GX2,
     * not on the 86C375 targeted here. */
    V9X_HW16_VBE_NO_CLEAR,
    /* Map the complete 64-MiB PCI BAR: the first 4 MiB is allocatable VRAM
     * and the ViRGE new-MMIO window sits at BAR + 16 MiB. */
    0x03ffu, 0xffffu,
    v9x_s3_publish_diagnostics,
    /* CreateDIBPDevice builds the screen PDEVICE on this target. */
    0
};
