#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cfgmgr32.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_RESULT_PATH V9X_DIAG_MGA_INI
#define V9X_TARGET_PREFIX "VEN_102B&DEV_051B"
#define V9X_MAX_RESOURCE_DATA 256u
#define V9X_MAX_MEMORY_RANGES 8u

static int v9x_starts_with_ci(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        char left = *text++;
        char right = *prefix++;
        if (left >= 'a' && left <= 'z') left = (char)(left - 32);
        if (right >= 'a' && right <= 'z') right = (char)(right - 32);
        if (left != right) return 0;
    }
    return 1;
}

static void v9x_copy(char *destination, const char *source, DWORD capacity)
{
    DWORD index = 0;
    if (capacity == 0u) return;
    while (index + 1u < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static void v9x_append(char *destination, const char *source, DWORD capacity)
{
    DWORD used = 0;
    while (used < capacity && destination[used] != '\0') ++used;
    if (used < capacity) v9x_copy(destination + used, source, capacity - used);
}

static void v9x_u32_decimal(char *text, DWORD value)
{
    char reverse[16];
    int count = 0;
    int index;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (index = 0; index < count; ++index) text[index] = reverse[count-index-1];
    text[count] = '\0';
}

static void v9x_u32_hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    text[0] = '0'; text[1] = 'x';
    for (shift = 28; shift >= 0; shift -= 4) {
        text[2 + (28-shift)/4] = digits[(value >> shift) & 15u];
    }
    text[10] = '\0';
}

static DWORD v9x_read_u32(const BYTE *data)
{
    return (DWORD)data[0] | ((DWORD)data[1] << 8) |
           ((DWORD)data[2] << 16) | ((DWORD)data[3] << 24);
}

static void v9x_write(const char *key, const char *value)
{
    HANDLE file;
    DWORD written;
    DWORD length;

    file = CreateFileA(V9X_RESULT_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return;
    SetFilePointer(file, 0, 0, FILE_END);
    length = lstrlenA(key);
    WriteFile(file, key, length, &written, 0);
    WriteFile(file, "=", 1u, &written, 0);
    length = lstrlenA(value);
    WriteFile(file, value, length, &written, 0);
    WriteFile(file, "\r\n", 2u, &written, 0);
    CloseHandle(file);
}

static void v9x_write_u32(const char *key, DWORD value, int hexadecimal)
{
    char text[16];
    if (hexadecimal) v9x_u32_hex(text, value);
    else v9x_u32_decimal(text, value);
    v9x_write(key, text);
}

static LONG v9x_find_device(char *device_id, DWORD capacity)
{
    HKEY pci_key;
    HKEY adapter_key;
    DWORD adapter_index = 0;
    char adapter[160];
    DWORD adapter_length;
    LONG status;

    status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Enum\\PCI", 0,
                           KEY_READ, &pci_key);
    if (status != ERROR_SUCCESS) return status;

    for (;;) {
        adapter_length = sizeof(adapter);
        status = RegEnumKeyExA(pci_key, adapter_index++, adapter,
                               &adapter_length, 0, 0, 0, 0);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS || !v9x_starts_with_ci(adapter,
                                                           V9X_TARGET_PREFIX)) {
            continue;
        }
        if (RegOpenKeyExA(pci_key, adapter, 0, KEY_READ, &adapter_key) ==
            ERROR_SUCCESS) {
            char instance[160];
            DWORD instance_length = sizeof(instance);
            status = RegEnumKeyExA(adapter_key, 0, instance, &instance_length,
                                   0, 0, 0, 0);
            RegCloseKey(adapter_key);
            if (status == ERROR_SUCCESS) {
                v9x_copy(device_id, "PCI\\", capacity);
                v9x_append(device_id, adapter, capacity);
                v9x_append(device_id, "\\", capacity);
                v9x_append(device_id, instance, capacity);
                RegCloseKey(pci_key);
                return ERROR_SUCCESS;
            }
        }
    }
    RegCloseKey(pci_key);
    return ERROR_FILE_NOT_FOUND;
}

