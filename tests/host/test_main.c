#include <stdio.h>
#include <string.h>

#include "velocity9x/build.h"
#include "velocity9x/components.h"
#include "velocity9x/backend_registry.h"
#include "velocity9x/matrox_millennium2.h"
#include "velocity9x/s3_virge.h"

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
 * becoming the x87 integer indefinite, and with it the near plane. */
unsigned int v9x_run_d3d_zfixed_tests(void);

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

int main(void)
{
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
    failures += v9x_run_d3d_zfixed_tests();

    if (failures != 0u) {
        printf("%u host test(s) failed\n", failures);
        return 1;
    }
    puts("Velocity9x host tests passed");
    return 0;
}
