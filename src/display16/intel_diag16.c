#define SetCursor V9xUserSetCursor
#include <windows.h>
#undef SetCursor

#include "velocity9x/diagpaths.h"
#include "velocity9x/intel_gma.h"
#include "velocity9x/intel16.h"

DWORD v9x_i9xx_first[V9X_I9XX_SNAPSHOT_DWORDS];
DWORD v9x_i9xx_second[V9X_I9XX_SNAPSHOT_DWORDS];
DWORD v9x_i9xx_offset[V9X_I9XX_SNAPSHOT_DWORDS];
DWORD v9x_i9xx_bar0;

extern WORD FAR PASCAL V9xPciReadIntelMmioBar(DWORD FAR *base);
extern WORD FAR PASCAL V9xMiniI9xxCapture(DWORD base);
extern void FAR PASCAL V9xEnsureDiagDir(void);
extern unsigned long v9x_vbe_vram_reported;

static const DWORD v9x_expected_offsets[V9X_I9XX_SNAPSHOT_DWORDS] = {
    V9X_I9XX_REG_PGTBL_CTL,
    V9X_I9XX_REG_RING_TAIL, V9X_I9XX_REG_RING_HEAD,
    V9X_I9XX_REG_RING_START, V9X_I9XX_REG_RING_CTL, V9X_I9XX_REG_HWS_PGA,
    V9X_I9XX_REG_PIPEA_CONF, V9X_I9XX_REG_PIPEA_HTOTAL,
    V9X_I9XX_REG_PIPEA_VTOTAL, V9X_I9XX_REG_PIPEA_SRC,
    V9X_I9XX_REG_DSPA_CNTR, V9X_I9XX_REG_DSPA_ADDR,
    V9X_I9XX_REG_DSPA_STRIDE,
    V9X_I9XX_REG_PIPEB_CONF, V9X_I9XX_REG_PIPEB_HTOTAL,
    V9X_I9XX_REG_PIPEB_VTOTAL, V9X_I9XX_REG_PIPEB_SRC,
    V9X_I9XX_REG_DSPB_CNTR, V9X_I9XX_REG_DSPB_ADDR,
    V9X_I9XX_REG_DSPB_STRIDE
};

static void v9x_hex32(char *text, DWORD value)
{
    static const char digits[] = "0123456789ABCDEF";
    short shift;
    for (shift = 28; shift >= 0; shift -= 4) {
        text[(28 - shift) / 4] = digits[(value >> shift) & 0x0ful];
    }
    text[8] = '\0';
}

static void v9x_hex_key(char *key, WORD index, char suffix)
{
    static const char digits[] = "0123456789ABCDEF";
    key[0] = 'R';
    key[1] = digits[(index >> 4) & 0x0fu];
    key[2] = digits[index & 0x0fu];
    key[3] = suffix;
    key[4] = '\0';
}

static void v9x_write_hex(const char *key, DWORD value)
{
    char text[9];
    v9x_hex32(text, value);
    WritePrivateProfileString("IntelMmio", key, text, V9X_DIAG_INTELMM_TXT);
}

static void v9x_unpack(struct v9x_i9xx_mmio_snapshot *snapshot,
                       const DWORD *raw)
{
    snapshot->pgtbl_ctl = raw[0];
    snapshot->ring_tail = raw[1];
    snapshot->ring_head = raw[2];
    snapshot->ring_start = raw[3];
    snapshot->ring_ctl = raw[4];
    snapshot->hws_pga = raw[5];
    snapshot->pipe[0].pipe_conf = raw[6];
    snapshot->pipe[0].htotal = raw[7];
    snapshot->pipe[0].vtotal = raw[8];
    snapshot->pipe[0].pipe_src = raw[9];
    snapshot->pipe[0].plane_control = raw[10];
    snapshot->pipe[0].plane_address = raw[11];
    snapshot->pipe[0].plane_stride = raw[12];
    snapshot->pipe[1].pipe_conf = raw[13];
    snapshot->pipe[1].htotal = raw[14];
    snapshot->pipe[1].vtotal = raw[15];
    snapshot->pipe[1].pipe_src = raw[16];
    snapshot->pipe[1].plane_control = raw[17];
    snapshot->pipe[1].plane_address = raw[18];
    snapshot->pipe[1].plane_stride = raw[19];
}

