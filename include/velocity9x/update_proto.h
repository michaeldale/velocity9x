/*
 * Velocity9x update-server protocol: the pure half.
 *
 * V9XUPD.EXE talks to the v9x_update_checker plugin at michaeldale.com.au
 * over plain HTTP (docs\plans\optional-update-checker-and-auto-updater.md).
 * Everything here is text handling with no Win32 call, so the host tests can
 * hold it to a table: URL encoding for the query string, the INI-shaped
 * replies, splitting the URLs a reply hands back, the status line and
 * Content-Length of a raw Winsock response, report codes and version order.
 *
 * The replies are read with this parser rather than written to a temp file
 * for GetPrivateProfileString: the signed release file must be parsed only
 * up to its signature, and one parser for both keeps the rules identical.
 */
#ifndef VELOCITY9X_UPDATE_PROTO_H
#define VELOCITY9X_UPDATE_PROTO_H

#include "velocity9x/types.h"

/* The server, its two client paths and the app slug it knows us by. The
 * report path is the brief's fallback; a check reply's report= overrides
 * it. */
#define V9X_UPDATE_HOST        "michaeldale.com.au"
#define V9X_UPDATE_PORT        80u
#define V9X_UPDATE_CHECK_PATH  "/v9update/check"
#define V9X_UPDATE_REPORT_PATH "/v9update/report"
#define V9X_UPDATE_APP         "velocity9x"
#define V9X_UPDATE_ISSUES_URL  "https://github.com/michaeldale/velocity9x/issues"

/* Longest host and path a reply may hand back. Longer is refused, not
 * truncated: a truncated path fetches something else. */
#define V9X_UPDATE_HOST_MAX 64u
#define V9X_UPDATE_PATH_MAX 256u

struct v9x_update_url {
    char host[V9X_UPDATE_HOST_MAX];
    v9x_u16 port;
    char path[V9X_UPDATE_PATH_MAX];
};

/* Percent-encode text for a query-string value (RFC 3986 unreserved
 * characters pass, everything else becomes %XX). V9X_FALSE, with output
 * empty, if it does not fit. */
v9x_u16 v9x_update_url_encode(const char *text, char *output,
                              v9x_u32 capacity);

/* The value of key in [section] of an INI-shaped text of the given length.
 * Section and key compare without case; the first match wins; CR, and
 * spaces around the key and value, are trimmed. V9X_FALSE when absent or
 * when the value does not fit capacity (refused rather than truncated). */
v9x_u16 v9x_update_ini_value(const char *text, v9x_u32 length,
                             const char *section, const char *key,
                             char *output, v9x_u32 capacity);

/* Split an absolute http:// URL. Only http, a host of letters, digits, dot
 * and hyphen, an optional port, and a path of printable non-space ASCII. */
v9x_u16 v9x_update_url_split(const char *url, struct v9x_update_url *url_out);

/* No Content-Length header in a response. */
#define V9X_UPDATE_NO_LENGTH 0xFFFFFFFFul

/* Parse the head of a raw HTTP/1.x response: the status code, where the
 * body starts, and Content-Length if present. V9X_FALSE until the blank
 * line ending the head is in the buffer, or if the status line is not
 * HTTP. */
v9x_u16 v9x_update_http_head(const char *raw, v9x_u32 length,
                             v9x_u32 *status, v9x_u32 *body_offset,
                             v9x_u32 *content_length);

/* A report code as the server issues them: V9X- and six characters of the
 * Crockford set (no I, L, O or U). */
v9x_u16 v9x_update_report_code_valid(const char *code);

/* A version: an optional leading v, then two or three dot-separated
 * decimal numbers, each below 65536. The patch is 0 when absent. */
v9x_u16 v9x_update_version_parse(const char *text, v9x_u32 *major,
                                 v9x_u32 *minor, v9x_u32 *patch);

/* -1, 0 or 1 as left is older than, equal to or newer than right,
 * numerically. Unparsable versions compare as -2, which every caller must
 * treat as "refuse". */
v9x_s32 v9x_update_version_compare(const char *left, const char *right);

#endif
