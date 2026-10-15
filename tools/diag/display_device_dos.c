/*
 * Velocity9x display-device switch (real-mode DOS).
 *
 * Asks an Intel video BIOS which display devices are active, and with SET
 * asks it to change them, through Int 10h AX=5F64h. Built for GitHub issue 6:
 * an embedded 945GSE board scans out on pipe A alone and hard-locks on the
 * Intel flip, and the netbook - the same silicon - drives only its panel on
 * pipe B and offers no hotkey or setup option to change that. Run from
 * AUTOEXEC.BAT before Windows, so the VBE mode set the driver makes later
 * lands on whatever device this left active.
 *
 * The function is documented in the C&T/Intel 69030 VGA BIOS OEM Reference
 * Guide, revision 1.0, section 6.8: BH=01h gets the active combination in
 * CX, BH=00h sets CX with BL bit 0 forcing execution, and AL=5Fh says the
 * function exists. Its CX bitmap is the 69030's and has no flat-panel bit
 * on pipe B, where the netbook's panel is. The netbook's 945GSE BIOS
 * (2026-10-15, boot 185) answered GET with AX=005Fh and CX=0800h while
 * scanning out on its panel alone, on pipe B: a sensible device word under
 * the code the 69030 guide calls failure. So neither the bitmap nor the AH
 * convention is taken from the guide. The tool reports AH raw, the value to
 * set is never built in, and SET takes the word to write from the command
 * line, chosen from what GET returned.
 *
 * Every call and its result are appended to V9XDDEV.TXT in the diagnostic
 * directory, so a run at boot can be read back over the agent afterwards.
 */
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "velocity9x/diagpaths.h"

#ifndef V9X_BUILD_ID
#define V9X_BUILD_ID "local"
#endif

#define V9X_DDEV_LOG V9X_DIAG_PATH("V9XDDEV.TXT")

#define V9X_DDEV_FUNCTION    0x5f64u
#define V9X_DDEV_SUB_SET     0x00u
#define V9X_DDEV_SUB_GET     0x01u
#define V9X_DDEV_FORCE       0x01u
#define V9X_DDEV_SUPPORTED   0x5fu

static FILE *v9x_log;

static void v9x_say(const char *text)
{
    fputs(text, stdout);
    if (v9x_log != NULL) {
        fputs(text, v9x_log);
    }
}

/* Only AL is read as a verdict; AH's meaning differs between BIOS
 * generations, so it is printed and left to the reader. */
static const char *v9x_status_name(unsigned short ax)
{
    if ((ax & 0x00ffu) != V9X_DDEV_SUPPORTED) {
        return "unsupported";
    }
    return "supported";
}

/* One 5F64h call. Returns AX; CX comes back through *cx. */
static unsigned short v9x_ddev_call(unsigned char sub, unsigned char options,
                                    unsigned short cx_in,
                                    unsigned short *cx_out)
{
    union REGS input;
    union REGS output;

    memset(&input, 0, sizeof(input));
    memset(&output, 0, sizeof(output));
    input.x.ax = V9X_DDEV_FUNCTION;
    input.h.bh = sub;
    input.h.bl = options;
    input.x.cx = cx_in;
    int86(0x10, &input, &output);
    *cx_out = output.x.cx;
    return output.x.ax;
}

static void v9x_ddev_get(const char *label)
{
    char line[96];
    unsigned short cx = 0u;
    unsigned short ax;

    ax = v9x_ddev_call(V9X_DDEV_SUB_GET, 0u, 0u, &cx);
    sprintf(line, "%s=ax=%04X status=%s cx=%04X\n", label, ax,
            v9x_status_name(ax), cx);
    v9x_say(line);
}

/* Strict four-digit hex, so a mistyped word is refused, not half-read. */
static int v9x_parse_word(const char *text, unsigned short *value)
{
    unsigned short result = 0u;
    int index;

    if (strlen(text) != 4u) {
        return 0;
    }
    for (index = 0; index < 4; ++index) {
        char c = text[index];
        unsigned short digit;

        if (c >= '0' && c <= '9') {
            digit = (unsigned short)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            digit = (unsigned short)(c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
            digit = (unsigned short)(c - 'a' + 10);
        } else {
            return 0;
        }
        result = (unsigned short)((result << 4) | digit);
    }
    *value = result;
    return 1;
}

static void v9x_usage(void)
{
    v9x_say("Usage: V9XDDEV          report the active display devices\n");
    v9x_say("       V9XDDEV SET xxxx set them to the hex word xxxx (forced)\n");
    v9x_say("Read GET's cx first; the bit layout depends on the BIOS.\n");
}

int main(int argc, char **argv)
{
    char line[96];
    struct dosdate_t date;
    struct dostime_t time;
    unsigned short wanted = 0u;
    unsigned short cx = 0u;
    unsigned short ax;
    int set = 0;

    if (argc == 3 && stricmp(argv[1], "SET") == 0) {
        if (!v9x_parse_word(argv[2], &wanted)) {
            v9x_usage();
            return 2;
        }
        set = 1;
    } else if (argc != 1) {
        v9x_usage();
        return 2;
    }

    /* Append, so successive boots stay side by side. A missing directory
     * costs the log only; the call still runs and prints. */
    v9x_log = fopen(V9X_DDEV_LOG, "a");
    _dos_getdate(&date);
    _dos_gettime(&time);
    sprintf(line, "[V9XDDEV %04u-%02u-%02u %02u:%02u:%02u]\n",
            date.year, date.month, date.day,
            time.hour, time.minute, time.second);
    v9x_say(line);
    v9x_say("Tool=Velocity9x display-device switch\n");
    v9x_say("Build=" V9X_BUILD_ID "\n");
    v9x_say(set ? "Mode=set\n" : "Mode=get\n");

    v9x_ddev_get("Before");
    if (set) {
        ax = v9x_ddev_call(V9X_DDEV_SUB_SET, V9X_DDEV_FORCE, wanted, &cx);
        sprintf(line, "Set=want=%04X ax=%04X status=%s\n", wanted, ax,
                v9x_status_name(ax));
        v9x_say(line);
        v9x_ddev_get("After");
    }
    v9x_say("\n");

    if (v9x_log != NULL) {
        fclose(v9x_log);
    }
    return 0;
}
