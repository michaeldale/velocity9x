/*
 * Intel GMA (Gen3) DOS survey - the Phase 0 evidence tool for the intel-gma
 * family. Query-only: no mode is set, no register is written, no PCI config
 * write is issued.
 *
 * What it gathers, and why:
 *
 *  - The FULL VBE mode list, walked from the controller block's VideoModePtr
 *    with 4F01h per mode. The generic vbe inventory probes six hard-coded
 *    numbers, which cannot prove a mode's absence; this family needs to know
 *    exactly what the Intel VBIOS publishes (and that 1024x576 is not in it)
 *    before the manifest's mode table is trusted.
 *  - PCI identity and BARs for the IGD (function 0), via INT 1Ah PCI BIOS
 *    word reads: vendor/device/revision/subsystem, BAR0 (MMIO), BAR1 (I/O),
 *    BAR2 (GMADR aperture - where 4F01h's PhysBasePtr should land), BAR3
 *    (GTT, Gen3 only).
 *  - GGC (host bridge config 0x52, stolen-memory size field) and BSM (IGD
 *    config 0x5C, stolen-memory base) - the UMA facts the audit record needs
 *    and the eventual native aperture diagnostics will publish.
 *  - A VBE/DDC EDID attempt (4F15h), for the panel's native 1024x576 timing.
 *  - The VBIOS shadow at C000:0000, saved whole for offline mode-table
 *    analysis (the 915resolution BT_3 table lives in it).
 *
 * Run from real DOS, never a DOS box: the ati bring-up measured that a DOS
 * box under a running Windows returns artefacts (see the VbeVramBytes note in
 * intel_hw16.c).
 */
#include <dos.h>
#include <stdio.h>
#include <string.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_INTEL_REPORT "C:\\V9XINTL.TXT"
#define V9X_VBIOS_IMAGE  "C:\\V9XVBIOS.BIN"

#define V9X_PCI_VENDOR_INTEL  0x8086u
#define V9X_PCI_DEVICE_945GSE 0x27aeu

static unsigned char controller_info[512];
static unsigned char mode_info[256];
static unsigned char edid_block[128];

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

/*
 * PCI BIOS INT 1Ah, word-granular config reads only. The dword read (B10Ah)
 * answers in ECX, which a 16-bit union REGS cannot see - the same reason
 * runtime.asm owns the driver's BAR0 read - so dwords are assembled from two
 * word reads instead. Returns 1 on success with the word in *value.
 */
static unsigned short v9x_pci_read_word(unsigned char bus,
                                        unsigned char device_function,
                                        unsigned short reg,
                                        unsigned short *value)
{
    union REGS input;
    union REGS output;

    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    input.x.ax = 0xb109u;
    input.h.bh = bus;
    input.h.bl = device_function;
    input.x.di = reg;
    int86(0x1a, &input, &output);
    if (output.x.cflag != 0 || output.h.ah != 0) {
        return 0;
    }
    *value = output.x.cx;
    return 1;
}

static unsigned short v9x_pci_read_dword(unsigned char bus,
                                         unsigned char device_function,
                                         unsigned short reg,
                                         unsigned long *value)
{
    unsigned short low;
    unsigned short high;

    if (v9x_pci_read_word(bus, device_function, reg, &low) == 0 ||
        v9x_pci_read_word(bus, device_function,
                          (unsigned short)(reg + 2u), &high) == 0) {
        return 0;
    }
    *value = (unsigned long)low | ((unsigned long)high << 16);
    return 1;
}

/* INT 1Ah B102h: find by vendor/device. Returns 1 with bus and dev/fn. */
static unsigned short v9x_pci_find_device(unsigned short vendor,
                                          unsigned short device,
                                          unsigned short index,
                                          unsigned char *bus,
                                          unsigned char *device_function)
{
    union REGS input;
    union REGS output;

    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    input.x.ax = 0xb102u;
    input.x.cx = device;
    input.x.dx = vendor;
    input.x.si = index;
    int86(0x1a, &input, &output);
    if (output.x.cflag != 0 || output.h.ah != 0) {
        return 0;
    }
    *bus = output.h.bh;
    *device_function = output.h.bl;
    return 1;
}

