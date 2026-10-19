/*
 * V9XMGAPC.EXE: Matrox PCI configuration-space survey, from Windows.
 *
 * Writes C:\V9XDIAG\V9XMGAPC.INI: for every function on buses 0 and 1
 * whose vendor is 102Bh, the first 256 bytes of config space in V9XSURV's
 * Config.XX layout, the revision, BAR0-BAR2 raw, and the control BAR that
 * v9x_matrox_mga_select_control_bar picks from them with no fallback. Then
 * the first bytes of the video option ROM image at C0000h.
 * Phase 0 of docs\plans\matrox-mystique.md.
 *
 * Why not V9XSURV: it is a DOS tool, and A8U4I5, where the Mystique is
 * fitted, takes no DOS programs. This reads the same words from a Win32
 * process, as hsw_survey_win32.c does on the Haswell.
 *
 * Access is PCI configuration mechanism #1: a dword to CF8h selects the
 * register, a dword from CFCh returns it. That CF8h write is the only
 * write; no config register is written, so BARs are not sized (Windows'
 * own LogConfig already gives the sizes). Each dword is read twice and an
 * UnstableReads count published, because another config cycle between our
 * CF8h write and CFCh read would return its register. CF8h is restored
 * after every access.
 *
 * The ROM read is of the system VM's first megabyte, which Windows 98 maps
 * at linear 0 for a Win32 process; it is guarded by IsBadReadPtr and only
 * read.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/mga_bars.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_MGAPC_PATH         V9X_DIAG_PATH("V9XMGAPC.INI")
#define V9X_PCI_CONFIG_ADDRESS ((WORD)0x0cf8u)
#define V9X_PCI_CONFIG_DATA    ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE         0x80000000ul
#define V9X_CONFIG_BYTES       256u
#define V9X_MATROX_VENDOR      0x102bul
#define V9X_LAST_BUS           1ul
#define V9X_DEVICES_PER_BUS    32ul
#define V9X_FUNCTIONS          8ul
#define V9X_HEADER_MULTI       0x00800000ul
#define V9X_ROM_LINEAR         0x000c0000ul
#define V9X_ROM_BYTES          256u

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

static DWORD v9x_config[V9X_CONFIG_BYTES / 4u];
static char v9x_text[32768];
static DWORD v9x_used;
static DWORD v9x_unstable;

static void v9x_put(const char *text)
{
    DWORD length;
    DWORD index;

    /* A byte loop, not CopyMemory: that is memcpy, and there is no CRT. */
    length = (DWORD)lstrlenA(text);
    if (v9x_used + length >= sizeof(v9x_text)) {
        return;
    }
    for (index = 0ul; index < length; ++index) {
        v9x_text[v9x_used + index] = text[index];
    }
    v9x_used += length;
}

static void v9x_hex_digits(char *text, DWORD value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int index;

    for (index = 0; index < digits; ++index) {
        text[index] = hex[(value >> ((digits - 1 - index) * 4)) & 15ul];
    }
    text[digits] = '\0';
}

static void v9x_key(const char *key, DWORD value, int digits)
{
    char text[12];

    v9x_put(key);
    v9x_put("=");
    v9x_hex_digits(text, value, digits);
    v9x_put(text);
    v9x_put("\r\n");
}

static DWORD v9x_config_read(DWORD bus, DWORD device, DWORD function,
                             DWORD offset)
{
    DWORD saved;
    DWORD first;
    DWORD second;
    DWORD address;

    address = V9X_PCI_ENABLE | (bus << 16) | (device << 11) |
              (function << 8) | (offset & 0xfcul);
    saved = v9x_inpd(V9X_PCI_CONFIG_ADDRESS);
    v9x_outpd(V9X_PCI_CONFIG_ADDRESS, address);
    first = v9x_inpd(V9X_PCI_CONFIG_DATA);
    v9x_outpd(V9X_PCI_CONFIG_ADDRESS, address);
    second = v9x_inpd(V9X_PCI_CONFIG_DATA);
    v9x_outpd(V9X_PCI_CONFIG_ADDRESS, saved);

    if (first != second) {
        ++v9x_unstable;
    }
    return first;
}

/* Config.00=2B10... : sixteen bytes per line, low byte first, as V9XSURV. */
static void v9x_dump(void)
{
    char text[40];
    DWORD line;
    DWORD byte;
    DWORD value;

    for (line = 0ul; line < V9X_CONFIG_BYTES; line += 16ul) {
        v9x_put("Config.");
        v9x_hex_digits(text, line, 2);
        v9x_put(text);
        v9x_put("=");
        for (byte = 0ul; byte < 16ul; ++byte) {
            value = (v9x_config[(line + byte) / 4ul] >>
                     (((line + byte) & 3ul) * 8ul)) & 0xfful;
            v9x_hex_digits(text + byte * 2ul, value, 2);
        }
        v9x_put(text);
        v9x_put("\r\n");
    }
}

