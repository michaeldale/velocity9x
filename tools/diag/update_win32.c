/*
 * V9XUPD.EXE: Velocity9x's update checker, updater and report sender.
 *
 * The only Velocity9x program that touches the network, and only when the
 * user clicks a button that launches it
 * (docs\plans\optional-update-checker-and-auto-updater.md). The Display
 * Properties pages start it; nothing network-related runs inside that
 * process.
 *
 *   V9XUPD /REPORT  run V9XTRACE.EXE, show what would be sent, send it.
 *
 * Runtime-free like the other diagnostic tools: static imports are KERNEL32,
 * USER32 and GDI32, and the network DLLs are loaded at run time by
 * update_net_win32.c.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/update_proto.h"
#include "update_net_win32.h"
#include "update_resource.h"

#define V9X_UPD_TITLE "Velocity9x"

/* The server's defaults (REPORT-SUBMISSION.md, Limits): 512 KB a file, 12
 * files a report. A longer file sends its last 512 KB, the end of a log
 * being the part that describes the problem. */
#define V9X_REPORT_FILE_MAX  (512ul * 1024ul)
#define V9X_REPORT_FILES_MAX 12u
/* A reply is a dozen short lines. */
#define V9X_REPLY_MAX        4096u
#define V9X_DESCRIPTION_MAX  200
/* V9XTRACE.EXE takes well under a second; a minute means it is stuck, and
 * the files already on disk are still worth sending. */
#define V9X_TRACE_WAIT_MS    60000ul

#define V9X_WM_PROGRESS (WM_APP + 1)
#define V9X_WM_FINISHED (WM_APP + 2)

/* A report's outcome, set by the worker thread. */
#define V9X_REPORT_SENT       0
#define V9X_REPORT_NO_NETWORK 1
#define V9X_REPORT_REFUSED    2
#define V9X_REPORT_FAILED     3

struct v9x_report_file {
    char name[13];
    char path[MAX_PATH];
    DWORD size;
    /* What will be sent: the size, or the last V9X_REPORT_FILE_MAX bytes. */
    DWORD send;
};

struct v9x_report_state {
    struct v9x_report_file files[V9X_REPORT_FILES_MAX];
    unsigned int count;
    char description[V9X_DESCRIPTION_MAX + 1];
    struct v9x_update_url endpoint;
    /* Worker results. */
    int outcome;
    unsigned int sent;
    char code[16];
    char message[256];
    char skipped[256];
    HWND progress;
};

struct v9x_memory_sink {
    BYTE *data;
    DWORD capacity;
    DWORD used;
};

static HINSTANCE v9x_instance;
static struct v9x_report_state v9x_report;
static BYTE v9x_reply[V9X_REPLY_MAX];

/* The diagnostics the report offers after the snapshot, in the order the
 * server brief lists them. */
static const char *const v9x_report_names[] = {
    "V9XTRACE.INI", "V9XBOOT.INI", "V9XHW.INI", "V9XDD.INI", "V9XGL.LOG",
    "V9XUPD.INI"
};