/* INT 1Ah B103h: find by class code (ECX wants the full 24-bit class, but the
 * low 16 bits via CX suffice for 0300h display/VGA with SI walking matches -
 * the word-registers limitation again; a display controller is class 03,
 * subclass 00, interface 00, so CX carries subclass+interface = 0000h and the
 * class byte has to be tested from config space afterwards). To stay honest
 * with 16-bit registers, this walks vendor 8086 display candidates instead:
 * B102h across the known Gen3 ids. Good enough for a survey that names its
 * target family. */
static const unsigned short v9x_gen3_ids[] = {
    0x27aeu, /* 945GME/GSE */
    0x27a2u, /* 945GM */
    0x2772u, /* 945G */
    0x2592u, /* 915GM */
    0x2582u, /* 915G */
    0x258au, /* E7221 */
    0x29c2u, /* G33 */
    0x29b2u, /* Q35 */
    0x29d2u  /* Q33 */
};

static void v9x_report_far_string(FILE *report, const char *key,
                                  unsigned long far_pointer)
{
    const char far *text;
    unsigned short at;

    if (far_pointer == 0ul) {
        fprintf(report, "%s=unavailable\n", key);
        return;
    }
    text = (const char far *)MK_FP((unsigned short)(far_pointer >> 16),
                                   (unsigned short)(far_pointer & 0xffffu));
    fprintf(report, "%s=", key);
    for (at = 0u; at < 128u && text[at] != '\0'; ++at) {
        fputc(text[at], report);
    }
    fputc('\n', report);
}

static void v9x_report_mode(FILE *report, unsigned short mode)
{
    unsigned short status;
    memset(mode_info, 0, sizeof(mode_info));
    status = v9x_vbe_call(0x4f01u, mode, mode_info);
    fprintf(report, "Mode%04X.Status=%04X\n", mode, status);
    if (status != 0x004fu) return;
    fprintf(report, "Mode%04X.Attributes=%04X\n", mode, v9x_u16(mode_info));
    fprintf(report, "Mode%04X.Width=%u\n", mode, v9x_u16(mode_info + 18));
    fprintf(report, "Mode%04X.Height=%u\n", mode, v9x_u16(mode_info + 20));
    fprintf(report, "Mode%04X.BitsPerPixel=%u\n", mode, mode_info[25]);
    fprintf(report, "Mode%04X.MemoryModel=%u\n", mode, mode_info[27]);
    fprintf(report, "Mode%04X.BytesPerScanLine=%u\n", mode,
            v9x_u16(mode_info + 16));
    fprintf(report, "Mode%04X.PhysicalBase=%08lX\n", mode,
            v9x_u32(mode_info + 40));
    fprintf(report, "Mode%04X.LinearBytesPerScanLine=%u\n", mode,
            v9x_u16(mode_info + 50));
}

