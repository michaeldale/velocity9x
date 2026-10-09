/*
 * Tests for the update-server protocol's pure half (update_proto.c).
 *
 * The reply fixtures are the shapes the v9x_update_checker plugin documents
 * in its README.md and docs\REPORT-SUBMISSION.md, CRLF as it sends them.
 */
#include <stdio.h>
#include <string.h>

#include "velocity9x/update_proto.h"

static unsigned int update_failures = 0u;

#define UPCHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++update_failures; \
    } \
} while (0)

static const char reply_report[] =
    "[report]\r\n"
    "status=ok\r\n"
    "report=V9X-4F7K2Q\r\n"
    "key=66251c8b3da83e3482b00d68a7835f99\r\n"
    "files=1\r\n"
    "issues=https://github.com/michaeldale/velocity9x/issues\r\n"
    "message=Report received. Quote V9X-4F7K2Q when you open an issue.\r\n";

static const char reply_check[] =
    "[update]\r\n"
    "status=update\r\n"
    "installed=0.13.0\r\n"
    "latest=0.14.0\r\n"
    "report=http://michaeldale.com.au/v9update/report\r\n"
    "\r\n"
    "[package]\r\n"
    "family=ati\r\n"
    "url=http://michaeldale.com.au/v9update/get/1/velocity9x-0.14.0-ati.zip\r\n"
    "size=347696\r\n";

static void test_url_encode(void)
{
    char out[32];

    UPCHECK(v9x_update_url_encode("V9XSNAP.INI", out, sizeof(out)));
    UPCHECK(strcmp(out, "V9XSNAP.INI") == 0);
    UPCHECK(v9x_update_url_encode("a b&c=d/e~f", out, sizeof(out)));
    UPCHECK(strcmp(out, "a%20b%26c%3Dd%2Fe~f") == 0);
    /* High bytes are encoded as their byte value, not dropped. */
    UPCHECK(v9x_update_url_encode("\xE9", out, sizeof(out)));
    UPCHECK(strcmp(out, "%E9") == 0);
    UPCHECK(v9x_update_url_encode("", out, sizeof(out)));
    UPCHECK(out[0] == '\0');
    /* Exactly fits: three characters and the terminator. */
    UPCHECK(v9x_update_url_encode("abc", out, 4u));
    UPCHECK(!v9x_update_url_encode("abcd", out, 4u));
    UPCHECK(out[0] == '\0');
    /* An escape that would not fit whole is refused, not split. */
    UPCHECK(!v9x_update_url_encode("a ", out, 4u));
}

static void test_ini_value(void)
{
    char out[64];
    v9x_u32 length = (v9x_u32)strlen(reply_report);

    UPCHECK(v9x_update_ini_value(reply_report, length, "report", "status",
                                 out, sizeof(out)));
    UPCHECK(strcmp(out, "ok") == 0);
    UPCHECK(v9x_update_ini_value(reply_report, length, "REPORT", "Report",
                                 out, sizeof(out)));
    UPCHECK(strcmp(out, "V9X-4F7K2Q") == 0);
    UPCHECK(v9x_update_ini_value(reply_report, length, "report", "message",
                                 out, sizeof(out)));
    UPCHECK(strcmp(out, "Report received. Quote V9X-4F7K2Q when you open an issue.") == 0);
    UPCHECK(!v9x_update_ini_value(reply_report, length, "report", "absent",
                                  out, sizeof(out)));
    UPCHECK(!v9x_update_ini_value(reply_report, length, "update", "status",
                                  out, sizeof(out)));
    /* Too long for the buffer: refused, not truncated. */
    UPCHECK(!v9x_update_ini_value(reply_report, length, "report", "key",
                                  out, 32u));
    UPCHECK(v9x_update_ini_value(reply_report, length, "report", "key",
                                 out, 33u));

    /* The same key in two sections: each section's own. */
    length = (v9x_u32)strlen(reply_check);
    UPCHECK(v9x_update_ini_value(reply_check, length, "update", "status",
                                 out, sizeof(out)));
    UPCHECK(strcmp(out, "update") == 0);
    UPCHECK(v9x_update_ini_value(reply_check, length, "package", "size",
                                 out, sizeof(out)));
    UPCHECK(strcmp(out, "347696") == 0);
    UPCHECK(!v9x_update_ini_value(reply_check, length, "package", "status",
                                  out, sizeof(out)));

    /* Bare LF, spaces around key and value, first match wins, and the
     * length bounds the scan even with more text behind it. */
    {
        static const char loose[] =
            "[a]\n  k  =  v1  \nk=v2\n[b]\nk=v3\n";

        UPCHECK(v9x_update_ini_value(loose, (v9x_u32)strlen(loose), "a", "k",
                                     out, sizeof(out)));
        UPCHECK(strcmp(out, "v1") == 0);
        UPCHECK(!v9x_update_ini_value(loose, 4u, "a", "k", out, sizeof(out)));
        UPCHECK(v9x_update_ini_value(loose, (v9x_u32)strlen(loose), "b", "k",
                                     out, sizeof(out)));
        UPCHECK(strcmp(out, "v3") == 0);
    }
}

