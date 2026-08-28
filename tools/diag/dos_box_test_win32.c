/*
 * Differential driver for the DOS-box hang
 * (docs\issues\2026-08-28-dos-box-entry-hang-gma950.md).
 *
 * The HP Mini 110 hangs hard when a DOS box opens, and it cannot be
 * instrumented: nothing on that path writes a boot stage, and a netbook with
 * no serial port cannot capture the mini-VDD's trace output at all. The only
 * evidence a hang can leave is therefore a record written *before* the risky
 * action and flushed to disk, which a later run reads back. A trial still
 * marked "armed" when the next run starts is a trial that took the machine
 * down with it.
 *
 * So each run resolves the previous trial, arms the next, launches it, and
 * waits for it to close. The tester double-clicks, power-cycles if it hangs,
 * and double-clicks again; the file accumulates the matrix with nothing
 * written down by hand and nothing to remember between reboots.
 *
 * A batch file cannot do this job. Running one opens a DOS box, which is the
 * thing under test.
 *
 * The desktop mode is recorded rather than changed. Switching depth or
 * resolution needs Display Properties and sometimes a restart, so the tester
 * changes the mode and re-runs; each trial carries the mode it ran under, and
 * the matrix assembles itself.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_DOSBOX_PATH   V9X_DIAG_DOSBOX_INI
#define V9X_SECTION       "Velocity9xDosBox"

/* COMSPEC is honoured where it is set: a machine booted from a different
 * drive has its command interpreter somewhere other than C:\COMMAND.COM, and
 * launching the wrong one would fail in a way that looks like the defect. */
#define V9X_DEFAULT_SHELL "COMMAND.COM"

/* Trials are capped so a file that is re-run for months stays readable, and
 * so the two-digit key format below cannot overflow. */
#define V9X_MAX_TRIALS    64u

static char *v9x_append_text(char *cursor, char *end, const char *text)
{
    while (cursor < end && *text != '\0') {
        *cursor++ = *text++;
    }
    return cursor;
}

static char *v9x_append_uint(char *cursor, char *end, DWORD value)
{
    char reverse[12];
    unsigned count = 0u;

    do {
        reverse[count++] = (char)('0' + value % 10ul);
        value /= 10ul;
    } while (value != 0ul);
    while (count != 0u && cursor < end) {
        *cursor++ = reverse[--count];
    }
    return cursor;
}

