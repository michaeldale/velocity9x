/*
 * CPU blit fallbacks for V9XHAL.DLL.
 *
 * Advertising DDCAPS_BLT makes the driver responsible for completing every
 * blit it admits: DDHAL_DRIVER_NOTHANDLED is reported to the application as
 * DDERR_UNSUPPORTED rather than being emulated. Every engine can decline a
 * shape it cannot express, so these two functions are what make the callback
 * always succeed, whatever engine is fitted - and on a chip with no engine at
 * all they are the whole of the blit path.
 *
 * They write through the mapped linear aperture, which is what the HEL would
 * have done. The caller has already drained the engine, so nothing is in
 * flight over the same memory.
 */
#include "ddhal_internal.h"

/*
 * Copy one row, in the direction that keeps an overlapping copy correct.
 *
 * The width matters: a full-screen 640x480x16 frame is 614400 bytes, and a
 * byte-at-a-time loop over the mapped aperture cost ~700 ms per frame -
 * about 1 FPS under Ironfield's BltFast presentation path, far worse than
 * the HEL this callback displaced. Moving a dword per iteration where the
 * ends allow it is what keeps the CPU fallback competitive with the
 * emulation it replaced.
 */
static void v9x_copy_row(BYTE *destination, const BYTE *source, DWORD bytes)
{
    DWORD blocks;

    if (destination == source || bytes == 0ul) {
        return;
    }
    if (destination < source) {
        if ((((DWORD)destination | (DWORD)source) & 3ul) == 0ul) {
            DWORD *wide_destination = (DWORD *)destination;
            const DWORD *wide_source = (const DWORD *)source;

            for (blocks = bytes >> 2; blocks != 0ul; --blocks) {
                *wide_destination++ = *wide_source++;
            }
            destination = (BYTE *)wide_destination;
            source = (const BYTE *)wide_source;
            bytes &= 3ul;
        }
        while (bytes-- != 0ul) {
            *destination++ = *source++;
        }
    } else {
        destination += bytes;
        source += bytes;
        if ((((DWORD)destination | (DWORD)source) & 3ul) == 0ul) {
            DWORD *wide_destination = (DWORD *)destination;
            const DWORD *wide_source = (const DWORD *)source;

            for (blocks = bytes >> 2; blocks != 0ul; --blocks) {
                *--wide_destination = *--wide_source;
            }
            destination = (BYTE *)wide_destination;
            source = (const BYTE *)wide_source;
            bytes &= 3ul;
        }
        while (bytes-- != 0ul) {
            *--destination = *--source;
        }
    }
}

/*
 * Copy a rectangle between two surfaces through the aperture.
 *
 * Source and destination can be the same surface (window scrolling), so the
 * row order is picked to keep an overlapping copy correct, and v9x_copy_row
 * does the same for the bytes within a row.
 */
void v9x_cpu_copy(V9X_DDHAL_BLTDATA *data, DWORD source_offset,
                  DWORD destination_offset, DWORD bytes_per_pixel)
{
    DWORD source_pitch = (DWORD)data->lpDDSrcSurface->lpGbl->lPitch;
    DWORD destination_pitch = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    DWORD row_bytes = (DWORD)(data->rSrc[2] - data->rSrc[0]) * bytes_per_pixel;
    DWORD height = (DWORD)(data->rSrc[3] - data->rSrc[1]);
    BYTE *base = (BYTE *)v9x_hal->fb.linear_base;
    DWORD row;

    source_offset += (DWORD)data->rSrc[1] * source_pitch +
                     (DWORD)data->rSrc[0] * bytes_per_pixel;
    destination_offset += (DWORD)data->rDest[1] * destination_pitch +
                          (DWORD)data->rDest[0] * bytes_per_pixel;

    if (destination_offset > source_offset) {
        for (row = height; row-- != 0ul;) {
            v9x_copy_row(base + destination_offset + row * destination_pitch,
                         base + source_offset + row * source_pitch,
                         row_bytes);
        }
    } else {
        for (row = 0ul; row < height; ++row) {
            v9x_copy_row(base + destination_offset + row * destination_pitch,
                         base + source_offset + row * source_pitch,
                         row_bytes);
        }
    }
}

void v9x_cpu_fill(V9X_DDHAL_BLTDATA *data, DWORD offset,
                         DWORD bytes_per_pixel)
{
    DWORD pitch = (DWORD)data->lpDDDestSurface->lpGbl->lPitch;
    DWORD width = (DWORD)(data->rDest[2] - data->rDest[0]);
    DWORD height = (DWORD)(data->rDest[3] - data->rDest[1]);
    /*
     * The full dword. At 24 and 32 bpp the fill colour genuinely needs all of
     * it; truncating to a WORD here, as this used to, silently discarded the
     * red channel of every high-colour fill.
     */
    DWORD colour = data->bltFX.dwFillColor;
    BYTE *row = (BYTE *)v9x_hal->fb.linear_base + offset +
                (DWORD)data->rDest[1] * pitch +
                (DWORD)data->rDest[0] * bytes_per_pixel;

    DWORD pair;
    DWORD blocks;

    /*
     * The 1- and 2-byte paths write a dword of several pixels at once, so they
     * need the colour replicated across it first. At 3 and 4 bytes per pixel a
     * dword is not a whole number of pixels either way, so those paths write a
     * pixel at a time and use the colour as it stands.
     */
    if (bytes_per_pixel == 1ul) {
        colour &= 0xfful;
        pair = colour | (colour << 8) | (colour << 16) | (colour << 24);
    } else if (bytes_per_pixel == 2ul) {
        colour &= 0xfffful;
        pair = colour | (colour << 16);
    } else {
        pair = colour;
    }

    /* Same reasoning as v9x_copy_row: a full-screen fill is hundreds of
     * thousands of pixels, so write a dword of two pixels once aligned. */
    while (height-- != 0ul) {
        DWORD count = width;

        if (bytes_per_pixel == 1ul) {
            BYTE *pixel = row;

            while (count != 0ul && (((DWORD)pixel) & 3ul) != 0ul) {
                *pixel++ = (BYTE)colour;
                --count;
            }
            for (blocks = count >> 2; blocks != 0ul; --blocks) {
                *(DWORD *)pixel = pair;
                pixel += 4;
            }
            for (count &= 3ul; count != 0ul; --count) {
                *pixel++ = (BYTE)colour;
            }
        } else if (bytes_per_pixel == 2ul) {
            WORD *pixel = (WORD *)row;

            if (count != 0ul && (((DWORD)pixel) & 3ul) != 0ul) {
                *pixel++ = (WORD)colour;
                --count;
            }
            for (blocks = count >> 1; blocks != 0ul; --blocks) {
                *(DWORD *)pixel = pair;
                pixel += 2;
            }
            if ((count & 1ul) != 0ul) {
                *pixel = (WORD)colour;
            }
        } else if (bytes_per_pixel == 4ul) {
            DWORD *pixel = (DWORD *)row;

            while (count-- != 0ul) {
                *pixel++ = pair;
            }
        } else {
            /*
             * 24 bpp. An odd pixel stride means no run of pixels ever lines up
             * with a dword boundary, so there is no wide store to reach for -
             * three bytes per pixel is the whole of it.
             */
            BYTE *pixel = row;

            while (count-- != 0ul) {
                pixel[0] = (BYTE)(pair & 0xfful);
                pixel[1] = (BYTE)((pair >> 8) & 0xfful);
                pixel[2] = (BYTE)((pair >> 16) & 0xfful);
                pixel += 3;
            }
        }
        row += pitch;
    }
}

