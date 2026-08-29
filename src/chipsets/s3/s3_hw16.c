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
 *
 * The entries after the Trio64 are its aliases: ids the Trio64 chip module
 * drives with the same two hooks and the same modes, bound but validated
 * nowhere. They are declared in that module, not this one, so the per-object
 * audit still sees one object per chip's code - and so the list here stays a
 * scan order rather than becoming a second place that describes a chip.
 */
#include "velocity9x/hw16.h"
#include "velocity9x/s3_regs16.h"

extern const V9X_HW16_DEVICE v9x_virge_device;
extern const V9X_HW16_DEVICE v9x_trio_device;
extern const V9X_HW16_DEVICE v9x_trio32_device;
extern const V9X_HW16_DEVICE v9x_aurora64_device;
extern const V9X_HW16_DEVICE v9x_trio32_64_device;
extern const V9X_HW16_DEVICE v9x_trio64uv_device;
extern const V9X_HW16_DEVICE v9x_trio64v2_device;

static const V9X_HW16_DEVICE * const v9x_s3_devices[] = {
    &v9x_virge_device,
    &v9x_trio_device,
    &v9x_trio32_device,
    &v9x_aurora64_device,
    &v9x_trio32_64_device,
    &v9x_trio64uv_device,
    &v9x_trio64v2_device
};

/*
 * Both chips take the same VBE modes: the same S3 BIOS mode numbers and the
 * same audited pitches. A table per chip would have been two copies of one
 * fact, and the pair could then disagree.
 */
/*
 * Every row here is one a BIOS dump showed, per the Stage 0 gate in
 * docs\decisions\2026-08-20-vbe-mode-inventory.md. Four S3 BIOSes were
 * measured - the physical Trio64, the 86Box ViRGE/DX and the 86Box Trio64 -
 * and two things they said shape this table:
 *
 * The high-colour numbers are 32 bpp on all of them, never 24. 0x0112, 0x0115
 * and 0x0118 each report BitsPerPixel=32 with a scan line of width*4 and a
 * reserved byte at 8@24. There is no packed 24-bpp mode in any S3 BIOS here,
 * so this family has no 24-bpp row and should not be given one on the strength
 * of the VESA numbering.
 *
 * The last two rows are listed by the 4 MiB cards and refused by the 2 MiB
 * physical Trio64, which has not the memory for them. They stay in the shared
 * table because ValidateMode now measures a mode against the card's own VRAM
 * and refuses what will not fit, so the row is honest on both.
 *
 * Deliberately absent: 1600x1200x8 (0x0120). It fits 2 MiB and the 4 MiB cards
 * list it, but the physical Trio64's BIOS does not - and a VRAM check catches a
 * mode too large, not a mode missing, so that row would validate and then fail
 * at 4F02h. Same for the ViRGE's 320x200 modes.
 */
static const V9X_HW16_MODE v9x_s3_modes[] = {
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 },
    {  800u, 600u,  8u,  800u, 0x0103u, 318, 159 },
    { 1024u, 768u,  8u, 1024u, 0x0105u, 407, 203 },
    { 1280u, 1024u, 8u, 1280u, 0x0107u, 508, 254 },
    /* 640x400 is VBE mode 100h, the first mode VESA defined and the default
     * screen size Doom95 asks DirectDraw for. Without it SetDisplayMode fails,
     * the game keeps the 16-bpp desktop mode and writes its 8-bpp frame into
     * it: one byte per pixel into a two-byte pitch renders the picture at half
     * width in garbage colours. It sits after the other 8-bpp entries so this
     * list runs in the same order as the MODES registry key GDI enumerates.
     *
     * KNOWN EXCEPTION to the "leave it out" rule below, and the one row in this
     * table that breaks it: BARRY's ROM does not list 0x0100 at all. Its list
     * runs 0101..011B plus 0211, and 0x0100 is one of the modes only the two
     * 4 MiB targets add (2026-08-20-vbe-mode-inventory.md:133). So on that card
     * the row validates - it is in the table and 256 KiB fits 2 MiB - and then
     * fails at 4F02h, exactly the failure the 1600x1200x8 paragraph predicts.
     * Measured 2026-08-26: selecting it as the desktop mode gives
     * Stage=fail-hardware-vbe-mode, a fall back to 640x480, and a modal Display
     * applet (2026-08-26-s3-physical-pipeline-inert.md §8). The row is kept
     * because removing it costs Doom95 its mode on every target whose BIOS does
     * list it, which is every other S3 target measured; what to do about the
     * older ROM is unresolved and tracked in that note. */
    {  640u, 400u,  8u,  640u, 0x0100u, 254, 127 },
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    {  800u, 600u, 16u, 1600u, 0x0114u, 318, 159 },
    { 1024u, 768u, 16u, 2048u, 0x0117u, 407, 203 },
    { 1280u, 1024u, 16u, 2560u, 0x011Au, 508, 254 },
    {  640u, 480u, 32u, 2560u, 0x0112u, 254, 127 },
    {  800u, 600u, 32u, 3200u, 0x0115u, 318, 159 },
    { 1024u, 768u, 32u, 4096u, 0x0118u, 407, 203 }
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
    /* CR36 decodes the installed size on both chips. Without this the heap was
     * 4 MiB by assumption, which is right on a 4 MiB card and twice the truth
     * on a 2 MiB Trio64. */
    v9x_s3_read_video_memory,
    /* CreateDIBPDevice builds the screen PDEVICE on both. */
    0,
    /* The card must be one of ours: CR59/CR5A and the CR58/CR53 pokes below
     * are S3 registers and mean something else on anything not S3. This stays
     * zero - it is the "proceed without knowing" flag, and this family never
     * should. What it gains instead is a second way to know. */
    0u,
    /*
     * On a VESA Local Bus machine there is no configuration space for the scan
     * to have matched, so the identity comes off the chip: CR2D/CR2E hold the
     * same device id the PCI parts publish. Reads only, and accepts only the
     * two ids this binary already names, so the register pokes above still run
     * on nothing but a card this family recognises.
     */
    v9x_s3_identify_without_pci
};
