/*
 * V9XMSDOS.EXE: set keys in C:\MSDOS.SYS [Options] from Windows.
 *
 *   V9XMSDOS BootMenu=1 BootMenuDefault=2 BootMenuDelay=3 AutoScan=0
 *
 * MSDOS.SYS is read-only, hidden and system, and the usual way to edit it
 * is ATTRIB from a DOS prompt - which the v9x agent must not run, because
 * DOS work through it leaves Windows unable to restart itself. This clears
 * the attributes with SetFileAttributes, writes each key with
 * WritePrivateProfileString (MSDOS.SYS is INI-shaped and that call keeps
 * every other line, the padding included), and puts the attributes back.
 *
 * The first run copies the original to C:\MSDOS.V9X and later runs never
 * overwrite it, so the untouched file is always there to restore. Each key's
 * value before and after goes to C:\V9XDIAG\V9XMSDOS.INI.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_MSDOS_PATH   "C:\\MSDOS.SYS"
#define V9X_MSDOS_BACKUP "C:\\MSDOS.V9X"
#define V9X_REPORT_PATH  V9X_DIAG_PATH("V9XMSDOS.INI")
#define V9X_SECTION      "Options"
#define V9X_REPORT       "Velocity9xMsdos"
#define V9X_TEXT_MAX     128

static void v9x_report(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_REPORT, key, value, V9X_REPORT_PATH);
}

/* Skip one command-line token, quoted or not, and the spaces after it. */
static char *v9x_next_token(char *at)
{
    if (*at == '"') {
        ++at;
        while (*at != '\0' && *at != '"') {
            ++at;
        }
        if (*at == '"') {
            ++at;
        }
    } else {
        while (*at != '\0' && *at != ' ' && *at != '\t') {
            ++at;
        }
    }
    while (*at == ' ' || *at == '\t') {
        ++at;
    }
    return at;
}

/* Copy one key=value token into key and value; 0 if it is not one. */
static int v9x_split(const char *at, char *key, char *value)
{
    int length;

    length = 0;
    while (*at != '\0' && *at != '=' && *at != ' ' && length < V9X_TEXT_MAX - 1) {
        key[length++] = *at++;
    }
    key[length] = '\0';
    if (*at != '=' || length == 0) {
        return 0;
    }
    ++at;
    length = 0;
    while (*at != '\0' && *at != ' ' && *at != '\t' && length < V9X_TEXT_MAX - 1) {
        value[length++] = *at++;
    }
    value[length] = '\0';
    return 1;
}

void __stdcall V9xMsdosEntry(void)
{
    char *at;
    char key[V9X_TEXT_MAX];
    char value[V9X_TEXT_MAX];
    char before[V9X_TEXT_MAX];
    char name[V9X_TEXT_MAX + 8];
    DWORD attributes;
    int count;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    WritePrivateProfileStringA(V9X_REPORT, 0, 0, V9X_REPORT_PATH);
    v9x_report("Build", V9X_BUILD_ID);
    v9x_report("Result", "INCOMPLETE");

    attributes = GetFileAttributesA(V9X_MSDOS_PATH);
    if (attributes == 0xfffffffful) {
        v9x_report("Result", "NO-MSDOS-SYS");
        ExitProcess(1u);
    }

    /* bFailIfExists: an existing backup is the first original; keep it. */
    CopyFileA(V9X_MSDOS_PATH, V9X_MSDOS_BACKUP, TRUE);
    if (GetFileAttributesA(V9X_MSDOS_BACKUP) == 0xfffffffful) {
        v9x_report("Result", "NO-BACKUP");
        ExitProcess(2u);
    }

    if (!SetFileAttributesA(V9X_MSDOS_PATH, FILE_ATTRIBUTE_NORMAL)) {
        v9x_report("Result", "ATTRIBUTES-REFUSED");
        ExitProcess(3u);
    }

    count = 0;
    at = v9x_next_token(GetCommandLineA());
    while (*at != '\0') {
        if (v9x_split(at, key, value)) {
            GetPrivateProfileStringA(V9X_SECTION, key, "(absent)", before,
                                     sizeof(before), V9X_MSDOS_PATH);
            WritePrivateProfileStringA(V9X_SECTION, key, value,
                                       V9X_MSDOS_PATH);
            lstrcpyA(name, key);
            lstrcatA(name, ".Before");
            v9x_report(name, before);
            lstrcpyA(name, key);
            lstrcatA(name, ".After");
            v9x_report(name, value);
            ++count;
        }
        at = v9x_next_token(at);
    }
    WritePrivateProfileStringA(0, 0, 0, V9X_MSDOS_PATH);

    SetFileAttributesA(V9X_MSDOS_PATH, attributes);
    v9x_report("Result", count > 0 ? "COMPLETE" : "NO-KEYS");
    WritePrivateProfileStringA(0, 0, 0, V9X_REPORT_PATH);
    ExitProcess(0u);
}
