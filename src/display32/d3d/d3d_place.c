/*
 * Surfaces an engine places itself, in blocks from DirectDraw's own heap.
 *
 * DirectDraw's heap gives a texture a pitch rounded to the engine's published
 * texture alignment. An engine that needs a page-aligned base therefore gets a
 * page-wide pitch: 4096 bytes for every texture, measured on Gen3
 * (MipGapActual=0x1000, 2026-09-25) and on the Mach64 (Tex8Pitch=0x1000,
 * 2026-09-29). Neither sampler takes that pitch. So the engine allocates a
 * block through the DDHAL32_VidMemAlloc that DDRAW.DLL exports "for drivers
 * that want to handle their own memory allocation", rounds its start up to
 * the alignment, and points each surface of the list into it at the pitch the
 * engine needs. It is what the Windows 98 DDK's ViRGE driver does for its own
 * chains (src\display\mini\s3v\S3_DD32.C:3046-3116, built with /DMIP).
 *
 * Moved here from d3d_i9xx.c (2026-09-29) so the Mach64 can use it too; the
 * Gen3 behaviour is unchanged. Three choices, each for a reason:
 *
 *  - The exports are looked up at run time, not linked. A DDRAW.DLL without
 *    them - or a process where it is not loaded - declines to the heap as
 *    before, and a link-time import would instead fail to load the HAL.
 *  - The block is aligned by over-asking one alignment's worth of rows and
 *    rounding up, because the heap call takes no alignment. The block's own
 *    start is kept in the first surface's dwReserved1, the field DDRAWI.H
 *    reserves for the display driver, because that is what has to be freed.
 *  - Every surface's lpVidMemHeap is left NULL, which is what tells DirectDraw
 *    the memory is not its to free; v9x_d3d_place_release frees the block
 *    when the first surface goes. The DDK sample's single-heap case leaves it
 *    NULL too.
 */
#include "d3d_internal.h"

/* DDRAWI.H's LPDDHAL_VIDMEMALLOC / _VIDMEMFREE shapes, 32-bit. */
typedef DWORD (WINAPI *V9X_D3D_PLACE_VIDMEMALLOC)(DWORD lpDD, int heap,
                                                  DWORD width, DWORD height);
typedef void (WINAPI *V9X_D3D_PLACE_VIDMEMFREE)(DWORD lpDD, int heap,
                                                DWORD memory);

/* Per process, as the DLL's data is; resolved on first use. */
static V9X_D3D_PLACE_VIDMEMALLOC v9x_d3d_place_vidmem_alloc;
static V9X_D3D_PLACE_VIDMEMFREE v9x_d3d_place_vidmem_free;

static int v9x_d3d_place_vidmem_resolve(void)
{
    HMODULE ddraw;

    if (v9x_d3d_place_vidmem_alloc != 0 && v9x_d3d_place_vidmem_free != 0) {
        return 1;
    }
    ddraw = GetModuleHandleA("DDRAW.DLL");
    if (ddraw == 0) {
        return 0;
    }
    v9x_d3d_place_vidmem_alloc = (V9X_D3D_PLACE_VIDMEMALLOC)GetProcAddress(
        ddraw, "DDHAL32_VidMemAlloc");
    v9x_d3d_place_vidmem_free = (V9X_D3D_PLACE_VIDMEMFREE)GetProcAddress(
        ddraw, "DDHAL32_VidMemFree");
    return (v9x_d3d_place_vidmem_alloc != 0 &&
            v9x_d3d_place_vidmem_free != 0) ? 1 : 0;
}

/*
 * One block of pitch * rows from DirectDraw's heap, aligned to `align` (a
 * power of two), with surface n of the list pointed at base + offsets[n] at
 * that pitch.
 *
 * The block's own start is kept in the first surface's dwReserved1 and every
 * surface's lpVidMemHeap left NULL, which is the signature
 * v9x_d3d_place_release frees by. Returns zero and the block's graphics
 * offset, or a V9X_D3D_PLACE_* reason having placed nothing.
 */
