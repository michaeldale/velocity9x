/*
 * V9XHSW.EXE: Haswell (Gen7.5) configuration-space survey, from Windows.
 *
 * Writes C:\V9XDIAG\V9XHSW.INI: the first 256 bytes of config space for the
 * host bridge (00:00.0) and the IGD (00:02.0) in V9XSURV's Config.XX
 * layout, then the layout v9x_intel_hsw_decode_layout derives from them.
 * Phase 1 of docs\plans\intel-haswell-bringup.md.
 *
 * Why not V9XSURV: it is a DOS tool, and DOS programs run through the v9x
 * agent leave Windows unable to reboot itself. This reads the same words
 * from a Win32 process instead.
 *
 * Access is PCI configuration mechanism #1: a dword to CF8h selects the
 * register, a dword from CFCh returns it. That CF8h write is the only
 * write: no config register, MMIO or GTT is touched. Windows 98 lets a
 * Win32 process reach untrapped ports directly, the same route
 * ati_rage2_scene_win32.c takes to the VGA DAC. Each dword is read twice
 * and both reads are published if they differ, because another config
 * cycle between our CF8h write and CFCh read would return its register.
 * CF8h is restored after every access.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"
#include "velocity9x/intel_hsw.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_HSW_PATH          V9X_DIAG_PATH("V9XHSW.INI")
#define V9X_PCI_CONFIG_ADDRESS ((WORD)0x0cf8u)
#define V9X_PCI_CONFIG_DATA    ((WORD)0x0cfcu)
#define V9X_PCI_ENABLE         0x80000000ul
#define V9X_CONFIG_BYTES       256u
#define V9X_IGD_DEVICE         2u

static DWORD v9x_inpd(WORD port);
#pragma aux v9x_inpd = "in eax,dx" parm [dx] value [eax] modify exact [eax]
static void v9x_outpd(WORD port, DWORD value);
#pragma aux v9x_outpd = "out dx,eax" parm [dx] [eax] modify exact []

static DWORD v9x_host[V9X_CONFIG_BYTES / 4u];
static DWORD v9x_igd[V9X_CONFIG_BYTES / 4u];
static char v9x_text[16384];
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

static DWORD v9x_config_read(DWORD device, DWORD offset)
{
    DWORD saved;
    DWORD first;
    DWORD second;
    DWORD address;

    address = V9X_PCI_ENABLE | (device << 11) | (offset & 0xfcul);
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

/* Config.00=8680... : sixteen bytes per line, low byte first, as V9XSURV. */
static void v9x_dump(const char *section, DWORD *words)
{
    char text[40];
    DWORD line;
    DWORD byte;
    DWORD value;

    v9x_put("\r\n[");
    v9x_put(section);
    v9x_put("]\r\n");
    for (line = 0ul; line < V9X_CONFIG_BYTES; line += 16ul) {
        v9x_put("Config.");
        v9x_hex_digits(text, line, 2);
        v9x_put(text);
        v9x_put("=");
        for (byte = 0ul; byte < 16ul; ++byte) {
            value = (words[(line + byte) / 4ul] >> (((line + byte) & 3ul) * 8ul)) &
                    0xfful;
            v9x_hex_digits(text + byte * 2ul, value, 2);
        }
        v9x_put(text);
        v9x_put("\r\n");
    }
}

static DWORD v9x_word16(DWORD *words, DWORD offset)
{
    return (words[offset / 4ul] >> ((offset & 2ul) * 8ul)) & 0xfffful;
}