static void v9x_report_pci(FILE *report)
{
    unsigned char bus = 0u;
    unsigned char device_function = 0u;
    unsigned short found = 0u;
    unsigned short index;
    unsigned short word_value;
    unsigned long dword_value;
    unsigned short bar;

    for (index = 0u;
         index < sizeof(v9x_gen3_ids) / sizeof(v9x_gen3_ids[0]); ++index) {
        if (v9x_pci_find_device(V9X_PCI_VENDOR_INTEL, v9x_gen3_ids[index],
                                0u, &bus, &device_function) != 0) {
            found = v9x_gen3_ids[index];
            break;
        }
    }
    if (found == 0u) {
        fprintf(report, "PciIgd=not-found\n");
        return;
    }

    fprintf(report, "PciIgd.VendorId=8086\n");
    fprintf(report, "PciIgd.DeviceId=%04X\n", found);
    fprintf(report, "PciIgd.Bus=%02X\n", bus);
    fprintf(report, "PciIgd.DeviceFunction=%02X\n", device_function);
    if (v9x_pci_read_word(bus, device_function, 0x08u, &word_value) != 0) {
        fprintf(report, "PciIgd.Revision=%02X\n",
                (unsigned short)(word_value & 0xffu));
    }
    if (v9x_pci_read_dword(bus, device_function, 0x2cu, &dword_value) != 0) {
        fprintf(report, "PciIgd.Subsystem=%08lX\n", dword_value);
    }
    /* BAR0 MMIO, BAR1 I/O, BAR2 GMADR, BAR3 GTT: config 0x10/14/18/1C. */
    for (bar = 0u; bar < 4u; ++bar) {
        if (v9x_pci_read_dword(bus, device_function,
                               (unsigned short)(0x10u + bar * 4u),
                               &dword_value) != 0) {
            fprintf(report, "PciIgd.Bar%u=%08lX\n", bar, dword_value);
        }
    }
    /* BSM: stolen-memory base, IGD config 0x5C, 1 MiB aligned. */
    if (v9x_pci_read_dword(bus, device_function, 0x5cu, &dword_value) != 0) {
        fprintf(report, "PciIgd.Bsm=%08lX\n", dword_value);
    }
    /* GGC: stolen-memory size field, HOST BRIDGE (bus 0, dev 0, fn 0)
     * config 0x52. GMS is bits 6:4; G33 adds GTT-size bits 9:8. Raw word
     * published, decoding belongs to the audit record. */
    if (v9x_pci_read_word(0u, 0u, 0x52u, &word_value) != 0) {
        fprintf(report, "PciHostBridge.Ggc=%04X\n", word_value);
    }
    if (v9x_pci_read_dword(0u, 0u, 0x00u, &dword_value) != 0) {
        fprintf(report, "PciHostBridge.Id=%08lX\n", dword_value);
    }
    /* The second display function, expected one function up (27A6 on the
     * 945GM/GSE). Reported so its presence is a recorded fact rather than a
     * Device Manager surprise. */
    if (v9x_pci_read_word(bus, (unsigned char)(device_function | 0x01u),
                          0x02u, &word_value) != 0) {
        fprintf(report, "PciIgd.Function1DeviceId=%04X\n", word_value);
    }
}

static void v9x_save_vbios(FILE *report)
{
    FILE *image;
    const unsigned char far *shadow;
    unsigned short chunk;
    static unsigned char buffer[512];
    unsigned long offset;
    unsigned short at;

    image = fopen(V9X_VBIOS_IMAGE, "wb");
    if (image == 0) {
        fprintf(report, "VbiosImage=open-failed\n");
        return;
    }
    for (offset = 0ul; offset < 0x10000ul; offset += sizeof(buffer)) {
        shadow = (const unsigned char far *)
            MK_FP(0xc000u, (unsigned short)offset);
        for (at = 0u; at < sizeof(buffer); ++at) {
            buffer[at] = shadow[at];
        }
        chunk = (unsigned short)fwrite(buffer, 1u, sizeof(buffer), image);
        if (chunk != sizeof(buffer)) {
            fclose(image);
            fprintf(report, "VbiosImage=short-write\n");
            return;
        }
    }
    fclose(image);
    fprintf(report, "VbiosImage=%s\n", V9X_VBIOS_IMAGE);
}

