/*
 * V9XPCIW.EXE: config-space survey of display functions and bridges, from
 * Windows. A scratch variant of tools\diag\matrox_pci_survey_win32.c for the
 * 2026-11-05 audit of M-J0F0SJ (Quadro FX 550 behind a PCIe bridge, on a
 * machine that takes no DOS programs, so not V9XSURV).
 *
 * Writes C:\V9XDIAG\V9XPCIW.INI: for every function on buses 0-255 whose
 * class is 03h (display) or 0604h (PCI-PCI bridge), and for 00:00.0, the
 * first 256 bytes of config space in V9XSURV's Config.XX layout. Then the
 * first 512 bytes of the option ROM image at C0000h as text.
 *
 * Access is PCI configuration mechanism #1. The CF8h write is the only
 * write; CF8h is restored after every access. Each dword is read twice and
 * mismatches are counted.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_PCIW_PATH          "C:\\V9XDIAG\\V9XPCIW.INI"
#define V9X_DIAG_DIR_PATH      "C:\\V9XDIAG"
#define V9X_PCI_CONFIG_ADDRESS ((WORD)0x0cf8u)
#define V9X_PCI_CONFIG_DATA    ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE         0x80000000ul
#define V9X_CONFIG_BYTES       256u
#define V9X_LAST_BUS           255ul
#define V9X_DEVICES_PER_BUS    32ul
#define V9X_FUNCTIONS          8ul
#define V9X_HEADER_MULTI       0x00800000ul
#define V9X_ROM_LINEAR         0x000c0000ul
#define V9X_ROM_BYTES          512u

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

static DWORD v9x_config[V9X_CONFIG_BYTES / 4u];
static char v9x_text[65536];
static DWORD v9x_used;
static DWORD v9x_unstable;

static void v9x_put(const char *text)
{
    DWORD length;
    DWORD index;

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
    v9x_key("VendorDevice", v9x_config[0], 8);
    v9x_key("ClassRev", v9x_config[0x08u / 4u], 8);
    v9x_key("Command", v9x_config[0x04u / 4u] & 0xfffful, 4);
    v9x_dump();
}

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
        v9x_hex_digits(line + 65, row, 3);
        v9x_put(line + 65);
        v9x_put("=");
        v9x_put(line);
        v9x_put("\r\n");
    }
}

void __stdcall V9xPciwSurveyEntry(void)
{
    HANDLE file;
    DWORD written;
    DWORD bus;
    DWORD device;
    DWORD function;
    DWORD functions;
    DWORD id;
    DWORD class_code;
    DWORD found;

    CreateDirectoryA(V9X_DIAG_DIR_PATH, 0);

    v9x_put("; Velocity9x display/bridge config-space survey (V9XPCIW)\r\n");
    v9x_put("\r\n[Report]\r\nTool=V9XPCIW\r\nBuild=" V9X_BUILD_ID "\r\n");
    v9x_put("Access=config-read-only\r\n");

    found = 0ul;
    for (bus = 0ul; bus <= V9X_LAST_BUS; ++bus) {
        for (device = 0ul; device < V9X_DEVICES_PER_BUS; ++device) {
            id = v9x_config_read(bus, device, 0ul, 0ul);
            if ((id & 0xfffful) == 0xfffful) {
                continue;
            }

            functions = (v9x_config_read(bus, device, 0ul, 0x0cul) &
                         V9X_HEADER_MULTI) != 0ul ? V9X_FUNCTIONS : 1ul;
            for (function = 0ul; function < functions; ++function) {
                id = v9x_config_read(bus, device, function, 0ul);
                if ((id & 0xfffful) == 0xfffful) {
                    continue;
                }
                class_code = v9x_config_read(bus, device, function, 0x08ul) >> 8;
                if ((class_code >> 16) != 0x03ul &&
                    (class_code >> 8) != 0x0604ul &&
                    !(bus == 0ul && device == 0ul && function == 0ul)) {
                    continue;
                }
                v9x_survey_function(bus, device, function);
                ++found;
            }
        }
    }

    v9x_rom();

    v9x_put("\r\n[Summary]\r\n");
    v9x_key("Functions", found, 2);
    v9x_key("UnstableReads", v9x_unstable, 4);
    v9x_put("Result=COMPLETE\r\n");

    file = CreateFileA(V9X_PCIW_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ExitProcess(2u);
    }
    WriteFile(file, v9x_text, v9x_used, &written, 0);
    CloseHandle(file);
    ExitProcess(0u);
}
