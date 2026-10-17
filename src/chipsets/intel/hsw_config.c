/*
 * Haswell (Gen7.5) configuration-space decode. Pure arithmetic over words
 * read elsewhere; see include\velocity9x\intel_hsw.h for the sources and
 * for why this is not the Gen3 decode.
 */
#include "velocity9x/intel_hsw.h"

/* GGMS 0, 1 and 2 are 0, 1 and 2 MB; 3 is reserved (datasheet 3.1.13). */
#define V9X_INTEL_HSW_GGMS_RESERVED ((v9x_u16)3u)
#define V9X_INTEL_HSW_ONE_MB        ((v9x_u32)0x00100000ul)

v9x_status v9x_intel_hsw_decode_ggc(v9x_u16 ggc,
                                    struct v9x_intel_hsw_ggc *out)
{
    v9x_u16 gms;
    v9x_u16 ggms;

    if (out == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    gms = (v9x_u16)((ggc >> V9X_INTEL_HSW_GGC_GMS_SHIFT) &
                    V9X_INTEL_HSW_GGC_GMS_MASK);
    ggms = (v9x_u16)((ggc >> V9X_INTEL_HSW_GGC_GGMS_SHIFT) &
                     V9X_INTEL_HSW_GGC_GGMS_MASK);

    out->gms = gms;
    out->ggms = ggms;
    out->vga_disabled = (v9x_u16)((ggc & V9X_INTEL_HSW_GGC_IVD) != 0u);
    out->locked = (v9x_u16)((ggc & V9X_INTEL_HSW_GGC_LOCK) != 0u);
    out->versatile_mode = (v9x_u16)((ggc & V9X_INTEL_HSW_GGC_VAMEN) != 0u);
    out->stolen_bytes = 0ul;
    out->gtt_stolen_bytes = 0ul;

    /*
     * The datasheet lists 00h to 10h and nothing above. A BIOS that wrote
     * 11h has either a table this document does not describe or a bug, and
     * guessing "17 * 32 MB" would place the GTT-stolen block wrongly.
     */
    if (gms > V9X_INTEL_HSW_GGC_GMS_MAX) {
        return V9X_STATUS_UNSUPPORTED;
    }
    if (ggms == V9X_INTEL_HSW_GGMS_RESERVED) {
        return V9X_STATUS_UNSUPPORTED;
    }

    out->stolen_bytes = (v9x_u32)gms * V9X_INTEL_HSW_GMS_UNIT_BYTES;
    out->gtt_stolen_bytes = (v9x_u32)ggms * V9X_INTEL_HSW_ONE_MB;
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_hsw_aperture_bytes(v9x_u8 msac, v9x_u32 *bytes)
{
    v9x_u8 size;

    if (bytes == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* LHSASH:LHSASL. 10b is "Illegal Programming" in Vol 12, not a size. */
    size = (v9x_u8)((msac >> V9X_INTEL_HSW_MSAC_SIZE_SHIFT) &
                    V9X_INTEL_HSW_MSAC_SIZE_MASK);
    if (size == 0u) {
        *bytes = 0x08000000ul;
        return V9X_STATUS_OK;
    }
    if (size == 1u) {
        *bytes = 0x10000000ul;
        return V9X_STATUS_OK;
    }
    if (size == 3u) {
        *bytes = 0x20000000ul;
        return V9X_STATUS_OK;
    }

    *bytes = 0ul;
    return V9X_STATUS_UNSUPPORTED;
}

v9x_status v9x_intel_hsw_decode_layout(
    const struct v9x_intel_hsw_config *config,
    struct v9x_intel_hsw_layout *out)
{
    v9x_u32 gtt_reach;

    if (config == 0 || out == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    out->refused = 0u;
    out->mmio_base = 0ul;
    out->gtt_base = 0ul;
    out->gtt_entries = 0ul;
    out->aperture_base = 0ul;
    out->aperture_bytes = 0ul;
    out->stolen_base = 0ul;
    out->gtt_stolen_base = 0ul;

    if (v9x_intel_hsw_decode_ggc(config->ggc, &out->ggc) != V9X_STATUS_OK) {
        if (out->ggc.gms > V9X_INTEL_HSW_GGC_GMS_MAX) {
            out->refused |= V9X_INTEL_HSW_LAYOUT_GMS_RESERVED;
        }
        if (out->ggc.ggms == V9X_INTEL_HSW_GGMS_RESERVED) {
            out->refused |= V9X_INTEL_HSW_LAYOUT_GGMS_RESERVED;
        }
    }
    if (out->ggc.stolen_bytes == 0ul && out->refused == 0u) {
        out->refused |= V9X_INTEL_HSW_LAYOUT_NO_STOLEN;
    }

    /*
     * Both BARs are 64-bit by definition (MEMTYP 10b). A 32-bit type here
     * would mean this is not the device the datasheet describes; a nonzero
     * high dword puts it where a Windows 98 linear address cannot reach.
     */
    if ((config->bar0_low & V9X_INTEL_HSW_BAR_TYPE_MASK) !=
        V9X_INTEL_HSW_BAR_MEM64) {
        out->refused |= V9X_INTEL_HSW_LAYOUT_BAR0_TYPE;
    }
    if ((config->bar2_low & V9X_INTEL_HSW_BAR_TYPE_MASK) !=
        V9X_INTEL_HSW_BAR_MEM64) {
        out->refused |= V9X_INTEL_HSW_LAYOUT_BAR2_TYPE;
    }
    if (config->bar0_high != 0ul || config->bar2_high != 0ul) {
        out->refused |= V9X_INTEL_HSW_LAYOUT_ABOVE_4G;
    }

    out->mmio_base = config->bar0_low & V9X_INTEL_HSW_GTTMMADR_MASK;
    if (out->mmio_base != 0ul) {
        out->gtt_base = out->mmio_base + V9X_INTEL_HSW_GTT_OFFSET;
    }

    if (v9x_intel_hsw_aperture_bytes(config->msac, &out->aperture_bytes) !=
        V9X_STATUS_OK) {
        out->refused |= V9X_INTEL_HSW_LAYOUT_MSAC_ILLEGAL;
    } else {
        out->aperture_base = config->bar2_low &
                             ~(out->aperture_bytes - 1ul);
    }

    if (out->mmio_base == 0ul || out->aperture_base == 0ul) {
        out->refused |= V9X_INTEL_HSW_LAYOUT_UNASSIGNED;
    }

    /*
     * Every aperture page needs a PTE. GGMS bytes / 4 entries, each mapping
     * 4 KB: 2 MB of GTT reaches 2 GB, 1 MB reaches 1 GB, 0 reaches nothing.
     */
    out->gtt_entries = out->ggc.gtt_stolen_bytes / V9X_INTEL_HSW_PTE_BYTES;
    gtt_reach = out->gtt_entries * V9X_INTEL_HSW_PAGE_BYTES;
    if (out->gtt_entries == 0ul || gtt_reach < out->aperture_bytes) {
        out->refused |= V9X_INTEL_HSW_LAYOUT_GTT_TOO_SMALL;
    }

    out->stolen_base = config->bdsm & V9X_INTEL_HSW_BASE_MASK;
    if (out->stolen_base >= out->ggc.gtt_stolen_bytes) {
        out->gtt_stolen_base = out->stolen_base - out->ggc.gtt_stolen_bytes;
    }

    /*
     * A second, independent number for the same fact: the host bridge's
     * BGSM, which the BIOS computed from the same GGC. Zero means the
     * caller did not read it, not that it is zero.
     */
    if (config->host_bgsm != 0ul &&
        (config->host_bgsm & V9X_INTEL_HSW_BASE_MASK) !=
            out->gtt_stolen_base) {
        out->refused |= V9X_INTEL_HSW_LAYOUT_BGSM_MISMATCH;
    }

    if (out->refused != 0u) {
        return V9X_STATUS_UNSUPPORTED;
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_hsw_decode_pte(v9x_u32 raw,
                                    struct v9x_intel_hsw_pte *out)
{
    if (out == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    out->valid = (v9x_u8)((raw & V9X_INTEL_HSW_PTE_VALID) != 0ul);
    out->physical_low = raw & V9X_INTEL_HSW_PTE_PAGE_MASK;
    out->physical_high = (v9x_u8)((raw & V9X_INTEL_HSW_PTE_HIGH_MASK) >>
                                  V9X_INTEL_HSW_PTE_HIGH_SHIFT);
    out->cache = (v9x_u8)((raw & V9X_INTEL_HSW_PTE_CC20_MASK) >> 1);
    if ((raw & V9X_INTEL_HSW_PTE_CC3) != 0ul) {
        out->cache = (v9x_u8)(out->cache | 0x8u);
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_intel_hsw_encode_pte(v9x_u32 physical, v9x_u8 cache,
                                    v9x_u32 *raw)
{
    v9x_u32 value;

    if (raw == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((physical & ~V9X_INTEL_HSW_PTE_PAGE_MASK) != 0ul ||
        (cache & (v9x_u8)~V9X_INTEL_HSW_CACHE_MASK) != 0u) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    /* Address 38:32 stays zero: the caller's physical is 32-bit. */
    value = physical | V9X_INTEL_HSW_PTE_VALID;
    value |= ((v9x_u32)(cache & 0x7u)) << 1;
    if ((cache & 0x8u) != 0u) {
        value |= V9X_INTEL_HSW_PTE_CC3;
    }
    *raw = value;
    return V9X_STATUS_OK;
}