static int v9x_text_equals(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

/*
 * Case-insensitive substring search over the command line. Only ASCII switch
 * names are ever looked for, so a byte-wise fold is enough and pulls in no
 * runtime.
 */
static int v9x_text_contains(const char *text, const char *needle)
{
    const char *scan;
    const char *want;
    char left;
    char right;

    while (*text != '\0') {
        scan = text;
        want = needle;
        while (*want != '\0') {
            left = *scan;
            right = *want;
            if (left >= 'A' && left <= 'Z') {
                left = (char)(left + ('a' - 'A'));
            }
            if (right >= 'A' && right <= 'Z') {
                right = (char)(right + ('a' - 'A'));
            }
            if (left != right) {
                break;
            }
            ++scan;
            ++want;
        }
        if (*want == '\0') {
            return 1;
        }
        ++text;
    }
    return 0;
}

/* "Trial07", and "Trial07State" when suffix is non-zero. Two digits always,
 * so the keys sort the way they were written. */
static void v9x_trial_key(char *key, unsigned index, const char *suffix)
{
    char *cursor = key;
    char *end = key + 30;

    cursor = v9x_append_text(cursor, end, "Trial");
    *cursor++ = (char)('0' + (index / 10u) % 10u);
    *cursor++ = (char)('0' + index % 10u);
    if (suffix != 0) {
        cursor = v9x_append_text(cursor, end, suffix);
    }
    *cursor = '\0';
}

static void v9x_write(const char *key, const char *value)
{
    WritePrivateProfileStringA(V9X_SECTION, key, value, V9X_DOSBOX_PATH);
}

/*
 * Push the cached profile writes out to disk. Everything this tool records is
 * only useful if it survives a power cut, and the next thing after most of
 * these calls is the action that may cause one.
 */
static void v9x_flush(void)
{
    WritePrivateProfileStringA(0, 0, 0, V9X_DOSBOX_PATH);
}

/*
 * A trial that is still armed when a later run starts never got to write its
 * own outcome, which on this machine means the box took the desktop with it.
 */
static void v9x_resolve_previous(unsigned trials)
{
    char key[32];
    char state[32];

    if (trials == 0u) {
        return;
    }
    v9x_trial_key(key, trials - 1u, "State");
    state[0] = '\0';
    GetPrivateProfileStringA(V9X_SECTION, key, "", state, sizeof(state),
                             V9X_DOSBOX_PATH);
    if (v9x_text_equals(state, "armed")) {
        v9x_write(key, "hung");
        v9x_flush();
    }
}

/*
 * Colour depth is BITSPIXEL times PLANES, not BITSPIXEL.
 *
 * The standard VGA driver is planar: four one-bit planes, so BITSPIXEL alone
 * reports 1 and a 16-colour mode goes into the record as "640x480x1", which
 * reads like a mono mode and cost a re-reading of the first control run.
 * Every packed mode has PLANES = 1, so the multiplication is correct there
 * too.
 */
static void v9x_describe_mode(char *text, char *end)
{
    HDC screen = GetDC(0);
    char *cursor = text;
    DWORD depth;

    if (screen == 0) {
        cursor = v9x_append_text(cursor, end, "unknown");
        *cursor = '\0';
        return;
    }
    depth = (DWORD)GetDeviceCaps(screen, BITSPIXEL) *
            (DWORD)GetDeviceCaps(screen, PLANES);

    cursor = v9x_append_uint(cursor, end,
                             (DWORD)GetDeviceCaps(screen, HORZRES));
    *cursor++ = 'x';
    cursor = v9x_append_uint(cursor, end,
                             (DWORD)GetDeviceCaps(screen, VERTRES));
    *cursor++ = 'x';
    cursor = v9x_append_uint(cursor, end, depth);
    *cursor = '\0';
    ReleaseDC(0, screen);
}

/*
 * Which display driver was loaded for this trial, read from SYSTEM.INI.
 *
 * These runs compare Velocity9x against the stock VGA driver on one machine,
 * and until now the record said only what mode was current - so which driver
 * produced a result had to be carried in the covering note rather than in the
 * file. A trial that cannot say what it was testing is worth less than it
 * should be. GetPrivateProfileString resolves a bare file name against the
 * Windows directory, which is where SYSTEM.INI lives.
 */
static void v9x_describe_driver(char *text, DWORD size)
{
    text[0] = '\0';
    GetPrivateProfileStringA("boot", "display.drv", "unknown", text, size,
                             "SYSTEM.INI");
    if (text[0] == '\0') {
        text[0] = '?';
        text[1] = '\0';
    }
}

/*
 * Launch the interpreter and wait for it to close.
 *
 * CreateProcess rather than WinExec because the wait is the whole point: a
 * return from it is proof the trial survived, and it is the only proof
 * available on a machine that cannot be traced. If the machine hangs instead,
 * this process dies with it and the record stays armed.
 */
static int v9x_run_shell(void)
{
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    char command[MAX_PATH];
    DWORD length;
    unsigned index;

    length = GetEnvironmentVariableA("COMSPEC", command, sizeof(command));
    if (length == 0ul || length >= sizeof(command)) {
        for (index = 0u; V9X_DEFAULT_SHELL[index] != '\0'; ++index) {
            command[index] = V9X_DEFAULT_SHELL[index];
        }
        command[index] = '\0';
    }

    for (index = 0u; index < sizeof(startup); ++index) {
        ((char *)&startup)[index] = '\0';
    }
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;

    if (!CreateProcessA(0, command, 0, 0, FALSE, 0, 0, 0,
                        &startup, &process)) {
        return 0;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return 1;
}

void __stdcall V9xDosBoxTestEntry(void)
{
    char key[32];
    char state_key[32];
    char mode[32];
    char driver[64];
    char record[160];
    char message[512];
    char count[12];
    char *cursor;
    char *end;
    const char *command_line;
    unsigned trials;
    int fullscreen;

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    v9x_write("Build", V9X_BUILD_ID);

    trials = (unsigned)GetPrivateProfileIntA(V9X_SECTION, "Trials", 0,
                                             V9X_DOSBOX_PATH);
    v9x_resolve_previous(trials);

    if (trials >= V9X_MAX_TRIALS) {
        MessageBoxA(0, "This file already holds the maximum number of trials. "
                       "Send C:\\V9XDIAG\\V9XDOSBX.INI in and delete it to "
                       "start a fresh run.",
                    "Velocity9x DOS box test", MB_OK | MB_ICONINFORMATION);
        ExitProcess(0u);
    }

    /*
     * Strict alternation by default - windowed, then full screen - so a run
     * that hangs is followed by the *other* action rather than repeating the
     * one that just took the machine down. /win and /full override it when a
     * single case needs repeating.
     */
    command_line = GetCommandLineA();
    if (v9x_text_contains(command_line, "/full")) {
        fullscreen = 1;
    } else if (v9x_text_contains(command_line, "/win")) {
        fullscreen = 0;
    } else {
        fullscreen = (int)(trials & 1u);
    }

    v9x_describe_mode(mode, mode + sizeof(mode) - 1);
    v9x_describe_driver(driver, sizeof(driver));

    cursor = record;
    end = record + sizeof(record) - 1;
    cursor = v9x_append_text(cursor, end, "mode=");
    cursor = v9x_append_text(cursor, end, mode);
    cursor = v9x_append_text(cursor, end, " driver=");
    cursor = v9x_append_text(cursor, end, driver);
    cursor = v9x_append_text(cursor, end, " action=");
    cursor = v9x_append_text(cursor, end,
                             fullscreen ? "fullscreen" : "windowed");
    *cursor = '\0';

    v9x_trial_key(key, trials, 0);
    v9x_trial_key(state_key, trials, "State");
    v9x_write(key, record);
    v9x_write(state_key, "armed");
    cursor = v9x_append_uint(count, count + sizeof(count) - 1,
                             (DWORD)(trials + 1u));
    *cursor = '\0';
    v9x_write("Trials", count);

    /* Everything above must reach the disk before the box opens. After this
     * point the machine may not get another chance to write anything. */
    v9x_flush();

    cursor = message;
    end = message + sizeof(message) - 1;
    cursor = v9x_append_text(cursor, end, "Trial ");
    cursor = v9x_append_uint(cursor, end, (DWORD)trials);
    cursor = v9x_append_text(cursor, end, " of the DOS box test.\r\n\r\n");
    cursor = v9x_append_text(cursor, end, "Desktop mode: ");
    cursor = v9x_append_text(cursor, end, mode);
    cursor = v9x_append_text(cursor, end, "\r\nThis trial: ");
    if (fullscreen) {
        cursor = v9x_append_text(cursor, end,
            "FULL SCREEN.\r\n\r\nPress OK. A DOS box opens in a window. "
            "Press ALT+ENTER to take it full screen, ALT+ENTER again to come "
            "back, then type EXIT.");
    } else {
        cursor = v9x_append_text(cursor, end,
            "WINDOWED.\r\n\r\nPress OK. A DOS box opens in a window. Leave it "
            "in the window, then type EXIT. Do not press ALT+ENTER.");
    }
    cursor = v9x_append_text(cursor, end,
        "\r\n\r\nIf the machine hangs, power it off, start it again and run "
        "this program once more. It will record the hang by itself - there is "
        "nothing to write down.");
    *cursor = '\0';

    if (MessageBoxA(0, message, "Velocity9x DOS box test",
                    MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
        v9x_write(state_key, "cancelled");
        v9x_flush();
        ExitProcess(0u);
    }

    if (!v9x_run_shell()) {
        v9x_write(state_key, "launch-failed");
        v9x_flush();
        MessageBoxA(0, "The command interpreter could not be started at all. "
                       "That is a fault in this test program or in COMSPEC, "
                       "not the defect under test.",
                    "Velocity9x DOS box test", MB_OK | MB_ICONERROR);
        ExitProcess(1u);
    }

    /* Reached only because the box closed and the desktop is still alive. */
    v9x_write(state_key, "survived");
    v9x_flush();

    MessageBoxA(0, "That trial survived.\r\n\r\nRun this program again for the "
                   "next one. When you have done both a windowed and a full "
                   "screen trial, change the colour depth or resolution in "
                   "Display Properties and run it again - each trial records "
                   "the mode it ran under.\r\n\r\nSend "
                   "C:\\V9XDIAG\\V9XDOSBX.INI when you are done.",
                "Velocity9x DOS box test", MB_OK | MB_ICONINFORMATION);
    ExitProcess(0u);
}
