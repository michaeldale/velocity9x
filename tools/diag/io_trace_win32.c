/*
 * V9XIOTR: read the mini-VDD's V86 port-I/O trace.
 *
 * The -IoTrace mini-VDD build traps the 8514/A register file for V86 VMs and
 * logs every access it passes through. This tool opens the statically loaded
 * V9XMINI by name, pulls the log through DeviceIoControl, and writes it to
 * C:\V9XDIAG\V9XIOTR.INI: the header counts, one line per access, and a tally
 * per port. `/reset` clears the log instead, so a run can be bracketed:
 * reset, one DOS box, read.
 *
 * Instrument for docs\issues\2026-09-06-dos-box-doubles-the-desktop-on-
 * physical-trio64.md. Runtime-free Win32, like the other diagnostics.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_SECTION            "Velocity9xIoTrace"
#define V9X_IOTRACE_DIOC_READ  1ul
#define V9X_IOTRACE_DIOC_RESET 2ul
#define V9X_IOTRACE_ENTRIES    512u

typedef struct v9x_io_trace_header {
    DWORD count;
    DWORD logged;
    DWORD installed;
    DWORD sys_vm_off;
} V9X_IO_TRACE_HEADER;

typedef struct v9x_io_trace_entry {
    DWORD vm;
    DWORD type_port;    /* I/O type in the high word, port in the low */
    DWORD value;
} V9X_IO_TRACE_ENTRY;

static void v9x_write(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_SECTION, key, value, V9X_DIAG_IOTR_INI);
}

static void v9x_hex(char *out, DWORD value, int digits)
{
    static const char digits16[] = "0123456789abcdef";
    int index;

    for (index = digits - 1; index >= 0; --index) {
        out[index] = digits16[value & 15ul];
        value >>= 4;
    }
    out[digits] = '\0';
}

static void v9x_uint(char *out, DWORD value)
{
    char reversed[12];
    int length = 0;
    int index;

    do {
        reversed[length++] = (char)('0' + (value % 10ul));
        value /= 10ul;
    } while (value != 0ul);
    for (index = 0; index < length; ++index) {
        out[index] = reversed[length - 1 - index];
    }
    out[length] = '\0';
}

static void v9x_write_uint(const char *key, DWORD value)
{
    char text[12];

    v9x_uint(text, value);
    v9x_write(key, text);
}

/* Type word: bit 2 output, bits 3-4 width (0 byte, 1 word, 2 dword), bit 5
 * string, bit 6 rep. The handler hands Simulate_IO the string forms, so the
 * logged entries are always single accesses. */
static const char *v9x_type_text(DWORD type)
{
    static const char *const names[6] = {
        "in1", "in2", "in4", "out1", "out2", "out4"
    };
    DWORD width = (type >> 3) & 3ul;
    DWORD output = (type >> 2) & 1ul;

    if (width > 2ul) {
        return "?";
    }
    return names[output * 3ul + width];
}

void WINAPI V9xIoTraceEntry(void)
{
    static unsigned char buffer[sizeof(V9X_IO_TRACE_HEADER) +
                                V9X_IOTRACE_ENTRIES *
                                    sizeof(V9X_IO_TRACE_ENTRY)];
    static DWORD per_port[32];
    static WORD per_port_id[32];
    const char *command_line = GetCommandLineA();
    int reset = 0;
    HANDLE device;
    DWORD returned = 0ul;
    const V9X_IO_TRACE_HEADER *header;
    const V9X_IO_TRACE_ENTRY *entries;
    DWORD index;
    DWORD ports = 0ul;

    while (*command_line != '\0') {
        if (command_line[0] == '/' && command_line[1] == 'r') {
            reset = 1;
        }
        ++command_line;
    }

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_SECTION, 0, 0, V9X_DIAG_IOTR_INI);
    v9x_write("Build", V9X_BUILD_ID);
    v9x_write("Mode", reset ? "reset" : "read");

    device = CreateFileA("\\\\.\\V9XMINI", 0, 0, 0, OPEN_EXISTING, 0, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_write_uint("OpenError", GetLastError());
        v9x_write("Result", "FAIL");
        v9x_write("Error", "open-failed");
        WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_IOTR_INI);
        ExitProcess(1ul);
    }

    if (reset) {
        if (!DeviceIoControl(device, V9X_IOTRACE_DIOC_RESET, 0, 0ul, 0, 0ul,
                             &returned, 0)) {
            v9x_write_uint("IoctlError", GetLastError());
            v9x_write("Result", "FAIL");
            v9x_write("Error", "reset-rejected");
        } else {
            v9x_write("Result", "PASS");
        }
        CloseHandle(device);
        WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_IOTR_INI);
        ExitProcess(0ul);
    }

    if (!DeviceIoControl(device, V9X_IOTRACE_DIOC_READ, 0, 0ul, buffer,
                         sizeof(buffer), &returned, 0) ||
        returned < sizeof(V9X_IO_TRACE_HEADER)) {
        v9x_write_uint("IoctlError", GetLastError());
        v9x_write_uint("Returned", returned);
        v9x_write("Result", "FAIL");
        v9x_write("Error", "read-rejected");
        CloseHandle(device);
        WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_IOTR_INI);
        ExitProcess(2ul);
    }
    CloseHandle(device);

    header = (const V9X_IO_TRACE_HEADER *)buffer;
    entries = (const V9X_IO_TRACE_ENTRY *)(buffer +
                                           sizeof(V9X_IO_TRACE_HEADER));
    v9x_write_uint("Returned", returned);
    v9x_write_uint("Count", header->count);
    v9x_write_uint("Logged", header->logged);
    v9x_write_uint("Installed", header->installed);
    v9x_write_uint("SysVmOff", header->sys_vm_off);

    for (index = 0ul; index < header->logged &&
                      index < V9X_IOTRACE_ENTRIES &&
                      (index + 1ul) * sizeof(V9X_IO_TRACE_ENTRY) <=
                          returned - sizeof(V9X_IO_TRACE_HEADER);
         ++index) {
        char key[16];
        char value[64];
        char part[12];
        WORD port = (WORD)(entries[index].type_port & 0xfffful);
        DWORD type = entries[index].type_port >> 16;
        DWORD slot;

        lstrcpyA(key, "E");
        v9x_uint(key + 1, index);
        lstrcpyA(value, "vm=");
        v9x_hex(part, entries[index].vm, 8);
        lstrcatA(value, part);
        lstrcatA(value, " port=");
        v9x_hex(part, port, 4);
        lstrcatA(value, part);
        lstrcatA(value, " ");
        lstrcatA(value, v9x_type_text(type));
        lstrcatA(value, " val=");
        v9x_hex(part, entries[index].value, 8);
        lstrcatA(value, part);
        v9x_write(key, value);

        for (slot = 0ul; slot < ports; ++slot) {
            if (per_port_id[slot] == port) {
                break;
            }
        }
        if (slot == ports && ports < 32ul) {
            per_port_id[ports] = port;
            per_port[ports] = 0ul;
            ++ports;
        }
        if (slot < 32ul) {
            ++per_port[slot];
        }
    }
    for (index = 0ul; index < ports; ++index) {
        char key[16];

        lstrcpyA(key, "Port");
        v9x_hex(key + 4, per_port_id[index], 4);
        v9x_write_uint(key, per_port[index]);
    }
    v9x_write("Result", "PASS");
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_IOTR_INI);
    ExitProcess(0ul);
}
