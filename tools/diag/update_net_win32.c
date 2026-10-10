/*
 * V9XUPD.EXE's HTTP client. See update_net_win32.h.
 *
 * WinInet first: it honours the proxy the user set in Internet Explorer
 * (INTERNET_OPEN_TYPE_PRECONFIG), which raw sockets cannot know about. Raw
 * Winsock HTTP/1.0 second: WSOCK32.DLL is present on every networked Win9x
 * machine, including one without Internet Explorer. The server brief
 * (v9x_update_checker docs\REPORT-SUBMISSION.md) asks for exactly this
 * order and for plain HTTP on port 80.
 *
 * Falling back is only safe before the request has been delivered: a POST
 * that WinInet sent, and whose reply was then lost, has already created a
 * report, and sending it again by Winsock would create a second one. So a
 * WinInet failure after HttpSendRequestA succeeded is final.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <winsock.h>

#include "velocity9x/build.h"
#include "velocity9x/update_proto.h"
#include "update_net_win32.h"

#define V9X_NET_AGENT "V9XUPD/" V9X_VERSION_STRING " (Win9x)"

/* Long enough for a slow modem link to deliver a 512 KB report file's
 * reply, short enough that a dead route does not look like a hang. */
#define V9X_NET_TIMEOUT_MS 30000ul
#define V9X_NET_TIMEOUT_S  30l

/* A response head larger than this is not the plugin's: it sends a handful
 * of headers. */
#define V9X_NET_HEAD_MAX 2048u
#define V9X_NET_CHUNK    4096u

/* Request line, Host, User-Agent, Content-Type, Content-Length and one
 * extra header, each bounded by the URL limits in update_proto.h. */
#define V9X_NET_REQUEST_MAX 1024u

/* INTERNET_FLAG_NO_AUTO_REDIRECT: the server never redirects /v9update/ URLs,
 * so a redirect is somebody else's answer and is not followed. */
#define V9X_NET_WININET_FLAGS (INTERNET_FLAG_RELOAD | \
    INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES | \
    INTERNET_FLAG_NO_UI | INTERNET_FLAG_PRAGMA_NOCACHE | \
    INTERNET_FLAG_NO_AUTO_REDIRECT)

typedef HINTERNET (WINAPI *v9x_internet_open_fn)(LPCSTR, DWORD, LPCSTR,
                                                 LPCSTR, DWORD);
typedef HINTERNET (WINAPI *v9x_internet_connect_fn)(HINTERNET, LPCSTR,
    INTERNET_PORT, LPCSTR, LPCSTR, DWORD, DWORD, DWORD);
typedef HINTERNET (WINAPI *v9x_http_open_request_fn)(HINTERNET, LPCSTR,
    LPCSTR, LPCSTR, LPCSTR, LPCSTR *, DWORD, DWORD);
typedef BOOL (WINAPI *v9x_http_send_request_fn)(HINTERNET, LPCSTR, DWORD,
                                                LPVOID, DWORD);
typedef BOOL (WINAPI *v9x_http_query_info_fn)(HINTERNET, DWORD, LPVOID,
                                              LPDWORD, LPDWORD);
typedef BOOL (WINAPI *v9x_internet_read_file_fn)(HINTERNET, LPVOID, DWORD,
                                                 LPDWORD);
typedef BOOL (WINAPI *v9x_internet_close_handle_fn)(HINTERNET);
typedef BOOL (WINAPI *v9x_internet_set_option_fn)(HINTERNET, DWORD, LPVOID,
                                                  DWORD);
typedef BOOL (WINAPI *v9x_internet_connected_fn)(LPDWORD, DWORD);

struct v9x_wininet {
    HMODULE module;
    v9x_internet_open_fn open;
    v9x_internet_connect_fn connect;
    v9x_http_open_request_fn open_request;
    v9x_http_send_request_fn send_request;
    v9x_http_query_info_fn query_info;
    v9x_internet_read_file_fn read_file;
    v9x_internet_close_handle_fn close_handle;
    v9x_internet_set_option_fn set_option;
    v9x_internet_connected_fn connected;
};

