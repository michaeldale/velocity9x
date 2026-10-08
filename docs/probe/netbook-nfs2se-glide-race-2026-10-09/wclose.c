/* WCLOSE.EXE <exe name>: posts WM_CLOSE to every top-level window of every
 * process whose executable file name matches (case-insensitive), and
 * writes what it did to C:\V9XDIAG\WCLOSE.TXT. GUI subsystem so the agent
 * does not treat it as a console program. Nothing is terminated. */
#include <windows.h>
#include <tlhelp32.h>

static char wclose_name[MAX_PATH];
static DWORD wclose_pids[64];
static int wclose_pid_count;
static int wclose_posted;

static int wclose_ends_with(const char *path, const char *name)
{
    int lp = lstrlenA(path);
    int ln = lstrlenA(name);

    if (ln > lp) {
        return 0;
    }
    return lstrcmpiA(path + lp - ln, name) == 0;
}

static BOOL CALLBACK wclose_window(HWND window, LPARAM unused)
{
    DWORD pid = 0;
    int i;

    (void)unused;
    GetWindowThreadProcessId(window, &pid);
    for (i = 0; i < wclose_pid_count; ++i) {
        if (wclose_pids[i] == pid) {
            PostMessageA(window, WM_CLOSE, 0, 0);
            ++wclose_posted;
        }
    }
    return TRUE;
}

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    HANDLE snapshot;
    PROCESSENTRY32 entry;
    char report[512];
    HANDLE log;
    DWORD written;
    int length;

    (void)instance; (void)previous; (void)show;
    while (*command == ' ') {
        ++command;
    }
    lstrcpynA(wclose_name, command, sizeof(wclose_name));
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        entry.dwSize = sizeof(entry);
        if (Process32First(snapshot, &entry)) {
            do {
                if (wclose_ends_with(entry.szExeFile, wclose_name) &&
                    wclose_pid_count < 64) {
                    wclose_pids[wclose_pid_count++] = entry.th32ProcessID;
                }
            } while (Process32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    EnumWindows(wclose_window, 0);
    length = wsprintfA(report, "name=%s processes=%d posted=%d\r\n", wclose_name,
                       wclose_pid_count, wclose_posted);
    log = CreateFileA("C:\\V9XDIAG\\WCLOSE.TXT", GENERIC_WRITE, 0, 0,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (log != INVALID_HANDLE_VALUE) {
        WriteFile(log, report, (DWORD)length, &written, 0);
        CloseHandle(log);
    }
    return wclose_posted > 0 ? 0 : 1;
}
