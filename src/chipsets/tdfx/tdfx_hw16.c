/*
 * The 3dfx family table.
 *
 * One chip at tier-0, so there is nothing to dispatch on and no hook to
 * supply: the VBE 4F02h mode set programs the card, 4F01h reports where the
 * framebuffer landed, 4F00h reports its size, and the CPU draws
 * (docs\plans\3dfx-vsa100-family.md, Phase 2).
 *
 * Two hooks. The memory size: VBE 4F00h reports 16 MiB on the 32 MiB
 * Voodoo4 4500, and the BIOS leaves the last 4 KiB page of BAR1's linear
 * view tiled (docs\decisions\2026-10-11-voodoo4-runs-velocity9x-at-tier-0-
 * and-its-vbe-reports-half-its-memory.md), so the size comes from the chip:
 * the dramInit0 straps, clamped to the first tiled page. And the desktop's
 * place: above the memory 3dfx's Glide is given, as 3dfx's own driver puts
 * it, because Glide's command FIFO sits at the bottom of memory
 * (docs\issues\2026-10-12-glide-cmdfifo-overlaps-the-desktop.md).
 */
#include "velocity9x/hw16.h"
#include "velocity9x/tdfx_vsa100.h"

/* enable16.c, filled in by the tier-0 stage-3 default from VBE 4F00h. */
extern unsigned long v9x_vbe_vram_reported;

/* runtime.asm, V9X_TDFX_FAMILY: dramInit0, tile control and tile compare
 * through BAR2, and one init or video register either way. Non-zero on
 * success. */
extern unsigned short __far __pascal V9xTdfxReadMemoryRegs(
    unsigned long __far *regs);
extern unsigned short __far __pascal V9xTdfxIoRead(unsigned short offset,
                                                    unsigned long __far *value);
extern unsigned short __far __pascal V9xTdfxIoWrite(unsigned short offset,
                                                     unsigned long value);

/* The raw registers and the decision, kept for the diagnostics. The tile
 * page is the one found at the first read: the BIOS's, or a Glide session's
 * the BIOS did not clear. */
static unsigned long v9x_tdfx_dram_init0;
static unsigned long v9x_tdfx_tile_first_page;
static unsigned long v9x_tdfx_usable_bytes;
static unsigned long v9x_tdfx_desktop_offset_bytes;
static unsigned long v9x_tdfx_glide_fb_bytes;
static unsigned short v9x_tdfx_layout_known;
/* What place_desktop last did: 1 programmed, 0 refused. Diagnostic. */
static unsigned short v9x_tdfx_desktop_placed;

extern const V9X_HW16_DEVICE v9x_vsa100_device;

static const V9X_HW16_DEVICE * const v9x_tdfx_devices[] = {
    &v9x_vsa100_device
};

/*
 * The VESA-standard rows. Every mode number is in the Voodoo4 4500 BIOS
 * 1.15's own 33-entry list (ROM offset 7C20h, FFFFh-terminated), in
 * docs\probe\a8u4i5-voodoo4-registers-2026-10-11\VSA100-ROM-IMAGE.BIN.
 * Whether the BIOS honours the linear-framebuffer bit for each, and the pitch
 * it reports, is not yet measured; the first enable's V9XMODES.INI says. Only
 * 111h has been set by VBE on a VSA-100 (the Mini-Me, under VelocityNT).
 *
 * The list's 112h/115h/118h and the 3dfx-numbered modes are left to the
 * boot-time BIOS merge rather than claimed here.
 *
 * 640x400 sits after the other 8-bpp rows so this list runs in the order of
 * the MODES registry key GDI enumerates, as in every other family.
 */
static const V9X_HW16_MODE v9x_tdfx_modes[] = {
    {  640u, 480u,  8u,  640u, 0x0101u, 254, 127 },
    {  800u, 600u,  8u,  800u, 0x0103u, 318, 159 },
    { 1024u, 768u,  8u, 1024u, 0x0105u, 407, 203 },
    {  640u, 400u,  8u,  640u, 0x0100u, 254, 127 },
    {  640u, 480u, 16u, 1280u, 0x0111u, 254, 127 },
    {  800u, 600u, 16u, 1600u, 0x0114u, 318, 159 },
    { 1024u, 768u, 16u, 2048u, 0x0117u, 407, 203 }
};

/* Decimal, into a caller-owned buffer of at least 11 bytes. Local for the
 * reason ati_hw16.c gives: the shared helper lives in an object that carries
 * another vendor's register sequence. */
static void v9x_tdfx_format_u32(char *text, unsigned long value)
{
    char digits[11];
    unsigned short count = 0u;
    unsigned short at = 0u;

    do {
        digits[count++] = (char)('0' + (unsigned short)(value % 10ul));
        value /= 10ul;
    } while (value != 0ul && count < (unsigned short)sizeof(digits));
    while (count != 0u) {
        text[at++] = digits[--count];
    }
    text[at] = '\0';
}

/* The chip's own word, or the family default when it carries none; see the
 * note in ati_hw16.c on why these are never literals. */