typedef int (PASCAL *v9x_wsa_startup_fn)(WORD, LPWSADATA);
typedef int (PASCAL *v9x_wsa_cleanup_fn)(void);
typedef int (PASCAL *v9x_wsa_last_error_fn)(void);
typedef struct hostent * (PASCAL *v9x_gethostbyname_fn)(const char *);
typedef SOCKET (PASCAL *v9x_socket_fn)(int, int, int);
typedef int (PASCAL *v9x_connect_fn)(SOCKET, const struct sockaddr *, int);
typedef int (PASCAL *v9x_send_fn)(SOCKET, const char *, int, int);
typedef int (PASCAL *v9x_recv_fn)(SOCKET, char *, int, int);
typedef int (PASCAL *v9x_closesocket_fn)(SOCKET);
typedef int (PASCAL *v9x_select_fn)(int, fd_set *, fd_set *, fd_set *,
                                    const struct timeval *);
typedef int (PASCAL *v9x_ioctlsocket_fn)(SOCKET, long, u_long *);

struct v9x_winsock {
    HMODULE module;
    BOOL started;
    v9x_wsa_startup_fn startup;
    v9x_wsa_cleanup_fn cleanup;
    v9x_wsa_last_error_fn last_error;
    v9x_gethostbyname_fn gethostbyname;
    v9x_socket_fn socket;
    v9x_connect_fn connect;
    v9x_send_fn send;
    v9x_recv_fn recv;
    v9x_closesocket_fn closesocket;
    v9x_select_fn select;
    v9x_ioctlsocket_fn ioctlsocket;
};

static struct v9x_wininet v9x_net_wininet;
static struct v9x_winsock v9x_net_winsock;