static DWORD v9x_length(const char *text)
{
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

/* Append, truncating to capacity. For display text only; anything sent to
 * the server is built with the refusing helpers in update_proto.c. */
static void v9x_append(char *buffer, DWORD capacity, const char *text)
{
    DWORD used = v9x_length(buffer);

    while (*text != '\0' && used + 1ul < capacity) {
        buffer[used++] = *text++;
    }
    buffer[used] = '\0';
}

static void v9x_copy(char *buffer, DWORD capacity, const char *text)
{
    buffer[0] = '\0';
    v9x_append(buffer, capacity, text);
}

/* Append that refuses rather than truncates: a cut-off query string asks
 * the server for something else. */
static BOOL v9x_append_exact(char *buffer, DWORD capacity, const char *text)
{
    DWORD used = v9x_length(buffer);
    DWORD length = v9x_length(text);
    DWORD index;

    if (used + length + 1ul > capacity) {
        return FALSE;
    }
    for (index = 0ul; index <= length; ++index) {
        buffer[used + index] = text[index];
    }
    return TRUE;
}

static void v9x_message(HWND owner, const char *text, UINT icon)
{
    MessageBoxA(owner, text, V9X_UPD_TITLE, MB_OK | icon);
}

static BOOL v9x_memory_sink_write(void *context, const BYTE *data,
                                  DWORD length)
{
    struct v9x_memory_sink *sink = (struct v9x_memory_sink *)context;
    DWORD index;

    /* One byte kept for the terminator the reply parser does not need but
     * a message box does. */
    if (sink->used + length + 1ul > sink->capacity) {
        return FALSE;
    }
    for (index = 0ul; index < length; ++index) {
        sink->data[sink->used + index] = data[index];
    }
    sink->used += length;
    sink->data[sink->used] = 0u;
    return TRUE;
}

/* The system directory's copy of a tool, else the one beside this
 * executable: the package folder, before the INF has installed anything. */
static BOOL v9x_find_tool(const char *file_name, char *path)
{
    UINT length = GetSystemDirectoryA(path, MAX_PATH);
    DWORD index;

    if (length != 0u && length + v9x_length(file_name) + 2ul < MAX_PATH) {
        path[length] = '\0';
        v9x_append(path, MAX_PATH, "\\");
        v9x_append(path, MAX_PATH, file_name);
        if (GetFileAttributesA(path) != 0xFFFFFFFFul) {
            return TRUE;
        }
    }
    length = GetModuleFileNameA(0, path, MAX_PATH);
    if (length == 0u || length >= MAX_PATH) {
        return FALSE;
    }
    index = length;
    while (index != 0ul && path[index - 1ul] != '\\') {
        --index;
    }
    if (index == 0ul || index + v9x_length(file_name) + 1ul > MAX_PATH) {
        return FALSE;
    }
    path[index] = '\0';
    v9x_append(path, MAX_PATH, file_name);
    return GetFileAttributesA(path) != 0xFFFFFFFFul;
}

/* Run V9XTRACE.EXE and wait for it. FALSE only when it could not be found
 * or started; a snapshot it wrote before faulting is still on disk. */
static BOOL v9x_run_trace(void)
{
    char path[MAX_PATH];
    char command[MAX_PATH + 2];
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    BYTE *raw = (BYTE *)&startup;
    DWORD index;

    if (!v9x_find_tool("V9XTRACE.EXE", path)) {
        return FALSE;
    }
    for (index = 0ul; index < sizeof(startup); ++index) {
        raw[index] = 0u;
    }
    startup.cb = sizeof(startup);
    v9x_copy(command, sizeof(command), "\"");
    v9x_append(command, sizeof(command), path);
    v9x_append(command, sizeof(command), "\"");
    if (!CreateProcessA(path, command, 0, 0, FALSE, 0ul, 0, 0, &startup,
                        &process)) {
        return FALSE;
    }
    (void)WaitForSingleObject(process.hProcess, V9X_TRACE_WAIT_MS);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return TRUE;
}

/* Add path to the report if it exists and is not empty (the server refuses
 * an empty body with 400). */
static void v9x_report_add(const char *path)
{
    struct v9x_report_file *file;
    WIN32_FIND_DATAA found;
    HANDLE search;
    DWORD index;
    DWORD start;

    if (v9x_report.count >= V9X_REPORT_FILES_MAX) {
        return;
    }
    search = FindFirstFileA(path, &found);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    FindClose(search);
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0ul ||
        found.nFileSizeHigh != 0ul || found.nFileSizeLow == 0ul) {
        return;
    }
    file = &v9x_report.files[v9x_report.count];
    v9x_copy(file->path, sizeof(file->path), path);
    start = v9x_length(path);
    while (start != 0ul && path[start - 1ul] != '\\') {
        --start;
    }
    for (index = 0ul; index < sizeof(file->name) - 1ul &&
                      path[start + index] != '\0'; ++index) {
        file->name[index] = path[start + index];
    }
    file->name[index] = '\0';
    file->size = found.nFileSizeLow;
    file->send = file->size > V9X_REPORT_FILE_MAX ? V9X_REPORT_FILE_MAX
                                                  : file->size;
    ++v9x_report.count;
}

/*
 * The snapshot V9XTRACE.EXE just wrote: it takes the first free name of
 * V9XSNAP.INI, V9XSNA1-7.INI and reuses V9XSNA7.INI after that, so the
 * newest by write time is the one, never the last by name.
 */