void V9X_I9XX_FAR v9x_intel_publish_mmio_fingerprint(void)
{
    struct v9x_i9xx_mmio_snapshot first;
    struct v9x_i9xx_mmio_snapshot second;
    struct v9x_i9xx_mode_expectation expected;
    struct v9x_i9xx_fingerprint result;
    DWORD bar0 = 0ul;
    WORD index;
    WORD width;
    WORD height;
    WORD bpp;
    WORD pitch;
    WORD offsets_ok = 1u;
    char key[5];

    V9xEnsureDiagDir();
    WritePrivateProfileString("IntelMmio", 0, 0, V9X_DIAG_INTELMM_TXT);
    WritePrivateProfileString("IntelMmio", "Access", "read-only",
                              V9X_DIAG_INTELMM_TXT);
    WritePrivateProfileString("IntelMmio", "BarProvenance", "PCI-BAR0-runtime",
                              V9X_DIAG_INTELMM_TXT);

    if (V9xPciReadIntelMmioBar(&bar0) == 0u ||
        V9xMiniI9xxCapture(bar0) == 0u) {
        WritePrivateProfileString("IntelMmio", "Result", "CAPTURE-FAILED",
                                  V9X_DIAG_INTELMM_TXT);
        return;
    }
    v9x_write_hex("Bar0", v9x_i9xx_bar0);
    for (index = 0u; index < V9X_I9XX_SNAPSHOT_DWORDS; ++index) {
        if (v9x_i9xx_offset[index] != v9x_expected_offsets[index]) {
            offsets_ok = 0u;
        }
        v9x_hex_key(key, index, 'O');
        v9x_write_hex(key, v9x_i9xx_offset[index]);
        v9x_hex_key(key, index, 'A');
        v9x_write_hex(key, v9x_i9xx_first[index]);
        v9x_hex_key(key, index, 'B');
        v9x_write_hex(key, v9x_i9xx_second[index]);
        v9x_hex_key(key, index, 'D');
        v9x_write_hex(key, v9x_i9xx_first[index] ^ v9x_i9xx_second[index]);
    }
    if (offsets_ok == 0u ||
        v9x_intel_bridge_mode_geometry(&width, &height, &bpp, &pitch) == 0u) {
        WritePrivateProfileString("IntelMmio", "Result", "CONTRACT-FAILED",
                                  V9X_DIAG_INTELMM_TXT);
        return;
    }

    v9x_unpack(&first, v9x_i9xx_first);
    v9x_unpack(&second, v9x_i9xx_second);
    expected.width = width;
    expected.height = height;
    expected.bits_per_pixel = bpp;
    expected.pitch_bytes = pitch;
    expected.gmadr_aperture_bytes = v9x_vbe_vram_reported != 0ul
        ? v9x_vbe_vram_reported : 16ul * 1024ul * 1024ul;
    if (v9x_i9xx_analyze_fingerprint(&first, &second, &expected, &result) !=
        V9X_STATUS_OK) {
        WritePrivateProfileString("IntelMmio", "Result", "DECODE-FAILED",
                                  V9X_DIAG_INTELMM_TXT);
        return;
    }
    v9x_write_hex("Flags", result.flags);
    v9x_write_hex("LivePipe", result.live_pipe);
    v9x_write_hex("LivePlane", result.live_plane);
    v9x_write_hex("TimingWidth", result.timing_width);
    v9x_write_hex("TimingHeight", result.timing_height);
    v9x_write_hex("TotalWidth", result.total_width);
    v9x_write_hex("TotalHeight", result.total_height);
    v9x_write_hex("SourceWidth", result.source_width);
    v9x_write_hex("SourceHeight", result.source_height);
    v9x_write_hex("PlaneBpp", result.plane_bits_per_pixel);
    v9x_write_hex("PlaneStride", result.plane_stride);
    v9x_write_hex("PlaneAddress", result.plane_address);
    WritePrivateProfileString(
        "IntelMmio", "Result",
        (result.flags & V9X_I9XX_FP_PHASE1_REQUIRED) ==
                V9X_I9XX_FP_PHASE1_REQUIRED ? "PASS" : "REVIEW",
        V9X_DIAG_INTELMM_TXT);
}
