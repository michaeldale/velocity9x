/*
 * Velocity9x VBE inventory (real-mode DOS, query only).
 *
 * Dumps what the video BIOS says its modes are, so a mode row is never
 * committed to a family table against a guess. Nothing here sets a mode: every
 * call is 4F00h, 4F01h or 4F03h, and the report records Access=query-only so a
 * dump cannot be mistaken for a mode-change test.
 *
 * Two sources of mode numbers, and the report says which a mode came from:
 *
 *   - the BIOS's own list, walked from VideoModePtr. This is the only way to
 *     see widescreen and OEM modes, which have no standard VESA numbers.
 *   - a fixed probe list of standard numbers, tried whether or not the BIOS
 *     advertises them. A BIOS that supports a mode without listing it is
 *     common enough to be worth the extra calls, and the ones that matter here
 *     are the high-colour numbers: VESA never assigned 32bpp numbers at all,
 *     so BIOSes disagree about whether 0x112/0x115/0x118 are packed 24bpp or
 *     32bpp. Which one a given card means is exactly what this tool is for.
 *
 * The mode numbers are copied out of VideoModePtr *before* the first 4F01h
 * call. The pointer is allowed to target the caller's own block, which 4F01h
 * would then overwrite - walking it lazily reads back mode information as if
 * it were mode numbers.
 */
#include <dos.h>
#include <stdio.h>
#include <string.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_VBE_REPORT "C:\\V9XVBE.TXT"

/* VBE 2.0 says a mode list may be any length; 128 is well past every BIOS
 * this project has met and bounds the staging array in the small model. */
#define V9X_MODE_LIST_MAX 128

static unsigned char controller_info[512];
static unsigned char mode_info[256];

static unsigned short mode_list[V9X_MODE_LIST_MAX];
static unsigned short mode_list_count;
static unsigned short mode_list_terminated;
static unsigned short mode_list_overflow;

/*
 * Standard numbers probed regardless of the BIOS list.
 *
 * 0x0107 and 0x011A are the 1280x1024 8- and 16-bpp entries; the rest are the
 * 15/24-bpp numbers, which is where a card reveals whether its "24bpp" mode is
 * really packed 24 or 32.
 */
static const unsigned short probe_modes[] = {
    0x0107u, 0x0110u, 0x0112u, 0x0113u, 0x0115u, 0x0116u,
    0x0118u, 0x0119u, 0x011Au, 0x011Bu
};

static unsigned short v9x_u16(const unsigned char *data)
{
    return (unsigned short)((unsigned short)data[0] |
                            ((unsigned short)data[1] << 8));
}

static unsigned long v9x_u32(const unsigned char *data)
{
    return (unsigned long)data[0] | ((unsigned long)data[1] << 8) |
           ((unsigned long)data[2] << 16) | ((unsigned long)data[3] << 24);
}

static unsigned short v9x_vbe_call(unsigned short function,
                                   unsigned short argument,
                                   void far *buffer)
{
    union REGS input;
    union REGS output;
    struct SREGS segments;

    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    segread(&segments);
    input.x.ax = function;
    input.x.cx = argument;
    if (buffer != 0) {
        segments.es = FP_SEG(buffer);
        input.x.di = FP_OFF(buffer);
    }
    int86x(0x10, &input, &output, &segments);
    return output.x.ax;
}

/* Copy the BIOS mode list into mode_list. Must run before any 4F01h. */
static void v9x_stage_mode_list(unsigned long video_mode_ptr)
{
    const unsigned short far *entry;
    unsigned short value;

    mode_list_count = 0u;
    mode_list_terminated = 0u;
    mode_list_overflow = 0u;

    if (video_mode_ptr == 0ul) {
        return;
    }
    entry = (const unsigned short far *)MK_FP(
        (unsigned short)(video_mode_ptr >> 16),
        (unsigned short)(video_mode_ptr & 0xffffu));

    for (;;) {
        value = *entry++;
        if (value == 0xffffu) {
            mode_list_terminated = 1u;
            return;
        }
        if (mode_list_count >= V9X_MODE_LIST_MAX) {
            mode_list_overflow = 1u;
            return;
        }
        mode_list[mode_list_count++] = value;
    }
}

static unsigned short v9x_mode_listed(unsigned short mode)
{
    unsigned short index;

    for (index = 0u; index < mode_list_count; ++index) {
        if (mode_list[index] == mode) {
            return 1u;
        }
    }
    return 0u;
}