static void test_url_split(void)
{
    struct v9x_update_url url;

    UPCHECK(v9x_update_url_split(
        "http://michaeldale.com.au/v9update/get/1/velocity9x-0.14.0-ati.zip",
        &url));
    UPCHECK(strcmp(url.host, "michaeldale.com.au") == 0);
    UPCHECK(url.port == 80u);
    UPCHECK(strcmp(url.path, "/v9update/get/1/velocity9x-0.14.0-ati.zip") == 0);

    UPCHECK(v9x_update_url_split("HTTP://localhost:8080", &url));
    UPCHECK(strcmp(url.host, "localhost") == 0);
    UPCHECK(url.port == 8080u);
    UPCHECK(strcmp(url.path, "/") == 0);

    UPCHECK(v9x_update_url_split(
        "http://h/index.php?bt_type=v9xu_report&app=velocity9x", &url));
    UPCHECK(strcmp(url.path, "/index.php?bt_type=v9xu_report&app=velocity9x") == 0);

    UPCHECK(!v9x_update_url_split("https://michaeldale.com.au/", &url));
    UPCHECK(!v9x_update_url_split("http:///path", &url));
    UPCHECK(!v9x_update_url_split("http://host:0/", &url));
    UPCHECK(!v9x_update_url_split("http://host:65536/", &url));
    UPCHECK(!v9x_update_url_split("http://host:/", &url));
    UPCHECK(!v9x_update_url_split("http://ho_st/", &url));
    UPCHECK(!v9x_update_url_split("http://user@host/", &url));
    UPCHECK(!v9x_update_url_split("http://host/a b", &url));
    UPCHECK(!v9x_update_url_split("http://host/a\r\nX: y", &url));
    UPCHECK(!v9x_update_url_split("http://host?q=1", &url));
}

