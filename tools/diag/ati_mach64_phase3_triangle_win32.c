#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#if defined(V9X_ARGB1555)
#define ATI3D_MAGIC 0x3e495441ul
#define ATI3D_DIOC 14u
#define ATI3D_HEADING "[AtiMach64Phase4Argb1555]\r\n"
#define ATI3D_OPERATION "guarded-argb1555-texture-alpha-observation"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4A1.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4A1.BMP"
#define ATI3D_GUARD_KEY "TextureOrGuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 19ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#elif defined(V9X_ARGB4444)
#define ATI3D_MAGIC 0x3f495441ul
#define ATI3D_DIOC 15u
#define ATI3D_HEADING "[AtiMach64Phase4Argb4444]\r\n"
#define ATI3D_OPERATION "guarded-argb4444-texture-alpha-observation"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4A4.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4A4.BMP"
#define ATI3D_GUARD_KEY "TextureOrGuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 19ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#elif defined(V9X_BILINEAR)
#define ATI3D_MAGIC 0x3d495441ul
#define ATI3D_DIOC 13u
#define ATI3D_HEADING "[AtiMach64Phase4Bilinear]\r\n"
#define ATI3D_OPERATION "guarded-rgb565-texture-bilinear-half-texel"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4BL.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4BL.BMP"
#define ATI3D_GUARD_KEY "TextureOrGuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 19ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#elif defined(V9X_WRAP)
#define ATI3D_MAGIC 0x3c495441ul
#define ATI3D_DIOC 12u
#define ATI3D_HEADING "[AtiMach64Phase4Wrap]\r\n"
#define ATI3D_OPERATION "guarded-rgb565-texture-nearest-wrap-st"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4WR.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4WR.BMP"
#define ATI3D_GUARD_KEY "TextureOrGuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 19ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#elif defined(V9X_PERSPECTIVE)
#define ATI3D_MAGIC 0x3b495441ul
#define ATI3D_DIOC 11u
#define ATI3D_HEADING "[AtiMach64Phase4Perspective]\r\n"
#define ATI3D_OPERATION "guarded-rgb565-texture-unequal-w-perspective"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4PW.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4PW.BMP"
#define ATI3D_GUARD_KEY "TextureOrGuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 19ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#elif defined(V9X_TEXTURE_STATE)
#define ATI3D_MAGIC 0x3a495441ul
#define ATI3D_DIOC 10u
#define ATI3D_HEADING "[AtiMach64Phase4TextureStateOnly]\r\n"
#define ATI3D_OPERATION "guarded-rgb565-texture-state-without-trigger"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4TS.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4TS.BMP"
#define ATI3D_GUARD_KEY "TextureOrGuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fdfful
#define ATI3D_STATE_COUNT 19ul
#define ATI3D_SETUP_COUNT 0ul
#define ATI3D_EXPECT_CHANGED 0ul
#elif defined(V9X_TEXTURE)
#define ATI3D_MAGIC 0x39495441ul
#define ATI3D_DIOC 9u
#define ATI3D_HEADING "[AtiMach64Phase4Texture]\r\n"
#define ATI3D_OPERATION "guarded-offscreen-rgb565-nearest-clamp-texture"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4TX.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4TX.BMP"
#define ATI3D_GUARD_KEY "TextureOrGuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 19ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#elif defined(V9X_ZCLEAR)
#define ATI3D_MAGIC 0x38495441ul
#define ATI3D_DIOC 8u
#define ATI3D_HEADING "[AtiMach64Phase4ZClear]\r\n"
#define ATI3D_OPERATION "guarded-offscreen-z16-write-then-2d-clear"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4ZC.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4ZC.BMP"
#define ATI3D_GUARD_KEY "DepthOrClearOrGuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 17ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#elif defined(V9X_ZWRITE)
#define ATI3D_MAGIC 0x37495441ul
#define ATI3D_DIOC 7u
#define ATI3D_HEADING "[AtiMach64Phase4ZWrite]\r\n"
#define ATI3D_OPERATION "guarded-offscreen-z16-less-write"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4ZW.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4ZW.BMP"
#define ATI3D_GUARD_KEY "DepthOrGuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 17ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#elif defined(V9X_ZTEST)
#define ATI3D_MAGIC 0x36495441ul
#define ATI3D_DIOC 6u
#define ATI3D_HEADING "[AtiMach64Phase4ZTest]\r\n"
#define ATI3D_OPERATION "guarded-offscreen-z16-less-no-write"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4Z0.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4Z0.BMP"
#define ATI3D_GUARD_KEY "DepthOrGuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 17ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#elif defined(V9X_GOURAUD)
#define ATI3D_MAGIC 0x35495441ul
#define ATI3D_DIOC 5u
#define ATI3D_HEADING "[AtiMach64Phase4Gouraud]\r\n"
#define ATI3D_OPERATION "guarded-offscreen-rgb565-gouraud-triangle"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI4G0.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI4G0.BMP"
#define ATI3D_GUARD_KEY "GuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 17ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#else
#define ATI3D_MAGIC 0x34495441ul
#define ATI3D_DIOC 4u
#define ATI3D_HEADING "[AtiMach64Phase3Triangle]\r\n"
#define ATI3D_OPERATION "guarded-offscreen-rgb565-flat-triangle"
#define ATI3D_TEXT_PATH "C:\\V9XDIAG\\ATI3D0.TXT"
#define ATI3D_BMP_PATH "C:\\V9XDIAG\\ATI3D0.BMP"
#define ATI3D_GUARD_KEY "GuardMismatches"
#define ATI3D_PASS_STATUS 0x0001fffful
#define ATI3D_STATE_COUNT 17ul
#define ATI3D_SETUP_COUNT 19ul
#define ATI3D_EXPECT_CHANGED 1ul
#endif
#define ATI3D_TRANSCRIPT_COUNT 38ul
#define ATI3D_WIDTH 64ul
#define ATI3D_HEIGHT 28ul