static void v9x_report_mode(FILE *report, unsigned short mode,
                            const char *source)
{
    unsigned short status;

    memset(mode_info, 0, sizeof(mode_info));
    status = v9x_vbe_call(0x4f01u, mode, mode_info);
    fprintf(report, "Mode%04X.Source=%s\n", mode, source);
    fprintf(report, "Mode%04X.Status=%04X\n", mode, status);
    if (status != 0x004fu) return;
    fprintf(report, "Mode%04X.Attributes=%04X\n", mode, v9x_u16(mode_info));
    fprintf(report, "Mode%04X.Width=%u\n", mode, v9x_u16(mode_info + 18));
    fprintf(report, "Mode%04X.Height=%u\n", mode, v9x_u16(mode_info + 20));
    fprintf(report, "Mode%04X.Planes=%u\n", mode, mode_info[24]);
    fprintf(report, "Mode%04X.BitsPerPixel=%u\n", mode, mode_info[25]);
    fprintf(report, "Mode%04X.MemoryModel=%u\n", mode, mode_info[27]);
    fprintf(report, "Mode%04X.BytesPerScanLine=%u\n", mode,
            v9x_u16(mode_info + 16));
    fprintf(report, "Mode%04X.PhysicalBase=%08lX\n", mode,
            v9x_u32(mode_info + 40));
    fprintf(report, "Mode%04X.LinearBytesPerScanLine=%u\n", mode,
            v9x_u16(mode_info + 50));
    fprintf(report, "Mode%04X.RedMask=%u@%u\n", mode,
            mode_info[31], mode_info[32]);
    fprintf(report, "Mode%04X.GreenMask=%u@%u\n", mode,
            mode_info[33], mode_info[34]);
    fprintf(report, "Mode%04X.BlueMask=%u@%u\n", mode,
            mode_info[35], mode_info[36]);
    /* The reserved field is what separates a 32bpp mode from a packed 24bpp
     * one when both report BitsPerPixel=32, and it is where an alpha or unused
     * byte lives. DirectColorModeInfo says whether the ramp is programmable. */
    fprintf(report, "Mode%04X.RsvdMask=%u@%u\n", mode,
            mode_info[37], mode_info[38]);
    fprintf(report, "Mode%04X.DirectColorModeInfo=%02X\n", mode,
            mode_info[39]);
}

int main(void)
{
    FILE *report;
    unsigned short status;
    unsigned short index;
    unsigned long video_mode_ptr = 0ul;
    union REGS input;
    union REGS output;

    report = fopen(V9X_VBE_REPORT, "wt");
    if (report == 0) return 1;
    fprintf(report, "Velocity9x VBE inventory\n");
    fprintf(report, "Build=%s\n", V9X_BUILD_ID);
    fprintf(report, "Access=query-only\n");

    memset(controller_info, 0, sizeof(controller_info));
    memcpy(controller_info, "VBE2", 4);
    status = v9x_vbe_call(0x4f00u, 0u, controller_info);
    fprintf(report, "ControllerStatus=%04X\n", status);
    if (status == 0x004fu) {
        fprintf(report, "Signature=%.4s\n", controller_info);
        fprintf(report, "Version=%04X\n", v9x_u16(controller_info + 4));
        fprintf(report, "TotalMemory64K=%u\n", v9x_u16(controller_info + 18));
        video_mode_ptr = v9x_u32(controller_info + 14);
    }

    /* Before any 4F01h: the list may live in the block 4F01h overwrites. */
    v9x_stage_mode_list(video_mode_ptr);
    fprintf(report, "VideoModePtr=%04X:%04X\n",
            (unsigned short)(video_mode_ptr >> 16),
            (unsigned short)(video_mode_ptr & 0xffffu));
    fprintf(report, "ModeListCount=%u\n", mode_list_count);
    fprintf(report, "ModeListTerminated=%s\n",
            mode_list_terminated != 0u ? "yes" : "no");
    fprintf(report, "ModeListOverflow=%s\n",
            mode_list_overflow != 0u ? "yes" : "no");

    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    input.x.ax = 0x4f03u;
    int86(0x10, &input, &output);
    fprintf(report, "CurrentModeStatus=%04X\n", output.x.ax);
    if (output.x.ax == 0x004fu) {
        fprintf(report, "CurrentMode=%04X\n", output.x.bx);
    }

    for (index = 0u; index < mode_list_count; ++index) {
        v9x_report_mode(report, mode_list[index], "list");
    }
    for (index = 0u;
         index < (unsigned short)(sizeof(probe_modes) / sizeof(probe_modes[0]));
         ++index) {
        if (v9x_mode_listed(probe_modes[index]) == 0u) {
            v9x_report_mode(report, probe_modes[index], "probe");
        }
    }

    fprintf(report, "Result=%s\n", status == 0x004fu ? "PASS" : "FAIL");
    fclose(report);
    puts("Velocity9x VBE inventory complete; no mode was changed.");
    return status == 0x004fu ? 0 : 2;
}
