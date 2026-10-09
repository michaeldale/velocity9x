/*
 * V9XUPD.EXE's HTTP client: WinInet when it is installed, raw Winsock
 * when it is not or when it cannot reach the server. Both DLLs are loaded at
 * run time, so the executable's static imports stay KERNEL32/USER32/GDI32/
 * ADVAPI32 and it starts on a machine with neither configured.
 */
#ifndef V9X_UPDATE_NET_WIN32_H
#define V9X_UPDATE_NET_WIN32_H

#define V9X_NET_OK         0
/* Nothing reached the server: no transport, no name, no connection. */
#define V9X_NET_NO_NETWORK 1
/* The server was reached and the exchange then failed part-way. */
#define V9X_NET_FAILED     2
/* The sink refused more data. */
#define V9X_NET_REFUSED    3

/* Receives the response body in pieces; FALSE stops the transfer. */
typedef BOOL (*v9x_net_sink)(void *context, const BYTE *data, DWORD length);

struct v9x_net_request {
    const char *method;        /* "GET" or "POST" */
    const char *host;
    WORD port;
    const char *path;          /* path and query, already encoded */
    const char *extra_header;  /* one "Name: value" line without CRLF, or 0 */
    const BYTE *body;
    DWORD body_length;
    v9x_net_sink sink;
    void *sink_context;
};

struct v9x_net_reply {
    DWORD status;              /* HTTP status code */
    DWORD content_length;      /* V9X_UPDATE_NO_LENGTH when not sent */
    DWORD received;            /* body bytes handed to the sink */
};

/* Whether a network looks usable: WinInet says connected, or the host's
 * name resolves. Sends nothing to the host itself. */
BOOL v9x_net_present(const char *host);

/* One request, one response. The User-Agent is V9XUPD's own. */
int v9x_net_send(const struct v9x_net_request *request,
                 struct v9x_net_reply *reply);

#endif
