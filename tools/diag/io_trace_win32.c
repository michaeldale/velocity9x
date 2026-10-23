/*
 * Readout for the -IoTrace mini-VDD: the port accesses V86 VMs made to the
 * trapped ports (the 8514/A file and the display adapter's I/O BARs), as
 * C:\V9XDIAG\V9XIOTR.TXT. "V9XIOTR /reset" clears the log instead, so a run
 * can be bracketed: reset, open the DOS box, read.
 *
 * An Intel GMA's I/O BAR is an MMIO_INDEX (+0) / MMIO_DATA (+4) pair; each
 * data access is annotated with the register the last index write named, so
 * the log reads as the display registers a DOS VM's video BIOS touched.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_IOTR_PATH        V9X_DIAG_PATH("V9XIOTR.TXT")
#define V9X_IOTR_DIOC_READ   1ul
#define V9X_IOTR_DIOC_RESET  2ul
/* Must match V9X_IOTRACE_ENTRIES and the header in src\minivdd32\loader.asm. */
#define V9X_IOTR_ENTRIES     4096ul
#define V9X_IOTR_BAR_MAX     6ul
/* Four trace counters, then the adapter record: config address, BAR count,
 * bases, sizes, the Intel MMIO_INDEX/MMIO_DATA pair, and the INT 10h guard's
 * count of mode sets kept from the BIOS. */
#define V9X_IOTR_HEADER      (8ul + 2ul * V9X_IOTR_BAR_MAX)
#define V9X_IOTR_INTEL_PAIR (6ul + 2ul * V9X_IOTR_BAR_MAX)
#define V9X_IOTR_GUARD_COUNT (V9X_IOTR_INTEL_PAIR + 1ul)
/* VMM I/O type bits (VMM.INC): output, and the width field. */
#define V9X_IOTR_OUTPUT      0x04ul
#define V9X_IOTR_WIDTH_MASK  0x18ul
#define V9X_IOTR_WIDTH_WORD  0x08ul
#define V9X_IOTR_WIDTH_DWORD 0x10ul
/* An Intel I/O BAR starts with the index at +0 and the data at +4. */
#define V9X_IOTR_GMA_PAIR    8ul

static DWORD v9x_buffer[V9X_IOTR_HEADER + V9X_IOTR_ENTRIES * 3ul];
static char v9x_line[160];

static char *v9x_append_text(char *cursor, const char *text)
{
    while (*text != '\0') {
        *cursor++ = *text++;
    }
    return cursor;
}

static char *v9x_append_hex(char *cursor, DWORD value, unsigned digits)
{
    static const char hex[] = "0123456789ABCDEF";

    while (digits != 0u) {
        --digits;
        *cursor++ = hex[(value >> (digits * 4u)) & 0xFul];
    }
    return cursor;
}

static char *v9x_append_uint(char *cursor, DWORD value)
{
    char reverse[12];
    unsigned count = 0u;

    do {
        reverse[count++] = (char)('0' + value % 10ul);
        value /= 10ul;
    } while (value != 0ul);
    while (count != 0u) {
        *cursor++ = reverse[--count];
    }
    return cursor;
}

static void v9x_write_line(HANDLE file, char *end)
{
    DWORD written;

    *end++ = '\r';
    *end++ = '\n';
    WriteFile(file, v9x_line, (DWORD)(end - v9x_line), &written, 0);
}

static void v9x_write_text(HANDLE file, const char *text)
{
    v9x_write_line(file, v9x_append_text(v9x_line, text));
}

/* The Intel MMIO_INDEX/MMIO_DATA pair's base if a port is in it, or 0. The
 * mini-VDD names the pair. */
static DWORD v9x_gma_bar(const DWORD *header, DWORD port)
{
    DWORD base = header[V9X_IOTR_INTEL_PAIR];

    if (base != 0ul && port >= base && port < base + V9X_IOTR_GMA_PAIR) {
        return base;
    }
    return 0ul;
}