static void v9x_report_add_snapshot(void)
{
    static const char digits[] = "P1234567";
    char path[] = V9X_DIAG_SNAP_INI;
    char newest[sizeof(path)];
    FILETIME newest_time;
    DWORD digit = sizeof(path) - 6ul;
    unsigned int index;

    newest[0] = '\0';
    newest_time.dwLowDateTime = 0ul;
    newest_time.dwHighDateTime = 0ul;
    for (index = 0u; digits[index] != '\0'; ++index) {
        WIN32_FIND_DATAA found;
        HANDLE search;

        path[digit] = digits[index];
        search = FindFirstFileA(path, &found);
        if (search == INVALID_HANDLE_VALUE) {
            continue;
        }
        FindClose(search);
        if (newest[0] == '\0' ||
            CompareFileTime(&found.ftLastWriteTime, &newest_time) > 0) {
            v9x_copy(newest, sizeof(newest), path);
            newest_time = found.ftLastWriteTime;
        }
    }
    if (newest[0] != '\0') {
        v9x_report_add(newest);
    }
}

static void v9x_report_gather(void)
{
    unsigned int index;
    char path[MAX_PATH];

    v9x_report.count = 0u;
    v9x_report_add_snapshot();
    for (index = 0u; index < sizeof(v9x_report_names) /
                             sizeof(v9x_report_names[0]); ++index) {
        v9x_copy(path, sizeof(path), V9X_DIAG_DIR "\\");
        v9x_append(path, sizeof(path), v9x_report_names[index]);
        v9x_report_add(path);
    }
}

