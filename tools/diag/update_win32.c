/*
 * V9XUPD.EXE: Velocity9x's update checker, updater and report sender.
 *
 * The only Velocity9x program that touches the network, and only when the
 * user clicks a button that launches it
 * (docs\plans\optional-update-checker-and-auto-updater.md). The Display
 * Properties pages start it; nothing network-related runs inside that
 * process.
 *
 *   V9XUPD [/CHECK]  ask the server for a newer release; offer to install it.
 *   V9XUPD /REPORT   run V9XTRACE.EXE, show what would be sent, send it.
 *   V9XUPD /FINISH   after the restart an update needs: did it take?
 *   /SERVER=http://host[:port]  another server, for a local test fixture.
 *
 * Nothing the server says about a release is believed until the release's
 * SIGNED.TXT verifies against the key compiled in (release_key.h): plain
 * HTTP can be rewritten on the way.
 *
 * Runtime-free like the other diagnostic tools: static imports are KERNEL32,
 * USER32, GDI32 and ADVAPI32, and the network DLLs are loaded at run time
 * by update_net_win32.c.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "velocity9x/build.h"
#include "velocity9x/diagpaths.h"
#include "velocity9x/release_key.h"
#include "velocity9x/sha256.h"
#include "velocity9x/update_proto.h"
#include "update_net_win32.h"
#include "update_resource.h"
#include "update_win32.h"

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
};

struct v9x_memory_sink {
    BYTE *data;
    DWORD capacity;
    DWORD used;
};

static HINSTANCE v9x_instance;
/* The update server: michaeldale.com.au, or /SERVER='s test fixture. */
static struct v9x_update_url v9x_server;
static BOOL v9x_server_override;
static struct v9x_report_state v9x_report;
static BYTE v9x_reply[V9X_REPLY_MAX];

/* The diagnostics the report offers after the snapshot, in the order the
 * server brief lists them. The Glide 2 and Glide 3 logs follow V9XGL.LOG:
 * without them a Glide report carries no record of the game's calls
 * (V9X-VCWHZ1, 2026-10-10, attached only a stale Quake 2 V9XGL.LOG). A
 * server that refuses a name costs that one file, not the report. */