struct ati3d_result {
    DWORD magic;
    DWORD status;
    DWORD command_status;
    DWORD revision_class;
    DWORD bar0;
    DWORD bar2;
    DWORD chip_id;
    DWORD config_stat0;
    DWORD gui_before;
    DWORD gui_after;
    DWORD mem_before;
    DWORD mem_after;
    DWORD target_offset;
    DWORD dst_off_pitch;
    DWORD color;
    DWORD one_over_area;
    DWORD interior_mismatch;
    DWORD exterior_mismatch;
    DWORD guard_mismatch;
    DWORD restore_mismatch;
    DWORD changed_pixels;
    DWORD min_x;
    DWORD min_y;
    DWORD max_x;
    DWORD max_y;
    DWORD fifo_sample;
    DWORD state_count;
    DWORD setup_count;
    DWORD reserved;
    DWORD timeout_stage;
    DWORD first_actual;
    DWORD first_expected;
    DWORD write_offsets[ATI3D_TRANSCRIPT_COUNT];
    DWORD write_values[ATI3D_TRANSCRIPT_COUNT];
    WORD pixels[ATI3D_WIDTH * ATI3D_HEIGHT];
};

typedef char ati3d_result_size_is_4016[
    sizeof(struct ati3d_result) == 4016 ? 1 : -1];

static void hex(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    int index;
    text[0] = '0';
    text[1] = 'x';
    for (index = 0; index < 8; ++index)
        text[2 + index] = digits[(value >> ((7 - index) * 4)) & 15u];
    text[10] = '\0';
}

static void dec(char *text, DWORD value)
{
    char reverse[11];
    int count = 0;
    int index = 0;
    if (value == 0ul) {
        text[0] = '0'; text[1] = '\0'; return;
    }
    while (value != 0ul) {
        reverse[count++] = (char)('0' + value % 10ul);
        value /= 10ul;
    }
    while (count != 0) text[index++] = reverse[--count];
    text[index] = '\0';
}

static void line(HANDLE file, const char *key, const char *value)
{
    DWORD written;
    WriteFile(file, key, (DWORD)lstrlenA(key), &written, 0);
    WriteFile(file, "=", 1ul, &written, 0);
    WriteFile(file, value, (DWORD)lstrlenA(value), &written, 0);
    WriteFile(file, "\r\n", 2ul, &written, 0);
}

static void hx(HANDLE file, const char *key, DWORD value)
{
    char text[11];
    hex(text, value);
    line(file, key, text);
}

static void dn(HANDLE file, const char *key, DWORD value)
{
    char text[11];
    dec(text, value);
    line(file, key, text);
}

static void transcript(HANDLE file, const struct ati3d_result *result)
{
    DWORD index;
    char offset_key[] = "WriteOffset00";
    char value_key[] = "WriteValue00";
    for (index = 0ul; index < ATI3D_TRANSCRIPT_COUNT; ++index) {
        offset_key[11] = (char)('0' + index / 10ul);
        offset_key[12] = (char)('0' + index % 10ul);
        value_key[10] = (char)('0' + index / 10ul);
        value_key[11] = (char)('0' + index % 10ul);
        hx(file, offset_key, result->write_offsets[index]);
        hx(file, value_key, result->write_values[index]);
    }
}

