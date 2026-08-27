#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9XMGAQ_MAGIC 0x3241474dul

struct v9x_mga_query_result {
    DWORD magic;
    DWORD status;
    DWORD vertical_count;
    DWORD operating_mode;
    DWORD control_base;
};

static DWORD v9x_parse_hex(const char *text)
{
    DWORD value = 0;
    int index = 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) index = 2;
    while (text[index] != '\0') {
        char digit = text[index++];
        if (digit >= '0' && digit <= '9') digit = (char)(digit - '0');
        else if (digit >= 'A' && digit <= 'F') digit = (char)(digit - 'A' + 10);
        else if (digit >= 'a' && digit <= 'f') digit = (char)(digit - 'a' + 10);
        else return 0;
        value = (value << 4) | (DWORD)digit;
    }
    return value;
}

static void v9x_hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    text[0] = '0'; text[1] = 'x';
    for (shift = 28; shift >= 0; shift -= 4)
        text[2 + (28-shift)/4] = digits[(value >> shift) & 15u];
    text[10] = '\0';
}

static void v9x_append(HANDLE file, const char *key, const char *value)
{
    DWORD written;
    WriteFile(file, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(file, "=", 1u, &written, 0);
    WriteFile(file, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(file, "\r\n", 2u, &written, 0);
}

void WINAPI V9xMatroxMmioQueryEntry(void)
{
    char base_text[24];
    char value_text[16];
    DWORD control_base;
    DWORD returned = 0;
    HANDLE device;
    HANDLE output;
    struct v9x_mga_query_result result;

    GetPrivateProfileStringA("MatroxInventory", "ControlBase", "",
                             base_text, sizeof(base_text), V9X_DIAG_MGA_INI);
    control_base = v9x_parse_hex(base_text);
    if (control_base == 0u || (control_base & 0x3fffu) != 0u) ExitProcess(2u);

    device = CreateFileA("\\\\.\\V9XMGAQ.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) ExitProcess(3u);
    if (!DeviceIoControl(device, 1u, &control_base, sizeof(control_base),
                         &result, sizeof(result), &returned, 0) ||
        returned != sizeof(result) || result.magic != V9XMGAQ_MAGIC ||
        result.control_base != control_base) {
        CloseHandle(device);
        ExitProcess(4u);
    }
    CloseHandle(device);

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    output = CreateFileA(V9X_DIAG_MGAMM_INI, GENERIC_WRITE, FILE_SHARE_READ,
                         0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(5u);
    {
        DWORD written;
        const char header[] = "[MatroxMmio]\r\n";
        WriteFile(output, header, (DWORD)lstrlenA(header), &written, 0);
    }
    v9x_append(output, "Build", V9X_BUILD_ID);
    v9x_append(output, "Access", "read-only");
    v9x_hex(value_text, result.control_base);
    v9x_append(output, "ControlBase", value_text);
    v9x_hex(value_text, result.status);
    v9x_append(output, "Status", value_text);
    v9x_hex(value_text, result.vertical_count);
    v9x_append(output, "VerticalCount", value_text);
    v9x_hex(value_text, result.operating_mode);
    v9x_append(output, "OperatingMode", value_text);
    v9x_append(output, "Result", "PASS");
    CloseHandle(output);
    ExitProcess(0u);
}