static BOOL CALLBACK v9x_report_dialog(HWND dialog, UINT message,
                                       WPARAM wparam, LPARAM lparam)
{
    (void)lparam;
    switch (message) {
    case WM_INITDIALOG: {
        HWND list = GetDlgItem(dialog, V9X_UPD_IDC_FILES);
        int stops[1];
        char line[96];
        DWORD total = 0ul;
        unsigned int index;

        stops[0] = 80;
        SendMessageA(list, LB_SETTABSTOPS, 1, (LPARAM)stops);
        for (index = 0u; index < v9x_report.count; ++index) {
            const struct v9x_report_file *file = &v9x_report.files[index];

            if (file->send != file->size) {
                wsprintfA(line, "%s\t%lu bytes (the last %lu KB of %lu)",
                          file->name, file->send, file->send / 1024ul,
                          file->size);
            } else {
                wsprintfA(line, "%s\t%lu bytes", file->name, file->size);
            }
            SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)line);
            total += file->send;
        }
        wsprintfA(line, "%u files, %lu bytes in all.", v9x_report.count,
                  total);
        SetDlgItemTextA(dialog, V9X_UPD_IDC_TOTAL, line);
        SendDlgItemMessageA(dialog, V9X_UPD_IDC_DESCRIPTION, EM_LIMITTEXT,
                            V9X_DESCRIPTION_MAX, 0);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK) {
            GetDlgItemTextA(dialog, V9X_UPD_IDC_DESCRIPTION,
                            v9x_report.description,
                            sizeof(v9x_report.description));
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Read what will be sent of one file: all of it, or its tail. */
static BOOL v9x_report_read(const struct v9x_report_file *file, BYTE *buffer)
{
    HANDLE handle;
    DWORD read = 0ul;
    BOOL ok;

    handle = CreateFileA(file->path, GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, 0,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    ok = SetFilePointer(handle, (LONG)(file->size - file->send), 0,
                        FILE_BEGIN) != 0xFFFFFFFFul &&
         ReadFile(handle, buffer, file->send, &read, 0) &&
         read == file->send;
    CloseHandle(handle);
    return ok;
}

static void v9x_report_note_skip(const char *name, const char *why)
{
    v9x_append(v9x_report.skipped, sizeof(v9x_report.skipped), name);
    v9x_append(v9x_report.skipped, sizeof(v9x_report.skipped), ": ");
    v9x_append(v9x_report.skipped, sizeof(v9x_report.skipped), why);
    v9x_append(v9x_report.skipped, sizeof(v9x_report.skipped), "\r\n");
}

/* The query for one file: app and file always; the description on the
 * first; the code and key on every one after. */
static BOOL v9x_report_path(const struct v9x_report_file *file,
                            const char *key, char *path, DWORD capacity)
{
    char encoded[V9X_DESCRIPTION_MAX * 3 + 1];

    v9x_copy(path, capacity, v9x_report.endpoint.path);
    if (!v9x_append_exact(path, capacity, "?app=" V9X_UPDATE_APP "&file=") ||
        !v9x_update_url_encode(file->name, encoded, sizeof(encoded)) ||
        !v9x_append_exact(path, capacity, encoded)) {
        return FALSE;
    }
    if (v9x_report.code[0] == '\0') {
        if (v9x_report.description[0] == '\0') {
            return TRUE;
        }
        return v9x_update_url_encode(v9x_report.description, encoded,
                                     sizeof(encoded)) &&
               v9x_append_exact(path, capacity, "&description=") &&
               v9x_append_exact(path, capacity, encoded);
    }
    return v9x_append_exact(path, capacity, "&report=") &&
           v9x_append_exact(path, capacity, v9x_report.code) &&
           v9x_append_exact(path, capacity, "&key=") &&
           v9x_append_exact(path, capacity, key);
}

/*
 * The worker thread: one POST per file, the snapshot first, as the server
 * brief's "Suggested flow" and per-status table say. The key lives only in
 * this function; it is never written to disk.
 */
static DWORD WINAPI v9x_report_worker(LPVOID parameter)
{
    char key[40];
    char path[V9X_UPDATE_PATH_MAX + V9X_DESCRIPTION_MAX * 3 + 64];
    char status[16];
    BYTE *buffer;
    unsigned int index;

    (void)parameter;
    key[0] = '\0';
    v9x_report.outcome = V9X_REPORT_SENT;
    buffer = (BYTE *)VirtualAlloc(0, V9X_REPORT_FILE_MAX, MEM_COMMIT,
                                  PAGE_READWRITE);
    if (buffer == 0) {
        v9x_report.outcome = V9X_REPORT_FAILED;
        v9x_copy(v9x_report.message, sizeof(v9x_report.message),
                 "Not enough memory to read the files.");
        goto done;
    }

    for (index = 0u; index < v9x_report.count; ++index) {
        const struct v9x_report_file *file = &v9x_report.files[index];
        struct v9x_net_request request;
        struct v9x_net_reply reply;
        struct v9x_memory_sink sink;
        int result;

        PostMessageA(v9x_report.progress, V9X_WM_PROGRESS, index, 0);
        if (!v9x_report_read(file, buffer)) {
            v9x_report_note_skip(file->name, "could not be read");
            continue;
        }
        if (!v9x_report_path(file, key, path, sizeof(path))) {
            v9x_report_note_skip(file->name, "request too long");
            continue;
        }

        sink.data = v9x_reply;
        sink.capacity = sizeof(v9x_reply);
        sink.used = 0ul;
        request.method = "POST";
        request.host = v9x_report.endpoint.host;
        request.port = v9x_report.endpoint.port;
        request.path = path;
        request.extra_header = 0;
        request.body = buffer;
        request.body_length = file->send;
        request.sink = v9x_memory_sink_write;
        request.sink_context = &sink;
        result = v9x_net_send(&request, &reply);
        if (result != V9X_NET_OK) {
            v9x_report.outcome = (result == V9X_NET_NO_NETWORK &&
                                  v9x_report.sent == 0u)
                                     ? V9X_REPORT_NO_NETWORK
                                     : V9X_REPORT_FAILED;
            v9x_copy(v9x_report.message, sizeof(v9x_report.message),
                     result == V9X_NET_NO_NETWORK
                         ? "The update server could not be reached."
                         : "The connection to the update server failed "
                           "part-way.");
            goto done;
        }
        v9x_report.message[0] = '\0';
        (void)v9x_update_ini_value((const char *)v9x_reply, sink.used,
                                   "report", "message",
                                   v9x_report.message,
                                   sizeof(v9x_report.message));
        status[0] = '\0';
        (void)v9x_update_ini_value((const char *)v9x_reply, sink.used,
                                   "report", "status", status,
                                   sizeof(status));

        if (reply.status == 200ul && lstrcmpA(status, "ok") == 0) {
            if (v9x_report.code[0] == '\0') {
                if (!v9x_update_ini_value((const char *)v9x_reply,
                                          sink.used, "report", "report",
                                          v9x_report.code,
                                          sizeof(v9x_report.code)) ||
                    !v9x_update_report_code_valid(v9x_report.code) ||
                    !v9x_update_ini_value((const char *)v9x_reply,
                                          sink.used, "report", "key", key,
                                          sizeof(key))) {
                    v9x_report.code[0] = '\0';
                    v9x_report.outcome = V9X_REPORT_FAILED;
                    v9x_copy(v9x_report.message,
                             sizeof(v9x_report.message),
                             "The server's reply did not carry a report "
                             "code.");
                    goto done;
                }
            }
            ++v9x_report.sent;
            continue;
        }
        /* Per file: empty, too large, or not text. Skip it and go on. */
        if (reply.status == 400ul || reply.status == 413ul ||
            reply.status == 415ul) {
            v9x_report_note_skip(file->name, v9x_report.message[0] != '\0'
                                                 ? v9x_report.message
                                                 : "refused");
            continue;
        }
        /*
         * Everything else stops: 429 (the daily limit), 503 (submission
         * turned off), 404, 500. 403 and 409 on an append mean the report
         * was closed or the key refused in the seconds since it was made;
         * the brief suggests starting a second report, but two codes for
         * one problem is worse than stopping with the files already sent.
         */
        v9x_report.outcome = V9X_REPORT_REFUSED;
        if (v9x_report.message[0] == '\0') {
            wsprintfA(v9x_report.message, "The server answered HTTP %lu.",
                      reply.status);
        }
        goto done;
    }
    if (v9x_report.sent == 0u && v9x_report.outcome == V9X_REPORT_SENT) {
        v9x_report.outcome = V9X_REPORT_REFUSED;
        v9x_copy(v9x_report.message, sizeof(v9x_report.message),
                 "No file could be sent.");
    }

done:
    if (buffer != 0) {
        VirtualFree(buffer, 0ul, MEM_RELEASE);
    }
    PostMessageA(v9x_report.progress, V9X_WM_FINISHED, 0, 0);
    return 0ul;
}

static BOOL CALLBACK v9x_progress_dialog(HWND dialog, UINT message,
                                         WPARAM wparam, LPARAM lparam)
{
    char line[64];
    DWORD thread_id;
    HANDLE thread;

    (void)lparam;
    switch (message) {
    case WM_INITDIALOG:
        v9x_report.progress = dialog;
        SetDlgItemTextA(dialog, V9X_UPD_IDC_STATUS,
                        "Connecting to the update server...");
        thread = CreateThread(0, 0ul, v9x_report_worker, 0, 0ul, &thread_id);
        if (thread == 0) {
            v9x_report.outcome = V9X_REPORT_FAILED;
            v9x_copy(v9x_report.message, sizeof(v9x_report.message),
                     "Could not start the sending thread.");
            EndDialog(dialog, 0);
            return TRUE;
        }
        CloseHandle(thread);
        return TRUE;
    case V9X_WM_PROGRESS:
        if ((unsigned int)wparam < v9x_report.count) {
            wsprintfA(line, "Sending %s (%u of %u)...",
                      v9x_report.files[wparam].name,
                      (unsigned int)wparam + 1u, v9x_report.count);
            SetDlgItemTextA(dialog, V9X_UPD_IDC_STATUS, line);
        }
        return TRUE;
    case V9X_WM_FINISHED:
        EndDialog(dialog, 0);
        return TRUE;
    }
    return FALSE;
}

static void v9x_copy_to_clipboard(HWND owner, const char *text)
{
    DWORD length = v9x_length(text);
    HGLOBAL memory;
    char *target;
    DWORD index;

    memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, length + 1ul);
    if (memory == 0) {
        return;
    }
    target = (char *)GlobalLock(memory);
    if (target == 0) {
        GlobalFree(memory);
        return;
    }
    for (index = 0ul; index <= length; ++index) {
        target[index] = text[index];
    }
    GlobalUnlock(memory);
    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_TEXT, memory) == 0) {
        GlobalFree(memory);
    }
    CloseClipboard();
}