DWORD v9x_d3d_place_block(V9X_DDHAL_CREATESURFACEDATA *data, DWORD align,
                          DWORD pitch, DWORD rows, const v9x_u32 *offsets,
                          DWORD *base_out)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    DWORD index;
    DWORD block;
    DWORD base;
    DWORD footprint;
    DWORD vram;

    *base_out = 0ul;
    if (align == 0ul || (align & (align - 1ul)) != 0ul || pitch == 0ul) {
        return V9X_D3D_PLACE_BOUNDS;
    }
    if (!v9x_d3d_place_vidmem_resolve()) {
        return V9X_D3D_PLACE_EXPORT;
    }
    if ((v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        (v9x_hal->fb.linear_base & (align - 1ul)) != 0ul) {
        return V9X_D3D_PLACE_BOUNDS;
    }

    /*
     * The block plus an alignment's worth of rows, so it can be rounded up
     * and still hold everything. Heap 0 is the one heap this driver
     * publishes (vmiData.dwNumHeaps); width is bytes, as the DDK sample
     * passes lPitch.
     */
    block = v9x_d3d_place_vidmem_alloc(
        data->lpDD, 0, pitch, rows + (align + pitch - 1ul) / pitch);
    if (block == 0ul) {
        return V9X_D3D_PLACE_ALLOC;
    }
    vram = v9x_hal->fb.vram_bytes;
    footprint = pitch * rows;
    base = 0xfffffffful;
    if (block >= v9x_hal->fb.linear_base &&
        block - v9x_hal->fb.linear_base < vram) {
        base = (block - v9x_hal->fb.linear_base + align - 1ul) &
               ~(align - 1ul);
    }
    if (base == 0xfffffffful || base > vram || footprint > vram - base) {
        v9x_d3d_place_vidmem_free(data->lpDD, 0, block);
        return V9X_D3D_PLACE_BOUNDS;
    }

    for (index = 0ul; index < data->dwSCnt; ++index) {
        V9X_DD_SURFACE_GBL *surface = list[index]->lpGbl;

        surface->fpVidMem = v9x_hal->fb.linear_base + base + offsets[index];
        surface->lPitch = (LONG)pitch;
        /* lpVidMemHeap, in the union DDRAWI.H shares with dwBlockSizeX:
         * NULL is "not DirectDraw's to free". */
        surface->dwBlockSizeX = 0ul;
        surface->dwReserved1 = 0ul;
    }
    list[0]->lpGbl->dwReserved1 = block;
    *base_out = base;
    return 0ul;
}

/*
 * One block of `bytes` from DirectDraw's heap, aligned to `align`, with
 * surface n of the list at base + offsets[n] and a pitch of pitches[n].
 *
 * For a chain whose levels each have their own pitch; otherwise as
 * v9x_d3d_place_block, whose signature it leaves for the release. The heap
 * is asked for rows of `align` bytes, one more than the rounding can cost.
 */
DWORD v9x_d3d_place_chain(V9X_DDHAL_CREATESURFACEDATA *data, DWORD align,
                          DWORD bytes, const v9x_u32 *offsets,
                          const v9x_u32 *pitches, DWORD *base_out)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    DWORD index;
    DWORD block;
    DWORD base;
    DWORD vram;

    *base_out = 0ul;
    if (align == 0ul || (align & (align - 1ul)) != 0ul || bytes == 0ul) {
        return V9X_D3D_PLACE_BOUNDS;
    }
    if (!v9x_d3d_place_vidmem_resolve()) {
        return V9X_D3D_PLACE_EXPORT;
    }
    if ((v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        (v9x_hal->fb.linear_base & (align - 1ul)) != 0ul) {
        return V9X_D3D_PLACE_BOUNDS;
    }

    block = v9x_d3d_place_vidmem_alloc(data->lpDD, 0, align,
                                       (bytes + align - 1ul) / align + 1ul);
    if (block == 0ul) {
        return V9X_D3D_PLACE_ALLOC;
    }
    vram = v9x_hal->fb.vram_bytes;
    base = 0xfffffffful;
    if (block >= v9x_hal->fb.linear_base &&
        block - v9x_hal->fb.linear_base < vram) {
        base = (block - v9x_hal->fb.linear_base + align - 1ul) &
               ~(align - 1ul);
    }
    if (base == 0xfffffffful || base > vram || bytes > vram - base) {
        v9x_d3d_place_vidmem_free(data->lpDD, 0, block);
        return V9X_D3D_PLACE_BOUNDS;
    }

    for (index = 0ul; index < data->dwSCnt; ++index) {
        V9X_DD_SURFACE_GBL *surface = list[index]->lpGbl;

        surface->fpVidMem = v9x_hal->fb.linear_base + base + offsets[index];
        surface->lPitch = (LONG)pitches[index];
        surface->dwBlockSizeX = 0ul;
        surface->dwReserved1 = 0ul;
    }
    list[0]->lpGbl->dwReserved1 = block;
    *base_out = base;
    return 0ul;
}

/*
 * Each surface of the list in its own block from DirectDraw's heap, aligned
 * to `align`, surface n at a pitch of pitches[n] for rows[n] rows.
 *
 * For an engine that reads every mip level at its own offset (the Rage
 * IIC's TEX_n_OFF), so a chain needs no contiguous block. Half-Life's
 * textures under Direct3D's texture management found no room for one block
 * in 960 of 1,274 creations on A8U4I5 (2026-10-03), while DirectDraw's heap
 * placed the same chains level by level - at pitches rounded to the
 * texture alignment, which the sampler cannot read below 32 texels. Every
 * surface keeps its block in its own dwReserved1, which
 * v9x_d3d_place_release frees surface by surface. On a failure the blocks
 * already taken are given back and nothing is placed.
 */
