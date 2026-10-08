/* Writes GlobalMemoryStatus to C:\V9XDIAG\MEMSTAT.TXT. GUI subsystem so
 * the agent does not treat it as a console (DOS VM) program. */
#include <windows.h>

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    MEMORYSTATUS status;
    char text[512];
    HANDLE file;
    DWORD written;
    int length;

    (void)instance; (void)previous; (void)command; (void)show;
    status.dwLength = sizeof(status);
    GlobalMemoryStatus(&status);
    length = wsprintfA(text,
        "dwLength=%lu\r\ndwMemoryLoad=%lu\r\ndwTotalPhys=%08lX\r\ndwAvailPhys=%08lX\r\n"
        "dwTotalPageFile=%08lX\r\ndwAvailPageFile=%08lX\r\ndwTotalVirtual=%08lX\r\n"
        "dwAvailVirtual=%08lX\r\n",
        status.dwLength, status.dwMemoryLoad, status.dwTotalPhys,
        status.dwAvailPhys, status.dwTotalPageFile, status.dwAvailPageFile,
        status.dwTotalVirtual, status.dwAvailVirtual);
    {
        DWORD spc, bps, freec, totalc;

        if (GetDiskFreeSpaceA("C:\\", &spc, &bps, &freec, &totalc)) {
            length += wsprintfA(text + length,
                "C: sectors/cluster=%lu bytes/sector=%lu free-clusters=%lu total-clusters=%lu free-MiB=%lu\r\n",
                spc, bps, freec, totalc, (freec / 1024ul) * spc * bps / 1024ul);
        }
    }
    file = CreateFileA("C:\\V9XDIAG\\MEMSTAT.TXT", GENERIC_WRITE, 0, 0,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, text, (DWORD)length, &written, 0);
        CloseHandle(file);
    }
    return 0;
}
