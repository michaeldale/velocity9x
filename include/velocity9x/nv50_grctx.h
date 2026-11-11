/*
 * NV50-family PGRAPH context program and context image, generated.
 *
 * The GT218 (and every NV50-family chip) has no PGRAPH firmware: the driver
 * builds a context program from the chipset, the unit mask in PBUS 1540h and
 * the memory type, uploads it (400324h = 0, then each word to 400328h), and
 * gives every channel a context image of the size the program implies, with
 * the image's non-zero defaults written in. This is Linux nouveau's
 * generator (nvkm/engine/gr/ctxnv50.c, MIT, Marcin Koscielnicki), ported
 * in src\chipsets\nvidia\gt218\nv50_grctx.c
 * (docs\plans\nvidia-gt218-pgraph-2d.md, step 1).
 *
 * Pure computation, no I/O, host-tested: the caller reads 1540h and writes
 * the outputs to the card.
 */
#ifndef VELOCITY9X_NV50_GRCTX_H
#define VELOCITY9X_NV50_GRCTX_H

#include "velocity9x/types.h"
#include "velocity9x/status.h"

/* The program store's size in words; nouveau refuses to emit past it
 * (ctxnv40.h, cp_out). */
#define V9X_NV50_GRCTX_PROGRAM_MAX 512ul

struct v9x_nv50_grctx_input {
    /* PMC_BOOT_0 bits 28:20: A8h for the GT218. */
    v9x_u32 chipset;
    /* PBUS 1540h as read: TPs 9:0, ROP partitions 23:16, MPs 27:24. */
    v9x_u32 units;
    /* Non-zero on GDDR5 memory: one default differs (ctxnv50.c, 407D08h). */
    v9x_u32 gddr5;
};

/*
 * Build the context program and, when image is not NULL, the context image.
 *
 * program gets at most V9X_NV50_GRCTX_PROGRAM_MAX words and *program_words
 * their count; *image_words is the image size in 32-bit words, padding
 * included, whatever image is. With image, image_max must hold that many
 * words; the image is zeroed and its defaults written. A program that would
 * not fit its store, or an image too small, returns
 * V9X_STATUS_INTEGER_OVERFLOW and nothing is to be uploaded.
 */
v9x_status v9x_nv50_grctx_build(const struct v9x_nv50_grctx_input *input,
                                v9x_u32 *program,
                                v9x_u32 *program_words,
                                v9x_u32 *image,
                                v9x_u32 image_max,
                                v9x_u32 *image_words);

#endif