DWORD v9x_d3d_place_each(V9X_DDHAL_CREATESURFACEDATA *data, DWORD align,
                         const v9x_u32 *pitches, const v9x_u32 *rows)
{
    V9X_DD_SURFACE_LCL **list = (V9X_DD_SURFACE_LCL **)data->lplpSList;
    DWORD index;
    DWORD vram;

    if (align == 0ul || (align & (align - 1ul)) != 0ul) {
        return V9X_D3D_PLACE_BOUNDS;
    }
    if (!v9x_d3d_place_vidmem_resolve()) {
        return V9X_D3D_PLACE_EXPORT;
    }
    if ((v9x_hal->fb.flags & V9X_DD_FB_VALID) == 0ul ||
        (v9x_hal->fb.linear_base & (align - 1ul)) != 0ul) {
        return V9X_D3D_PLACE_BOUNDS;
    }
    vram = v9x_hal->fb.vram_bytes;
    for (index = 0ul; index < data->dwSCnt; ++index) {
        V9X_DD_SURFACE_GBL *surface = list[index]->lpGbl;
        DWORD pitch = pitches[index];
        DWORD block = 0ul;
        DWORD base = 0xfffffffful;
        DWORD reason = V9X_D3D_PLACE_BOUNDS;

        if (pitch != 0ul) {
            block = v9x_d3d_place_vidmem_alloc(
                data->lpDD, 0, pitch,
                rows[index] + (align + pitch - 1ul) / pitch);
            reason = V9X_D3D_PLACE_ALLOC;
        }
        if (block != 0ul && block >= v9x_hal->fb.linear_base &&
            block - v9x_hal->fb.linear_base < vram) {
            base = (block - v9x_hal->fb.linear_base + align - 1ul) &
                   ~(align - 1ul);
            reason = V9X_D3D_PLACE_BOUNDS;
        }
        if (base == 0xfffffffful || base > vram ||
            pitch * rows[index] > vram - base) {
            DWORD undo;

            if (block != 0ul) {
                v9x_d3d_place_vidmem_free(data->lpDD, 0, block);
            }
            for (undo = 0ul; undo < index; ++undo) {
                V9X_DD_SURFACE_GBL *placed = list[undo]->lpGbl;

                v9x_d3d_place_vidmem_free(data->lpDD, 0,
                                          placed->dwReserved1);
                placed->dwReserved1 = 0ul;
                placed->fpVidMem = 0ul;
            }
            return reason;
        }
        surface->fpVidMem = v9x_hal->fb.linear_base + base;
        surface->lPitch = (LONG)pitch;
        surface->dwBlockSizeX = 0ul;
        surface->dwReserved1 = block;
    }
    return 0ul;
}

/*
 * The first surface of a list v9x_d3d_place_block placed is going; free the
 * block. Returns 1 when it freed one.
 *
 * Recognised by the signature the placement leaves and the heap never does:
 * a texture, mip level or Z buffer whose lpVidMemHeap is NULL, whose
 * dwReserved1 holds a block start, and whose fpVidMem lies within the
 * alignment that start was rounded up across. A surface DirectDraw placed has
 * its heap set and is ignored, and so is every later surface of a list, whose
 * dwReserved1 is zero. Cleared after the free, so a second destroy of the
 * same surface frees nothing.
 */
int v9x_d3d_place_release(V9X_DDHAL_DESTROYSURFACEDATA *data, DWORD align)
{
    V9X_DD_SURFACE_LCL *surface;
    V9X_DD_SURFACE_GBL *global;
    DWORD block;

    if (v9x_hal == 0 || data == 0) {
        return 0;
    }
    surface = (V9X_DD_SURFACE_LCL *)data->lpDDSurface;
    if (surface == 0 || surface->lpGbl == 0) {
        return 0;
    }
    global = surface->lpGbl;
    block = global->dwReserved1;
    if ((surface->ddsCaps & (V9X_DDSCAPS_MIPMAP | V9X_DDSCAPS_ZBUFFER |
                             V9X_DDSCAPS_TEXTURE)) == 0ul ||
        (surface->ddsCaps & V9X_DDSCAPS_SYSTEMMEMORY) != 0ul ||
        block == 0ul || global->dwBlockSizeX != 0ul ||
        global->fpVidMem < block ||
        global->fpVidMem - block >= align) {
        return 0;
    }
    if (!v9x_d3d_place_vidmem_resolve()) {
        return 0;
    }
    v9x_d3d_place_vidmem_free(data->lpDD, 0, block);
    global->dwReserved1 = 0ul;
    return 1;
}