static BOOL CALLBACK v9x_sent_dialog(HWND dialog, UINT message,
                                     WPARAM wparam, LPARAM lparam)
{
    static HFONT code_font;
    char text[768];

    (void)lparam;
    switch (message) {
    case WM_INITDIALOG:
        /* Large enough to read off a 640x480 screen and type into an
         * issue on another computer. */
        code_font = CreateFontA(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                FIXED_PITCH | FF_MODERN, "Courier New");
        if (code_font != 0) {
            SendDlgItemMessageA(dialog, V9X_UPD_IDC_CODE, WM_SETFONT,
                                (WPARAM)code_font, FALSE);
        }
        SetDlgItemTextA(dialog, V9X_UPD_IDC_CODE, v9x_report.code);
        wsprintfA(text, "%u of %u files were received.\r\n\r\n"
                  "To ask for help, open an issue at "
                  V9X_UPDATE_ISSUES_URL " and quote this code. "
                  "Copy report on the Velocity9x tab includes it too.",
                  v9x_report.sent, v9x_report.count);
        if (v9x_report.skipped[0] != '\0') {
            v9x_append(text, sizeof(text), "\r\n\r\nNot sent:\r\n");
            v9x_append(text, sizeof(text), v9x_report.skipped);
        }
        if (v9x_report.outcome != V9X_REPORT_SENT) {
            v9x_append(text, sizeof(text), "\r\n\r\nStopped early: ");
            v9x_append(text, sizeof(text), v9x_report.message);
        }
        SetDlgItemTextA(dialog, V9X_UPD_IDC_MESSAGE, text);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wparam) == V9X_UPD_IDC_COPY) {
            v9x_copy_to_clipboard(dialog, v9x_report.code);
            return TRUE;
        }
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        if (code_font != 0) {
            DeleteObject(code_font);
            code_font = 0;
        }
        break;
    }
    return FALSE;
}