static DWORD v9x_net_length(const char *text)
{
    DWORD length = 0ul;

    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

/* Append, refusing (and reporting) anything that would not fit. */
static BOOL v9x_net_append(char *buffer, DWORD capacity, DWORD *used,
                           const char *text)
{
    DWORD length = v9x_net_length(text);
    DWORD index;

    if (*used + length + 1ul > capacity) {
        return FALSE;
    }
    for (index = 0ul; index < length; ++index) {
        buffer[*used + index] = text[index];
    }
    *used += length;
    buffer[*used] = '\0';
    return TRUE;
}

static BOOL v9x_net_load_wininet(void)
{
    struct v9x_wininet *api = &v9x_net_wininet;

    if (api->module != 0) {
        return api->open != 0;
    }
    api->module = LoadLibraryA("WININET.DLL");
    if (api->module == 0) {
        return FALSE;
    }
    api->open = (v9x_internet_open_fn)GetProcAddress(api->module,
                                                     "InternetOpenA");
    api->connect = (v9x_internet_connect_fn)GetProcAddress(api->module,
        "InternetConnectA");
    api->open_request = (v9x_http_open_request_fn)GetProcAddress(api->module,
        "HttpOpenRequestA");
    api->send_request = (v9x_http_send_request_fn)GetProcAddress(api->module,
        "HttpSendRequestA");
    api->query_info = (v9x_http_query_info_fn)GetProcAddress(api->module,
        "HttpQueryInfoA");
    api->read_file = (v9x_internet_read_file_fn)GetProcAddress(api->module,
        "InternetReadFile");
    api->close_handle = (v9x_internet_close_handle_fn)GetProcAddress(
        api->module, "InternetCloseHandle");
    api->set_option = (v9x_internet_set_option_fn)GetProcAddress(api->module,
        "InternetSetOptionA");
    api->connected = (v9x_internet_connected_fn)GetProcAddress(api->module,
        "InternetGetConnectedState");
    if (api->open == 0 || api->connect == 0 || api->open_request == 0 ||
        api->send_request == 0 || api->query_info == 0 ||
        api->read_file == 0 || api->close_handle == 0) {
        /* An IE too old for this; Winsock still works. */
        api->open = 0;
        return FALSE;
    }
    return TRUE;
}

static BOOL v9x_net_load_winsock(void)
{
    struct v9x_winsock *api = &v9x_net_winsock;
    WSADATA data;

    if (api->module != 0) {
        return api->started;
    }
    api->module = LoadLibraryA("WSOCK32.DLL");
    if (api->module == 0) {
        return FALSE;
    }
    api->startup = (v9x_wsa_startup_fn)GetProcAddress(api->module,
                                                      "WSAStartup");
    api->cleanup = (v9x_wsa_cleanup_fn)GetProcAddress(api->module,
                                                      "WSACleanup");
    api->last_error = (v9x_wsa_last_error_fn)GetProcAddress(api->module,
        "WSAGetLastError");
    api->gethostbyname = (v9x_gethostbyname_fn)GetProcAddress(api->module,
        "gethostbyname");
    api->socket = (v9x_socket_fn)GetProcAddress(api->module, "socket");
    api->connect = (v9x_connect_fn)GetProcAddress(api->module, "connect");
    api->send = (v9x_send_fn)GetProcAddress(api->module, "send");
    api->recv = (v9x_recv_fn)GetProcAddress(api->module, "recv");
    api->closesocket = (v9x_closesocket_fn)GetProcAddress(api->module,
                                                          "closesocket");
    api->select = (v9x_select_fn)GetProcAddress(api->module, "select");
    api->ioctlsocket = (v9x_ioctlsocket_fn)GetProcAddress(api->module,
                                                          "ioctlsocket");
    if (api->startup == 0 || api->cleanup == 0 || api->last_error == 0 ||
        api->gethostbyname == 0 || api->socket == 0 || api->connect == 0 ||
        api->send == 0 || api->recv == 0 || api->closesocket == 0 ||
        api->select == 0 || api->ioctlsocket == 0) {
        return FALSE;
    }
    /* Winsock 1.1: what Win95 shipped, and all this needs. */
    if (api->startup(MAKEWORD(1, 1), &data) != 0) {
        return FALSE;
    }
    api->started = TRUE;
    return TRUE;
}

BOOL v9x_net_present(const char *host)
{
    DWORD flags = 0ul;

    if (v9x_net_load_wininet() && v9x_net_wininet.connected != 0 &&
        v9x_net_wininet.connected(&flags, 0ul)) {
        return TRUE;
    }
    /* Resolving the name sends one DNS query and nothing to the host. */
    if (v9x_net_load_winsock() &&
        v9x_net_winsock.gethostbyname(host) != 0) {
        return TRUE;
    }
    return FALSE;
}

static BOOL v9x_net_header_lines(const struct v9x_net_request *request,
                                 char *headers, DWORD capacity)
{
    DWORD used = 0ul;

    headers[0] = '\0';
    if (request->body_length != 0ul &&
        !v9x_net_append(headers, capacity, &used,
                        "Content-Type: application/octet-stream\r\n")) {
        return FALSE;
    }
    if (request->extra_header != 0 &&
        (!v9x_net_append(headers, capacity, &used, request->extra_header) ||
         !v9x_net_append(headers, capacity, &used, "\r\n"))) {
        return FALSE;
    }
    return TRUE;
}

/*
 * The request through WinInet. *delivered is set once HttpSendRequestA has
 * returned success, after which the caller must not retry by Winsock.
 */
static int v9x_net_send_wininet(const struct v9x_net_request *request,
                                struct v9x_net_reply *reply,
                                BOOL *delivered)
{
    struct v9x_wininet *api = &v9x_net_wininet;
    HINTERNET session;
    HINTERNET connection = 0;
    HINTERNET handle = 0;
    char headers[256];
    BYTE chunk[V9X_NET_CHUNK];
    DWORD value;
    DWORD size;
    DWORD read;
    int result = V9X_NET_NO_NETWORK;

    *delivered = FALSE;
    reply->transport = "wininet";
    reply->stage = "connect";
    if (!v9x_net_header_lines(request, headers, sizeof(headers))) {
        return V9X_NET_FAILED;
    }
    session = api->open(V9X_NET_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, 0, 0,
                        0ul);
    if (session == 0) {
        reply->error = GetLastError();
        return V9X_NET_NO_NETWORK;
    }
    if (api->set_option != 0) {
        value = V9X_NET_TIMEOUT_MS;
        (void)api->set_option(session, INTERNET_OPTION_CONNECT_TIMEOUT,
                              &value, sizeof(value));
        (void)api->set_option(session, INTERNET_OPTION_RECEIVE_TIMEOUT,
                              &value, sizeof(value));
        (void)api->set_option(session, INTERNET_OPTION_SEND_TIMEOUT,
                              &value, sizeof(value));
    }
    connection = api->connect(session, request->host,
                              (INTERNET_PORT)request->port, 0, 0,
                              INTERNET_SERVICE_HTTP, 0ul, 0ul);
    if (connection == 0) {
        reply->error = GetLastError();
        goto done;
    }
    handle = api->open_request(connection, request->method, request->path,
                               "HTTP/1.0", 0, 0, V9X_NET_WININET_FLAGS, 0ul);
    if (handle == 0) {
        reply->error = GetLastError();
        goto done;
    }
    /* WinInet sends the body inside this call and does not say how much of
     * it went, so a failure here may have delivered part of the body. The
     * server refuses a body shorter than its Content-Length (reports.php),
     * so the Winsock retry that follows cannot leave a truncated report. */
    reply->stage = "send";
    if (!api->send_request(handle, headers, v9x_net_length(headers),
                           (LPVOID)request->body, request->body_length)) {
        reply->error = GetLastError();
        goto done;
    }
    *delivered = TRUE;
    result = V9X_NET_FAILED;

    reply->stage = "reply";
    size = sizeof(value);
    if (!api->query_info(handle, HTTP_QUERY_STATUS_CODE |
                         HTTP_QUERY_FLAG_NUMBER, &value, &size, 0)) {
        reply->error = GetLastError();
        goto done;
    }
    reply->status = value;
    size = sizeof(value);
    reply->content_length = V9X_UPDATE_NO_LENGTH;
    if (api->query_info(handle, HTTP_QUERY_CONTENT_LENGTH |
                        HTTP_QUERY_FLAG_NUMBER, &value, &size, 0)) {
        reply->content_length = value;
    }

    reply->stage = "receive";
    for (;;) {
        if (!api->read_file(handle, chunk, sizeof(chunk), &read)) {
            reply->error = GetLastError();
            goto done;
        }
        if (read == 0ul) {
            break;
        }
        if (!request->sink(request->sink_context, chunk, read)) {
            result = V9X_NET_REFUSED;
            goto done;
        }
        reply->received += read;
    }
    /* A body shorter than its Content-Length is a dropped connection, not
     * a reply. */
    if (reply->content_length != V9X_UPDATE_NO_LENGTH &&
        reply->received != reply->content_length) {
        goto done;
    }
    result = V9X_NET_OK;

done:
    if (handle != 0) {
        (void)api->close_handle(handle);
    }
    if (connection != 0) {
        (void)api->close_handle(connection);
    }
    (void)api->close_handle(session);
    return result;
}

/* Wait until the socket is readable (or, for a connect, writable). */
static BOOL v9x_net_wait(SOCKET socket_handle, BOOL for_write)
{
    struct v9x_winsock *api = &v9x_net_winsock;
    fd_set set;
    fd_set errors;
    struct timeval timeout;

    FD_ZERO(&set);
    FD_SET(socket_handle, &set);
    FD_ZERO(&errors);
    FD_SET(socket_handle, &errors);
    timeout.tv_sec = V9X_NET_TIMEOUT_S;
    timeout.tv_usec = 0l;
    /* One socket in each set: a return of 1 from the write/read set and 0
     * from the error set cannot be told apart without __WSAFDIsSet, so
     * the error set is checked by count below instead. */
    if (for_write) {
        if (api->select(0, 0, &set, &errors, &timeout) != 1) {
            return FALSE;
        }
        return errors.fd_count == 0u;
    }
    return api->select(0, &set, 0, 0, &timeout) == 1;
}

/*
 * Send all of data on the non-blocking socket, adding what went to *sent.
 *
 * A writable socket only promises room for some bytes, not for the whole
 * piece: Win9x's stack has an 8 KB send buffer, and a non-blocking send
 * that finds it full answers WSAEWOULDBLOCK. That is the stack asking to be
 * called again, not a failure, and treating it as one ended a report after
 * its first 16 KB piece. Pieces are V9X_NET_CHUNK so each fits the buffer.
 */
static BOOL v9x_net_send_all(SOCKET socket_handle, const char *data,
                             DWORD length, DWORD *sent_total,
                             DWORD *error)
{
    struct v9x_winsock *api = &v9x_net_winsock;
    DWORD progress = GetTickCount();
    int sent;

    while (length != 0ul) {
        if (!v9x_net_wait(socket_handle, TRUE)) {
            *error = (DWORD)api->last_error();
            return FALSE;
        }
        sent = api->send(socket_handle, data,
                         length > V9X_NET_CHUNK ? (int)V9X_NET_CHUNK
                                                : (int)length, 0);
        /* Retried until the same timeout a dead route gets, so a stack
         * that reports writable and then refuses forever is a failure. */
        if (sent == SOCKET_ERROR &&
            api->last_error() == WSAEWOULDBLOCK) {
            if (GetTickCount() - progress > V9X_NET_TIMEOUT_MS) {
                *error = (DWORD)WSAEWOULDBLOCK;
                return FALSE;
            }
            Sleep(10ul);
            continue;
        }
        if (sent <= 0) {
            *error = sent == SOCKET_ERROR ? (DWORD)api->last_error() : 0ul;
            return FALSE;
        }
        data += sent;
        length -= (DWORD)sent;
        *sent_total += (DWORD)sent;
        progress = GetTickCount();
    }
    return TRUE;
}

static int v9x_net_send_winsock(const struct v9x_net_request *request,
                                struct v9x_net_reply *reply)
{
    struct v9x_winsock *api = &v9x_net_winsock;
    struct hostent *host;
    struct sockaddr_in address;
    SOCKET socket_handle;
    u_long non_blocking = 1ul;
    char text[V9X_NET_REQUEST_MAX];
    char headers[256];
    char number[12];
    char head[V9X_NET_HEAD_MAX];
    DWORD used = 0ul;
    DWORD head_used = 0ul;
    DWORD body_offset = 0ul;
    BOOL head_done = FALSE;
    int received;
    int result = V9X_NET_NO_NETWORK;

    reply->transport = "winsock";
    reply->stage = "connect";
    if (!v9x_net_load_winsock()) {
        return V9X_NET_NO_NETWORK;
    }
    host = api->gethostbyname(request->host);
    if (host == 0 || host->h_addrtype != AF_INET || host->h_length != 4) {
        reply->error = (DWORD)api->last_error();
        return V9X_NET_NO_NETWORK;
    }
    if (!v9x_net_header_lines(request, headers, sizeof(headers))) {
        return V9X_NET_FAILED;
    }
    wsprintfA(number, "%lu", request->body_length);
    if (!v9x_net_append(text, sizeof(text), &used, request->method) ||
        !v9x_net_append(text, sizeof(text), &used, " ") ||
        !v9x_net_append(text, sizeof(text), &used, request->path) ||
        !v9x_net_append(text, sizeof(text), &used, " HTTP/1.0\r\nHost: ") ||
        !v9x_net_append(text, sizeof(text), &used, request->host) ||
        !v9x_net_append(text, sizeof(text), &used,
                        "\r\nUser-Agent: " V9X_NET_AGENT "\r\n") ||
        !v9x_net_append(text, sizeof(text), &used, headers)) {
        return V9X_NET_FAILED;
    }
    if (request->body_length != 0ul &&
        (!v9x_net_append(text, sizeof(text), &used, "Content-Length: ") ||
         !v9x_net_append(text, sizeof(text), &used, number) ||
         !v9x_net_append(text, sizeof(text), &used, "\r\n"))) {
        return V9X_NET_FAILED;
    }
    if (!v9x_net_append(text, sizeof(text), &used, "\r\n")) {
        return V9X_NET_FAILED;
    }

    socket_handle = api->socket(AF_INET, SOCK_STREAM, 0);
    if (socket_handle == INVALID_SOCKET) {
        return V9X_NET_NO_NETWORK;
    }
    /* Non-blocking, so connect gives up after V9X_NET_TIMEOUT_S rather
     * than the stack's own minute and more. */
    (void)api->ioctlsocket(socket_handle, FIONBIO, &non_blocking);
    address.sin_family = AF_INET;
    address.sin_port = (u_short)(((request->port & 0xFFu) << 8) |
                                 ((request->port >> 8) & 0xFFu));
    {
        BYTE *raw = (BYTE *)&address.sin_addr;
        BYTE *source = (BYTE *)host->h_addr_list[0];

        raw[0] = source[0];
        raw[1] = source[1];
        raw[2] = source[2];
        raw[3] = source[3];
    }
    if (api->connect(socket_handle, (struct sockaddr *)&address,
                     sizeof(address)) != 0 &&
        api->last_error() != WSAEWOULDBLOCK) {
        reply->error = (DWORD)api->last_error();
        goto done;
    }
    if (!v9x_net_wait(socket_handle, TRUE)) {
        reply->error = (DWORD)api->last_error();
        goto done;
    }
    /* Connected. From here a failure is part-way, and is final. */
    result = V9X_NET_FAILED;
    reply->stage = "send";
    {
        DWORD head_sent = 0ul;

        if (!v9x_net_send_all(socket_handle, text, used, &head_sent,
                              &reply->error) ||
            !v9x_net_send_all(socket_handle, (const char *)request->body,
                              request->body_length, &reply->body_sent,
                              &reply->error)) {
            goto done;
        }
    }

    reply->stage = "receive";
    for (;;) {
        char chunk[V9X_NET_CHUNK];

        if (!v9x_net_wait(socket_handle, FALSE)) {
            reply->error = (DWORD)api->last_error();
            goto done;
        }
        received = api->recv(socket_handle, chunk, sizeof(chunk), 0);
        if (received < 0) {
            reply->error = (DWORD)api->last_error();
            goto done;
        }
        if (received == 0) {
            break;
        }
        if (!head_done) {
            DWORD index;
            DWORD status;
            DWORD length;

            if (head_used + (DWORD)received > sizeof(head)) {
                goto done;
            }
            for (index = 0ul; index < (DWORD)received; ++index) {
                head[head_used + index] = chunk[index];
            }
            head_used += (DWORD)received;
            if (!v9x_update_http_head(head, head_used, &status,
                                      &body_offset, &length)) {
                continue;
            }
            head_done = TRUE;
            reply->status = status;
            reply->content_length = length;
            if (head_used > body_offset) {
                if (!request->sink(request->sink_context,
                                   (const BYTE *)head + body_offset,
                                   head_used - body_offset)) {
                    result = V9X_NET_REFUSED;
                    goto done;
                }
                reply->received += head_used - body_offset;
            }
            continue;
        }
        if (!request->sink(request->sink_context, (const BYTE *)chunk,
                           (DWORD)received)) {
            result = V9X_NET_REFUSED;
            goto done;
        }
        reply->received += (DWORD)received;
    }
    if (!head_done) {
        goto done;
    }
    if (reply->content_length != V9X_UPDATE_NO_LENGTH &&
        reply->received != reply->content_length) {
        goto done;
    }
    result = V9X_NET_OK;

done:
    (void)api->closesocket(socket_handle);
    return result;
}

int v9x_net_send(const struct v9x_net_request *request,
                 struct v9x_net_reply *reply)
{
    BOOL delivered = FALSE;
    int result;

    reply->status = 0ul;
    reply->content_length = V9X_UPDATE_NO_LENGTH;
    reply->received = 0ul;
    reply->transport = 0;
    reply->stage = 0;
    reply->error = 0ul;
    reply->body_sent = 0ul;
    if (v9x_net_load_wininet()) {
        result = v9x_net_send_wininet(request, reply, &delivered);
        if (result == V9X_NET_OK || delivered) {
            return result;
        }
        reply->status = 0ul;
        reply->content_length = V9X_UPDATE_NO_LENGTH;
        reply->received = 0ul;
        reply->error = 0ul;
    }
    return v9x_net_send_winsock(request, reply);
}
