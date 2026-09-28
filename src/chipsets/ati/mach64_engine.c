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

static int v9x_m64_finite_float(float value)
{
    return (v9x_m64_float_bits(value) & 0x7f800000ul) != 0x7f800000ul;
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

v9x_status v9x_m64_build_gouraud_triangle(
                              const struct v9x_m64_gouraud_triangle *triangle,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_m64_flat_triangle flat;
    v9x_status status;
    v9x_u32 index;
    if (written != 0) *written = 0ul;
    if (triangle == 0) return V9X_STATUS_INVALID_ARGUMENT;
    for (index = 0ul; index < 3ul; ++index) {
        flat.vertex[index] = triangle->vertex[index];
    }
    flat.color = triangle->color[0];
    status = v9x_m64_build_flat_triangle(&flat, offsets, values, capacity,
                                         written);
    if (status != V9X_STATUS_OK) return status;
    values[4] = triangle->color[0];
    values[10] = triangle->color[1];
    values[16] = triangle->color[2];
    return V9X_STATUS_OK;
}

v9x_status v9x_m64_build_depth_triangle(
                              const struct v9x_m64_depth_triangle *triangle,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_m64_flat_triangle flat;
    v9x_status status;
    v9x_u32 index;
    if (written != 0) *written = 0ul;
    if (triangle == 0) return V9X_STATUS_INVALID_ARGUMENT;
    for (index = 0ul; index < 3ul; ++index) {
        flat.vertex[index] = triangle->vertex[index];
    }
    flat.color = triangle->color;
    status = v9x_m64_build_flat_triangle(&flat, offsets, values, capacity,
                                         written);
    if (status != V9X_STATUS_OK) return status;
    /* Mobility-M writes setup Z[31:16] directly to a Z16 surface.  The
     * historical Mesa <<15 convention produced exactly half the requested
     * stored value on the measured 1002:4c4d revision 64 part. */
    values[3] = (v9x_u32)triangle->depth[0] << 16;
    values[9] = (v9x_u32)triangle->depth[1] << 16;
    values[15] = (v9x_u32)triangle->depth[2] << 16;
    return V9X_STATUS_OK;
}

v9x_status v9x_m64_build_textured_triangle(
                              const struct v9x_m64_textured_triangle *triangle,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    struct v9x_m64_flat_triangle flat;
    v9x_status status;
    v9x_u32 index;
    if (written != 0) *written = 0ul;
    if (triangle == 0) return V9X_STATUS_INVALID_ARGUMENT;
    for (index = 0ul; index < 3ul; ++index) {
        v9x_u32 w_bits = v9x_m64_float_bits(triangle->w[index]);
        if (!v9x_m64_finite_float(triangle->s[index]) ||
            !v9x_m64_finite_float(triangle->t[index]) ||
            !v9x_m64_finite_float(triangle->w[index]) ||
            (w_bits & 0x80000000ul) != 0ul ||
            (w_bits & 0x7ffffffful) == 0ul) {
            return V9X_STATUS_INVALID_ARGUMENT;
        }
        flat.vertex[index] = triangle->vertex[index];
    }
    flat.color = triangle->color;
    status = v9x_m64_build_flat_triangle(&flat, offsets, values, capacity,
                                         written);
    if (status != V9X_STATUS_OK) return status;
    for (index = 0ul; index < 3ul; ++index) {
        values[index * 6ul] = v9x_m64_float_bits(triangle->s[index]);
        values[index * 6ul + 1ul] = v9x_m64_float_bits(triangle->t[index]);
        values[index * 6ul + 2ul] = v9x_m64_float_bits(triangle->w[index]);
    }
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
    offsets[10] = V9X_M64_SCALE_3D_CNTL; values[10] = 0x000100c1ul;
    offsets[11] = V9X_M64_DP_FRGD_CLR; values[11] = 0ul;
    offsets[12] = V9X_M64_DP_WRITE_MASK; values[12] = 0xfffffffful;
    /* RGB565 in destination, composite, source, host and scale fields. */
    offsets[13] = V9X_M64_DP_PIX_WIDTH; values[13] = 0x40040444ul;
    /* Vertex 3 is the flat-shading provoking vertex. */
    offsets[14] = V9X_M64_SETUP_CNTL;
    values[14] = V9X_M64_SETUP_FLAT_VERTEX_3;
    offsets[15] = V9X_M64_TEX_SIZE_PITCH; values[15] = 0ul;
    offsets[16] = V9X_M64_TEX_CNTL; values[16] = 0ul;
    *written = V9X_M64_FLAT_STATE_DWORDS;
    return V9X_STATUS_OK;
}

v9x_status v9x_m64_build_gouraud_state(
                              const struct v9x_m64_flat_state *state,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    v9x_status status = v9x_m64_build_flat_state(
        state, offsets, values, capacity, written);
    if (status != V9X_STATUS_OK) return status;
    values[14] = V9X_M64_SETUP_GOURAUD;
    return V9X_STATUS_OK;
}

v9x_status v9x_m64_build_z_control(v9x_u32 compare,
                                   v9x_u32 write_enable,
                                   v9x_u32 *value)
{
    v9x_u32 test;
    if (value != 0) *value = 0ul;
    if (value == 0 || write_enable > 1ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    switch (compare) {
    case 1ul: test = V9X_M64_Z_TEST_NEVER; break;
    case 2ul: test = V9X_M64_Z_TEST_LESS; break;
    case 3ul: test = V9X_M64_Z_TEST_EQUAL; break;
    case 4ul: test = V9X_M64_Z_TEST_LESSEQUAL; break;
    case 5ul: test = V9X_M64_Z_TEST_GREATER; break;
    case 6ul: test = V9X_M64_Z_TEST_NOTEQUAL; break;
    case 7ul: test = V9X_M64_Z_TEST_GREATEREQUAL; break;
    case 8ul: test = V9X_M64_Z_TEST_ALWAYS; break;
    default: return V9X_STATUS_INVALID_ARGUMENT;
    }
    *value = V9X_M64_Z_ENABLE | test;
    if (write_enable != 0ul) *value |= V9X_M64_Z_WRITE_ENABLE;
    return V9X_STATUS_OK;
}

v9x_status v9x_m64_build_alpha_control(v9x_u32 compare,
                                       v9x_u32 reference,
                                       v9x_u32 source_vertex,
                                       v9x_u32 *value)
{
    v9x_u32 test;
    if (value != 0) *value = 0ul;
    if (value == 0 || reference > 255ul || source_vertex > 1ul) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    switch (compare) {
    case 1ul: test = V9X_M64_ALPHA_TEST_NEVER; break;
    case 2ul: test = V9X_M64_ALPHA_TEST_LESS; break;
    case 3ul: test = V9X_M64_ALPHA_TEST_EQUAL; break;
    case 4ul: test = V9X_M64_ALPHA_TEST_LESSEQUAL; break;
    case 5ul: test = V9X_M64_ALPHA_TEST_GREATER; break;
    case 6ul: test = V9X_M64_ALPHA_TEST_NOTEQUAL; break;
    case 7ul: test = V9X_M64_ALPHA_TEST_GREATEREQUAL; break;
    case 8ul: test = V9X_M64_ALPHA_TEST_ALWAYS; break;
    default: return V9X_STATUS_INVALID_ARGUMENT;
    }
    *value = V9X_M64_ALPHA_TEST_ENABLE | test |
             (reference << V9X_M64_ALPHA_REFERENCE_SHIFT);
    if (source_vertex != 0ul) {
        *value |= V9X_M64_ALPHA_TEST_SOURCE_VERTEX;
    }
    return V9X_STATUS_OK;
}

v9x_status v9x_m64_build_depth_state(
                              const struct v9x_m64_depth_state *state,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 color_end;
    v9x_u32 depth_end;
    v9x_u32 pitch_pixels;
    v9x_u32 z_control;
    v9x_status status;
    if (written != 0) *written = 0ul;
    if (state == 0) return V9X_STATUS_INVALID_ARGUMENT;
    status = v9x_m64_build_flat_state(&state->color, offsets, values,
                                      capacity, written);
    if (status != V9X_STATUS_OK) return status;
    if (state->depth_width != state->color.target_width ||
        state->depth_height != state->color.target_height ||
        state->depth_offset & 7ul || state->depth_pitch_bytes == 0ul ||
        state->depth_pitch_bytes & 15ul) {
        *written = 0ul;
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    pitch_pixels = state->depth_pitch_bytes >> 1;
    if ((pitch_pixels & 7ul) != 0ul || (pitch_pixels >> 3) > 1023ul ||
        state->depth_width > pitch_pixels) {
        *written = 0ul;
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    if (state->depth_height - 1ul >
        (0xfffffffful - state->depth_offset) /
        state->depth_pitch_bytes) {
        *written = 0ul;
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    depth_end = state->depth_offset +
                (state->depth_height - 1ul) * state->depth_pitch_bytes;
    if (state->depth_width > (0xfffffffful - depth_end) / 2ul) {
        *written = 0ul;
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    depth_end += state->depth_width * 2ul;
    if (depth_end > state->color.vram_bytes) {
        *written = 0ul;
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    color_end = state->color.target_offset +
                (state->color.target_height - 1ul) *
                state->color.target_pitch_bytes +
                state->color.target_width * 2ul;
    if (state->depth_offset < color_end &&
        state->color.target_offset < depth_end) {
        *written = 0ul;
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_m64_build_z_control(state->compare, state->write_enable,
                                     &z_control);
    if (status != V9X_STATUS_OK) {
        *written = 0ul;
        return status;
    }
    values[7] = ((pitch_pixels >> 3) << 22) |
                (state->depth_offset >> 3);
    values[8] = z_control;
    return V9X_STATUS_OK;
}

static int v9x_m64_power_of_two(v9x_u32 value)
{
    return value != 0ul && (value & (value - 1ul)) == 0ul;
}

static v9x_u32 v9x_m64_log2(v9x_u32 value)
{
    v9x_u32 result = 0ul;
    while (value > 1ul) {
        value >>= 1;
        ++result;
    }
    return result;
}

v9x_status v9x_m64_build_texture_state(
                              const struct v9x_m64_texture_state *state,
                              v9x_u32 *offsets, v9x_u32 *values,
                              v9x_u32 capacity, v9x_u32 *written)
{
    v9x_u32 width_log2;
    v9x_u32 height_log2;
    v9x_u32 max_log2;
    v9x_u32 expected_pitch;
    v9x_u32 texture_bytes;
    v9x_u32 texture_end;
    v9x_u32 color_end;
    v9x_u32 texture_pix_width;
    v9x_status status;
    if (written != 0) *written = 0ul;
    if (state == 0 || offsets == 0 || values == 0 || written == 0 ||
        capacity < V9X_M64_TEXTURED_STATE_DWORDS) {
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    status = v9x_m64_build_flat_state(&state->color, offsets, values,
                                      capacity, written);
    if (status != V9X_STATUS_OK) return status;
    /* The historical local texture heap binds at its 4 KiB granularity.
     * Smaller alignment reached the fetcher on Mobility-M but wedged it. */
    if ((state->texture_offset & 4095ul) != 0ul ||
        !v9x_m64_power_of_two(state->texture_width) ||
        !v9x_m64_power_of_two(state->texture_height) ||
        state->texture_width < 8ul || state->texture_width > 1024ul ||
        state->texture_height < 8ul || state->texture_height > 1024ul ||
        state->wrap_s > 1ul || state->wrap_t > 1ul ||
        state->bilinear_min > 1ul || state->bilinear_mag > 1ul) {
        *written = 0ul;
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    switch (state->texture_format) {
    case V9X_M64_TEXTURE_FORMAT_RGB565:
        texture_pix_width = V9X_M64_SCALE_3D_TEXTURE_RGB565;
        break;
    case V9X_M64_TEXTURE_FORMAT_ARGB1555:
        texture_pix_width = V9X_M64_SCALE_3D_TEXTURE_ARGB1555;
        break;
    case V9X_M64_TEXTURE_FORMAT_ARGB4444:
        texture_pix_width = V9X_M64_SCALE_3D_TEXTURE_ARGB4444;
        break;
    default:
        *written = 0ul;
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    expected_pitch = state->texture_width > state->texture_height
        ? state->texture_width * 2ul : state->texture_height * 2ul;
    if (state->texture_pitch_bytes != expected_pitch) {
        *written = 0ul;
        return V9X_STATUS_INVALID_ARGUMENT;
    }
    texture_bytes = expected_pitch * state->texture_height;
    if (state->texture_offset > 0xfffffffful - texture_bytes) {
        *written = 0ul;
        return V9X_STATUS_INTEGER_OVERFLOW;
    }
    texture_end = state->texture_offset + texture_bytes;
    if (texture_end > state->color.vram_bytes) {
        *written = 0ul;
        return V9X_STATUS_INSUFFICIENT_MEMORY;
    }
    color_end = state->color.target_offset +
                (state->color.target_height - 1ul) *
                state->color.target_pitch_bytes +
                state->color.target_width * 2ul;
    if (state->texture_offset < color_end &&
        state->color.target_offset < texture_end) {
        *written = 0ul;
        return V9X_STATUS_INVALID_ARGUMENT;
    }

    width_log2 = v9x_m64_log2(state->texture_width);
    height_log2 = v9x_m64_log2(state->texture_height);
    max_log2 = width_log2 > height_log2 ? width_log2 : height_log2;
    values[10] = V9X_M64_SCALE_3D_FCN_TEXTURE | 0x00010001ul |
                 (state->bilinear_min != 0ul
                    ? V9X_M64_TEX_BLEND_FCN_LINEAR : 0ul) |
                 (state->bilinear_mag != 0ul
                    ? V9X_M64_BILINEAR_TEX_EN : 0ul) |
                 (state->texture_format != V9X_M64_TEXTURE_FORMAT_RGB565
                    ? V9X_M64_TEX_MAP_AEN : 0ul);
    values[13] = (values[13] & 0x0ffffffful) |
                 texture_pix_width;
    values[14] = V9X_M64_SETUP_GOURAUD;
    values[15] = width_log2 | (max_log2 << 4) | (height_log2 << 8);
    values[16] = (state->wrap_s == 0ul ? V9X_M64_TEXTURE_CLAMP_S : 0ul) |
                 (state->wrap_t == 0ul ? V9X_M64_TEXTURE_CLAMP_T : 0ul) |
                 V9X_M64_TEX_CACHE_FLUSH | V9X_M64_TEX_CACHE_SIZE_4K;
    offsets[17] = V9X_M64_SECONDARY_TEX_OFF;
    values[17] = 0ul;
    offsets[18] = V9X_M64_TEX_0_OFF + max_log2 * 4ul;
    values[18] = state->texture_offset;
    *written = V9X_M64_TEXTURED_STATE_DWORDS;
    return V9X_STATUS_OK;
}