static void v9x_dump(HANDLE file, const DWORD *header, DWORD bytes)
{
    const DWORD *log = header + V9X_IOTR_HEADER;
    DWORD entries = (bytes - V9X_IOTR_HEADER * 4ul) / 12ul;
    DWORD gma_index = 0ul;
    DWORD index;
    char *cursor;

    cursor = v9x_append_text(v9x_line, "Count=");
    cursor = v9x_append_uint(cursor, header[0]);
    cursor = v9x_append_text(cursor, " Logged=");
    cursor = v9x_append_uint(cursor, header[1]);
    cursor = v9x_append_text(cursor, " Installed=");
    cursor = v9x_append_uint(cursor, header[2]);
    cursor = v9x_append_text(cursor, " SysVmOff=");
    cursor = v9x_append_uint(cursor, header[3]);
    v9x_write_line(file, cursor);

    /* Config address: bus 23:16, device 15:11, function 10:8. */
    cursor = v9x_append_text(v9x_line, "Adapter=");
    if (header[4] == 0ul) {
        cursor = v9x_append_text(cursor, "none");
    } else {
        cursor = v9x_append_hex(cursor, (header[4] >> 16) & 0xFFul, 2u);
        cursor = v9x_append_text(cursor, ":");
        cursor = v9x_append_hex(cursor, (header[4] >> 11) & 0x1Ful, 2u);
        cursor = v9x_append_text(cursor, ".");
        cursor = v9x_append_hex(cursor, (header[4] >> 8) & 0x7ul, 1u);
    }
    cursor = v9x_append_text(cursor, " IoBars=");
    cursor = v9x_append_uint(cursor, header[5]);
    for (index = 0ul; index < header[5] && index < V9X_IOTR_BAR_MAX;
         ++index) {
        cursor = v9x_append_text(cursor, " ");
        cursor = v9x_append_hex(cursor, header[6ul + index], 4u);
        cursor = v9x_append_text(cursor, "+");
        cursor = v9x_append_hex(cursor,
                                header[6ul + V9X_IOTR_BAR_MAX + index], 4u);
    }
    v9x_write_line(file, cursor);

    cursor = v9x_append_text(v9x_line, "IntelPair=");
    cursor = v9x_append_hex(cursor, header[V9X_IOTR_INTEL_PAIR], 4u);
    cursor = v9x_append_text(cursor, " Int10Guarded=");
    cursor = v9x_append_uint(cursor, header[V9X_IOTR_GUARD_COUNT]);
    v9x_write_line(file, cursor);
    v9x_write_text(file, "; seq vm dir width port value [gma register]");

    for (index = 0ul; index < entries; ++index) {
        DWORD vm = log[index * 3ul];
        DWORD type = log[index * 3ul + 1ul] >> 16;
        DWORD port = log[index * 3ul + 1ul] & 0xFFFFul;
        DWORD value = log[index * 3ul + 2ul];
        DWORD width = type & V9X_IOTR_WIDTH_MASK;
        DWORD gma_base = v9x_gma_bar(header, port);

        if (width == V9X_IOTR_WIDTH_DWORD) {
            /* as logged */
        } else if (width == V9X_IOTR_WIDTH_WORD) {
            value &= 0xFFFFul;
        } else {
            value &= 0xFFul;
        }
        cursor = v9x_append_uint(v9x_line, index);
        cursor = v9x_append_text(cursor, " ");
        cursor = v9x_append_hex(cursor, vm, 8u);
        cursor = v9x_append_text(cursor,
                                 (type & V9X_IOTR_OUTPUT) != 0ul ? " out "
                                                                 : " in  ");
        cursor = v9x_append_text(cursor,
                                 width == V9X_IOTR_WIDTH_DWORD ? "d " :
                                 width == V9X_IOTR_WIDTH_WORD ? "w " : "b ");
        cursor = v9x_append_hex(cursor, port, 4u);
        cursor = v9x_append_text(cursor, " ");
        cursor = v9x_append_hex(cursor, value, 8u);
        if (gma_base != 0ul) {
            if (port == gma_base && (type & V9X_IOTR_OUTPUT) != 0ul &&
                width == V9X_IOTR_WIDTH_DWORD) {
                gma_index = value;
            } else if (port == gma_base + 4ul) {
                cursor = v9x_append_text(cursor, " reg=");
                cursor = v9x_append_hex(cursor, gma_index, 5u);
            }
        }
        v9x_write_line(file, cursor);
    }
}

void __stdcall V9xIoTraceEntry(void)
{
    const char *command = GetCommandLineA();
    BOOL reset = FALSE;
    HANDLE device;
    HANDLE file;
    DWORD bytes = 0ul;

    while (*command != '\0') {
        if (command[0] == '/' &&
            (command[1] == 'r' || command[1] == 'R')) {
            reset = TRUE;
        }
        ++command;
    }

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    file = CreateFileA(V9X_IOTR_PATH, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        ExitProcess(2u);
    }
    v9x_write_text(file, "Build=" V9X_BUILD_ID);

    device = CreateFileA("\\\\.\\V9XMINI", 0, 0, 0, 0, 0, 0);
    if (device == INVALID_HANDLE_VALUE) {
        v9x_write_text(file, "Result=NO-DEVICE");
        CloseHandle(file);
        ExitProcess(1u);
    }

    if (reset) {
        if (DeviceIoControl(device, V9X_IOTR_DIOC_RESET, 0, 0, 0, 0, &bytes,
                            0)) {
            v9x_write_text(file, "Result=RESET");
        } else {
            v9x_write_text(file, "Result=RESET-FAILED");
        }
    } else if (!DeviceIoControl(device, V9X_IOTR_DIOC_READ, 0, 0, v9x_buffer,
                                sizeof(v9x_buffer), &bytes, 0) ||
               bytes < V9X_IOTR_HEADER * 4ul) {
        v9x_write_text(file, "Result=READ-FAILED");
    } else {
        v9x_dump(file, v9x_buffer, bytes);
        v9x_write_text(file, "Result=COMPLETE");
    }
    CloseHandle(device);
    CloseHandle(file);
    ExitProcess(0u);
}
