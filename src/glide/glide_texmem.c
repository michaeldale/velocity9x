/*
 * The TMU address space a Glide game manages itself
 * (docs\plans\glide-2x-wrapper.md, Phase 1). See glide_texmem.h.
 */
#include "glide_texmem.h"

/* The last GrTextureFormat_t (AP_88); 6 and 7 are reserved. */
#define V9X_GLIDE_TEXFMT_LAST 14ul

static v9x_u16 v9x_glide_format_valid(v9x_u32 format)
{
    return (format <= V9X_GLIDE_TEXFMT_LAST && format != 6ul && format != 7ul) ?
           V9X_TRUE : V9X_FALSE;
}

v9x_u16 v9x_glide_level_size(v9x_u32 lod, v9x_u32 aspect, v9x_u32 *width,
                             v9x_u32 *height)
{
    v9x_u32 edge;

    if (lod > V9X_GLIDE_LOD_1 || aspect > V9X_GLIDE_ASPECT_1X8) {
        return V9X_FALSE;
    }
    edge = V9X_GLIDE_LOD_EDGE >> lod;
    *width = edge;
    *height = edge;
    if (aspect < V9X_GLIDE_ASPECT_1X1) {
        *height = edge >> (V9X_GLIDE_ASPECT_1X1 - aspect);
    } else if (aspect > V9X_GLIDE_ASPECT_1X1) {
        *width = edge >> (aspect - V9X_GLIDE_ASPECT_1X1);
    }
    if (*width == 0ul) {
        *width = 1ul;
    }
    if (*height == 0ul) {
        *height = 1ul;
    }
    return V9X_TRUE;
}

v9x_u32 v9x_glide_level_bytes(v9x_u32 lod, v9x_u32 aspect, v9x_u32 format)
{
    v9x_u32 width;
    v9x_u32 height;
    v9x_u32 bytes;

    if (!v9x_glide_format_valid(format) ||
        !v9x_glide_level_size(lod, aspect, &width, &height)) {
        return 0ul;
    }
    bytes = width * height;
    if (format >= V9X_GLIDE_TEXFMT_16BIT) {
        bytes *= 2ul;
    }
    return (bytes + V9X_GLIDE_TEX_GRANULE - 1ul) & ~(V9X_GLIDE_TEX_GRANULE - 1ul);
}

v9x_u32 v9x_glide_texmem_required(const V9X_GLIDE_TEXINFO *info,
                                  v9x_u32 even_odd)
{
    v9x_u32 first = info->small_lod < info->large_lod ? info->small_lod :
                    info->large_lod;
    v9x_u32 last = info->small_lod < info->large_lod ? info->large_lod :
                   info->small_lod;
    v9x_u32 total = 0ul;
    v9x_u32 bytes;
    v9x_u32 lod;
    v9x_u32 mask;

    for (lod = first; lod <= last; ++lod) {
        mask = (lod & 1ul) ? V9X_GLIDE_MIPMAPLEVELMASK_ODD :
                             V9X_GLIDE_MIPMAPLEVELMASK_EVEN;
        if ((even_odd & mask) == 0ul) {
            continue;
        }
        bytes = v9x_glide_level_bytes(lod, info->aspect, info->format);
        if (bytes == 0ul) {
            return 0ul;
        }
        total += bytes;
    }
    return total;
}

/* A start address's bucket. Textures sit at multiples of the granule and
 * often of 2 KiB, so the bits are mixed rather than taken low. */
static unsigned int v9x_glide_bucket(v9x_u32 start)
{
    return (unsigned int)(((start >> 3) * 2654435761ul) >> 22) %
           V9X_GLIDE_TEXMEM_BUCKETS;
}

static void v9x_glide_unlink(V9X_GLIDE_TEXMEM *mem, unsigned int index)
{
    v9x_u32 *link = &mem->buckets[v9x_glide_bucket(mem->records[index].start)];

    while (*link != 0ul) {
        if (*link == (v9x_u32)index + 1ul) {
            *link = mem->records[index].next;
            break;
        }
        link = &mem->records[*link - 1ul].next;
    }
    mem->records[index].in_use = 0ul;
    mem->records[index].next = 0ul;
}

void v9x_glide_texmem_init(V9X_GLIDE_TEXMEM *mem, v9x_u8 *bytes, v9x_u32 size)
{
    unsigned int i;
    v9x_u32 k;

    for (i = 0u; i < V9X_GLIDE_TEXMEM_RECORDS; ++i) {
        mem->records[i].in_use = 0ul;
        mem->records[i].next = 0ul;
    }
    for (i = 0u; i < V9X_GLIDE_TEXMEM_BUCKETS; ++i) {
        mem->buckets[i] = 0ul;
    }
    for (i = 0u; i < V9X_GLIDE_TEXMEM_PAGES; ++i) {
        mem->page_generation[i] = 0ul;
    }
    if (size > (v9x_u32)V9X_GLIDE_TEXMEM_PAGES * V9X_GLIDE_TEXMEM_PAGE_BYTES) {
        size = (v9x_u32)V9X_GLIDE_TEXMEM_PAGES * V9X_GLIDE_TEXMEM_PAGE_BYTES;
    }
    mem->bytes = bytes;
    mem->size = bytes != 0 ? size : 0ul;
    for (k = 0ul; k < mem->size; ++k) {
        bytes[k] = 0u;
    }
    mem->clock = 0ul;
}

