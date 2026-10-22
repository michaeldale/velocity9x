/*
 * V9XG5DD.EXE: Ironlake EDID through the PCH's GMBUS, not the BIOS.
 *
 * The mini-VDD's VBE 4F15h read fails on W9QI2F4F at every boot
 * (VbeCache f=0407: DDC reported present, the read itself failed), while
 * a DOS survey through the same BIOS read a valid block on 2026-09-28.
 * This asks G5DD.VXD to read 128 bytes at I2C address 50h on each GMBUS
 * pin pair 2-6 and writes C:\V9XDIAG\V9XG5DD.INI:
 *
 *   [Report]  build, the IGD's GTTMMADR, the VxD's status;
 *   [Pin<n>]  per pair: the outcome, GMBUS2 at the end, bytes read, the
 *             block as hex, and what src/common/edid.c makes of it -
 *             header, checksum, version, manufacturer, product, the
 *             preferred timing.
 *
 * Pair 2 is the analog monitor's dedicated DDC pins and 3 the LVDS panel's
 * (PRM Vol 3 Part 3 2.2.3.1); 4, 5 and 6 are ports C, B and D, which the
 * PRM leaves "Reserved" and Linux 6.6 intel_gmbus.c uses for them on Ibex
 * Peak. The VxD writes only GMBUS0-2, and leaves them as reset finds them.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/edid.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_G5DD_PATH        V9X_DIAG_PATH("V9XG5DD.INI")
#define V9X_G5DD_VXD         "\\\\.\\G5DD.VXD"
#define V9X_G5DD_MAGIC       0x44443547ul
#define V9X_G5DD_READ        1u
#define V9X_G5DD_SLOTS       6u
#define V9X_G5DD_PIN_FIRST   2u
#define V9X_G5DD_PIN_LAST    6u
#define V9X_G5DD_EDID_DWORDS 32u
#define V9X_PCI_ADDRESS      ((WORD)0x0cf8u)
#define V9X_PCI_DATA         ((WORD)0x0cfcu)
#define V9X_IGD_CONFIG       0x80001000ul
#define V9X_CFG_GTTMMADR     0x10ul
#define V9X_GTTMMADR_MASK    0xffc00000ul

#define V9X_ST_MMIO_MAPPED   0x00000004ul

/* EDID bytes 8-11: the manufacturer, three 5-bit letters, then the
 * product code, little-endian (VESA E-EDID 1.4 3.4). */
#define V9X_EDID_MANUFACTURER 8u
#define V9X_EDID_PRODUCT      10u

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

struct v9x_g5dd_request {
    DWORD base;
    DWORD pins;
};

/* Must match G5D_RESULT_DWORDS in gen5_ddc_probe.asm. */
struct v9x_g5dd_slot {
    DWORD pin;
    DWORD outcome;
    DWORD gmbus2;
    DWORD bytes;
    DWORD data[V9X_G5DD_EDID_DWORDS];
};

struct v9x_g5dd_result {
    DWORD magic;
    DWORD status;
    DWORD igd_id;
    DWORD base;
    DWORD pins;
    DWORD cf8;
    DWORD reserved[2];
    struct v9x_g5dd_slot slot[V9X_G5DD_SLOTS];
};

static const char *const v9x_outcomes[] = {
    "not-asked", "ok", "in-use", "nak", "timeout-data", "timeout-idle"
};

static struct v9x_g5dd_request v9x_request;
static struct v9x_g5dd_result v9x_result;

static DWORD v9x_config_read(DWORD device, DWORD offset)
{
    DWORD saved;
    DWORD value;

    saved = v9x_inpd(V9X_PCI_ADDRESS);
    v9x_outpd(V9X_PCI_ADDRESS, device | (offset & 0xfcul));
    value = v9x_inpd(V9X_PCI_DATA);
    v9x_outpd(V9X_PCI_ADDRESS, saved);
    return value;
}

static void v9x_hex(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15ul];
    }
    text[digits] = '\0';
}

static void v9x_key(const char *section, const char *key, DWORD value)
{
    char text[12];

    v9x_hex(text, value, 8);
    WritePrivateProfileStringA(section, key, text, V9X_G5DD_PATH);
}

static void v9x_text(const char *section, const char *key, const char *value)
{
    WritePrivateProfileStringA(section, key, value, V9X_G5DD_PATH);
}

static void v9x_decimal(const char *section, const char *key, DWORD value)
{
    char text[12];
    char reversed[12];
    int count = 0;
    int at = 0;

    do {
        reversed[count++] = (char)('0' + value % 10ul);
        value /= 10ul;
    } while (value != 0ul && count < 11);
    while (count > 0) {
        text[at++] = reversed[--count];
    }
    text[at] = '\0';
    v9x_text(section, key, text);
}

static int v9x_read(void)
{
    HANDLE device;
    DWORD returned;
    BOOL ok;

    device = CreateFileA(V9X_G5DD_VXD, 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_text("Report", "Result", "VXD-LOAD-FAILED");
        v9x_key("Report", "LoadError", GetLastError());
        return 0;
    }
    returned = 0ul;
    ok = DeviceIoControl(device, V9X_G5DD_READ, &v9x_request,
                         sizeof(v9x_request), &v9x_result,
                         sizeof(v9x_result), &returned, 0);
    CloseHandle(device);
    if (!ok || returned != sizeof(v9x_result) ||
        v9x_result.magic != V9X_G5DD_MAGIC) {
        v9x_text("Report", "Result", "DIOC-FAILED");
        v9x_key("Report", "Returned", returned);
        return 0;
    }
    return (v9x_result.status & V9X_ST_MMIO_MAPPED) != 0ul;
}

