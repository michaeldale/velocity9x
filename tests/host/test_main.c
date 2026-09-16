#include <stdio.h>
#include <string.h>

#include "velocity9x/build.h"
#include "velocity9x/components.h"
#include "velocity9x/backend_registry.h"
#include "velocity9x/matrox_millennium2.h"
#include "velocity9x/s3_virge.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/intel_gen3_3d.h"
#include "../../src/display32/d3d/d3d_raster.h"

/* tests\host\test_family_matrix.c: assertions against the manifest-generated
 * family matrix. It keeps its own failure count and returns it. */
unsigned int v9x_run_family_matrix_tests(void);

/* tests\host\test_hw16_modes.c: the per-family V9X_HW16_MODE tables against
 * the same generated matrix, same convention. */
unsigned int v9x_run_hw16_mode_tests(void);

/* tests\host\test_vbe_parse.c: the VBE 4F00h/4F01h result parsers, same
 * convention. */
unsigned int v9x_run_vbe_parse_tests(void);

/* tests\host\test_vbe_modes.c: runtime mode-table construction from the
 * family baseline plus the scanned BIOS list, same convention. */
unsigned int v9x_run_vbe_modes_tests(void);

/* tests\host\test_vbe_cache.c: what the mini-VDD's reported counts and status
 * flags permit a consumer to believe, same convention. */
unsigned int v9x_run_vbe_cache_tests(void);

/* tests\host\test_edid.c: EDID base-block parsing and its negative corpus,
 * same convention. */
unsigned int v9x_run_edid_tests(void);

/* tests\host\test_mtrr.c: the write-combining decision, over the MSR states
 * ring 0 reports, same convention. */
unsigned int v9x_run_mtrr_tests(void);

/* tests\host\test_d3dmode.c: which Direct3D back end the SYSTEM.INI setting
 * and the chip's engine descriptor resolve to, same convention. */
unsigned int v9x_run_d3dmode_tests(void);

/* tests\host	est_vbe_crtc.c: the full EDID detailed timing and the VBE 3.0
 * CRTC block built from it, same convention. */
unsigned int v9x_run_vbe_crtc_tests(void);

/* The ViRGE 1.31 depth conversion: the clamp that keeps sz = 1.0 from
 * becoming the x87 integer indefinite, and with it the near plane. The
 * converter uses Watcom's #pragma aux to test the HAL's actual x87 code. */
#ifdef __WATCOMC__
unsigned int v9x_run_d3d_zfixed_tests(void);
#endif

/* tests\host\test_d3d_raster.c: the CPU rasterizer's coverage rule, its
 * refusals and its Gouraud interpolation, same convention. */
unsigned int v9x_run_d3d_raster_tests(void);

/* tests\host\test_donewait.c: whether the idle wait keeps spinning for a
 * 3D-done bit the part may not have, same convention. */
unsigned int v9x_run_donewait_tests(void);

/* tests\host\test_i9xx_mmio.c: read-only Gen3 fingerprint decoding. */
unsigned int v9x_run_i9xx_mmio_tests(void);

/* tests\host\test_i9xx_gtt.c: streaming Gen3 GTT inventory. */
unsigned int v9x_run_i9xx_gtt_tests(void);

/* tests\host\test_i9xx_ring.c: Phase 4 sandbox and packet policy. */
unsigned int v9x_run_i9xx_ring_tests(void);

/* tests\host\test_i9xx_arm.c: one-shot Phase 4 arm contract. */
unsigned int v9x_run_i9xx_arm_tests(void);

/* tests\host	est_i9xx_3d.c: Phase 5 float transport, builders,
 * golden stream and decoder. */
unsigned int v9x_run_i9xx_3d_tests(void);

static unsigned int failures = 0u;

#define CHECK(expression) do { \
    if (!(expression)) { \
        printf("FAIL %s:%u: %s\n", __FILE__, (unsigned int)__LINE__, #expression); \
        ++failures; \
    } \
} while (0)

struct capture_sink {
    struct v9x_log_record records[8];
    v9x_u16 count;
};