static const char *const v9x_report_names[] = {
    "V9XTRACE.INI", "V9XBOOT.INI", "V9XHW.INI", "V9XDD.INI", "V9XGL.LOG",
    "V9XGLIDE.LOG", "V9XGLD3.LOG", "V9XUPD.INI"
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
 * Where a failed upload stopped, as one line, written to V9XUPD.INI - which
 * the next report sends - and returned for the message. Three uploads of
 * 2026-10-10 reached the server as their first 16 KB and nothing on either
 * side said which transport gave up, at what stage, or with what error.
 */
static void v9x_report_record_failure(const struct v9x_report_file *file,
                                      const struct v9x_net_reply *reply,
                                      char *detail, DWORD capacity)
{
    SYSTEMTIME now;
    char line[224];

    wsprintfA(line, "%s: %s %s error %lu, %lu of %lu body bytes sent",
              file->name,
              reply->transport != 0 ? reply->transport : "no transport",
              reply->stage != 0 ? reply->stage : "start", reply->error,
              reply->body_sent, file->send);
    v9x_copy(detail, capacity, line);

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    GetLocalTime(&now);
    wsprintfA(line + v9x_length(line), " at %04u-%02u-%02u %02u:%02u",
              now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
    WritePrivateProfileStringA("Velocity9xReport", "LastSendFailure", line,
                               V9X_DIAG_UPDATE_INI);
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_UPDATE_INI);
}

/*
 * The report job, on the worker thread: one POST per file, the snapshot
 * first, as the server brief's "Suggested flow" and per-status table say.
 * The key lives only in this function; it is never written to disk.
 */
static void v9x_report_job(void)
{
    char key[40];
    char path[V9X_UPDATE_PATH_MAX + V9X_DESCRIPTION_MAX * 3 + 64];
    char status[16];
    BYTE *buffer;
    unsigned int index;

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

        {
            char line[64];

            wsprintfA(line, "Sending %s (%u of %u)...", file->name,
                      index + 1u, v9x_report.count);
            v9x_progress_set(line);
        }
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
            char detail[160];

            v9x_report.outcome = (result == V9X_NET_NO_NETWORK &&
                                  v9x_report.sent == 0u)
                                     ? V9X_REPORT_NO_NETWORK
                                     : V9X_REPORT_FAILED;
            v9x_copy(v9x_report.message, sizeof(v9x_report.message),
                     result == V9X_NET_NO_NETWORK
                         ? "The update server could not be reached."
                         : "The connection to the update server failed "
                           "part-way.");
            v9x_report_record_failure(file, &reply, detail, sizeof(detail));
            v9x_append(v9x_report.message, sizeof(v9x_report.message), " (");
            v9x_append(v9x_report.message, sizeof(v9x_report.message),
                       detail);
            v9x_append(v9x_report.message, sizeof(v9x_report.message), ")");
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
}

/*
 * The progress dialog: one line of text while a job runs on a worker
 * thread, so a slow modem or a stalled server never leaves a window that
 * does not repaint. The job sets the text through v9x_progress_set, whose
 * SetDlgItemTextA is answered by this thread's dialog loop.
 */
typedef void (*v9x_job_fn)(void);

static v9x_job_fn v9x_job;
static HWND v9x_progress_window;
static BOOL v9x_job_started;

void v9x_progress_set(const char *text)
{
    if (v9x_progress_window != 0) {
        SetDlgItemTextA(v9x_progress_window, V9X_UPD_IDC_STATUS, text);
    }
}

static DWORD WINAPI v9x_job_thread(LPVOID parameter)
{
    (void)parameter;
    v9x_job();
    PostMessageA(v9x_progress_window, V9X_WM_FINISHED, 0, 0);
    return 0ul;
}

static BOOL CALLBACK v9x_progress_dialog(HWND dialog, UINT message,
                                         WPARAM wparam, LPARAM lparam)
{
    DWORD thread_id;
    HANDLE thread;

    (void)wparam;
    switch (message) {
    case WM_INITDIALOG:
        v9x_progress_window = dialog;
        SetDlgItemTextA(dialog, V9X_UPD_IDC_STATUS, (const char *)lparam);
        thread = CreateThread(0, 0ul, v9x_job_thread, 0, 0ul, &thread_id);
        if (thread == 0) {
            EndDialog(dialog, 0);
            return TRUE;
        }
        v9x_job_started = TRUE;
        CloseHandle(thread);
        return TRUE;
    case V9X_WM_FINISHED:
        v9x_progress_window = 0;
        EndDialog(dialog, 0);
        return TRUE;
    }
    return FALSE;
}

/* Run job on a worker thread behind the progress dialog. FALSE only when
 * the thread could not be started. */
static BOOL v9x_run_job(v9x_job_fn job, const char *first_text)
{
    v9x_job = job;
    v9x_job_started = FALSE;
    (void)DialogBoxParamA(v9x_instance,
                          MAKEINTRESOURCEA(V9X_UPD_DLG_PROGRESS), 0,
                          v9x_progress_dialog, (LPARAM)first_text);
    v9x_progress_window = 0;
    return v9x_job_started;
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
    if (v9x_server_override) {
        v9x_copy(v9x_report.endpoint.host,
                 sizeof(v9x_report.endpoint.host), v9x_server.host);
        v9x_report.endpoint.port = v9x_server.port;
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
    if (!v9x_run_job(v9x_report_job, "Connecting to the update server...")) {
        v9x_message(0, "Could not start sending.", MB_ICONEXCLAMATION);
        return;
    }

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

/* ------------------------------------------------------------------ *
 *  Check, download and install
 * ------------------------------------------------------------------ */

/* Generous bounds on what each fetch may return: a check reply is a dozen
 * lines, SIGNED.TXT one section per family, notes a page of text, and a
 * package about 400 KB (the largest 0.14.0 zip is 394 KB). */
#define V9X_CHECK_REPLY_MAX  8192u
#define V9X_SIGNED_MAX       16384u
#define V9X_NOTES_MAX        16384u
#define V9X_PACKAGE_MAX      (8ul * 1024ul * 1024ul)

struct v9x_check_state {
    char status[24];
    char latest[V9X_RELEASE_VERSION_MAX];
    char message[160];
    char notes_url[V9X_UPDATE_HOST_MAX + V9X_UPDATE_PATH_MAX + 16];
    char signed_url[V9X_UPDATE_HOST_MAX + V9X_UPDATE_PATH_MAX + 16];
    char package_url[V9X_UPDATE_HOST_MAX + V9X_UPDATE_PATH_MAX + 16];
    char package_file[V9X_RELEASE_FILE_MAX];
    char package_size[16];
    char package_sha256[72];
    /* Set only when the signed file verified and agreed with the reply. */
    BOOL verified;
    char error[256];
};

/* A sink that hashes as it stores and refuses past its capacity. */
struct v9x_package_sink {
    BYTE *data;
    DWORD capacity;
    DWORD used;
    struct v9x_sha256 hash;
};

static struct v9x_check_state v9x_check;
static struct v9x_update_job v9x_update;
/* The large buffers, allocated when a check starts rather than static:
 * Watcom's linker writes _BSS into the image, and these would add some
 * 140 KB to an executable that has to fit each family's floppy. */
struct v9x_check_buffers {
    struct v9x_inf_plan plan;
    char signed_text[V9X_SIGNED_MAX + 1u];
    char notes_text[V9X_NOTES_MAX + 1u];
    char check_reply[V9X_CHECK_REPLY_MAX + 1u];
};

static struct v9x_check_buffers *v9x_buffers;

static BOOL v9x_package_sink_write(void *context, const BYTE *data,
                                   DWORD length)
{
    struct v9x_package_sink *sink = (struct v9x_package_sink *)context;
    DWORD index;

    if (sink->used + length > sink->capacity) {
        return FALSE;
    }
    for (index = 0ul; index < length; ++index) {
        sink->data[sink->used + index] = data[index];
    }
    sink->used += length;
    v9x_sha256_update(&sink->hash, data, length);
    return TRUE;
}

/* GET url into a sink. V9X_NET_* as v9x_net_send, plus V9X_NET_FAILED for
 * a URL that is not plain http or a reply that is not 200. */
static int v9x_get(const char *url, v9x_net_sink sink, void *context)
{
    struct v9x_update_url parts;
    struct v9x_net_request request;
    struct v9x_net_reply reply;
    int result;

    if (!v9x_update_url_split(url, &parts)) {
        return V9X_NET_FAILED;
    }
    /* /SERVER redirects every fetch, including the URLs the reply hands
     * back, so a local fixture serves the whole exchange. */
    if (v9x_server_override) {
        v9x_copy(parts.host, sizeof(parts.host), v9x_server.host);
        parts.port = v9x_server.port;
    }
    request.method = "GET";
    request.host = parts.host;
    request.port = parts.port;
    request.path = parts.path;
    request.extra_header = 0;
    request.body = 0;
    request.body_length = 0ul;
    request.sink = sink;
    request.sink_context = context;
    result = v9x_net_send(&request, &reply);
    if (result == V9X_NET_OK && reply.status != 200ul) {
        return V9X_NET_FAILED;
    }
    return result;
}

static void v9x_check_value(const char *section, const char *key,
                            char *value, DWORD capacity, DWORD length)
{
    if (!v9x_update_ini_value(v9x_buffers->check_reply, length, section,
                              key, value, capacity)) {
        value[0] = '\0';
    }
}

/*
 * The check job: ask the server, and when it offers an update, fetch and
 * verify SIGNED.TXT and hold the reply to it. Only the signed file's
 * version, size and SHA-256 are used from here on; the reply's are
 * compared, never trusted.
 */
static void v9x_check_job(void)
{
    struct v9x_memory_sink sink;
    struct v9x_release_info *release = &v9x_update.release;
    static const v9x_u8 key[32] = V9X_RELEASE_PUBLIC_KEY;
    char encoded_family[96];
    char path[V9X_UPDATE_PATH_MAX];
    char url[V9X_UPDATE_HOST_MAX + V9X_UPDATE_PATH_MAX + 16];
    char report_url[V9X_UPDATE_HOST_MAX + V9X_UPDATE_PATH_MAX + 16];
    v9x_u32 covered = 0ul;
    v9x_u8 reply_digest[32];
    int result;

    v9x_check.verified = FALSE;
    v9x_check.error[0] = '\0';
    if (!v9x_update_url_encode(v9x_update.install.family, encoded_family,
                               sizeof(encoded_family))) {
        v9x_copy(v9x_check.error, sizeof(v9x_check.error),
                 "The card family name is too long.");
        return;
    }
    wsprintfA(path, V9X_UPDATE_CHECK_PATH "?app=" V9X_UPDATE_APP
              "&version=" V9X_VERSION_STRING "&family=%s", encoded_family);
    wsprintfA(url, "http://%s:%u%s", v9x_server.host,
              (unsigned int)v9x_server.port, path);

    sink.data = (BYTE *)v9x_buffers->check_reply;
    sink.capacity = sizeof(v9x_buffers->check_reply);
    sink.used = 0ul;
    result = v9x_get(url, v9x_memory_sink_write, &sink);
    if (result != V9X_NET_OK) {
        v9x_copy(v9x_check.error, sizeof(v9x_check.error),
                 result == V9X_NET_NO_NETWORK
                     ? "The update server could not be reached."
                     : "The update server's reply could not be read.");
        return;
    }
    v9x_check_value("update", "status", v9x_check.status,
                    sizeof(v9x_check.status), sink.used);
    v9x_check_value("update", "latest", v9x_check.latest,
                    sizeof(v9x_check.latest), sink.used);
    v9x_check_value("update", "message", v9x_check.message,
                    sizeof(v9x_check.message), sink.used);
    v9x_check_value("update", "notes", v9x_check.notes_url,
                    sizeof(v9x_check.notes_url), sink.used);
    v9x_check_value("update", "signed", v9x_check.signed_url,
                    sizeof(v9x_check.signed_url), sink.used);
    v9x_check_value("package", "url", v9x_check.package_url,
                    sizeof(v9x_check.package_url), sink.used);
    v9x_check_value("package", "file", v9x_check.package_file,
                    sizeof(v9x_check.package_file), sink.used);
    v9x_check_value("package", "size", v9x_check.package_size,
                    sizeof(v9x_check.package_size), sink.used);
    v9x_check_value("package", "sha256", v9x_check.package_sha256,
                    sizeof(v9x_check.package_sha256), sink.used);

    /* The report URL the server advertises, for /REPORT next time. */
    {
        struct v9x_update_url parts;

        v9x_check_value("update", "report", report_url, sizeof(report_url),
                        sink.used);
        if (report_url[0] != '\0' &&
            v9x_update_url_split(report_url, &parts)) {
            CreateDirectoryA(V9X_DIAG_DIR, 0);
            WritePrivateProfileStringA("Velocity9xUpdate", "ReportUrl",
                                       report_url, V9X_DIAG_UPDATE_INI);
        }
    }

    if (lstrcmpA(v9x_check.status, "update") != 0) {
        return;
    }

    v9x_progress_set("Checking the release signature...");
    if (v9x_check.signed_url[0] == '\0') {
        v9x_copy(v9x_check.error, sizeof(v9x_check.error),
                 "The server offers no signed release file for this "
                 "version, so it cannot be installed from here.");
        return;
    }
    sink.data = (BYTE *)v9x_buffers->signed_text;
    sink.capacity = sizeof(v9x_buffers->signed_text);
    sink.used = 0ul;
    if (v9x_get(v9x_check.signed_url, v9x_memory_sink_write, &sink) !=
        V9X_NET_OK) {
        v9x_copy(v9x_check.error, sizeof(v9x_check.error),
                 "The signed release file could not be downloaded.");
        return;
    }
    if (!v9x_release_verify(v9x_buffers->signed_text, sink.used, key,
                            &covered) ||
        !v9x_release_read(v9x_buffers->signed_text, covered, V9X_UPDATE_APP,
                          v9x_update.install.family, release)) {
        v9x_copy(v9x_check.error, sizeof(v9x_check.error),
                 "The release file's signature does not verify, or it has "
                 "no package for this card. Nothing was installed. This can "
                 "mean the download was altered on the way.");
        return;
    }
    /* The signed file decides; the reply must agree with it. */
    if (lstrcmpA(release->version, v9x_check.latest) != 0 ||
        v9x_update_version_compare(release->version,
                                   V9X_VERSION_STRING) != 1 ||
        lstrcmpA(release->file, v9x_check.package_file) != 0 ||
        !v9x_release_hex_digest(v9x_check.package_sha256, reply_digest)) {
        v9x_copy(v9x_check.error, sizeof(v9x_check.error),
                 "The server's reply does not match the signed release "
                 "file, or offers a version that is not newer. Nothing was "
                 "installed.");
        return;
    }
    {
        unsigned int index;

        for (index = 0u; index < 32u; ++index) {
            if (reply_digest[index] != release->sha256[index]) {
                v9x_copy(v9x_check.error, sizeof(v9x_check.error),
                         "The server's package hash does not match the "
                         "signed release file. Nothing was installed.");
                return;
            }
        }
    }
    v9x_check.verified = TRUE;

    /* Release notes are a courtesy: a failure leaves the box empty. */
    v9x_buffers->notes_text[0] = '\0';
    if (v9x_check.notes_url[0] != '\0') {
        v9x_progress_set("Reading the release notes...");
        sink.data = (BYTE *)v9x_buffers->notes_text;
        sink.capacity = sizeof(v9x_buffers->notes_text);
        sink.used = 0ul;
        if (v9x_get(v9x_check.notes_url, v9x_memory_sink_write, &sink) !=
            V9X_NET_OK) {
            v9x_buffers->notes_text[0] = '\0';
        }
    }
}

/* The download job: fetch the package, hash it against the signed
 * SHA-256, then unpack and stage (v9x_install_prepare). */
static void v9x_download_job(void)
{
    struct v9x_package_sink sink;
    v9x_u8 digest[32];
    unsigned int index;
    int result;

    v9x_update.error[0] = '\0';
    if (v9x_update.release.size > V9X_PACKAGE_MAX) {
        v9x_copy(v9x_update.error, sizeof(v9x_update.error),
                 "The package is larger than this updater accepts.");
        return;
    }
    v9x_update.zip = (BYTE *)VirtualAlloc(0, v9x_update.release.size,
                                          MEM_COMMIT, PAGE_READWRITE);
    if (v9x_update.zip == 0) {
        v9x_copy(v9x_update.error, sizeof(v9x_update.error),
                 "Not enough memory to download the package.");
        return;
    }
    sink.data = v9x_update.zip;
    sink.capacity = v9x_update.release.size;
    sink.used = 0ul;
    v9x_sha256_init(&sink.hash);
    v9x_progress_set("Downloading the new release...");
    result = v9x_get(v9x_check.package_url, v9x_package_sink_write, &sink);
    if (result != V9X_NET_OK) {
        v9x_copy(v9x_update.error, sizeof(v9x_update.error),
                 result == V9X_NET_REFUSED
                     ? "The package is larger than the signed release "
                       "file says."
                     : "The download failed part-way.");
        return;
    }
    v9x_sha256_final(&sink.hash, digest);
    if (sink.used != v9x_update.release.size) {
        v9x_copy(v9x_update.error, sizeof(v9x_update.error),
                 "The package is not the size the signed release file "
                 "says.");
        return;
    }
    for (index = 0u; index < 32u; ++index) {
        if (digest[index] != v9x_update.release.sha256[index]) {
            v9x_copy(v9x_update.error, sizeof(v9x_update.error),
                     "The package's SHA-256 does not match the signed "
                     "release file. Nothing was installed.");
            return;
        }
    }
    v9x_update.zip_length = sink.used;

    v9x_progress_set("Unpacking...");
    v9x_update.plan = &v9x_buffers->plan;
    if (!v9x_install_prepare(&v9x_update)) {
        /* v9x_install_prepare wrote the reason. */
        return;
    }
}

static BOOL CALLBACK v9x_update_dialog(HWND dialog, UINT message,
                                       WPARAM wparam, LPARAM lparam)
{
    char headline[160];

    (void)lparam;
    switch (message) {
    case WM_INITDIALOG:
        wsprintfA(headline, "Velocity9x %s is available. This computer "
                  "has %s, for the %s family.", v9x_update.release.version,
                  V9X_VERSION_STRING, v9x_update.install.family);
        SetDlgItemTextA(dialog, V9X_UPD_IDC_HEADLINE, headline);
        SetDlgItemTextA(dialog, V9X_UPD_IDC_NOTES,
                        v9x_buffers->notes_text[0] != '\0'
                            ? v9x_buffers->notes_text
                            : "The release notes could not be read.");
        /* Focus on the button, not the notes, which would otherwise come
         * up with all their text selected. */
        SetFocus(GetDlgItem(dialog, IDOK));
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL) {
            EndDialog(dialog, LOWORD(wparam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void v9x_record_check(const char *result)
{
    SYSTEMTIME now;
    char text[32];

    CreateDirectoryA(V9X_DIAG_DIR, 0);
    GetLocalTime(&now);
    wsprintfA(text, "%04u-%02u-%02u %02u:%02u", now.wYear, now.wMonth,
              now.wDay, now.wHour, now.wMinute);
    WritePrivateProfileStringA("Velocity9xUpdate", "LastCheck", text,
                               V9X_DIAG_UPDATE_INI);
    WritePrivateProfileStringA("Velocity9xUpdate", "LastCheckResult", result,
                               V9X_DIAG_UPDATE_INI);
    WritePrivateProfileStringA(0, 0, 0, V9X_DIAG_UPDATE_INI);
}

/*
 * The Check for updates... button: network present, then consent, then
 * the check, then - for a verified newer release - the offer, the
 * download, the staging and the restart. Each refusal says why and what
 * to do instead.
 */
static void v9x_run_check(void)
{
    char text[640];
    char why[320];

    v9x_buffers = (struct v9x_check_buffers *)VirtualAlloc(0,
        sizeof(*v9x_buffers), MEM_COMMIT, PAGE_READWRITE);
    if (v9x_buffers == 0) {
        v9x_message(0, "Not enough memory to check for updates.",
                    MB_ICONEXCLAMATION);
        return;
    }
    if (!v9x_install_find(&v9x_update.install, why, sizeof(why))) {
        v9x_message(0, why, MB_ICONEXCLAMATION);
        return;
    }
    if (!v9x_net_present(v9x_server.host)) {
        v9x_message(0, "No network connection was found, so Velocity9x "
                       "cannot check for updates from this computer.\r\n\r\n"
                       "New releases are listed at "
                       "https://github.com/michaeldale/velocity9x/releases.",
                       MB_ICONEXCLAMATION);
        return;
    }
    wsprintfA(text, "Check for a newer Velocity9x?\r\n\r\nThis contacts %s "
              "and sends only the installed version (%s) and card family "
              "(%s).", v9x_server.host, V9X_VERSION_STRING,
              v9x_update.install.family);
    if (MessageBoxA(0, text, V9X_UPD_TITLE,
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    if (!v9x_run_job(v9x_check_job, "Contacting the update server...")) {
        v9x_message(0, "Could not start the check.", MB_ICONEXCLAMATION);
        return;
    }
    if (v9x_check.error[0] != '\0') {
        v9x_record_check("error");
        v9x_message(0, v9x_check.error, MB_ICONEXCLAMATION);
        return;
    }
    if (lstrcmpA(v9x_check.status, "current") == 0) {
        v9x_record_check("current");
        wsprintfA(text, "Velocity9x %s is the latest release.",
                  V9X_VERSION_STRING);
        v9x_message(0, text, MB_ICONINFORMATION);
        return;
    }
    if (lstrcmpA(v9x_check.status, "update") != 0 || !v9x_check.verified) {
        v9x_record_check(v9x_check.status);
        wsprintfA(text, "The update server answered: %s%s%s",
                  v9x_check.status[0] != '\0' ? v9x_check.status
                                              : "nothing usable",
                  v9x_check.message[0] != '\0' ? ". " : ".",
                  v9x_check.message);
        v9x_message(0, text, MB_ICONEXCLAMATION);
        return;
    }
    v9x_record_check("update");

    if (DialogBoxParamA(v9x_instance, MAKEINTRESOURCEA(V9X_UPD_DLG_UPDATE),
                        0, v9x_update_dialog, 0) != IDOK) {
        return;
    }
    if (!v9x_run_job(v9x_download_job, "Downloading the new release...")) {
        v9x_message(0, "Could not start the download.", MB_ICONEXCLAMATION);
        return;
    }
    if (v9x_update.error[0] != '\0') {
        v9x_message(0, v9x_update.error, MB_ICONEXCLAMATION);
        return;
    }
    if (!v9x_install_commit(&v9x_update)) {
        v9x_message(0, v9x_update.error, MB_ICONEXCLAMATION);
        return;
    }
    wsprintfA(text, "Velocity9x %s is ready to install. It replaces the "
              "driver files when Windows restarts; the current ones are "
              "kept in %s.\r\n\r\nRestart Windows now?",
              v9x_update.release.version, v9x_update.backup_dir);
    if (MessageBoxA(0, text, V9X_UPD_TITLE, MB_YESNO | MB_ICONQUESTION) ==
        IDYES) {
        (void)ExitWindowsEx(EWX_REBOOT, 0ul);
        return;
    }
    v9x_message(0, "The update will be installed the next time Windows "
                   "restarts.", MB_ICONINFORMATION);
}

/* The value after "/NAME=" on the command line, up to the next space. */
static BOOL v9x_switch_value(const char *switch_text, char *value,
                             DWORD capacity)
{
    const char *command = GetCommandLineA();
    DWORD length = v9x_length(switch_text);

    for (; *command != '\0'; ++command) {
        DWORD index;

        for (index = 0ul; index < length; ++index) {
            char left = command[index];

            if (left >= 'a' && left <= 'z') {
                left = (char)(left - 'a' + 'A');
            }
            if (left != switch_text[index]) {
                break;
            }
        }
        if (index != length) {
            continue;
        }
        command += length;
        for (index = 0ul; command[index] != '\0' && command[index] != ' ' &&
                          index + 1ul < capacity; ++index) {
            value[index] = command[index];
        }
        value[index] = '\0';
        return index != 0ul;
    }
    return FALSE;
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

    /* /FINISH runs from RunOnce, before the desktop, and Windows waits for
     * it: it checks silently and starts /RESULT to say how it went. Both
     * come before the single-instance mutex, which /FINISH still holds
     * when /RESULT starts. */
    if (v9x_has_switch("/FINISH")) {
        v9x_install_finish();
        ExitProcess(0ul);
    }
    if (v9x_has_switch("/RESULT")) {
        v9x_install_result();
        ExitProcess(0ul);
    }

    /* One at a time: two copies would send the same files twice, or stage
     * two updates. */
    mutex = CreateMutexA(0, FALSE, "Velocity9xUpdate");
    if (mutex != 0 && GetLastError() == ERROR_ALREADY_EXISTS) {
        v9x_message(0, "Velocity9x is already checking for updates or "
                       "sending a report.", MB_ICONINFORMATION);
        ExitProcess(1ul);
    }

    v9x_copy(v9x_server.host, sizeof(v9x_server.host), V9X_UPDATE_HOST);
    v9x_server.port = (v9x_u16)V9X_UPDATE_PORT;
    {
        char url[V9X_UPDATE_HOST_MAX + 16];

        if (v9x_switch_value("/SERVER=", url, sizeof(url))) {
            if (!v9x_update_url_split(url, &v9x_server)) {
                v9x_message(0, "/SERVER= takes http://host[:port].",
                            MB_ICONEXCLAMATION);
                ExitProcess(1ul);
            }
            v9x_server_override = TRUE;
        }
    }

    if (v9x_has_switch("/REPORT")) {
        v9x_run_report();
        ExitProcess(0ul);
    }
    if (v9x_has_switch("/?")) {
        v9x_message(0, "V9XUPD.EXE " V9X_VERSION_STRING ", build "
                       V9X_BUILD_ID "\r\n\r\n"
                       "/CHECK   check for a newer release (the default)\r\n"
                       "/REPORT  send a diagnostic report",
                       MB_ICONINFORMATION);
        ExitProcess(0ul);
    }
    v9x_run_check();
    ExitProcess(0ul);
}