/* The block as four 32-byte hex rows, Block0 to Block3. */
static void v9x_publish_block(const char *section, const v9x_u8 *block)
{
    char key[8];
    char text[65];
    DWORD row;
    DWORD index;

    for (row = 0ul; row < 4ul; ++row) {
        for (index = 0ul; index < 32ul; ++index) {
            v9x_hex(text + index * 2ul, block[row * 32ul + index], 2);
        }
        text[64] = '\0';
        key[0] = 'B'; key[1] = 'l'; key[2] = 'o'; key[3] = 'c'; key[4] = 'k';
        key[5] = (char)('0' + row);
        key[6] = '\0';
        v9x_text(section, key, text);
    }
}

/* What the block says, or why it is not an EDID. */
static void v9x_publish_edid(const char *section, const v9x_u8 *block)
{
    static const v9x_u8 header[8] = { 0x00, 0xff, 0xff, 0xff,
                                      0xff, 0xff, 0xff, 0x00 };
    struct v9x_edid_summary summary;
    struct v9x_edid_timing timing;
    char name[4];
    DWORD index;
    DWORD sum = 0ul;
    DWORD id;
    int header_ok = 1;

    for (index = 0ul; index < 8ul; ++index) {
        if (block[index] != header[index]) {
            header_ok = 0;
        }
    }
    for (index = 0ul; index < V9X_EDID_BLOCK_BYTES; ++index) {
        sum += block[index];
    }
    v9x_text(section, "Header", header_ok ? "valid" : "invalid");
    v9x_text(section, "Checksum", (sum & 0xfful) == 0ul ? "valid" : "invalid");
    if (!header_ok) {
        return;
    }

    id = ((DWORD)block[V9X_EDID_MANUFACTURER] << 8) |
         block[V9X_EDID_MANUFACTURER + 1u];
    name[0] = (char)('@' + ((id >> 10) & 31ul));
    name[1] = (char)('@' + ((id >> 5) & 31ul));
    name[2] = (char)('@' + (id & 31ul));
    name[3] = '\0';
    v9x_text(section, "Manufacturer", name);
    v9x_key(section, "Product",
            ((DWORD)block[V9X_EDID_PRODUCT + 1u] << 8) |
                block[V9X_EDID_PRODUCT]);

    if (v9x_edid_parse(block, &summary) != V9X_TRUE) {
        v9x_text(section, "Parse", "refused");
        return;
    }
    v9x_text(section, "Parse", "accepted");
    v9x_key(section, "Version", summary.version);
    v9x_decimal(section, "PreferredWidth", summary.preferred_width);
    v9x_decimal(section, "PreferredHeight", summary.preferred_height);
    v9x_decimal(section, "Extensions", summary.extension_count);
    if (v9x_edid_parse_timing(block, &timing) == V9X_TRUE) {
        v9x_decimal(section, "PixelClockHz", timing.pixel_clock_hz);
        v9x_decimal(section, "HBlank", timing.h_blank);
        v9x_decimal(section, "VBlank", timing.v_blank);
    }
}

void __stdcall V9xGen5DdcEntry(void)
{
    DWORD bar0;
    DWORD pin;
    DWORD answered = 0ul;
    char section[8];

    DeleteFileA(V9X_G5DD_PATH);
    v9x_text("Report", "Tool", "V9XG5DD");
    v9x_text("Report", "Build", V9X_BUILD_ID);
    v9x_text("Report", "Access", "gmbus-read");

    bar0 = v9x_config_read(V9X_IGD_CONFIG, V9X_CFG_GTTMMADR);
    v9x_key("Report", "Bar0", bar0);
    v9x_request.base = bar0 & V9X_GTTMMADR_MASK;
    v9x_request.pins = 0ul;
    for (pin = V9X_G5DD_PIN_FIRST; pin <= V9X_G5DD_PIN_LAST; ++pin) {
        v9x_request.pins |= 1ul << pin;
    }
    v9x_key("Report", "Pins", v9x_request.pins);

    if (!v9x_read()) {
        v9x_key("Report", "Status", v9x_result.status);
        WritePrivateProfileStringA(0, 0, 0, V9X_G5DD_PATH);
        ExitProcess(2u);
    }
    v9x_key("Report", "Status", v9x_result.status);
    v9x_key("Report", "IgdId", v9x_result.igd_id);
    v9x_key("Report", "Base", v9x_result.base);

    for (pin = V9X_G5DD_PIN_FIRST; pin <= V9X_G5DD_PIN_LAST; ++pin) {
        const struct v9x_g5dd_slot *slot = &v9x_result.slot[pin - 1ul];

        section[0] = 'P'; section[1] = 'i'; section[2] = 'n';
        section[3] = (char)('0' + pin);
        section[4] = '\0';
        v9x_text(section, "Outcome",
                 slot->outcome < sizeof(v9x_outcomes) / sizeof(v9x_outcomes[0])
                     ? v9x_outcomes[slot->outcome] : "unknown");
        v9x_key(section, "Gmbus2", slot->gmbus2);
        v9x_decimal(section, "Bytes", slot->bytes);
        if (slot->outcome == 1ul &&
            slot->bytes == V9X_G5DD_EDID_DWORDS * 4ul) {
            v9x_publish_block(section, (const v9x_u8 *)slot->data);
            v9x_publish_edid(section, (const v9x_u8 *)slot->data);
            ++answered;
        }
    }

    v9x_decimal("Report", "PinsAnswered", answered);
    v9x_text("Report", "Result", answered != 0ul ? "EDID-READ" : "NO-EDID");
    WritePrivateProfileStringA(0, 0, 0, V9X_G5DD_PATH);
    ExitProcess(0u);
}
