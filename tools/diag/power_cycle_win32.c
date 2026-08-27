#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#ifndef SC_MONITORPOWER
#define SC_MONITORPOWER 0xF170u
#endif

static void v9x_write_result(const char *result, const char *stage)
{
    const char path[] = V9X_DIAG_PWR_INI;
    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA("Velocity9xPower", 0, 0, path);
    WritePrivateProfileStringA("Velocity9xPower", "Result", result, path);
    WritePrivateProfileStringA("Velocity9xPower", "Stage", stage, path);
    WritePrivateProfileStringA("Velocity9xPower", "Build", V9X_BUILD_ID,
                               path);
    WritePrivateProfileStringA(0, 0, 0, path);
}

static int v9x_monitor_message(LPARAM state)
{
    DWORD_PTR result = 0ul;
    return SendMessageTimeoutA(HWND_BROADCAST, WM_SYSCOMMAND,
                               (WPARAM)SC_MONITORPOWER, state,
                               SMTO_ABORTIFHUNG, 5000u, &result) != 0;
}

void WINAPI V9xPowerCycleEntry(void)
{
    v9x_write_result("RUNNING", "off");
    if (!v9x_monitor_message((LPARAM)2l)) {
        v9x_write_result("FAIL", "off");
        ExitProcess(1ul);
    }
    Sleep(2500ul);
    v9x_write_result("RUNNING", "on");
    if (!v9x_monitor_message((LPARAM)-1l)) {
        v9x_write_result("FAIL", "on");
        ExitProcess(2ul);
    }
    Sleep(1000ul);
    v9x_write_result("PASS", "complete");
    ExitProcess(0ul);
}
