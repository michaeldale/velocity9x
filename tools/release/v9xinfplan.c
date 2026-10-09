/*
 * v9xinfplan - plan every model of a generated VELOCITY9X.INF with the
 * updater's own INF subset (src\common\update_inf.c).
 *
 *   v9xinfplan <inf>    exit 0 when every model plans, 1 when one does not
 *
 * build-active-package.ps1 runs it on each INF it writes, so a generator
 * change that steps outside what V9XUPD.EXE can apply fails the build
 * instead of an installed user's update. Host-only: stdio and malloc.
 *
 * Models are read the way SetupX finds them: [Manufacturer] names the
 * models section, each of whose lines is "description"=install-section,ids.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "velocity9x/update_inf.h"
#include "velocity9x/update_proto.h"

#define V9XINFPLAN_MAX_FILE (256ul * 1024ul)

static struct v9x_inf_plan v9xinfplan_plan;

/* The text after '=' up to the first ',' of each line in [section]. */
static int v9xinfplan_models(const char *text, size_t length,
                             const char *section)
{
    char header[80];
    const char *at;
    int failures = 0;
    int models = 0;

    sprintf(header, "[%s]", section);
    at = strstr(text, header);
    if (at == 0) {
        fprintf(stderr, "v9xinfplan: no [%s]\n", section);
        return 1;
    }
    at = strchr(at, '\n');
    while (at != 0 && *++at != '\0' && *at != '[') {
        const char *end = strchr(at, '\n');
        const char *equals = strchr(at, '=');
        char install[64];
        size_t used = 0u;

        if (end == 0) {
            end = text + length;
        }
        if (*at != ';' && equals != 0 && equals < end) {
            const char *name = equals + 1;

            while (name < end && *name != ',' && *name != '\r' &&
                   used + 1u < sizeof(install)) {
                install[used++] = *name++;
            }
            install[used] = '\0';
            ++models;
            if (!v9x_inf_plan(text, (v9x_u32)length, install,
                              &v9xinfplan_plan)) {
                fprintf(stderr, "v9xinfplan: %s: %s\n", install,
                        v9xinfplan_plan.error);
                ++failures;
            } else {
                printf("%s: %lu operations, %lu files\n", install,
                       (unsigned long)v9xinfplan_plan.count,
                       (unsigned long)v9xinfplan_plan.copies);
            }
        }
        at = (*end == '\0') ? 0 : end;
    }
    if (models == 0) {
        fprintf(stderr, "v9xinfplan: [%s] lists no models\n", section);
        return 1;
    }
    return failures;
}

int main(int argc, char **argv)
{
    FILE *file;
    char *text;
    size_t length;
    char models[64];
    int result;

    if (argc != 2) {
        fputs("usage: v9xinfplan <inf>\n", stderr);
        return 2;
    }
    file = fopen(argv[1], "rb");
    if (file == 0) {
        fprintf(stderr, "v9xinfplan: cannot open %s\n", argv[1]);
        return 2;
    }
    text = (char *)malloc(V9XINFPLAN_MAX_FILE + 1u);
    if (text == 0) {
        fclose(file);
        return 2;
    }
    length = fread(text, 1u, V9XINFPLAN_MAX_FILE, file);
    fclose(file);
    text[length] = '\0';

    /* [Manufacturer] has one line, Name=ModelsSection. */
    {
        const char *at = strstr(text, "[Manufacturer]");
        const char *equals;
        size_t used = 0u;

        if (at == 0 || (at = strchr(at, '\n')) == 0 ||
            (equals = strchr(at, '=')) == 0) {
            fprintf(stderr, "v9xinfplan: no [Manufacturer] entry\n");
            free(text);
            return 1;
        }
        ++equals;
        while (*equals != '\r' && *equals != '\n' && *equals != '\0' &&
               *equals != ',' && used + 1u < sizeof(models)) {
            models[used++] = *equals++;
        }
        models[used] = '\0';
    }
    result = v9xinfplan_models(text, length, models);
    free(text);
    return result == 0 ? 0 : 1;
}