static void test_http_head(void)
{
    static const char ok[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=us-ascii\r\n"
        "content-length:  42\r\n"
        "\r\n"
        "[report]\r\n";
    static const char no_length[] =
        "HTTP/1.0 413 Request Entity Too Large\n\nbody";
    v9x_u32 status = 0u;
    v9x_u32 body = 0u;
    v9x_u32 length = 0u;

    UPCHECK(v9x_update_http_head(ok, (v9x_u32)strlen(ok), &status, &body,
                                 &length));
    UPCHECK(status == 200u);
    UPCHECK(length == 42u);
    UPCHECK(strcmp(ok + body, "[report]\r\n") == 0);

    UPCHECK(v9x_update_http_head(no_length, (v9x_u32)strlen(no_length),
                                 &status, &body, &length));
    UPCHECK(status == 413u);
    UPCHECK(length == V9X_UPDATE_NO_LENGTH);
    UPCHECK(strcmp(no_length + body, "body") == 0);

    /* Every prefix short of the blank line is incomplete, never a parse. */
    {
        v9x_u32 cut;
        v9x_u32 full = (v9x_u32)(strstr(ok, "\r\n\r\n") - ok) + 4u;

        for (cut = 0u; cut < full; ++cut) {
            UPCHECK(!v9x_update_http_head(ok, cut, &status, &body, &length));
        }
    }
    UPCHECK(!v9x_update_http_head("ICY 200 OK\r\n\r\n", 14u, &status, &body,
                                  &length));
    UPCHECK(!v9x_update_http_head("HTTP/1.1 2x0 OK\r\n\r\n", 19u, &status,
                                  &body, &length));
    {
        static const char oversized[] =
            "HTTP/1.1 200 OK\r\nContent-Length: 99999999999\r\n\r\n";
        static const char empty[] =
            "HTTP/1.1 200 OK\r\nContent-Length:\r\n\r\n";

        UPCHECK(!v9x_update_http_head(oversized, (v9x_u32)strlen(oversized), &status,
                                      &body, &length));
        UPCHECK(!v9x_update_http_head(empty, (v9x_u32)strlen(empty), &status,
                                      &body, &length));
    }
}

static void test_report_code(void)
{
    UPCHECK(v9x_update_report_code_valid("V9X-4F7K2Q"));
    UPCHECK(v9x_update_report_code_valid("V9X-000000"));
    UPCHECK(!v9x_update_report_code_valid("V9X-4F7K2"));
    UPCHECK(!v9x_update_report_code_valid("V9X-4F7K2QQ"));
    UPCHECK(!v9x_update_report_code_valid("V9X-4F7K2I"));
    UPCHECK(!v9x_update_report_code_valid("V9X-4f7k2q"));
    UPCHECK(!v9x_update_report_code_valid("V9Y-4F7K2Q"));
    UPCHECK(!v9x_update_report_code_valid(""));
}

static void test_version(void)
{
    v9x_u32 major = 9u;
    v9x_u32 minor = 9u;
    v9x_u32 patch = 9u;

    UPCHECK(v9x_update_version_parse("0.15.0", &major, &minor, &patch));
    UPCHECK(major == 0u && minor == 15u && patch == 0u);
    UPCHECK(v9x_update_version_parse("v1.2", &major, &minor, &patch));
    UPCHECK(major == 1u && minor == 2u && patch == 0u);
    UPCHECK(!v9x_update_version_parse("1", &major, &minor, &patch));
    UPCHECK(!v9x_update_version_parse("1.2.3.4", &major, &minor, &patch));
    UPCHECK(!v9x_update_version_parse("1..2", &major, &minor, &patch));
    UPCHECK(!v9x_update_version_parse("1.2.", &major, &minor, &patch));
    UPCHECK(!v9x_update_version_parse("1.2.3-beta", &major, &minor, &patch));
    UPCHECK(!v9x_update_version_parse("65536.0.0", &major, &minor, &patch));
    UPCHECK(!v9x_update_version_parse("", &major, &minor, &patch));

    /* Numeric, not string, order: 0.9.2 is older than 0.10.0. */
    UPCHECK(v9x_update_version_compare("0.9.2", "0.10.0") == -1);
    UPCHECK(v9x_update_version_compare("0.10.0", "0.9.2") == 1);
    UPCHECK(v9x_update_version_compare("0.15.0", "v0.15") == 0);
    UPCHECK(v9x_update_version_compare("0.15.1", "0.15.0") == 1);
    UPCHECK(v9x_update_version_compare("0.15.0", "garbage") == -2);
}

unsigned int v9x_run_update_proto_tests(void)
{
    test_url_encode();
    test_ini_value();
    test_url_split();
    test_http_head();
    test_report_code();
    test_version();
    return update_failures;
}