int main(void)
{
    FILE *report;
    unsigned short status;
    unsigned long mode_list_pointer;
    const unsigned short far *mode_list;
    unsigned short mode_count;
    unsigned short at;

    report = fopen(V9X_INTEL_REPORT, "wt");
    if (report == 0) return 1;
    fprintf(report, "Velocity9x Intel GMA survey\n");
    fprintf(report, "Build=%s\n", V9X_BUILD_ID);
    fprintf(report, "Access=query-only\n");

    v9x_report_pci(report);

    memset(controller_info, 0, sizeof(controller_info));
    memcpy(controller_info, "VBE2", 4);
    status = v9x_vbe_call(0x4f00u, 0u, controller_info);
    fprintf(report, "ControllerStatus=%04X\n", status);
    if (status == 0x004fu) {
        fprintf(report, "Signature=%.4s\n", controller_info);
        fprintf(report, "Version=%04X\n", v9x_u16(controller_info + 4));
        fprintf(report, "TotalMemory64K=%u\n", v9x_u16(controller_info + 18));
        v9x_report_far_string(report, "OemString",
                              v9x_u32(controller_info + 6));
        v9x_report_far_string(report, "OemVendor",
                              v9x_u32(controller_info + 22));
        v9x_report_far_string(report, "OemProductName",
                              v9x_u32(controller_info + 26));
        v9x_report_far_string(report, "OemProductRev",
                              v9x_u32(controller_info + 30));

        /*
         * The whole point of this tool over the generic inventory: walk the
         * VBIOS's own list rather than probing guessed numbers. The pointer
         * may aim into the controller block's scratch area or into the VBIOS
         * segment; both are readable far pointers here. Copy-out first, since
         * later VBE calls may reuse the scratch area.
         */
        mode_list_pointer = v9x_u32(controller_info + 14);
        if (mode_list_pointer != 0ul) {
            static unsigned short modes[128];
            mode_list = (const unsigned short far *)
                MK_FP((unsigned short)(mode_list_pointer >> 16),
                      (unsigned short)(mode_list_pointer & 0xffffu));
            mode_count = 0u;
            while (mode_count < 128u && mode_list[mode_count] != 0xffffu) {
                modes[mode_count] = mode_list[mode_count];
                ++mode_count;
            }
            fprintf(report, "ModeListCount=%u\n", mode_count);
            for (at = 0u; at < mode_count; ++at) {
                v9x_report_mode(report, modes[at]);
            }
        } else {
            fprintf(report, "ModeListCount=unavailable\n");
        }
    }

    /* VBE/DDC (4F15h): BL=00 asks capability, BL=01 reads a 128-byte block.
     * On this VBIOS pin routing decides whether the LVDS panel answers; a
     * failure here is a finding, not an error - the EDID then comes from the
     * Windows registry capture instead. */
    {
        union REGS input;
        union REGS output;
        struct SREGS segments;

        memset(&input, 0, sizeof(input));
        memset(&output, 0, sizeof(output));
        segread(&segments);
        input.x.ax = 0x4f15u;
        input.h.bl = 0x00u;
        int86x(0x10, &input, &output, &segments);
        fprintf(report, "DdcCapabilityStatus=%04X\n", output.x.ax);

        memset(&input, 0, sizeof(input));
        memset(&output, 0, sizeof(output));
        segread(&segments);
        memset(edid_block, 0, sizeof(edid_block));
        input.x.ax = 0x4f15u;
        input.h.bl = 0x01u;
        segments.es = FP_SEG((void far *)edid_block);
        input.x.di = FP_OFF((void far *)edid_block);
        int86x(0x10, &input, &output, &segments);
        fprintf(report, "DdcReadStatus=%04X\n", output.x.ax);
        if (output.x.ax == 0x004fu) {
            fprintf(report, "Edid=");
            for (at = 0u; at < sizeof(edid_block); ++at) {
                fprintf(report, "%02X", edid_block[at]);
            }
            fputc('\n', report);
        }
    }

    v9x_save_vbios(report);

    fprintf(report, "Result=%s\n", status == 0x004fu ? "PASS" : "FAIL");
    fclose(report);
    puts("Velocity9x Intel GMA survey complete; no mode was changed.");
    return status == 0x004fu ? 0 : 2;
}