/* Record the code where Copy report (settings_status.c) and a later report
 * can find it. The key is deliberately not written. */
static void v9x_report_record(void)
{
    SYSTEMTIME now;
    char text[32];

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    GetLocalTime(&now);
    wsprintfA(text, "%04u-%02u-%02u %02u:%02u", now.wYear, now.wMonth,
              now.wDay, now.wHour, now.wMinute);
    WritePrivateProfileStringA("Velocity9xReport", "LastReport",
                               v9x_report.code, V9X_DIAG_REPORT_INI);
    WritePrivateProfileStringA("Velocity9xReport", "LastReportTime", text,
                               V9X_DIAG_REPORT_INI);
    wsprintfA(text, "%u", v9x_report.sent);
    WritePrivateProfileStringA("Velocity9xReport", "LastReportFiles", text,
                               V9X_DIAG_REPORT_INI);
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_REPORT_INI);
}

static void v9x_run_report(void)
{
    char text[512];
    char url[V9X_UPDATE_HOST_MAX + V9X_UPDATE_PATH_MAX + 16];
    BOOL traced;

    traced = v9x_run_trace();
    v9x_report_gather();
    if (v9x_report.count == 0u) {
        v9x_message(0, traced
            ? "V9XTRACE.EXE ran but no diagnostic files were found in "
              "C:\\V9XDIAG, so there is nothing to send."
            : "V9XTRACE.EXE was not found, and there are no diagnostic "
              "files in C:\\V9XDIAG to send.", MB_ICONEXCLAMATION);
        return;
    }

    /* A check reply's report= URL, saved by /CHECK, wins over the
     * built-in path, so the server can move it. */
    GetPrivateProfileStringA("Velocity9xUpdate", "ReportUrl", "", url,
                             sizeof(url), V9X_DIAG_UPDATE_INI);
    if (url[0] == '\0' ||
        !v9x_update_url_split(url, &v9x_report.endpoint)) {
        v9x_copy(v9x_report.endpoint.host,
                 sizeof(v9x_report.endpoint.host), V9X_UPDATE_HOST);
        v9x_report.endpoint.port = (v9x_u16)V9X_UPDATE_PORT;
        v9x_copy(v9x_report.endpoint.path,
                 sizeof(v9x_report.endpoint.path), V9X_UPDATE_REPORT_PATH);
    }

    if (!v9x_net_present(v9x_report.endpoint.host)) {
        v9x_copy(text, sizeof(text),
                 "No network connection was found, so the report cannot be "
                 "sent from this computer.\r\n\r\nThe files are in "
                 "C:\\V9XDIAG. From any computer, open http://");
        v9x_append(text, sizeof(text), V9X_UPDATE_HOST);
        v9x_append(text, sizeof(text), V9X_UPDATE_REPORT_PATH);
        v9x_append(text, sizeof(text), " and attach them there.");
        v9x_message(0, text, MB_ICONEXCLAMATION);
        return;
    }
    if (!traced) {
        v9x_message(0, "V9XTRACE.EXE was not found, so no new snapshot was "
                       "taken. The files already in C:\\V9XDIAG can still "
                       "be sent.", MB_ICONINFORMATION);
    }
    if (DialogBoxParamA(v9x_instance,
                        MAKEINTRESOURCEA(V9X_UPD_DLG_REPORT), 0,
                        v9x_report_dialog, 0) != IDOK) {
        return;
    }
    (void)DialogBoxParamA(v9x_instance,
                          MAKEINTRESOURCEA(V9X_UPD_DLG_PROGRESS), 0,
                          v9x_progress_dialog, 0);

    if (v9x_report.code[0] != '\0') {
        v9x_report_record();
        (void)DialogBoxParamA(v9x_instance,
                              MAKEINTRESOURCEA(V9X_UPD_DLG_SENT), 0,
                              v9x_sent_dialog, 0);
        return;
    }
    if (v9x_report.outcome == V9X_REPORT_NO_NETWORK) {
        v9x_copy(text, sizeof(text), v9x_report.message);
        v9x_append(text, sizeof(text), "\r\n\r\nThe files are in "
                   "C:\\V9XDIAG. From any computer, open http://");
        v9x_append(text, sizeof(text), V9X_UPDATE_HOST);
        v9x_append(text, sizeof(text), V9X_UPDATE_REPORT_PATH);
        v9x_append(text, sizeof(text), " and attach them there.");
        v9x_message(0, text, MB_ICONEXCLAMATION);
        return;
    }
    v9x_copy(text, sizeof(text), "The report was not sent.\r\n\r\n");
    v9x_append(text, sizeof(text), v9x_report.message);
    if (v9x_report.skipped[0] != '\0') {
        v9x_append(text, sizeof(text), "\r\n\r\n");
        v9x_append(text, sizeof(text), v9x_report.skipped);
    }
    v9x_message(0, text, MB_ICONEXCLAMATION);
}