static const char *v9x_tdfx_word(const char *value, const char *fallback)
{
    return value != 0 ? value : fallback;
}

/*
 * The memory layout, decided once: the dramInit0 straps decoded and clamped
 * to the tile boundary the BIOS leaves - not the one found, which after a
 * Glide session is Glide's, because place_desktop writes the BIOS's back -
 * and split between Glide and the desktop by vsa100_decode.c. 0 when the
 * registers cannot be read or do not decode.
 */
static unsigned short v9x_tdfx_layout(void)
{
    unsigned long regs[3];

    if (v9x_tdfx_layout_known != 0u) {
        return 1u;
    }
    if (V9xTdfxReadMemoryRegs((unsigned long __far *)regs) == 0u) {
        return 0u;
    }
    v9x_tdfx_dram_init0 = regs[0];
    v9x_tdfx_tile_first_page = v9x_tdfx_vsa100_tile_first_page(regs[1],
                                                                regs[2]);
    v9x_tdfx_usable_bytes = v9x_tdfx_vsa100_usable_memory(
        regs[0], V9X_TDFX_LFB_BIOS_TILE_CONTROL, 0ul);
    if (v9x_tdfx_usable_bytes == 0ul) {
        return 0u;
    }
    v9x_tdfx_desktop_offset_bytes =
        v9x_tdfx_vsa100_desktop_offset(v9x_tdfx_usable_bytes);
    v9x_tdfx_glide_fb_bytes =
        v9x_tdfx_vsa100_glide_bytes(v9x_tdfx_usable_bytes);
    v9x_tdfx_layout_known = 1u;
    return 1u;
}

/*
 * Video memory for the driver: from the desktop to the end of the linear
 * view, so the DirectDraw heap never reaches Glide's memory below or the
 * tiled view above. 0 when the layout is unknown, which leaves the enable
 * sequence on VBE 4F00h.
 */
static unsigned long v9x_tdfx_read_video_memory(void)
{
    if (v9x_tdfx_layout() == 0u) {
        return 0ul;
    }
    return v9x_tdfx_usable_bytes - v9x_tdfx_desktop_offset_bytes;
}

/*
 * Scan the desktop out from its place in the layout, after the BIOS mode
 * set. Four writes through BAR2, each read back where it is a plain
 * register:
 *
 * 1. lfbMemoryConfig: the BIOS's tile control, so BAR1's linear view
 *    reaches the desktop again. Glide moves the boundary to its own buffers
 *    and leaves it there, and the mode set does not restore it.
 * 2. vidProcCfg: desktop fetch on, overlay off, linear. A Glide session
 *    that never closed leaves its overlay showing through a reset and a
 *    mode set (A8U4I5 boot 425).
 * 3. vidDesktopOverlayStride: the mode's pitch, for the same reason.
 * 4. vidDesktopStartAddr: the offset.
 *
 * Returns the offset, or 0 when the registers cannot be reached or the
 * start address did not take, and then writes the start back to 0, so the
 * scanout and the mapping Enable makes from the answer agree.
 */
static unsigned long v9x_tdfx_place_desktop(unsigned short pitch_bytes)
{
    unsigned long value;

    v9x_tdfx_desktop_placed = 0u;
    if (v9x_tdfx_layout() == 0u ||
        V9xTdfxIoWrite((unsigned short)V9X_TDFX_LFB_MEMORY_CONFIG,
                       V9X_TDFX_LFB_BIOS_TILE_CONTROL) == 0u) {
        return 0ul;
    }
    if (V9xTdfxIoRead((unsigned short)V9X_TDFX_VID_PROC_CFG,
                      (unsigned long __far *)&value) == 0u ||
        V9xTdfxIoWrite((unsigned short)V9X_TDFX_VID_PROC_CFG,
                       v9x_tdfx_vsa100_desktop_vidproc(value)) == 0u) {
        return 0ul;
    }
    if (V9xTdfxIoRead((unsigned short)V9X_TDFX_VID_DESKTOP_STRIDE,
                      (unsigned long __far *)&value) == 0u ||
        V9xTdfxIoWrite((unsigned short)V9X_TDFX_VID_DESKTOP_STRIDE,
                       v9x_tdfx_vsa100_desktop_stride(value,
                                                      pitch_bytes)) == 0u) {
        return 0ul;
    }
    if (V9xTdfxIoWrite((unsigned short)V9X_TDFX_VID_DESKTOP_START,
                       v9x_tdfx_desktop_offset_bytes) == 0u ||
        V9xTdfxIoRead((unsigned short)V9X_TDFX_VID_DESKTOP_START,
                      (unsigned long __far *)&value) == 0u ||
        value != v9x_tdfx_desktop_offset_bytes) {
        (void)V9xTdfxIoWrite((unsigned short)V9X_TDFX_VID_DESKTOP_START,
                             0ul);
        return 0ul;
    }
    v9x_tdfx_desktop_placed = 1u;
    return v9x_tdfx_desktop_offset_bytes;
}