static void v9x_survey_function(DWORD bus, DWORD device, DWORD function)
{
    char text[12];
    DWORD offset;
    v9x_u16 control_index;
    v9x_u32 control_base;
    v9x_u16 chosen;

    for (offset = 0ul; offset < V9X_CONFIG_BYTES; offset += 4ul) {
        v9x_config[offset / 4ul] =
            v9x_config_read(bus, device, function, offset);
    }

    v9x_put("\r\n[Pci.");
    v9x_hex_digits(text, bus, 2);
    v9x_put(text);
    v9x_put(".");
    v9x_hex_digits(text, device, 2);
    v9x_put(text);
    v9x_put(".");
    v9x_hex_digits(text, function, 1);
    v9x_put(text);
    v9x_put("]\r\n");
    v9x_key("DeviceId", v9x_config[0] >> 16, 4);
    v9x_key("Revision", v9x_config[0x08u / 4u] & 0xfful, 2);
    v9x_key("Subsystem", v9x_config[0x2cu / 4u], 8);
    v9x_key("Command", v9x_config[0x04u / 4u] & 0xfffful, 4);
    v9x_key("Bar0", v9x_config[0x10u / 4u], 8);
    v9x_key("Bar1", v9x_config[0x14u / 4u], 8);
    v9x_key("Bar2", v9x_config[0x18u / 4u], 8);
    v9x_key("RomBar", v9x_config[0x30u / 4u], 8);
    v9x_key("Option", v9x_config[0x40u / 4u], 8);

    /* What the driver's rule makes of these BARs on silicon: no fallback,
     * so a refusal here is a refusal the driver would only escape through
     * the 86Box path. */
    chosen = v9x_matrox_mga_select_control_bar(v9x_config[0x10u / 4u],
                                               v9x_config[0x14u / 4u],
                                               V9X_MGA_NO_FALLBACK_BAR,
                                               &control_index,
                                               &control_base);
    v9x_key("RuleAccepted", chosen, 1);
    v9x_key("RuleControlBar", control_index, 1);
    v9x_key("RuleControlBase", control_base, 8);
    v9x_dump();
}

/* Option ROM image at C0000h: 55AA, length, then the BIOS's own text.
 * Printable bytes are published as-is, others as '.'. */
static void v9x_rom(void)
{
    const BYTE *rom;
    char line[68];
    DWORD row;
    DWORD column;
    BYTE value;

    rom = (const BYTE *)V9X_ROM_LINEAR;
    v9x_put("\r\n[Rom]\r\n");
    if (IsBadReadPtr(rom, V9X_ROM_BYTES)) {
        v9x_put("Readable=0\r\n");
        return;
    }
    v9x_put("Readable=1\r\n");
    v9x_key("Signature", ((DWORD)rom[0] << 8) | rom[1], 4);
    v9x_key("Blocks512", rom[2], 2);
    for (row = 0ul; row < V9X_ROM_BYTES; row += 64ul) {
        for (column = 0ul; column < 64ul; ++column) {
            value = rom[row + column];
            line[column] = (char)(value >= 0x20u && value < 0x7fu ? value : '.');
        }
        line[64] = '\0';
        v9x_put("Text.");
        v9x_hex_digits(line + 65, row, 2);
        v9x_put(line + 65);
        v9x_put("=");
        v9x_put(line);
        v9x_put("\r\n");
    }
}

void __stdcall V9xMgaPciSurveyEntry(void)
{
    HANDLE file;
    DWORD written;
    DWORD bus;
    DWORD device;
    DWORD function;
    DWORD functions;
    DWORD id;
    DWORD found;

    CreateDirectoryA(V9X_DIAG_DIR, 0);

    v9x_put("; Velocity9x Matrox config-space survey (V9XMGAPC)\r\n");
    v9x_put("\r\n[Report]\r\nTool=V9XMGAPC\r\nBuild=" V9X_BUILD_ID "\r\n");
    v9x_put("Access=config-read-only\r\n");

    found = 0ul;
    for (bus = 0ul; bus <= V9X_LAST_BUS; ++bus) {
        for (device = 0ul; device < V9X_DEVICES_PER_BUS; ++device) {
            id = v9x_config_read(bus, device, 0ul, 0ul);
            if ((id & 0xfffful) == 0xfffful) {
                continue;
            }

            /* Only a multi-function header has functions 1-7 to scan. */
            functions = (v9x_config_read(bus, device, 0ul, 0x0cul) &
                         V9X_HEADER_MULTI) != 0ul ? V9X_FUNCTIONS : 1ul;
            for (function = 0ul; function < functions; ++function) {
                id = v9x_config_read(bus, device, function, 0ul);
                if ((id & 0xfffful) != V9X_MATROX_VENDOR) {
                    continue;
                }
                v9x_survey_function(bus, device, function);
                ++found;
            }
        }
    }

    v9x_rom();

    v9x_put("\r\n[Summary]\r\n");
    v9x_key("MatroxFunctions", found, 2);
    v9x_key("UnstableReads", v9x_unstable, 4);
    v9x_put(found != 0ul ? "Result=COMPLETE\r\n" : "Result=NO-MATROX\r\n");

    file = CreateFileA(V9X_MGAPC_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ExitProcess(2u);
    }
    WriteFile(file, v9x_text, v9x_used, &written, 0);
    CloseHandle(file);
    ExitProcess(0u);
}
