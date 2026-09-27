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
    if (offset == V9X_M64_DST_HEIGHT_WIDTH ||
        offset == V9X_M64_ONE_OVER_AREA) return;
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

v9x_status v9x_m64_build_fill_origin_repair(
                              const struct v9x_m64_fill *fill,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 check_offsets[V9X_M64_FILL_DWORDS];
    v9x_u32 check_values[V9X_M64_FILL_DWORDS];
    v9x_u32 checked = 0ul;
    v9x_u32 pitch_pixels;
    v9x_status status;
    if (written != 0) *written = 0ul;
    if (fill == 0 || offsets == 0 || values == 0 || written == 0) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_m64_build_fill(fill, check_offsets, check_values,
                                V9X_M64_FILL_DWORDS, &checked);
    if (status != V9X_STATUS_OK) return status;
    if (fill->left != 0ul || fill->top != 0ul) return V9X_STATUS_OK;
    if (capacity < V9X_M64_FILL_REPAIR_DWORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    /*
     * The physical LM leaves the first destination word unchanged when a
     * solid fill begins at logical (0,0). Re-address that exact word from an
     * aligned base 16 bytes earlier and x=8. The main fill's scissor includes
     * x=8 only on surfaces at least nine pixels wide; otherwise decline.
     */
    if (fill->target_offset < 16ul || fill->target_width <= 8ul) {
        return V9X_STATUS_UNSUPPORTED;
    }
    pitch_pixels = fill->target_pitch_bytes >> 1;
    offsets[0] = V9X_M64_DST_OFF_PITCH;
    values[0] = ((pitch_pixels >> 3) << 22) |
                ((fill->target_offset - 16ul) >> 3);
    offsets[1] = V9X_M64_DST_Y_X;
    values[1] = 0x00080000ul;
    offsets[2] = V9X_M64_DST_HEIGHT_WIDTH;
    values[2] = 0x00010001ul;
    *written = V9X_M64_FILL_REPAIR_DWORDS;
    return V9X_STATUS_OK;
}

static v9x_status v9x_m64_validate_copy_surface(v9x_u32 vram_bytes,
                                                v9x_u32 offset,
                                                v9x_u32 pitch_bytes,
                                                v9x_u32 surface_width,
                                                v9x_u32 surface_height,
                                                v9x_u32 left,
                                                v9x_u32 top,
                                                v9x_u32 width,
                                                v9x_u32 height)
{
    v9x_u32 pitch_pixels;
    v9x_u32 right;
    v9x_u32 bottom;
    v9x_u32 end;
    if (vram_bytes == 0ul || (offset & 7ul) != 0ul ||
        pitch_bytes == 0ul || (pitch_bytes & 15ul) != 0ul ||
        surface_width == 0ul || surface_height == 0ul ||
        width == 0ul || height == 0ul || left > 4095ul || top > 16383ul ||
        width > 4096ul || height > 16384ul ||
        left > 4096ul - width || top > 16384ul - height) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    right = left + width;
    bottom = top + height;
    pitch_pixels = pitch_bytes >> 1;
    if ((pitch_pixels & 7ul) != 0ul || (pitch_pixels >> 3) > 1023ul ||
        surface_width > pitch_pixels || right > surface_width ||
        bottom > surface_height) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (bottom - 1ul > (0xfffffffful - offset) / pitch_bytes) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    end = offset + (bottom - 1ul) * pitch_bytes;
    if (right > (0xfffffffful - end) / 2ul) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    end += right * 2ul;
    return end > vram_bytes ? V9X_STATUS_INSUFFICIENT_MEMORY : V9X_STATUS_OK;
}

v9x_status v9x_m64_build_copy(const struct v9x_m64_copy *copy,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 source_x;
    v9x_u32 source_y;
    v9x_u32 destination_x;
    v9x_u32 destination_y;
    v9x_u32 direction = V9X_M64_DST_X_DIR | V9X_M64_DST_Y_DIR;
    v9x_status status;
    if (written != 0) *written = 0ul;
    if (copy == 0 || offsets == 0 || values == 0 || written == 0 ||
        capacity < V9X_M64_COPY_DWORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_m64_validate_copy_surface(
        copy->vram_bytes, copy->source_offset, copy->source_pitch_bytes,
        copy->source_width, copy->source_height, copy->source_left,
        copy->source_top, copy->width, copy->height);
    if (status != V9X_STATUS_OK) return status;
    status = v9x_m64_validate_copy_surface(
        copy->vram_bytes, copy->destination_offset,
        copy->destination_pitch_bytes, copy->destination_width,
        copy->destination_height, copy->destination_left,
        copy->destination_top, copy->width, copy->height);
    if (status != V9X_STATUS_OK) return status;

    source_x = copy->source_left;
    source_y = copy->source_top;
    destination_x = copy->destination_left;
    destination_y = copy->destination_top;
    if (copy->source_offset == copy->destination_offset &&
        copy->source_pitch_bytes == copy->destination_pitch_bytes) {
        if (destination_x > source_x && destination_x < source_x + copy->width) {
            direction &= ~V9X_M64_DST_X_DIR;
            source_x += copy->width - 1ul;
            destination_x += copy->width - 1ul;
        }
        if (destination_y > source_y && destination_y < source_y + copy->height) {
            direction &= ~V9X_M64_DST_Y_DIR;
            source_y += copy->height - 1ul;
            destination_y += copy->height - 1ul;
        }
    }

    offsets[0] = V9X_M64_DP_WRITE_MASK; values[0] = 0xfffffffful;
    offsets[1] = V9X_M64_DP_PIX_WIDTH; values[1] = 0x00040404ul;
    offsets[2] = V9X_M64_SRC_OFF_PITCH;
    values[2] = (((copy->source_pitch_bytes >> 1) >> 3) << 22) |
                (copy->source_offset >> 3);
    offsets[3] = V9X_M64_DST_OFF_PITCH;
    values[3] = (((copy->destination_pitch_bytes >> 1) >> 3) << 22) |
                (copy->destination_offset >> 3);
    offsets[4] = V9X_M64_DP_SRC; values[4] = 0x00000300ul;
    offsets[5] = V9X_M64_DP_MIX; values[5] = 0x00070000ul;
    offsets[6] = V9X_M64_CLR_CMP_CNTL; values[6] = 0ul;
    offsets[7] = V9X_M64_DST_CNTL; values[7] = direction;
    offsets[8] = V9X_M64_SC_LEFT_RIGHT;
    values[8] = (copy->destination_width - 1ul) << 16;
    offsets[9] = V9X_M64_SC_TOP_BOTTOM;
    values[9] = (copy->destination_height - 1ul) << 16;
    offsets[10] = V9X_M64_SRC_Y_X;
    values[10] = (source_x << 16) | source_y;
    offsets[11] = V9X_M64_SRC_WIDTH1; values[11] = copy->width;
    offsets[12] = V9X_M64_DST_Y_X;
    values[12] = (destination_x << 16) | destination_y;
    offsets[13] = V9X_M64_DST_HEIGHT_WIDTH;
    values[13] = (copy->width << 16) | copy->height;
    *written = V9X_M64_COPY_DWORDS;
    return V9X_STATUS_OK;
}

static v9x_u32 v9x_m64_float_bits(float value)
{
    union {
        float value;
        v9x_u32 bits;
    } converted;
    converted.value = value;
    return converted.bits;
}

static void v9x_m64_encode_flat_vertex(
                              const struct v9x_m64_point *vertex,
                              v9x_u32 color, const v9x_u32 *registers,
                              v9x_u32 *offsets, v9x_u32 *values)
{
    offsets[0] = registers[0]; values[0] = 0ul;
    offsets[1] = registers[1]; values[1] = 0ul;
    offsets[2] = registers[2]; values[2] = 0x3f800000ul;
    offsets[3] = registers[3]; values[3] = 0x7fff8000ul;
    offsets[4] = registers[4]; values[4] = color;
    offsets[5] = registers[5];
    values[5] = ((vertex->x * 4ul) << 16) | (vertex->y * 4ul);
}

v9x_status v9x_m64_build_flat_triangle(
                              const struct v9x_m64_flat_triangle *triangle,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    static const v9x_u32 vertex_registers[3][6] = {
        { V9X_M64_VERTEX_1_S, V9X_M64_VERTEX_1_T,
          V9X_M64_VERTEX_1_W, V9X_M64_VERTEX_1_Z,
          V9X_M64_VERTEX_1_ARGB, V9X_M64_VERTEX_1_X_Y },
        { V9X_M64_VERTEX_2_S, V9X_M64_VERTEX_2_T,
          V9X_M64_VERTEX_2_W, V9X_M64_VERTEX_2_Z,
          V9X_M64_VERTEX_2_ARGB, V9X_M64_VERTEX_2_X_Y },
        { V9X_M64_VERTEX_3_S, V9X_M64_VERTEX_3_T,
          V9X_M64_VERTEX_3_W, V9X_M64_VERTEX_3_Z,
          V9X_M64_VERTEX_3_ARGB, V9X_M64_VERTEX_3_X_Y }
    };
    v9x_s32 dx1;
    v9x_s32 dy1;
    v9x_s32 dx2;
    v9x_s32 dy2;
    v9x_s32 area;
    v9x_u32 index;
    float one_over_area;
    if (written != 0) *written = 0ul;
    if (triangle == 0 || offsets == 0 || values == 0 || written == 0 ||
        capacity < V9X_M64_FLAT_TRIANGLE_DWORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0ul; index < 3ul; ++index) {
        if (triangle->vertex[index].x > 16383ul ||
            triangle->vertex[index].y > 16383ul) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
    }
    dx1 = (v9x_s32)triangle->vertex[1].x -
          (v9x_s32)triangle->vertex[0].x;
    dy1 = (v9x_s32)triangle->vertex[1].y -
          (v9x_s32)triangle->vertex[0].y;
    dx2 = (v9x_s32)triangle->vertex[2].x -
          (v9x_s32)triangle->vertex[0].x;
    dy2 = (v9x_s32)triangle->vertex[2].y -
          (v9x_s32)triangle->vertex[0].y;
    area = dx1 * dy2 - dy1 * dx2;
    if (area == 0l) return V9X_STATUS_INVALID_ARGUMENT;

    for (index = 0ul; index < 3ul; ++index) {
        v9x_m64_encode_flat_vertex(&triangle->vertex[index],
            triangle->color, vertex_registers[index],
            offsets + index * 6ul, values + index * 6ul);
    }
    one_over_area = 1.0f / (float)area;
    offsets[18] = V9X_M64_ONE_OVER_AREA;
    values[18] = v9x_m64_float_bits(one_over_area);
    *written = V9X_M64_FLAT_TRIANGLE_DWORDS;
    return V9X_STATUS_OK;
}

v9x_status v9x_m64_build_flat_state(
                              const struct v9x_m64_flat_state *state,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 pitch_pixels;
    v9x_u32 last_row;
    v9x_u32 end;
    v9x_u32 off_pitch;
    if (written != 0) *written = 0ul;
    if (state == 0 || offsets == 0 || values == 0 || written == 0 ||
        capacity < V9X_M64_FLAT_STATE_DWORDS || state->vram_bytes == 0ul ||
        (state->target_offset & 7ul) != 0ul ||
        state->target_pitch_bytes == 0ul ||
        (state->target_pitch_bytes & 15ul) != 0ul ||
        state->target_width == 0ul || state->target_height == 0ul ||
        state->scissor_left >= state->scissor_right ||
        state->scissor_top >= state->scissor_bottom ||
        state->scissor_right > state->target_width ||
        state->scissor_bottom > state->target_height ||
        state->target_width > 4096ul || state->target_height > 16384ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    pitch_pixels = state->target_pitch_bytes >> 1;
    if ((pitch_pixels & 7ul) != 0ul || (pitch_pixels >> 3) > 1023ul ||
        state->target_width > pitch_pixels) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    last_row = state->target_height - 1ul;
    if (last_row >
        (0xfffffffful - state->target_offset) /
        state->target_pitch_bytes) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    end = state->target_offset + last_row * state->target_pitch_bytes;
    if (state->target_width > (0xfffffffful - end) / 2ul) {
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    end += state->target_width * 2ul;
    if (end > state->vram_bytes) return V9X_STATUS_INSUFFICIENT_MEMORY;
    off_pitch = ((pitch_pixels >> 3) << 22) |
                (state->target_offset >> 3);

    offsets[0] = V9X_M64_DP_MIX; values[0] = 0x00070007ul;
    offsets[1] = V9X_M64_DP_SRC; values[1] = 0x00000505ul;
    offsets[2] = V9X_M64_CLR_CMP_CNTL; values[2] = 0ul;
    offsets[3] = V9X_M64_GUI_TRAJ_CNTL; values[3] = 3ul;
    offsets[4] = V9X_M64_SC_LEFT_RIGHT;
    values[4] = ((state->scissor_right - 1ul) << 16) |
                state->scissor_left;
    offsets[5] = V9X_M64_SC_TOP_BOTTOM;
    values[5] = ((state->scissor_bottom - 1ul) << 16) |
                state->scissor_top;
    offsets[6] = V9X_M64_DST_OFF_PITCH; values[6] = off_pitch;
    offsets[7] = V9X_M64_Z_OFF_PITCH; values[7] = off_pitch;
    offsets[8] = V9X_M64_Z_CNTL; values[8] = 0ul;
    offsets[9] = V9X_M64_ALPHA_TST_CNTL; values[9] = 0ul;
    /* Shade, source factor ONE, destination factor ZERO; all extras off. */
    offsets[10] = V9X_M64_SCALE_3D_CNTL; values[10] = 0x000100c0ul;
    offsets[11] = V9X_M64_DP_FRGD_CLR; values[11] = 0ul;
    offsets[12] = V9X_M64_DP_WRITE_MASK; values[12] = 0xfffffffful;
    /* RGB565 in destination, composite, source, host and scale fields. */
    offsets[13] = V9X_M64_DP_PIX_WIDTH; values[13] = 0x40040444ul;
    /* Vertex 3 is the flat-shading provoking vertex. */
    offsets[14] = V9X_M64_SETUP_CNTL; values[14] = 0x00000018ul;
    offsets[15] = V9X_M64_TEX_SIZE_PITCH; values[15] = 0ul;
    offsets[16] = V9X_M64_TEX_CNTL; values[16] = 0ul;
    *written = V9X_M64_FLAT_STATE_DWORDS;
    return V9X_STATUS_OK;
}