/* Raise the generation of every page [start, end) touches; end > start. */
static void v9x_glide_pages_written(V9X_GLIDE_TEXMEM *mem, v9x_u32 start,
                                    v9x_u32 end)
{
    v9x_u32 page;

    for (page = start / V9X_GLIDE_TEXMEM_PAGE_BYTES;
         page <= (end - 1ul) / V9X_GLIDE_TEXMEM_PAGE_BYTES; ++page) {
        ++mem->page_generation[page];
    }
}

/* The sum of the generations of the pages [start, end) touches. */
static v9x_u32 v9x_glide_pages_sum(const V9X_GLIDE_TEXMEM *mem, v9x_u32 start,
                                   v9x_u32 end)
{
    v9x_u32 page;
    v9x_u32 sum = 0ul;

    for (page = start / V9X_GLIDE_TEXMEM_PAGE_BYTES;
         page <= (end - 1ul) / V9X_GLIDE_TEXMEM_PAGE_BYTES; ++page) {
        sum += mem->page_generation[page];
    }
    return sum;
}

v9x_u32 v9x_glide_texmem_download(V9X_GLIDE_TEXMEM *mem, v9x_u32 start,
                                  v9x_u32 even_odd,
                                  const V9X_GLIDE_TEXINFO *info,
                                  const v9x_u8 *data)
{
    v9x_u32 required = v9x_glide_texmem_required(info, even_odd);
    v9x_u32 first = info->small_lod < info->large_lod ? info->small_lod :
                    info->large_lod;
    v9x_u32 last = info->small_lod < info->large_lod ? info->large_lod :
                   info->small_lod;
    v9x_u32 span;
    v9x_u32 source = 0ul;
    v9x_u32 offset = 0ul;
    v9x_u32 lod;

    if (required == 0ul || start >= mem->size || data == 0) {
        return 0ul;
    }
    span = required < mem->size - start ? required : mem->size - start;

    /* Largest level first: the smallest GrLOD_t number. */
    for (lod = first; lod <= last; ++lod) {
        v9x_u32 width;
        v9x_u32 height;
        v9x_u32 level;
        v9x_u32 mask = (lod & 1ul) ? V9X_GLIDE_MIPMAPLEVELMASK_ODD :
                                     V9X_GLIDE_MIPMAPLEVELMASK_EVEN;
        v9x_u32 k;

        (void)v9x_glide_level_size(lod, info->aspect, &width, &height);
        level = width * height *
                (info->format >= V9X_GLIDE_TEXFMT_16BIT ? 2ul : 1ul);
        if ((even_odd & mask) != 0ul) {
            for (k = 0ul; k < level && offset + k < span; ++k) {
                mem->bytes[start + offset + k] = data[source + k];
            }
            offset += v9x_glide_level_bytes(lod, info->aspect, info->format);
        }
        source += level;
    }
    v9x_glide_pages_written(mem, start, start + span);
    return span;
}

int v9x_glide_texmem_source(V9X_GLIDE_TEXMEM *mem, v9x_u32 start,
                            v9x_u32 even_odd, const V9X_GLIDE_TEXINFO *info,
                            v9x_u32 *state)
{
    V9X_GLIDE_TEXREC *record;
    v9x_u32 required = v9x_glide_texmem_required(info, even_odd);
    v9x_u32 end = start + required;
    v9x_u32 link;
    v9x_u32 sum;
    unsigned int i;
    unsigned int slot = V9X_GLIDE_TEXMEM_RECORDS;
    unsigned int oldest = V9X_GLIDE_TEXMEM_RECORDS;
    unsigned int bucket;

    *state = V9X_GLIDE_TEXMEM_NEW;
    if (required == 0ul || start >= mem->size || required > mem->size - start) {
        return -1;
    }
    sum = v9x_glide_pages_sum(mem, start, end);
    ++mem->clock;

    bucket = v9x_glide_bucket(start);
    for (link = mem->buckets[bucket]; link != 0ul; link = record->next) {
        record = &mem->records[link - 1ul];
        if (record->in_use && record->start == start &&
            record->even_odd == even_odd &&
            record->info.small_lod == info->small_lod &&
            record->info.large_lod == info->large_lod &&
            record->info.aspect == info->aspect &&
            record->info.format == info->format) {
            *state = record->generations == sum ? V9X_GLIDE_TEXMEM_CURRENT :
                                                  V9X_GLIDE_TEXMEM_STALE;
            record->generations = sum;
            record->last_used = mem->clock;
            return (int)(link - 1ul);
        }
    }

    for (i = 0u; i < V9X_GLIDE_TEXMEM_RECORDS; ++i) {
        if (!mem->records[i].in_use) {
            slot = i;
            break;
        }
        if (oldest == V9X_GLIDE_TEXMEM_RECORDS ||
            mem->records[i].last_used < mem->records[oldest].last_used) {
            oldest = i;
        }
    }
    if (slot == V9X_GLIDE_TEXMEM_RECORDS) {
        slot = oldest;
        v9x_glide_unlink(mem, slot);
    }

    record = &mem->records[slot];
    record->in_use = 1ul;
    record->start = start;
    record->end = end;
    record->even_odd = even_odd;
    record->info = *info;
    record->generations = sum;
    record->last_used = mem->clock;
    record->next = mem->buckets[bucket];
    mem->buckets[bucket] = (v9x_u32)slot + 1ul;
    return (int)slot;
}
