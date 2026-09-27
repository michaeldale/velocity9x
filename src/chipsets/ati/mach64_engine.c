#include "velocity9x/ati_mach64_engine.h"

static v9x_u32 v9x_m64_popcount16(v9x_u32 value)
{
    v9x_u32 count = 0ul;
    value &= 0xfffful;
    while (value != 0ul) {
        value &= value - 1ul;
        ++count;
    }
    return count;
}

v9x_u32 v9x_m64_fifo_free(v9x_u16 fifo_model, v9x_u32 status)
{
    if (fifo_model == V9X_M64_FIFO_VTB_PLUS) {
        return (status & V9X_M64_GUI_FIFO_MASK) >> V9X_M64_GUI_FIFO_SHIFT;
    }
    if ((status & V9X_M64_FIFO_ERR) != 0ul) {
        return 0ul;
    }
    return V9X_M64_VT_FIFO_ENTRIES - v9x_m64_popcount16(status);
}

v9x_status v9x_m64_engine_init(struct v9x_m64_engine *engine,
                               const struct v9x_m64_io *io,
                               v9x_u16 fifo_model)
{
    v9x_u32 index;
    if (engine == 0 || io == 0 || io->read == 0 || io->write == 0 ||
        fifo_model > V9X_M64_FIFO_VTB_PLUS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    engine->io = *io;
    engine->fifo_model = fifo_model;
    engine->quarantined = V9X_FALSE;
    engine->fifo_cached = 0ul;
    engine->fifo_reads = 0ul;
    engine->register_writes = 0ul;
    engine->fifo_timeouts = 0ul;
    engine->idle_timeouts = 0ul;
    engine->reset_count = 0ul;
    engine->shadow_count = 0ul;
    for (index = 0ul; index < V9X_M64_SHADOW_ENTRIES; ++index) {
        engine->shadow[index].offset = 0ul;
        engine->shadow[index].value = 0ul;
    }
    return V9X_STATUS_OK;
}

static v9x_u32 v9x_m64_status_offset(const struct v9x_m64_engine *engine)
{
    return engine->fifo_model == V9X_M64_FIFO_VTB_PLUS
        ? V9X_M64_GUI_STAT : V9X_M64_FIFO_STAT;
}

v9x_status v9x_m64_reserve(struct v9x_m64_engine *engine,
                           v9x_u32 entries, v9x_u32 spin_limit)
{
    v9x_u32 status;
    if (engine == 0 || entries == 0ul || engine->quarantined) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if ((engine->fifo_model == V9X_M64_FIFO_PRE_VTB && entries > 16ul) ||
        (engine->fifo_model == V9X_M64_FIFO_VTB_PLUS && entries > 1023ul)) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (engine->fifo_cached >= entries) {
        engine->fifo_cached -= entries;
        return V9X_STATUS_OK;
    }
    do {
        status = engine->io.read(engine->io.context,
                                 v9x_m64_status_offset(engine));
        ++engine->fifo_reads;
        if (engine->fifo_model == V9X_M64_FIFO_PRE_VTB &&
            (status & V9X_M64_FIFO_ERR) != 0ul) {
            engine->quarantined = V9X_TRUE;
            return V9X_STATUS_INVALID_STATE;
        }
        engine->fifo_cached = v9x_m64_fifo_free(engine->fifo_model, status);
        if (engine->fifo_cached >= entries) {
            engine->fifo_cached -= entries;
            return V9X_STATUS_OK;
        }
    } while (spin_limit-- != 0ul);
    ++engine->fifo_timeouts;
    return V9X_STATUS_TIMEOUT;
}

static void v9x_m64_shadow(struct v9x_m64_engine *engine,
                           v9x_u32 offset, v9x_u32 value)
{
    v9x_u32 index;
    /* Replaying a trigger would execute the old operation again. */
    if (offset == V9X_M64_DST_HEIGHT_WIDTH) return;
    for (index = 0ul; index < engine->shadow_count; ++index) {
        if (engine->shadow[index].offset == offset) {
            engine->shadow[index].value = value;
            return;
        }
    }
    if (engine->shadow_count < V9X_M64_SHADOW_ENTRIES) {
        engine->shadow[engine->shadow_count].offset = offset;
        engine->shadow[engine->shadow_count].value = value;
        ++engine->shadow_count;
    }
}

v9x_status v9x_m64_emit_batch(struct v9x_m64_engine *engine,
                              const v9x_u32 *offsets,
                              const v9x_u32 *values,
                              v9x_u32 count, v9x_u32 spin_limit)
{
    v9x_u32 index;
    v9x_status status;
    if (engine == 0 || offsets == 0 || values == 0 || count == 0ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_m64_reserve(engine, count, spin_limit);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    /* No status read is permitted inside this loop. */
    for (index = 0ul; index < count; ++index) {
        engine->io.write(engine->io.context, offsets[index], values[index]);
        ++engine->register_writes;
        v9x_m64_shadow(engine, offsets[index], values[index]);
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_m64_wait_idle(struct v9x_m64_engine *engine,
                             v9x_u32 spin_limit)
{
    v9x_u32 status;
    if (engine == 0 || engine->quarantined) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    do {
        status = engine->io.read(engine->io.context,
                                 v9x_m64_status_offset(engine));
        ++engine->fifo_reads;
        if (engine->fifo_model == V9X_M64_FIFO_VTB_PLUS) {
            if ((status & V9X_M64_GUI_ACTIVE) == 0ul) {
                engine->fifo_cached = v9x_m64_fifo_free(
                    engine->fifo_model, status);
                return V9X_STATUS_OK;
            }
        } else {
            if ((status & V9X_M64_FIFO_ERR) != 0ul) {
                engine->quarantined = V9X_TRUE;
                return V9X_STATUS_INVALID_STATE;
            }
            if ((status & 0xfffful) == 0ul) {
                engine->fifo_cached = V9X_M64_VT_FIFO_ENTRIES;
                return V9X_STATUS_OK;
            }
        }
    } while (spin_limit-- != 0ul);
    ++engine->idle_timeouts;
    return V9X_STATUS_TIMEOUT;
}

v9x_status v9x_m64_cpu_read_barrier(struct v9x_m64_engine *engine,
                                    v9x_u32 spin_limit)
{
    v9x_u32 value;
    v9x_status status = v9x_m64_wait_idle(engine, spin_limit);
    if (status != V9X_STATUS_OK) {
        return status;
    }
    value = engine->io.read(engine->io.context, V9X_M64_MEM_BUF_CNTL);
    engine->io.write(engine->io.context, V9X_M64_MEM_BUF_CNTL,
                     value | V9X_M64_INVALIDATE_RB_CACHE);
    ++engine->register_writes;
    return V9X_STATUS_OK;
}

v9x_status v9x_m64_reset_replay(struct v9x_m64_engine *engine,
                                v9x_u32 spin_limit)
{
    v9x_u32 bus;
    v9x_u32 test;
    v9x_u32 count;
    v9x_u32 index;
    v9x_status status;
    if (engine == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    bus = engine->io.read(engine->io.context, V9X_M64_BUS_CNTL);
    test = engine->io.read(engine->io.context, V9X_M64_GEN_TEST_CNTL);
    engine->io.write(engine->io.context, V9X_M64_BUS_CNTL,
        (bus & ~V9X_M64_BUS_HOST_ERR_INT_EN) |
        V9X_M64_BUS_HOST_ERR_INT | V9X_M64_BUS_FLUSH_BUF);
    engine->io.write(engine->io.context, V9X_M64_GEN_TEST_CNTL,
                     test & ~V9X_M64_GEN_GUI_RESETB);
    engine->io.write(engine->io.context, V9X_M64_GEN_TEST_CNTL,
                     test | V9X_M64_GEN_GUI_RESETB);
    engine->register_writes += 3ul;
    ++engine->reset_count;
    engine->fifo_cached = 0ul;
    engine->quarantined = V9X_FALSE;

    count = engine->shadow_count;
    for (index = 0ul; index < count; ++index) {
        status = v9x_m64_reserve(engine, 1ul, spin_limit);
        if (status != V9X_STATUS_OK) {
            engine->quarantined = V9X_TRUE;
            return status;
        }
        engine->io.write(engine->io.context, engine->shadow[index].offset,
                         engine->shadow[index].value);
        ++engine->register_writes;
    }
    status = v9x_m64_wait_idle(engine, spin_limit);
    if (status != V9X_STATUS_OK) {
        engine->quarantined = V9X_TRUE;
    }
    return status;
}

v9x_status v9x_m64_build_fill(const struct v9x_m64_fill *fill,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 pitch_pixels;
    v9x_u32 end;
    if (written != 0) *written = 0ul;
    if (fill == 0 || offsets == 0 || values == 0 || written == 0 ||
        capacity < V9X_M64_FILL_DWORDS || fill->vram_bytes == 0ul ||
        (fill->target_offset & 7ul) != 0ul ||
        (fill->target_pitch_bytes & 15ul) != 0ul ||
        fill->target_pitch_bytes == 0ul || fill->target_width == 0ul ||
        fill->target_height == 0ul || fill->left >= fill->right ||
        fill->top >= fill->bottom || fill->right > fill->target_width ||
        fill->bottom > fill->target_height || fill->right > 4096ul ||
        fill->bottom > 16384ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    pitch_pixels = fill->target_pitch_bytes >> 1;
    if ((pitch_pixels & 7ul) != 0ul || (pitch_pixels >> 3) > 1023ul ||
        fill->target_width > pitch_pixels) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (fill->bottom - 1ul >
        (0xfffffffful - fill->target_offset) / fill->target_pitch_bytes) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    end = fill->target_offset +
          (fill->bottom - 1ul) * fill->target_pitch_bytes;
    if (fill->right > (0xfffffffful - end) / 2ul) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    end += fill->right * 2ul;
    if (end > fill->vram_bytes) return V9X_STATUS_INSUFFICIENT_MEMORY;

    offsets[0] = V9X_M64_DST_OFF_PITCH;
    values[0] = ((pitch_pixels >> 3) << 22) |
                (fill->target_offset >> 3);
    offsets[1] = V9X_M64_DST_CNTL; values[1] = 0x00000003ul;
    offsets[2] = V9X_M64_SC_LEFT_RIGHT;
    values[2] = ((fill->target_width - 1ul) << 16);
    offsets[3] = V9X_M64_SC_TOP_BOTTOM;
    values[3] = ((fill->target_height - 1ul) << 16);
    offsets[4] = V9X_M64_DP_FRGD_CLR; values[4] = fill->color;
    offsets[5] = V9X_M64_DP_WRITE_MASK; values[5] = 0xfffffffful;
    offsets[6] = V9X_M64_DP_PIX_WIDTH; values[6] = 0x00040004ul;
    offsets[7] = V9X_M64_DP_MIX; values[7] = 0x00070003ul;
    offsets[8] = V9X_M64_DP_SRC; values[8] = 0x00000100ul;
    offsets[9] = V9X_M64_CLR_CMP_CNTL; values[9] = 0ul;
    offsets[10] = V9X_M64_DST_Y_X;
    values[10] = (fill->left << 16) | fill->top;
    offsets[11] = V9X_M64_DST_HEIGHT_WIDTH;
    values[11] = ((fill->right - fill->left) << 16) |
                 (fill->bottom - fill->top);
    *written = V9X_M64_FILL_DWORDS;
    return V9X_STATUS_OK;
}