static CONFIGRET v9x_capture_memory_resources(DEVINST device)
{
    LOG_CONF logical_config;
    RES_DES current;
    RES_DES next;
    CONFIGRET result;
    DWORD count = 0;

    result = CM_Get_First_Log_Conf(&logical_config, device, ALLOC_LOG_CONF);
    if (result != CR_SUCCESS) return result;
    current = (RES_DES)logical_config;
    for (;;) {
        BYTE data[V9X_MAX_RESOURCE_DATA];
        ULONG size = 0;
        char key[32];
        char suffix[4];
        DWORD base_low;
        DWORD base_high;
        DWORD end_low;
        DWORD end_high;
        DWORD range_bytes;

        result = CM_Get_Next_Res_Des(&next, current, ResType_Mem, 0, 0);
        if (current != (RES_DES)logical_config) {
            CM_Free_Res_Des_Handle(current);
        }
        if (result == CR_NO_MORE_RES_DES) {
            result = CR_SUCCESS;
            break;
        }
        if (result != CR_SUCCESS) break;
        current = next;
        if (count >= V9X_MAX_MEMORY_RANGES) continue;
        if (CM_Get_Res_Des_Data_Size(&size, current, 0) != CR_SUCCESS ||
            size < 32u || size > sizeof(data) ||
            CM_Get_Res_Des_Data(current, data, size, 0) != CR_SUCCESS) {
            result = 1u;
            break;
        }
        base_low = v9x_read_u32(data + 8);
        base_high = v9x_read_u32(data + 12);
        end_low = v9x_read_u32(data + 16);
        end_high = v9x_read_u32(data + 20);
        range_bytes = (base_high == 0u && end_high == 0u &&
                       end_low >= base_low) ? end_low - base_low + 1u : 0u;
        suffix[0] = (char)('0' + count);
        suffix[1] = '\0';
        v9x_copy(key, "Memory", sizeof(key)); v9x_append(key, suffix, sizeof(key));
        v9x_append(key, "BaseLow", sizeof(key)); v9x_write_u32(key, base_low, 1);
        v9x_copy(key, "Memory", sizeof(key)); v9x_append(key, suffix, sizeof(key));
        v9x_append(key, "BaseHigh", sizeof(key)); v9x_write_u32(key, base_high, 1);
        v9x_copy(key, "Memory", sizeof(key)); v9x_append(key, suffix, sizeof(key));
        v9x_append(key, "EndLow", sizeof(key)); v9x_write_u32(key, end_low, 1);
        v9x_copy(key, "Memory", sizeof(key)); v9x_append(key, suffix, sizeof(key));
        v9x_append(key, "EndHigh", sizeof(key)); v9x_write_u32(key, end_high, 1);
        v9x_copy(key, "Memory", sizeof(key)); v9x_append(key, suffix, sizeof(key));
        v9x_append(key, "Flags", sizeof(key));
        v9x_write_u32(key, v9x_read_u32(data + 24), 1);
        if (range_bytes == 0x00004000ul) {
            v9x_write_u32("ControlBase", base_low, 1);
        } else if (range_bytes == 0x01000000ul &&
                   (v9x_read_u32(data + 24) & 4u) != 0u) {
            v9x_write_u32("FramebufferBase", base_low, 1);
            v9x_write_u32("FramebufferApertureBytes", range_bytes, 1);
        } else if (range_bytes == 0x00800000ul) {
            v9x_write_u32("PseudoDmaBase", base_low, 1);
        }
        ++count;
    }
    if (current != (RES_DES)logical_config) CM_Free_Res_Des_Handle(current);
    CM_Free_Log_Conf_Handle(logical_config);
    v9x_write_u32("MemoryRangeCount", count, 0);
    return result;
}

void WINAPI V9xMatroxInventoryEntry(void)
{
    char device_id[MAX_DEVICE_ID_LEN];
    DEVINST device;
    CONFIGRET config_result;
    HDC display;
    LONG find_result;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    DeleteFileA(V9X_RESULT_PATH);
    {
        HANDLE file;
        DWORD written;
        file = CreateFileA(V9X_RESULT_PATH, GENERIC_WRITE, FILE_SHARE_READ, 0,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file == INVALID_HANDLE_VALUE) ExitProcess(1u);
        WriteFile(file, "[MatroxInventory]\r\n", 19u, &written, 0);
        CloseHandle(file);
    }
    v9x_write("Build", V9X_BUILD_ID);
    v9x_write("VendorId", "102B");
    v9x_write("DeviceId", "051B");
    v9x_write("Chip", "MGA-2164W");
    v9x_write("Access", "read-only");

    display = GetDC(0);
    if (display != 0) {
        v9x_write_u32("DesktopWidth", (DWORD)GetDeviceCaps(display, HORZRES), 0);
        v9x_write_u32("DesktopHeight", (DWORD)GetDeviceCaps(display, VERTRES), 0);
        v9x_write_u32("DesktopBpp", (DWORD)(GetDeviceCaps(display, BITSPIXEL) *
                      GetDeviceCaps(display, PLANES)), 0);
        ReleaseDC(0, display);
    }

    find_result = v9x_find_device(device_id, sizeof(device_id));
    if (find_result != ERROR_SUCCESS) {
        v9x_write("Result", "FAIL-device-not-found");
        ExitProcess(2u);
    }
    v9x_write("DeviceInstance", device_id);
    config_result = CM_Locate_DevNodeA(&device, device_id, CM_LOCATE_DEVNODE_NORMAL);
    if (config_result != CR_SUCCESS) {
        v9x_write_u32("ConfigManagerError", config_result, 1);
        v9x_write("Result", "FAIL-locate-devnode");
        ExitProcess(3u);
    }
    config_result = v9x_capture_memory_resources(device);
    if (config_result != CR_SUCCESS) {
        v9x_write_u32("ConfigManagerError", config_result, 1);
        v9x_write("Result", "FAIL-resources");
        ExitProcess(4u);
    }
    v9x_write("Result", "PASS");
    ExitProcess(0u);
}