static v9x_status capture_log(void *context,
                              const struct v9x_log_record *record)
{
    struct capture_sink *sink = (struct capture_sink *)context;
    if (sink->count >= 8u) {
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    sink->records[sink->count++] = *record;
    return V9X_STATUS_OK;
}

static void test_mode_layout(void)
{
    struct v9x_mode_request request;
    struct v9x_mode_layout layout;

    request.width = 640u;
    request.height = 480u;
    request.bits_per_pixel = 8u;
    request.pitch_alignment = 8u;
    request.framebuffer_bytes = 4ul * 1024ul * 1024ul;
    CHECK(v9x_mode_calculate(&request, &layout) == V9X_STATUS_OK);
    CHECK(layout.pitch_bytes == 640ul);
    CHECK(layout.visible_bytes == 307200ul);
    CHECK(layout.offscreen_bytes == request.framebuffer_bytes - 307200ul);

    request.width = 641u;
    request.bits_per_pixel = 16u;
    CHECK(v9x_mode_calculate(&request, &layout) == V9X_STATUS_OK);
    CHECK(layout.pitch_bytes == 1288ul);

    request.bits_per_pixel = 24u;
    CHECK(v9x_mode_calculate(&request, &layout) == V9X_STATUS_OK);
    CHECK(layout.pitch_bytes == 1928ul);

    request.bits_per_pixel = 32u;
    CHECK(v9x_mode_calculate(&request, &layout) == V9X_STATUS_OK);
    CHECK(layout.pitch_bytes == 2568ul);

    /* The depths that divide into whole bytes are the supported set; 15bpp is
     * the one a VBE mode list will actually offer and the layout maths cannot
     * express, so it stays a refusal rather than rounding to 16. */
    request.bits_per_pixel = 15u;
    CHECK(v9x_mode_calculate(&request, &layout) == V9X_STATUS_UNSUPPORTED);

    request.bits_per_pixel = 8u;
    request.pitch_alignment = 3u;
    CHECK(v9x_mode_calculate(&request, &layout) == V9X_STATUS_INVALID_ARGUMENT);

    request.pitch_alignment = 8u;
    request.framebuffer_bytes = 1024ul;
    CHECK(v9x_mode_calculate(&request, &layout) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
}

static void test_mode_layout_rejects_bad_arguments(void)
{
    struct v9x_mode_request request;
    struct v9x_mode_layout layout;

    request.width = 640u;
    request.height = 480u;
    request.bits_per_pixel = 8u;
    request.pitch_alignment = 8u;
    request.framebuffer_bytes = 4ul * 1024ul * 1024ul;

    CHECK(v9x_mode_calculate(0, &layout) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(v9x_mode_calculate(&request, 0) == V9X_STATUS_INVALID_ARGUMENT);

    request.width = 0u;
    CHECK(v9x_mode_calculate(&request, &layout) == V9X_STATUS_INVALID_ARGUMENT);
    CHECK(layout.pitch_bytes == 0ul);
    CHECK(layout.visible_bytes == 0ul);
    CHECK(layout.offscreen_bytes == 0ul);
    request.width = 640u;

    request.height = 0u;
    CHECK(v9x_mode_calculate(&request, &layout) == V9X_STATUS_INVALID_ARGUMENT);
    request.height = 480u;

    request.pitch_alignment = 0u;
    CHECK(v9x_mode_calculate(&request, &layout) == V9X_STATUS_INVALID_ARGUMENT);
}

static void test_mode_layout_overflow(void)
{
    struct v9x_mode_request request;
    struct v9x_mode_layout layout;

    /* pitch 131072 * height 65535 exceeds 32 bits. */
    request.width = 65535u;
    request.height = 65535u;
    request.bits_per_pixel = 16u;
    request.pitch_alignment = 8u;
    request.framebuffer_bytes = 0xfffffffful;
    CHECK(v9x_mode_calculate(&request, &layout) == V9X_STATUS_INTEGER_OVERFLOW);
    CHECK(layout.pitch_bytes == 0ul);
    CHECK(layout.visible_bytes == 0ul);
    CHECK(layout.offscreen_bytes == 0ul);
}

static v9x_u32 prng_state = 0x12345678ul;

static v9x_u32 prng_next(void)
{
    prng_state = prng_state * 1664525ul + 1013904223ul;
    return prng_state;
}

static void test_mode_layout_properties(void)
{
    static const v9x_u16 alignments[8] = { 1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u };
    struct v9x_mode_request request;
    struct v9x_mode_layout layout;
    v9x_u32 iteration;

    for (iteration = 0ul; iteration < 20000ul; ++iteration) {
        v9x_u32 raw_pitch;
        v9x_u32 alignment_mask;
        v9x_u32 pitch;
        v9x_status status;

        request.width = (v9x_u16)(prng_next() & 0xffffu);
        request.height = (v9x_u16)(prng_next() & 0xffffu);
        request.bits_per_pixel = ((prng_next() & 1ul) != 0ul) ? 16u : 8u;
        request.pitch_alignment = alignments[prng_next() & 7ul];
        request.framebuffer_bytes = prng_next();

        status = v9x_mode_calculate(&request, &layout);

        if (request.width == 0u || request.height == 0u) {
            CHECK(status == V9X_STATUS_INVALID_ARGUMENT);
            continue;
        }

        /* Small enough to compute exactly in 32 bits. */
        raw_pitch = (v9x_u32)request.width *
                    (v9x_u32)(request.bits_per_pixel / 8u);
        alignment_mask = (v9x_u32)request.pitch_alignment - 1ul;
        pitch = (raw_pitch + alignment_mask) & ~alignment_mask;

        /* Independent overflow decision via exact double arithmetic. */
        if ((double)pitch * (double)request.height > 4294967295.0) {
            CHECK(status == V9X_STATUS_INTEGER_OVERFLOW);
        } else if (pitch * (v9x_u32)request.height >
                   request.framebuffer_bytes) {
            CHECK(status == V9X_STATUS_INSUFFICIENT_MEMORY);
        } else {
            CHECK(status == V9X_STATUS_OK);
            CHECK(layout.pitch_bytes == pitch);
            CHECK(layout.pitch_bytes % request.pitch_alignment == 0ul);
            CHECK(layout.pitch_bytes >= raw_pitch);
            CHECK(layout.pitch_bytes - raw_pitch <
                  (v9x_u32)request.pitch_alignment);
            CHECK(layout.visible_bytes ==
                  pitch * (v9x_u32)request.height);
            CHECK(layout.offscreen_bytes ==
                  request.framebuffer_bytes - layout.visible_bytes);
        }

        if (status != V9X_STATUS_OK) {
            CHECK(layout.pitch_bytes == 0ul);
            CHECK(layout.visible_bytes == 0ul);
            CHECK(layout.offscreen_bytes == 0ul);
        }
    }
}

static void test_framebuffer_resource_validation(void)
{
    struct v9x_pci_bar_resource bar;
    struct v9x_framebuffer_binding binding;

    bar.physical_base = 0xe0000000ul;
    bar.aperture_bytes = 64ul * 1024ul * 1024ul;
    bar.flags = V9X_PCI_BAR_MEMORY | V9X_PCI_BAR_PREFETCHABLE;

    CHECK(v9x_framebuffer_validate_binding(&bar,
                                            4ul * 1024ul * 1024ul,
                                            0ul,
                                            &binding) == V9X_STATUS_OK);
    CHECK(binding.physical_base == bar.physical_base);
    CHECK(binding.aperture_bytes == bar.aperture_bytes);
    CHECK(binding.vram_bytes == 4ul * 1024ul * 1024ul);
    CHECK(binding.override_active == V9X_FALSE);

    CHECK(v9x_framebuffer_validate_binding(&bar,
                                            0ul,
                                            2ul * 1024ul * 1024ul,
                                            &binding) == V9X_STATUS_OK);
    CHECK(binding.vram_bytes == 2ul * 1024ul * 1024ul);
    CHECK(binding.override_active == V9X_TRUE);

    CHECK(v9x_framebuffer_validate_binding(0, 1ul, 0ul, &binding) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(binding.physical_base == 0ul);
    CHECK(v9x_framebuffer_validate_binding(&bar, 1ul, 0ul, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);

    bar.flags = V9X_PCI_BAR_IO;
    CHECK(v9x_framebuffer_validate_binding(&bar, 1ul, 0ul, &binding) ==
          V9X_STATUS_UNSUPPORTED);
    bar.flags = V9X_PCI_BAR_MEMORY | V9X_PCI_BAR_64BIT;
    CHECK(v9x_framebuffer_validate_binding(&bar, 1ul, 0ul, &binding) ==
          V9X_STATUS_UNSUPPORTED);
    bar.flags = V9X_PCI_BAR_MEMORY | (v9x_u16)0x0010u;
    CHECK(v9x_framebuffer_validate_binding(&bar, 1ul, 0ul, &binding) ==
          V9X_STATUS_INVALID_ARGUMENT);

    bar.flags = V9X_PCI_BAR_MEMORY;
    bar.physical_base = 0ul;
    CHECK(v9x_framebuffer_validate_binding(&bar, 1ul, 0ul, &binding) ==
          V9X_STATUS_INVALID_ARGUMENT);
    bar.physical_base = 0xe0000000ul;
    bar.aperture_bytes = 3ul * 1024ul * 1024ul;
    CHECK(v9x_framebuffer_validate_binding(&bar, 1ul, 0ul, &binding) ==
          V9X_STATUS_INVALID_ARGUMENT);
    bar.aperture_bytes = 64ul * 1024ul * 1024ul;
    bar.physical_base = 0xe1000000ul;
    CHECK(v9x_framebuffer_validate_binding(&bar, 1ul, 0ul, &binding) ==
          V9X_STATUS_INVALID_ARGUMENT);

    bar.physical_base = 0xe0000000ul;
    CHECK(v9x_framebuffer_validate_binding(&bar,
                                            65ul * 1024ul * 1024ul,
                                            0ul,
                                            &binding) ==
          V9X_STATUS_INSUFFICIENT_MEMORY);
    CHECK(binding.physical_base == 0ul);
    CHECK(v9x_framebuffer_validate_binding(&bar, 0ul, 0ul, &binding) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_framebuffer_resource_properties(void)
{
    struct v9x_pci_bar_resource bar;
    struct v9x_framebuffer_binding binding;
    v9x_u32 iteration;

    for (iteration = 0ul; iteration < 10000ul; ++iteration) {
        v9x_u16 shift = (v9x_u16)(12u + (prng_next() % 16ul));
        v9x_u32 aperture = 1ul << shift;
        v9x_u32 vram = (prng_next() % aperture) + 1ul;

        bar.aperture_bytes = aperture;
        bar.physical_base = prng_next() & ~(aperture - 1ul);
        if (bar.physical_base == 0ul) {
            bar.physical_base = aperture;
        }
        bar.flags = V9X_PCI_BAR_MEMORY;
        if ((prng_next() & 1ul) != 0ul) {
            bar.flags |= V9X_PCI_BAR_PREFETCHABLE;
        }

        CHECK(v9x_framebuffer_validate_binding(&bar, vram, 0ul, &binding) ==
              V9X_STATUS_OK);
        CHECK(binding.physical_base == bar.physical_base);
        CHECK(binding.aperture_bytes == aperture);
        CHECK(binding.vram_bytes == vram);
        CHECK(binding.override_active == V9X_FALSE);

        bar.physical_base += 1ul;
        CHECK(v9x_framebuffer_validate_binding(&bar, vram, 0ul, &binding) ==
              V9X_STATUS_INVALID_ARGUMENT);
        CHECK(binding.physical_base == 0ul);
    }
}

static void test_probe_is_strict(void)
{
    struct v9x_backend_state state;
    struct v9x_pci_identity pci;
    struct v9x_pci_bar_resource bar;
    struct v9x_mode_request request;
    struct v9x_mode_layout layout;

    memset(&state, 0, sizeof(state));
    pci.vendor_id = V9X_PCI_VENDOR_S3;
    pci.device_id = V9X_PCI_DEVICE_VIRGE_DX;
    pci.revision = 1u;
    CHECK(v9x_s3_virge_probe(&state, &pci) == V9X_STATUS_OK);
    CHECK(state.initialized == V9X_TRUE);
    CHECK(state.resources_bound == V9X_FALSE);
    CHECK(state.capabilities == 0ul);

    request.width = 640u;
    request.height = 480u;
    request.bits_per_pixel = 8u;
    request.pitch_alignment = 8u;
    request.framebuffer_bytes = 1ul; /* The backend must use trusted state. */
    CHECK(v9x_s3_virge_validate_mode(&state, &request, &layout) ==
          V9X_STATUS_INVALID_STATE);

    bar.physical_base = 0xe0000000ul;
    bar.aperture_bytes = 64ul * 1024ul * 1024ul;
    bar.flags = V9X_PCI_BAR_MEMORY | V9X_PCI_BAR_PREFETCHABLE;
    CHECK(v9x_s3_virge_bind_framebuffer(&state,
                                        &bar,
                                        4ul * 1024ul * 1024ul,
                                        0ul) == V9X_STATUS_OK);
    CHECK(state.resources_bound == V9X_TRUE);
    CHECK(state.vram_bytes == 4ul * 1024ul * 1024ul);
    CHECK(state.capabilities == 0ul);
    CHECK(v9x_s3_virge_validate_mode(&state, &request, &layout) ==
          V9X_STATUS_OK);
    CHECK(layout.visible_bytes == 307200ul);

    CHECK(v9x_s3_virge_bind_framebuffer(&state, 0, 1ul, 0ul) ==
          V9X_STATUS_INVALID_ARGUMENT);
    CHECK(state.resources_bound == V9X_FALSE);
    CHECK(state.framebuffer.physical_base == 0ul);

    CHECK(v9x_s3_virge_bind_framebuffer(&state,
                                        &bar,
                                        4ul * 1024ul * 1024ul,
                                        0ul) == V9X_STATUS_OK);

    bar.flags = V9X_PCI_BAR_IO;
    CHECK(v9x_s3_virge_bind_framebuffer(&state, &bar, 1ul, 0ul) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(state.resources_bound == V9X_FALSE);
    CHECK(state.vram_bytes == 0ul);
    CHECK(state.framebuffer.physical_base == 0ul);
    CHECK(state.capabilities == 0ul);

    pci.device_id = 0x5631u;
    CHECK(v9x_s3_virge_probe(&state, &pci) == V9X_STATUS_UNSUPPORTED);
    CHECK(state.initialized == V9X_FALSE);
    CHECK(state.resources_bound == V9X_FALSE);
    CHECK(state.capabilities == 0ul);
    CHECK(state.pci.vendor_id == 0u);
    CHECK(v9x_s3_virge_validate_mode(&state, &request, &layout) ==
          V9X_STATUS_INVALID_STATE);
}

static void test_s3_virge_clock_decode(void)
{
    struct v9x_clock_info clocks;

    /* 14.318 MHz * (65 + 2) / (18 + 2) / 1 = 47.965 MHz. */
    CHECK(v9x_s3_virge_decode_clock_pll(0x12u, 0x41u, &clocks) ==
          V9X_STATUS_OK);
    CHECK(clocks.memory_clock_khz == 47965ul);
    CHECK(clocks.core_clock_khz == clocks.memory_clock_khz);
    CHECK((clocks.flags & V9X_CLOCK_CORE_VALID) != 0u);
    CHECK((clocks.flags & V9X_CLOCK_MEMORY_VALID) != 0u);
    CHECK((clocks.flags & V9X_CLOCK_CORE_SHARED_MCLK) != 0u);

    CHECK(v9x_s3_virge_decode_clock_pll(0xffu, 0xffu, &clocks) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(clocks.flags == 0u);
    CHECK(v9x_s3_virge_decode_clock_pll(0u, 0u, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_s3_virge_memory_decode(void)
{
    v9x_u32 bytes;

    /* CR36 bits 7:5 carry the installed-memory code. The values below are the
     * ones the Trio32/64 and ViRGE/DX actually emit; the low bits are chip
     * configuration and must be ignored. */
    CHECK(v9x_s3_virge_decode_memory_size(0x00u, &bytes) == V9X_STATUS_OK);
    CHECK(bytes == 4ul * 1024ul * 1024ul);
    /* 86Box builds a 4 MiB ViRGE/DX CR36 as 2 | (0 << 2) | (1 << 4). */
    CHECK(v9x_s3_virge_decode_memory_size(0x12u, &bytes) == V9X_STATUS_OK);
    CHECK(bytes == 4ul * 1024ul * 1024ul);
    CHECK(v9x_s3_virge_decode_memory_size(0x60u, &bytes) == V9X_STATUS_OK);
    CHECK(bytes == 8ul * 1024ul * 1024ul);
    CHECK(v9x_s3_virge_decode_memory_size(0x92u, &bytes) == V9X_STATUS_OK);
    CHECK(bytes == 2ul * 1024ul * 1024ul);
    CHECK(v9x_s3_virge_decode_memory_size(0xc0u, &bytes) == V9X_STATUS_OK);
    CHECK(bytes == 1ul * 1024ul * 1024ul);
    CHECK(v9x_s3_virge_decode_memory_size(0xe0u, &bytes) == V9X_STATUS_OK);
    CHECK(bytes == 512ul * 1024ul);

    /* Codes 1, 2 and 5 belong to other S3 parts and must not be guessed. */
    CHECK(v9x_s3_virge_decode_memory_size(0x20u, &bytes) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(bytes == 0ul);
    CHECK(v9x_s3_virge_decode_memory_size(0x40u, &bytes) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_s3_virge_decode_memory_size(0xa0u, &bytes) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(v9x_s3_virge_decode_memory_size(0u, 0) ==
          V9X_STATUS_INVALID_ARGUMENT);
}

static void test_backend_registry_and_millennium2(void)
{
    struct v9x_backend_state state;
    struct v9x_pci_identity pci;
    struct v9x_pci_bar_resource bar;
    struct v9x_mode_request request;
    struct v9x_mode_layout layout;
    const struct v9x_backend_ops *ops;

    memset(&state, 0, sizeof(state));
    pci.vendor_id = V9X_PCI_VENDOR_MATROX;
    pci.device_id = V9X_PCI_DEVICE_MILLENNIUM_II;
    pci.revision = 0u;
    ops = v9x_backend_for_pci(&pci);
    CHECK(ops == v9x_matrox_millennium2_backend());
    CHECK(ops->probe(&state, &pci) == V9X_STATUS_OK);
    CHECK(state.initialized == V9X_TRUE);
    CHECK(state.pci.vendor_id == 0x102bu);
    CHECK(state.pci.device_id == 0x051bu);

    bar.physical_base = 0xe0000000ul;
    bar.aperture_bytes = 16ul * 1024ul * 1024ul;
    bar.flags = V9X_PCI_BAR_MEMORY | V9X_PCI_BAR_PREFETCHABLE;
    CHECK(ops->bind_framebuffer(&state, &bar, 8ul * 1024ul * 1024ul, 0ul) ==
          V9X_STATUS_OK);
    CHECK(state.vram_bytes == 8ul * 1024ul * 1024ul);
    CHECK(state.capabilities == 0ul);

    request.width = 1024u;
    request.height = 768u;
    request.bits_per_pixel = 16u;
    request.pitch_alignment = 8u;
    request.framebuffer_bytes = 1ul;
    CHECK(ops->validate_mode(&state, &request, &layout) == V9X_STATUS_OK);
    CHECK(layout.pitch_bytes == 2048ul);

    pci.device_id = 0x051au; /* Mystique is a separate future backend. */
    CHECK(v9x_backend_for_pci(&pci) == 0);
    CHECK(v9x_matrox_millennium2_probe(&state, &pci) ==
          V9X_STATUS_UNSUPPORTED);
    CHECK(state.initialized == V9X_FALSE);

    pci.vendor_id = V9X_PCI_VENDOR_S3;
    pci.device_id = V9X_PCI_DEVICE_VIRGE_DX;
    CHECK(v9x_backend_for_pci(&pci) == v9x_s3_virge_backend());
    CHECK(v9x_backend_for_pci(0) == 0);
}

static void test_components_and_log(void)
{
    struct capture_sink sink;
    struct v9x_logger logger;
    struct v9x_backend_state backend;
    struct v9x_component_state display;
    struct v9x_component_state minivdd;

    memset(&sink, 0, sizeof(sink));
    memset(&backend, 0, sizeof(backend));
    memset(&display, 0, sizeof(display));
    memset(&minivdd, 0, sizeof(minivdd));
    v9x_log_init(&logger, capture_log, &sink);

    CHECK(v9x_display16_start(&display, &logger, &backend) == V9X_STATUS_OK);
    CHECK(v9x_display16_start(&display, &logger, &backend) ==
          V9X_STATUS_INVALID_STATE);
    CHECK(v9x_minivdd32_start(&minivdd, &logger, &backend) == V9X_STATUS_OK);
    CHECK(v9x_display16_stop(&display) == V9X_STATUS_OK);
    CHECK(v9x_minivdd32_stop(&minivdd) == V9X_STATUS_OK);

    CHECK(sink.count == 4u);
    CHECK(sink.records[0].magic == V9X_LOG_MAGIC);
    CHECK(sink.records[0].size == 32u);
    CHECK(sink.records[0].sequence == 0ul);
    CHECK(sink.records[1].sequence == 1ul);
    CHECK(sink.records[0].argument0 == V9X_COMPONENT_DISPLAY16);
    CHECK(sink.records[1].argument0 == V9X_COMPONENT_MINIVDD32);
}

static void append_char(char *buffer, unsigned int capacity,
                        unsigned int *at, char value)
{
    if (*at < capacity) {
        buffer[(*at)++] = value;
    }
}

static void append_decimal(char *buffer, unsigned int capacity,
                           unsigned int *at, unsigned int value)
{
    char digits[12];
    unsigned int count = 0u;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count-- != 0u) {
        append_char(buffer, capacity, at, digits[count]);
    }
}

static void test_build_identity(void)
{
    const struct v9x_build_identity *identity = v9x_get_build_identity();
    char expected[32];
    unsigned int at;

    CHECK(identity != 0);
    CHECK(identity->major == V9X_VERSION_MAJOR);
    CHECK(identity->minor == V9X_VERSION_MINOR);
    CHECK(identity->patch == V9X_VERSION_PATCH);
    CHECK(identity->build_id != 0);
    CHECK(identity->build_id[0] != '\0');

    /*
     * The numbers and the string are separate defines, and the build scripts
     * read only the string while the driver reports only the numbers. Nothing
     * else would notice them drifting apart, so this composes one from the
     * other.
     *
     * Built by hand rather than with sprintf: this suite is also compiled by
     * MSVC at /W4 /WX, which rejects sprintf outright.
     */
    at = 0u;
    append_decimal(expected, sizeof(expected), &at, V9X_VERSION_MAJOR);
    append_char(expected, sizeof(expected), &at, '.');
    append_decimal(expected, sizeof(expected), &at, V9X_VERSION_MINOR);
    append_char(expected, sizeof(expected), &at, '.');
    append_decimal(expected, sizeof(expected), &at, V9X_VERSION_PATCH);
    append_char(expected, sizeof(expected), &at, '\0');
    CHECK(strcmp(expected, V9X_VERSION_STRING) == 0);
}


/*
 * --emit-intel-3d-stream: print the Phase 4 and Phase 5 streams and their
 * CRCs, from the COMPILED BUILDERS.
 *
 * This exists so scripts\gen-intel-3d-stream.ps1 has a source of truth that is
 * the same code the driver runs, rather than a fourth PowerShell
 * reimplementation. The layout move at step 2 found three hand-maintained
 * copies of one CRC stale at once; this is what stops that recurring.
 *
 * Output is deliberately flat and machine-readable - one KEY=VALUE per line,
 * hex without a prefix - so the renderer does no parsing worth the name.
 */
static void emit_dword_table(const char *prefix, v9x_u32 base,
                             const v9x_u32 *stream,
                             v9x_u32 count)
{
    v9x_u32 index;
    for (index = 0ul; index < count; ++index) {
        printf("%s%04X=%08lX\n", prefix,
               (unsigned int)(base + index),
               (unsigned long)stream[index]);
    }
}


/*
 * The software reference for Phase 5's triangle.
 *
 * src\display32\d3d\d3d_raster.c is the CPU rasteriser this project already
 * has, already host-tested, and already sampling at (x + 0.5, y + 0.5) - which
 * is the same pixel-centre convention the hardware's DSTORG half-pixel bias
 * selects (docs\decisions\2026-09-14-intel-gen3-3d-packet-audit.md section 4).
 * That correspondence is why a software reference can agree with this hardware
 * at all, and it is the reason to use this rasteriser rather than write a
 * fourth point-in-triangle test.
 *
 * It runs HOST-SIDE ONLY. Nothing here goes near the netbook: the driver
 * reports what the GPU produced and this says what it should have been, and
 * the comparison happens in the validator where a disagreement is cheap.
 *
 * The comparison is REPORTED, not failed, until a golden is promoted - the
 * plan is explicit about that, and so is the validator. A one-pixel band along
 * the edges is licensed to differ, because the audit did not establish the
 * hardware's fill rule and this rasteriser's is its own.
 */
static void emit_intel_3d_reference(void)
{
    static v9x_u16 pixels[V9X_I9XX_TARGET_WIDTH * V9X_I9XX_TARGET_HEIGHT];
    V9X_D3D_RASTER_TARGET target;
    V9X_D3D_RASTER_VERTEX vertices[3];
    v9x_u32 index;
    v9x_u32 row;
    v9x_u32 column;
    v9x_u16 software_color;
    v9x_u16 intel_color;

    /* The fill first, exactly as the sequencer fills before drawing. */
    for (index = 0ul;
         index < V9X_I9XX_TARGET_WIDTH * V9X_I9XX_TARGET_HEIGHT; ++index) {
        pixels[index] = (v9x_u16)V9X_I9XX_FILL_RGB565;
    }

    target.pixels = pixels;
    target.pitch = V9X_I9XX_TARGET_PITCH;
    target.width = V9X_I9XX_TARGET_WIDTH;
    target.height = V9X_I9XX_TARGET_HEIGHT;
    target.format = V9X_D3D_RASTER_PIXFMT_RGB565;

    for (index = 0ul; index < 3ul; ++index) {
        vertices[index].z = 0;
        vertices[index].u = 0;
        vertices[index].v = 0;
        /* The same colour at every vertex, which is what makes flat versus
         * smooth shading moot on the hardware and makes the interpolator
         * here produce a flat fill rather than a gradient. */
        /*
         * Derived from V9X_I9XX_TRI_COLOR_BGRA, never typed. These were
         * literal 0xf8/0x64/0x28 until 2026-09-15, so changing the
         * triangle colour left the generated reference describing the
         * old one - the same defect as the packet offsets, a value
         * hand-copied from its source and then not following it.
         */
        vertices[index].red =
            (v9x_u8)((V9X_I9XX_TRI_COLOR_BGRA >> 16) & 0xfful);
        vertices[index].green =
            (v9x_u8)((V9X_I9XX_TRI_COLOR_BGRA >> 8) & 0xfful);
        vertices[index].blue =
            (v9x_u8)(V9X_I9XX_TRI_COLOR_BGRA & 0xfful);
        vertices[index].alpha = 0xff;
    }
    /*
     * The rasteriser takes SUBPIXEL coordinates - four fractional bits, so
     * sixteen units per pixel. Passing whole pixels draws the triangle at
     * one sixteenth scale in the corner, which is what happened first and
     * which every probe then missed. The hardware takes floats in whole
     * pixels; this is the one place the two conventions differ, so the
     * shift is explicit rather than folded into the constants.
     */
    vertices[0].x = V9X_I9XX_TRI_X0 << V9X_D3D_RASTER_SUBPIXEL_BITS;
    vertices[0].y = V9X_I9XX_TRI_Y0 << V9X_D3D_RASTER_SUBPIXEL_BITS;
    vertices[1].x = V9X_I9XX_TRI_X1 << V9X_D3D_RASTER_SUBPIXEL_BITS;
    vertices[1].y = V9X_I9XX_TRI_Y1 << V9X_D3D_RASTER_SUBPIXEL_BITS;
    vertices[2].x = V9X_I9XX_TRI_X2 << V9X_D3D_RASTER_SUBPIXEL_BITS;
    vertices[2].y = V9X_I9XX_TRI_Y2 << V9X_D3D_RASTER_SUBPIXEL_BITS;

    if (v9x_d3d_raster_triangle(&target, 0, 0, 0, vertices) == 0) {
        printf("REFERROR=raster\n");
        return;
    }

    software_color = v9x_d3d_raster_rgb565(
        (v9x_u8)((V9X_I9XX_TRI_COLOR_BGRA >> 16) & 0xfful),
        (v9x_u8)((V9X_I9XX_TRI_COLOR_BGRA >> 8) & 0xfful),
        (v9x_u8)(V9X_I9XX_TRI_COLOR_BGRA & 0xfful));
    intel_color = v9x_i9xx_rgb565_round(
        (V9X_I9XX_TRI_COLOR_BGRA >> 16) & 0xfful,
        (V9X_I9XX_TRI_COLOR_BGRA >> 8) & 0xfful,
        V9X_I9XX_TRI_COLOR_BGRA & 0xfful);

    printf("REFFILL=%08lX\n", (unsigned long)V9X_I9XX_FILL_RGB565);
    printf("REFCOLOR=%08lX\n", (unsigned long)software_color);
    /*
     * What this chip is measured to store for the same triangle
     * colour. Both are published, so a capture that differs from the
     * software rasteriser can be told apart from one that differs from
     * the hardware - the first is the known and documented conversion
     * difference, the second is a regression.
     */
    printf("REFICOLOR=%08lX\n", (unsigned long)intel_color);

    /*
     * The fourteen named probes the capture reports, at the same coordinates
     * intel_3d16.c samples. Emitted as the 16-bit pixel the reference produced,
     * so the validator compares like with like rather than re-deriving
     * "inside" from geometry.
     */
    for (index = 0ul; index < 14ul; ++index) {
        static const v9x_u16 probe_x[14] = {
            320u, 175u, 465u, 320u, 320u, 250u, 390u,
            0u, 639u, 0u, 639u, 320u, 40u, 600u
        };
        static const v9x_u16 probe_y[14] = {
            213u, 128u, 128u, 385u, 130u, 250u, 250u,
            0u, 0u, 479u, 479u, 40u, 400u, 400u
        };
        v9x_u16 pixel = pixels[(v9x_u32)probe_y[index] *
                               V9X_I9XX_TARGET_WIDTH +
                               (v9x_u32)probe_x[index]];

        printf("REFPX%04X=%08lX\n", (unsigned int)index,
               (unsigned long)pixel);
        /*
         * The same probe under the conversion the Intel colour backend
         * was measured to use. COVERAGE comes from the rasteriser and
         * only the colour is substituted: the two disagree about how a
         * byte becomes a 565 level, not about which pixels the triangle
         * covers, and re-deciding "inside" here from geometry would be
         * a second rasteriser to get wrong.
         *
         * The fill needs no conversion - V9X_I9XX_FILL_RGB565 is already
         * a 565 value and is handed to the blitter as one - so an
         * outside probe passes through unchanged and the two references
         * agree there.
         */
        printf("REFIPX%04X=%08lX\n", (unsigned int)index,
               (unsigned long)(pixel == software_color ?
                               intel_color : pixel));
    }

    /* Row checksums, folded exactly as the sequencer folds them: one XOR of
     * every dword across the row. */
    for (row = 0ul; row < V9X_I9XX_TARGET_HEIGHT; ++row) {
        v9x_u32 crc = 0xfffffffful;
        for (column = 0ul; column < V9X_I9XX_TARGET_WIDTH; column += 2ul) {
            v9x_u32 pair =
                (v9x_u32)pixels[row * V9X_I9XX_TARGET_WIDTH + column] |
                ((v9x_u32)pixels[row * V9X_I9XX_TARGET_WIDTH + column + 1ul]
                 << 16);
            crc ^= pair;
        }
        printf("REFR%04X=%08lX\n", (unsigned int)row, (unsigned long)crc);
    }
}

/*
 * Every Phase 6 scene, in execution order.
 *
 * The scene table is the single source of truth for what a build draws, so
 * the mini-VDD tables, the arm CRCs and the validator all come from HERE
 * rather than from four hand-maintained copies - which is what Phase 4 had,
 * and three of them were stale.
 *
 * Probe coordinates and expectations are emitted with the streams because the
 * capture validator has to know what each scene was asking before it can say
 * whether the answer is a result or a regression.
 */
static int emit_intel_scenes(void)
{
    struct v9x_i9xx_scene scene;
    v9x_u32 stream[160];
    v9x_u32 written;
    v9x_u32 index;
    v9x_u32 probe;
    char prefix[16];

    printf("SCENECOUNT=%04X\n", (unsigned int)v9x_i9xx_scene_count());
    printf("SCENEAUTHORISED=%04X\n",
           (unsigned int)v9x_i9xx_scene_authorised_draws());
    printf("SCENECOMBINEDCRC=%08lX\n",
           (unsigned long)v9x_i9xx_scene_combined_crc());
    printf("SCENETOTALPROBES=%04X\n",
           (unsigned int)v9x_i9xx_scene_total_probes());
    /*
     * The four texture quadrant colours, for the capture validator.
     *
     * A probe expecting a quadrant is compared against one of these. Emitted
     * rather than written into the validator, which would be a second place
     * for four constants to drift from the blits that paint them.
     */
    for (index = 0ul; index < 4ul; ++index) {
        printf("TEXQ%04X=%04X\n", (unsigned int)index,
               (unsigned int)v9x_i9xx_texture_quadrant_color(index));
    }

    for (index = 0ul; index < v9x_i9xx_scene_count(); ++index) {
        if (v9x_i9xx_scene_at(index, &scene) != V9X_STATUS_OK) {
            printf("SCENEERROR=%04X\n", (unsigned int)index);
            return 1;
        }
        written = 0ul;
        if (v9x_i9xx_build_scene_stream(&scene, stream, 160ul, &written) !=
                V9X_STATUS_OK) {
            printf("SCENEERROR=%04X\n", (unsigned int)index);
            return 1;
        }
        printf("SC%04XID=%04X\n", (unsigned int)index,
               (unsigned int)scene.id);
        printf("SC%04XCOUNT=%04X\n", (unsigned int)index,
               (unsigned int)written);
        printf("SC%04XCRC=%08lX\n", (unsigned int)index,
               (unsigned long)v9x_i9xx_scene_crc(index));
        printf("SC%04XTRIS=%04X\n", (unsigned int)index,
               (unsigned int)scene.triangle_count);
        /* Whether the scene paints and samples a texture. The validator needs
         * it to know which scenes owe a texture-guard reading and which must
         * not carry one. */
        printf("SC%04XTEXTURED=%04X\n", (unsigned int)index,
               (unsigned int)scene.textured);
        /*
         * Each triangle's colour as this chip is MEASURED to store it. The
         * validator needs it to compare a probe that expected a triangle
         * against what the probe read; without it those probes were parsed
         * and never checked, which is the same defect as not reading them.
         */
        for (probe = 0ul; probe < scene.triangle_count; ++probe) {
            printf("SC%04XT%04XCOLOR=%04X\n", (unsigned int)index,
                   (unsigned int)probe,
                   (unsigned int)v9x_i9xx_rgb565_round(
                       (scene.triangles[probe].color >> 16) & 0xfful,
                       (scene.triangles[probe].color >> 8) & 0xfful,
                       scene.triangles[probe].color & 0xfful));
            /* Whether that 565 value has been OBSERVED on this chip or is
             * a prediction the boot exists to test. The validator fails on
             * the first and reports the second. */
            printf("SC%04XT%04XMEASURED=%04X\n", (unsigned int)index,
                   (unsigned int)probe,
                   (unsigned int)scene.triangles[probe].color_measured);
        }
        printf("SC%04XPROBES=%04X\n", (unsigned int)index,
               (unsigned int)scene.probe_count);
        printf("SC%04XPRIM=%04X\n", (unsigned int)index,
               (unsigned int)v9x_i9xx_scene_primitive_offset(&scene));
        for (probe = 0ul; probe < scene.probe_count; ++probe) {
            printf("SC%04XP%04XNAME=%s\n", (unsigned int)index,
                   (unsigned int)probe, scene.probes[probe].name);
            printf("SC%04XP%04XX=%04X\n", (unsigned int)index,
                   (unsigned int)probe, (unsigned int)scene.probes[probe].x);
            printf("SC%04XP%04XY=%04X\n", (unsigned int)index,
                   (unsigned int)probe, (unsigned int)scene.probes[probe].y);
            printf("SC%04XP%04XEXPECT=%04X\n", (unsigned int)index,
                   (unsigned int)probe,
                   (unsigned int)scene.probes[probe].expect);
        }
        sprintf(prefix, "SC%04X", (unsigned int)index);
        emit_dword_table(prefix, 0ul, stream, written);
    }
    return 0;
}

static int emit_intel_3d_stream(void)
{
    struct v9x_i9xx_sandbox_layout layout;
    struct v9x_i9xx_phase5_parameters parameters;
    v9x_u32 phase5[160];
    v9x_u32 probe[2];
    v9x_u32 blt[8];
    v9x_u32 written = 0ul;
    v9x_u32 phase4_crc;
    v9x_u32 phase5_crc;

    if (v9x_i9xx_sandbox_calculate(0x007b0000ul, 0x7f800000ul, &layout) !=
            V9X_STATUS_OK) {
        printf("ERROR=layout\n");
        return 1;
    }
    v9x_i9xx_phase5_parameters(&parameters);

    /* Phase 4's stream, so its revised constants are generated from its own
     * compiled builder rather than re-typed after the layout move. */
    if (v9x_i9xx_build_mi_probe(probe, 2ul, &written) != V9X_STATUS_OK ||
        written != 2ul) {
        printf("ERROR=probe\n");
        return 1;
    }
    if (v9x_i9xx_build_color_blt(
            layout.scratch_offset + 0x100ul, 8u, 8u, 32u, 0x55aa33ccul,
            layout.scratch_offset, layout.scratch_bytes,
            blt, 6ul, &written) != V9X_STATUS_OK || written != 6ul) {
        printf("ERROR=blt\n");
        return 1;
    }
    blt[6] = V9X_I9XX_MI_FLUSH;
    blt[7] = V9X_I9XX_MI_NOOP;
    phase4_crc = v9x_i9xx_phase4_execution_crc(probe, blt);

    if (v9x_i9xx_build_phase5_stream(phase5, 160ul, &written) !=
            V9X_STATUS_OK) {
        printf("ERROR=phase5\n");
        return 1;
    }
    phase5_crc = v9x_i9xx_phase5_execution_crc();

    printf("SCHEMA=1\n");
    printf("RESERVEOFFSET=%08lX\n", (unsigned long)layout.reserve_offset);
    printf("RINGSTART=%08lX\n", (unsigned long)layout.ring_offset);
    printf("SCRATCHOFFSET=%08lX\n", (unsigned long)layout.scratch_offset);
    printf("TARGETOFFSET=%08lX\n", (unsigned long)layout.target_offset);
    printf("TARGETPITCH=%08lX\n", (unsigned long)layout.target_pitch);
    printf("TARGETBYTES=%08lX\n", (unsigned long)layout.target_bytes);
    printf("GUARDUPPER=%08lX\n", (unsigned long)layout.guard_upper_offset);
    printf("FILLWORD=%08lX\n", (unsigned long)parameters.fill_word);
    printf("TRICOLOR=%08lX\n", (unsigned long)parameters.triangle_color);
    printf("P4COUNT=%04X\n", 10u);
    emit_dword_table("P4", 0ul, probe, 2ul);
    emit_dword_table("P4", 2ul, blt, 8ul);
    /*
     * Two Phase 4 CRCs, because they cover different things. The PACKET
     * CRC is over the ten staged dwords and is recomputable from the
     * table, which is what lets check-tree verify the generated file with
     * no compiler. The EXECUTION CRC additionally covers the full-ring
     * NOOP wrap the mini-VDD submits between the probes, so it cannot be
     * derived from the table at all - only the compiled builder knows it.
     */
    {
        v9x_u32 packet[10];
        v9x_u32 copy;
        for (copy = 0ul; copy < 2ul; ++copy) { packet[copy] = probe[copy]; }
        for (copy = 0ul; copy < 8ul; ++copy) {
            packet[copy + 2ul] = blt[copy];
        }
        printf("P4PACKETCRC=%08lX\n",
               (unsigned long)v9x_i9xx_crc32_dwords(packet, 10ul));
    }
    printf("P4CRC=%08lX\n", (unsigned long)phase4_crc);
    printf("P5COUNT=%04X\n", (unsigned int)written);
    emit_dword_table("P5", 0ul, phase5, written);
    printf("P5CRC=%08lX\n", (unsigned long)phase5_crc);
    /* The dword the _3DPRIMITIVE starts at. The executor stops between its
     * two submissions exactly here. */
    /* The one computation, not a fourth copy of the sum. */
    printf("P5PRIM=%04X\n",
           (unsigned int)v9x_i9xx_phase5_primitive_offset());
    printf("COMBINEDCRC=%08lX\n",
           (unsigned long)v9x_i9xx_combined_arm_crc(phase4_crc, phase5_crc));
    /*
     * What a Phase 6 arm token must carry: the Phase 4 replay and every
     * scene in execution order, combined exactly as the Phase 5 token
     * combines the replay and its single draw. Emitted rather than left for
     * the packaging script to compute, so the armer and the driver cannot
     * derive it independently and disagree.
     */
    printf("SCENEARMCRC=%08lX\n",
           (unsigned long)v9x_i9xx_combined_arm_crc(
               v9x_i9xx_phase4_execution_crc(probe, blt),
               v9x_i9xx_scene_combined_crc()));
    emit_intel_3d_reference();
    return emit_intel_scenes();
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--emit-intel-3d-stream") == 0) {
        return emit_intel_3d_stream();
    }
    (void)argc;
    (void)argv;
    test_mode_layout();
    test_mode_layout_rejects_bad_arguments();
    test_mode_layout_overflow();
    test_mode_layout_properties();
    test_framebuffer_resource_validation();
    test_framebuffer_resource_properties();
    test_probe_is_strict();
    test_s3_virge_clock_decode();
    test_s3_virge_memory_decode();
    test_backend_registry_and_millennium2();
    test_components_and_log();
    test_build_identity();
    failures += v9x_run_family_matrix_tests();
    failures += v9x_run_hw16_mode_tests();
    failures += v9x_run_vbe_parse_tests();
    failures += v9x_run_vbe_modes_tests();
    failures += v9x_run_vbe_cache_tests();
    failures += v9x_run_edid_tests();
    failures += v9x_run_mtrr_tests();
    failures += v9x_run_d3dmode_tests();
    failures += v9x_run_vbe_crtc_tests();
#ifdef __WATCOMC__
    failures += v9x_run_d3d_zfixed_tests();
#else
    puts("SKIP: ViRGE x87 depth conversion (requires Open Watcom)");
#endif
    failures += v9x_run_d3d_raster_tests();
    failures += v9x_run_donewait_tests();
    failures += v9x_run_i9xx_mmio_tests();
    failures += v9x_run_i9xx_gtt_tests();
    failures += v9x_run_i9xx_ring_tests();
    failures += v9x_run_i9xx_arm_tests();
    failures += v9x_run_i9xx_3d_tests();

    if (failures != 0u) {
        printf("%u host test(s) failed\n", failures);
        return 1;
    }
    puts("Velocity9x host tests passed");
    return 0;
}