/* Whether the command line carries switch_text, without case. */
static BOOL v9x_has_switch(const char *switch_text)
{
    const char *command = GetCommandLineA();
    DWORD length = v9x_length(switch_text);

    for (; *command != '\0'; ++command) {
        DWORD index;

        for (index = 0ul; index < length; ++index) {
            char left = command[index];
            char right = switch_text[index];

            if (left >= 'a' && left <= 'z') {
                left = (char)(left - 'a' + 'A');
            }
            if (left != right) {
                break;
            }
        }
        if (index == length) {
            return TRUE;
        }
    }
    return FALSE;
}

void WINAPI V9xUpdateEntry(void)
{
    HANDLE mutex;

    v9x_instance = GetModuleHandleA(0);
    /* One at a time: two copies would send the same files twice, or later
     * stage two updates. */
    mutex = CreateMutexA(0, FALSE, "Velocity9xUpdate");
    if (mutex != 0 && GetLastError() == ERROR_ALREADY_EXISTS) {
        v9x_message(0, "Velocity9x is already checking for updates or "
                       "sending a report.", MB_ICONINFORMATION);
        ExitProcess(1ul);
    }

    if (v9x_has_switch("/REPORT")) {
        v9x_run_report();
        ExitProcess(0ul);
    }
    v9x_message(0, "V9XUPD.EXE " V9X_VERSION_STRING ", build "
                   V9X_BUILD_ID "\r\n\r\n"
                   "/REPORT  send a diagnostic report", MB_ICONINFORMATION);
    ExitProcess(0ul);
}