static int write_bmp(const struct ati3d_result *result)
{
    BITMAPFILEHEADER file_header;
    BITMAPINFOHEADER info_header;
    HANDLE file;
    DWORD written;
    DWORD row;
    DWORD masks[3];
    file = CreateFileA(ATI3D_BMP_PATH, GENERIC_WRITE,
                       FILE_SHARE_READ, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    file_header.bfType = 0x4d42u;
    file_header.bfReserved1 = 0u;
    file_header.bfReserved2 = 0u;
    file_header.bfOffBits = sizeof(file_header) + sizeof(info_header) +
                            sizeof(masks);
    file_header.bfSize = file_header.bfOffBits +
                         ATI3D_WIDTH * ATI3D_HEIGHT * 2ul;
    info_header.biSize = sizeof(info_header);
    info_header.biWidth = ATI3D_WIDTH;
    info_header.biHeight = ATI3D_HEIGHT;
    info_header.biPlanes = 1u;
    info_header.biBitCount = 16u;
    info_header.biCompression = BI_BITFIELDS;
    info_header.biSizeImage = ATI3D_WIDTH * ATI3D_HEIGHT * 2ul;
    info_header.biXPelsPerMeter = 0l;
    info_header.biYPelsPerMeter = 0l;
    info_header.biClrUsed = 0ul;
    info_header.biClrImportant = 0ul;
    masks[0] = 0x0000f800ul;
    masks[1] = 0x000007e0ul;
    masks[2] = 0x0000001ful;
    WriteFile(file, &file_header, sizeof(file_header), &written, 0);
    WriteFile(file, &info_header, sizeof(info_header), &written, 0);
    WriteFile(file, masks, sizeof(masks), &written, 0);
    for (row = ATI3D_HEIGHT; row != 0ul; --row)
        WriteFile(file, result->pixels + (row - 1ul) * ATI3D_WIDTH,
                  ATI3D_WIDTH * 2ul, &written, 0);
    CloseHandle(file);
    return 1;
}

void WINAPI V9xAtiMach64Phase3Entry(void)
{
    struct ati3d_result result;
    HANDLE device;
    HANDLE file;
    DWORD bytes = 0ul;
    DWORD pass;
    char heading[] = ATI3D_HEADING;
    device = CreateFileA("\\\\.\\ATIEN.VXD", 0, 0, 0, CREATE_NEW,
                         FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (device == INVALID_HANDLE_VALUE) ExitProcess(2u);
    if (!DeviceIoControl(device, ATI3D_DIOC, 0, 0, &result, sizeof(result),
                         &bytes, 0) || bytes != sizeof(result) ||
        result.magic != ATI3D_MAGIC) {
        CloseHandle(device);
        ExitProcess(3u);
    }
    CloseHandle(device);
    CreateDirectoryA("C:\\V9XDIAG", 0);
    file = CreateFileA(ATI3D_TEXT_PATH, GENERIC_WRITE,
                       FILE_SHARE_READ, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) ExitProcess(4u);
    WriteFile(file, heading, (DWORD)lstrlenA(heading), &bytes, 0);
    line(file, "Build", V9X_BUILD_ID);
    line(file, "Operation", ATI3D_OPERATION);
    hx(file, "Status", result.status);
    hx(file, "PciCommandStatus", result.command_status);
    hx(file, "PciRevisionClass", result.revision_class);
    hx(file, "PciBar0", result.bar0);
    hx(file, "PciBar2", result.bar2);
    hx(file, "ConfigChipId", result.chip_id);
    hx(file, "ConfigStat0", result.config_stat0);
    hx(file, "GuiStatBefore", result.gui_before);
    hx(file, "GuiStatAfter", result.gui_after);
    hx(file, "MemBufCntlBefore", result.mem_before);
    hx(file, "MemBufCntlAfterInvalidate", result.mem_after);
    hx(file, "TargetOffset", result.target_offset);
    hx(file, "DstOffPitch", result.dst_off_pitch);
    hx(file, "VertexArgb", result.color);
    hx(file, "OneOverArea", result.one_over_area);
    dn(file, "InteriorMismatches", result.interior_mismatch);
    dn(file, "ExteriorMismatches", result.exterior_mismatch);
    dn(file, ATI3D_GUARD_KEY, result.guard_mismatch);
    dn(file, "RestoreMismatches", result.restore_mismatch);
    dn(file, "ChangedPixels", result.changed_pixels);
    dn(file, "ChangedMinX", result.min_x);
    dn(file, "ChangedMinY", result.min_y);
    dn(file, "ChangedMaxX", result.max_x);
    dn(file, "ChangedMaxY", result.max_y);
    hx(file, "FifoSample", result.fifo_sample);
    dn(file, "StateWriteCount", result.state_count);
    dn(file, "SetupWriteCount", result.setup_count);
    dn(file, "RecoveryResetCount", result.reserved);
    dn(file, "TimeoutStage", result.timeout_stage);
    hx(file, "FirstActual", result.first_actual);
    hx(file, "FirstExpected", result.first_expected);
    transcript(file, &result);
    pass = result.status == ATI3D_PASS_STATUS &&
           result.state_count == ATI3D_STATE_COUNT &&
           result.setup_count == ATI3D_SETUP_COUNT &&
           result.interior_mismatch == 0ul &&
           result.exterior_mismatch == 0ul &&
           result.guard_mismatch == 0ul &&
           result.restore_mismatch == 0ul &&
           ((ATI3D_EXPECT_CHANGED != 0ul && result.changed_pixels != 0ul) ||
            (ATI3D_EXPECT_CHANGED == 0ul && result.changed_pixels == 0ul)) &&
           result.reserved == 0ul &&
           result.timeout_stage == 0ul;
    line(file, "Result", pass ? "PASS" : "REVIEW");
    CloseHandle(file);
    if (!write_bmp(&result)) ExitProcess(5u);
    ExitProcess(pass ? 0u : 1u);
}
