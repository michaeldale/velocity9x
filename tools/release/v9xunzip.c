/*
 * v9xunzip - extract every member of a zip with the updater's own reader.
 *
 *   v9xunzip <zipfile> <outdir>
 *
 * A host tool: it exists so the code V9XUPD.EXE will run on Windows 98
 * (src\common\zipread.c, inflate.c, crc32.c) can be run over every
 * published release zip and its output compared with another unzipper's.
 * Members are written flat into outdir, which must already exist.
 *
 * Names are checked with the long limit, not the 8.3 one the updater's
 * by-name lookup applies: releases carry VELOCITY9X.INF and FIRSTBOOT.TXT,
 * and this tool's job is to exercise the decoder on all of them.
 *
 * Exit status 0 when every member extracted and verified, 1 otherwise.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "velocity9x/inflate.h"
#include "velocity9x/zipread.h"

static int v9xunzip_read_file(const char *path, v9x_u8 **data, v9x_u32 *length)
{
    FILE *file;
    long size;

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    size = ftell(file);
    if (size < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    *data = (v9x_u8 *)malloc((size_t)size + 1u);
    if (*data == NULL) {
        fclose(file);
        return 0;
    }
    if (fread(*data, 1u, (size_t)size, file) != (size_t)size) {
        free(*data);
        fclose(file);
        return 0;
    }
    fclose(file);
    *length = (v9x_u32)size;
    return 1;
}

static int v9xunzip_write_file(const char *path,
                               const v9x_u8 *data,
                               v9x_u32 length)
{
    FILE *file;
    int ok;

    file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }
    ok = 1;
    if (length != 0ul && fwrite(data, 1u, (size_t)length, file) != (size_t)length) {
        ok = 0;
    }
    if (fclose(file) != 0) {
        ok = 0;
    }
    return ok;
}

int main(int argc, char **argv)
{
    static struct v9x_inflate_state state;
    struct v9x_zip zip;
    struct v9x_zip_entry entry;
    v9x_u8 *archive;
    v9x_u8 *output;
    v9x_u32 archive_length;
    v9x_u32 output_length;
    char path[1024];
    char name[V9X_ZIP_NAME_MAX_LONG + 1];
    v9x_u16 index;
    v9x_u16 status;
    unsigned int failures;
    unsigned long total;

    if (argc != 3) {
        fprintf(stderr, "usage: v9xunzip <zipfile> <outdir>\n");
        return 2;
    }
    if (!v9xunzip_read_file(argv[1], &archive, &archive_length)) {
        fprintf(stderr, "v9xunzip: cannot read %s\n", argv[1]);
        return 1;
    }
    status = v9x_zip_open(&zip, archive, archive_length);
    if (status != V9X_ZIP_OK) {
        fprintf(stderr, "v9xunzip: %s: open failed, status %u\n", argv[1],
                (unsigned int)status);
        return 1;
    }

    failures = 0u;
    total = 0ul;
    for (index = 0u; index < zip.entry_count; ++index) {
        status = v9x_zip_entry_at(&zip, index, &entry);
        if (status != V9X_ZIP_OK) {
            fprintf(stderr, "v9xunzip: entry %u: status %u\n",
                    (unsigned int)index, (unsigned int)status);
            ++failures;
            continue;
        }
        if (v9x_zip_name_is_safe(entry.name, entry.name_length,
                                 V9X_ZIP_NAME_MAX_LONG) != V9X_TRUE) {
            fprintf(stderr, "v9xunzip: entry %u: unsafe name\n",
                    (unsigned int)index);
            ++failures;
            continue;
        }
        memcpy(name, entry.name, entry.name_length);
        name[entry.name_length] = '\0';
        if (strlen(argv[2]) + 1u + entry.name_length + 1u > sizeof(path)) {
            fprintf(stderr, "v9xunzip: %s: path too long\n", name);
            ++failures;
            continue;
        }
        sprintf(path, "%s\\%s", argv[2], name);

        output = (v9x_u8 *)malloc((size_t)entry.uncompressed_size + 1u);
        if (output == NULL) {
            fprintf(stderr, "v9xunzip: %s: out of memory\n", name);
            ++failures;
            continue;
        }
        status = v9x_zip_extract(&zip, index, &state, output,
                                 entry.uncompressed_size, &output_length);
        if (status != V9X_ZIP_OK) {
            fprintf(stderr, "v9xunzip: %s: extract failed, status %u\n", name,
                    (unsigned int)status);
            ++failures;
        } else if (!v9xunzip_write_file(path, output, output_length)) {
            fprintf(stderr, "v9xunzip: %s: cannot write\n", path);
            ++failures;
        } else {
            printf("%-14s %8lu %s\n", name, (unsigned long)output_length,
                   entry.method == 0u ? "stored" : "deflate");
            total += output_length;
        }
        free(output);
    }

    printf("%u entries, %lu bytes, %u failure(s)\n",
           (unsigned int)zip.entry_count, total, failures);
    free(archive);
    return failures == 0u ? 0 : 1;
}