/*
 * For dd16.c's EXT_HWC answer and gdi_accel.c's 2D engine arm: the
 * desktop's byte offset in video memory as last placed, and the fbRam Glide
 * is told. A placement that failed left the desktop at 0, and Glide is then
 * told all the linear memory, as before the split: the overlap is back, but
 * every address agrees with the scanout.
 */
unsigned long v9x_tdfx_desktop_offset(void)
{
    return v9x_tdfx_desktop_placed != 0u ? v9x_tdfx_desktop_offset_bytes
                                         : 0ul;
}

unsigned long v9x_tdfx_glide_bytes(void)
{
    if (v9x_tdfx_layout() == 0u) {
        return 0ul;
    }
    return v9x_tdfx_desktop_placed != 0u ? v9x_tdfx_glide_fb_bytes
                                         : v9x_tdfx_usable_bytes;
}

/*
 * Key order is the diagnostic contract; see the note in s3_regs16.c. The
 * clock is not read, so the clock key says what is true; the memory keys
 * appended last are the hook's inputs and answer.
 */
static void v9x_tdfx_publish_diagnostics(const V9X_HW16_DEVICE *device,
                                         v9x_hw16_write_fn write)
{
    char number[11];

    write("SchemaVersion", "1");
    write("Adapter", device->adapter);
    write("VendorId", device->vendor_text);
    write("DeviceId", device->device_text);
    write("ClockDetector", device->clock_detector);
    write("ClockStatus", "unavailable");
    write("ModeSwitching", device->mode_switching);
    write("Acceleration", v9x_tdfx_word(device->acceleration, "none"));
    write("Direct3D", v9x_tdfx_word(device->direct3d, "not-advertised"));
    if (v9x_vbe_vram_reported != 0ul) {
        v9x_tdfx_format_u32(number, v9x_vbe_vram_reported);
        write("VbeVramBytes", number);
    } else {
        write("VbeVramBytes", "unavailable");
    }
    if (v9x_tdfx_usable_bytes != 0ul) {
        v9x_tdfx_format_u32(number, v9x_tdfx_dram_init0);
        write("DramInit0", number);
        v9x_tdfx_format_u32(number, v9x_tdfx_tile_first_page);
        write("TileFirstPage", number);
        v9x_tdfx_format_u32(number, v9x_tdfx_usable_bytes);
        write("VramUsableBytes", number);
    } else {
        write("VramUsableBytes", "unavailable");
    }
    /* The desktop's place and Glide's share: the place_desktop decision. */
    v9x_tdfx_format_u32(number, v9x_tdfx_desktop_offset());
    write("DesktopOffset", number);
    write("DesktopPlaced", v9x_tdfx_desktop_placed != 0u ? "yes" : "no");
    v9x_tdfx_format_u32(number, v9x_tdfx_glide_bytes());
    write("GlideFbBytes", number);
}

const V9X_HW16_OPS v9x_hw16 = {
    "3dfx",
    v9x_tdfx_devices,
    (unsigned short)(sizeof(v9x_tdfx_devices) / sizeof(v9x_tdfx_devices[0])),
    v9x_tdfx_modes,
    (unsigned short)(sizeof(v9x_tdfx_modes) / sizeof(v9x_tdfx_modes[0])),
    /* The generic linear-framebuffer bit, and VBE's don't-clear bit. The
     * BIOS clears from offset 0, where Glide's command FIFO lives; a game
     * that restores the desktop mode before Glide has turned its FIFO off
     * (Rollcage on WM_CLOSE, A8U4I5 boot 427) then had the clear land in an
     * enabled FIFO and jam it for the next Glide session. The desktop is
     * repainted after every mode set, so nothing needs the clear. */
    V9X_HW16_VBE_LINEAR | V9X_HW16_VBE_NO_CLEAR,
    /* 32 MiB: one VSA-100's memory, the Voodoo4 4500's whole 32 MiB, and so
     * also the ceiling this family believes from 4F00h. The ceiling matters
     * twice: the Mini-Me's BIOS left only about 32 MiB of its 64 linear, and
     * a Voodoo5's BIOS is reported to sum memory across chips while the
     * desktop is served by one (plan, "Facts from documents only"). */
    0x01ffu, 0xffffu,
    v9x_tdfx_publish_diagnostics,
    0,
    /* NULL: the mode set is sufficient at tier-0. */
    0,
    /* NULL: ask the BIOS through 4F01h. */
    0,
    v9x_tdfx_read_video_memory,
    /* NULL: CreateDIBPDevice builds the screen PDEVICE. */
    0,
    /* Strict, for the reason ati_hw16.c gives: a vendor package stays bound
     * to the hardware it names. */
    0u,
    /* NULL: no non-PCI identity. */
    0,
    /* NULL: DirectDraw gets everything read_video_memory reports. */
    0,
    /* The BIOS packs the pitch. */
    0u,
    /* NULL: the straps are trusted. */
    0,
    v9x_tdfx_place_desktop
};