static void v9x_decode(void)
{
    struct v9x_intel_hsw_config config;
    struct v9x_intel_hsw_layout layout;
    v9x_status status;

    config.ggc = (v9x_u16)v9x_word16(v9x_host, V9X_INTEL_HSW_CFG_GGC);
    config.bdsm = v9x_igd[V9X_INTEL_HSW_CFG_BDSM / 4u];
    config.host_bgsm = v9x_host[V9X_INTEL_HSW_CFG_HOST_BGSM / 4u];
    config.bar0_low = v9x_igd[0x10u / 4u];
    config.bar0_high = v9x_igd[0x14u / 4u];
    config.bar2_low = v9x_igd[0x18u / 4u];
    config.bar2_high = v9x_igd[0x1cu / 4u];
    config.msac = (v9x_u8)((v9x_igd[V9X_INTEL_HSW_CFG_MSAC / 4u] >> 16) &
                           0xfful);
    status = v9x_intel_hsw_decode_layout(&config, &layout);

    v9x_put("\r\n[Decode]\r\n");
    v9x_key("Status", status, 4);
    v9x_key("Refused", layout.refused, 4);
    v9x_key("Ggc", config.ggc, 4);
    v9x_key("GgcMirror", v9x_word16(v9x_igd, V9X_INTEL_HSW_CFG_GGC), 4);
    v9x_key("Gms", layout.ggc.gms, 2);
    v9x_key("Ggms", layout.ggc.ggms, 2);
    v9x_key("StolenBytes", layout.ggc.stolen_bytes, 8);
    v9x_key("GttStolenBytes", layout.ggc.gtt_stolen_bytes, 8);
    v9x_key("VgaDisabled", layout.ggc.vga_disabled, 1);
    v9x_key("Locked", layout.ggc.locked, 1);
    v9x_key("MmioBase", layout.mmio_base, 8);
    v9x_key("GttBase", layout.gtt_base, 8);
    v9x_key("GttEntries", layout.gtt_entries, 8);
    v9x_key("ApertureBase", layout.aperture_base, 8);
    v9x_key("ApertureBytes", layout.aperture_bytes, 8);
    v9x_key("Msac", config.msac, 2);
    v9x_key("StolenBase", layout.stolen_base, 8);
    v9x_key("GttStolenBase", layout.gtt_stolen_base, 8);
    v9x_key("HostBdsm", v9x_host[V9X_INTEL_HSW_CFG_HOST_BDSM / 4u], 8);
    v9x_key("HostBgsm", config.host_bgsm, 8);
    v9x_key("HostTolud", v9x_host[0xbcu / 4u], 8);
}

void __stdcall V9xHswSurveyEntry(void)
{
    HANDLE file;
    DWORD offset;
    DWORD written;
    DWORD host_id;
    DWORD igd_id;

    CreateDirectoryA(V9X_DIAG_DIR, 0);

    v9x_put("; Velocity9x Haswell config-space survey (V9XHSW)\r\n");
    v9x_put("\r\n[Report]\r\nTool=V9XHSW\r\nBuild=" V9X_BUILD_ID "\r\n");
    v9x_put("Access=config-read-only\r\n");

    host_id = v9x_config_read(0ul, 0ul);
    igd_id = v9x_config_read(V9X_IGD_DEVICE, 0ul);
    v9x_key("HostId", host_id, 8);
    v9x_key("IgdId", igd_id, 8);

    /* Nothing to read if mechanism #1 is trapped or this is not Intel. */
    if ((host_id & 0xfffful) != 0x8086ul || (igd_id & 0xfffful) != 0x8086ul) {
        v9x_put("Result=NOT-INTEL\r\n");
    } else {
        for (offset = 0ul; offset < V9X_CONFIG_BYTES; offset += 4ul) {
            v9x_host[offset / 4ul] = v9x_config_read(0ul, offset);
            v9x_igd[offset / 4ul] = v9x_config_read(V9X_IGD_DEVICE, offset);
        }
        v9x_key("UnstableReads", v9x_unstable, 4);
        v9x_put("Result=COMPLETE\r\n");
        v9x_dump("Host", v9x_host);
        v9x_dump("Igd", v9x_igd);
        v9x_decode();
    }

    file = CreateFileA(V9X_HSW_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ExitProcess(2u);
    }
    WriteFile(file, v9x_text, v9x_used, &written, 0);
    CloseHandle(file);
    ExitProcess(0u);
}
