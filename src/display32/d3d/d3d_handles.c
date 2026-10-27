/*
 * CreateSurfaceEx handles (d3d_handles.h).
 */
#include "d3d_handles.h"

#define V9X_D3D_HANDLE_MASK (V9X_D3D_SURFACE_HANDLES - 1u)

typedef char v9x_d3d_assert_handles_pow2[
    (V9X_D3D_SURFACE_HANDLES & V9X_D3D_HANDLE_MASK) == 0u ? 1 : -1];

void v9x_d3d_handles_init(V9X_D3D_HANDLES *table)
{
    unsigned int i;

    for (i = 0u; i < V9X_D3D_SURFACE_HANDLES; ++i) {
        table->entry[i].handle = 0ul;
        table->entry[i].used = 0ul;
        table->entry[i].local = 0;
        table->entry[i].surface = 0;
        table->entry[i].user = 0ul;
    }
}

V9X_D3D_SURFACE_HANDLE *v9x_d3d_handles_find(V9X_D3D_HANDLES *table,
                                             const void *local,
                                             v9x_u32 handle)
{
    unsigned int probe;

    if (handle == 0ul) {
        return 0;
    }
    for (probe = 0u; probe < V9X_D3D_SURFACE_HANDLES; ++probe) {
        V9X_D3D_SURFACE_HANDLE *entry =
            &table->entry[(handle + probe) & V9X_D3D_HANDLE_MASK];

        if (!entry->used) {
            return 0;
        }
        if (entry->handle == handle && entry->local == local) {
            return entry;
        }
    }
    return 0;
}

V9X_D3D_SURFACE_HANDLE *v9x_d3d_handles_set(V9X_D3D_HANDLES *table,
                                            const void *local,
                                            v9x_u32 handle, void *surface)
{
    V9X_D3D_SURFACE_HANDLE *entry = v9x_d3d_handles_find(table, local,
                                                        handle);
    unsigned int probe;

    if (handle == 0ul) {
        return 0;
    }
    if (entry != 0) {
        if (entry->surface != surface) {
            entry->surface = surface;
            entry->user = 0ul;
        }
        return entry;
    }
    /* The first free slot on the probe path, freed or never used. */
    for (probe = 0u; probe < V9X_D3D_SURFACE_HANDLES; ++probe) {
        entry = &table->entry[(handle + probe) & V9X_D3D_HANDLE_MASK];
        if (entry->handle == 0ul) {
            entry->handle = handle;
            entry->used = 1ul;
            entry->local = local;
            entry->surface = surface;
            entry->user = 0ul;
            return entry;
        }
    }
    return 0;
}

static void v9x_d3d_handles_free(V9X_D3D_SURFACE_HANDLE *entry)
{
    entry->handle = 0ul;
    entry->local = 0;
    entry->surface = 0;
    entry->user = 0ul;
}

v9x_u32 v9x_d3d_handles_drop(V9X_D3D_HANDLES *table, const void *local,
                             v9x_u32 handle)
{
    V9X_D3D_SURFACE_HANDLE *entry = v9x_d3d_handles_find(table, local,
                                                        handle);

    if (entry == 0) {
        return 0ul;
    }
    v9x_d3d_handles_free(entry);
    return 1ul;
}

v9x_u32 v9x_d3d_handles_drop_local(V9X_D3D_HANDLES *table,
                                   const void *local)
{
    v9x_u32 freed = 0ul;
    unsigned int i;

    for (i = 0u; i < V9X_D3D_SURFACE_HANDLES; ++i) {
        if (table->entry[i].handle != 0ul && table->entry[i].local == local) {
            v9x_d3d_handles_free(&table->entry[i]);
            ++freed;
        }
    }
    return freed;
}

v9x_u32 v9x_d3d_handles_drop_surface(V9X_D3D_HANDLES *table,
                                     const void *surface)
{
    v9x_u32 freed = 0ul;
    unsigned int i;

    if (surface == 0) {
        return 0ul;
    }
    for (i = 0u; i < V9X_D3D_SURFACE_HANDLES; ++i) {
        if (table->entry[i].handle != 0ul &&
            table->entry[i].surface == surface) {
            v9x_d3d_handles_free(&table->entry[i]);
            ++freed;
        }
    }
    return freed;
}
